#include "plugin/PluginProcessor.h"

#include "BinaryData.h"
#include "engine/EffectFactory.h"
#include "plugin/PluginEditor.h"
#include "presets/Preset.h"

#include <cstdlib>
#include <cstring>

namespace mfx::plugin {

// ------------------------------------------------------------------ SlotParameter
ParamInfo SlotParameter::Meta::toInfo(const char** choicePtrs) const {
    ParamInfo in;
    in.id = "";
    in.name = name;
    in.min = min; in.max = max; in.def = def;
    in.unit = unit; in.scale = scale;
    in.numChoices = numChoices;
    for (int k = 0; k < numChoices && k < 16; ++k) choicePtrs[k] = choices[k];
    in.choices = numChoices > 0 ? choicePtrs : nullptr;
    return in;
}

SlotParameter::Meta SlotParameter::snapshot() const {
    // seqlock reader: retry while a write is in progress or happened during the copy
    Meta m;
    for (;;) {
        const uint32_t s1 = seq_.load(std::memory_order_acquire);
        if (s1 & 1u) continue;
        std::memcpy(&m, &meta_, sizeof(Meta));
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq_.load(std::memory_order_relaxed) == s1) return m;
    }
}

void SlotParameter::bind(const Effect* fx) {
    Meta m;
    if (fx) {
        m.bound = index_ < 0 || index_ < fx->params().size();
        std::strncpy(m.effectName, fx->displayName(), sizeof(m.effectName) - 1);
        if (index_ >= 0 && index_ < fx->params().size()) {
            const auto& in = fx->params().info(index_);
            std::strncpy(m.name, in.name, sizeof(m.name) - 1);
            m.min = in.min; m.max = in.max; m.def = in.def; m.unit = in.unit; m.scale = in.scale;
            m.numChoices = std::min(in.numChoices, 16);
            for (int k = 0; k < m.numChoices; ++k) std::strncpy(m.choices[k], in.choices[k], sizeof(m.choices[k]) - 1);
        }
    }
    // seqlock writer (message thread only)
    seq_.fetch_add(1, std::memory_order_acq_rel);
    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy(&meta_, &m, sizeof(Meta));
    seq_.fetch_add(1, std::memory_order_release);
    if (fx && index_ < 0) value_.store(engine_.isBypassed(slot_) ? 1.0f : 0.0f);
    if (fx && index_ >= 0 && index_ < fx->params().size()) value_.store(fx->params().getNormalized(index_));
}

void SlotParameter::setValue(float v) {
    v = juce::jlimit(0.0f, 1.0f, v);
    if (index_ < 0) {
        v = v >= 0.5f ? 1.0f : 0.0f;
        engine_.setBypass(slot_, v >= 0.5f); // atomic, engine outlives the parameter
    } else {
        const Meta m = snapshot();
        if (m.bound && (m.unit == ParamUnit::Choice || m.unit == ParamUnit::Toggle)) {
            const char* cp[16];
            const ParamInfo in = m.toInfo(cp);
            v = paramToNormalized(in, std::round(paramFromNormalized(in, v)));
        }
    }
    value_.store(v);
    dirty_.store(true);
    hostChanged_.store(true);
}

float SlotParameter::getDefaultValue() const {
    if (index_ < 0) return 0.0f;
    const Meta m = snapshot();
    if (!m.bound) return 0.5f;
    const char* cp[16];
    return paramToNormalized(m.toInfo(cp), m.def);
}

juce::String SlotParameter::getName(int maxLen) const {
    const Meta m = snapshot();
    juce::String n;
    if (m.effectName[0]) {
        if (index_ < 0) n = "S" + juce::String(slot_ + 1) + " " + m.effectName + " Bypass";
        else if (m.bound) n = "S" + juce::String(slot_ + 1) + " " + m.name;
        else n = "S" + juce::String(slot_ + 1) + " (unused " + juce::String(index_ + 1) + ")";
    } else {
        n = "Slot " + juce::String(slot_ + 1) + (index_ < 0 ? " Bypass" : " P" + juce::String(index_ + 1));
    }
    return n.substring(0, maxLen);
}

juce::String SlotParameter::getText(float v, int) const {
    if (index_ < 0) return v >= 0.5f ? "Bypassed" : "Active";
    const Meta m = snapshot();
    if (!m.bound) return juce::String(v, 3);
    const char* cp[16];
    const ParamInfo in = m.toInfo(cp);
    return formatParamValue(in, paramFromNormalized(in, v));
}

float SlotParameter::getValueForText(const juce::String& t) const {
    const Meta m = snapshot();
    if (index_ < 0 || !m.bound) return juce::jlimit(0.0f, 1.0f, t.getFloatValue());
    const char* cp[16];
    const ParamInfo in = m.toInfo(cp);
    for (int k = 0; k < in.numChoices; ++k) if (t == cp[k]) return paramToNormalized(in, (float)k);
    return paramToNormalized(in, juce::jlimit(in.min, in.max, t.getFloatValue()));
}

bool SlotParameter::isDiscrete() const {
    if (index_ < 0) return true;
    const Meta m = snapshot();
    return m.bound && (m.unit == ParamUnit::Choice || m.unit == ParamUnit::Toggle);
}

int SlotParameter::getNumSteps() const {
    if (index_ < 0) return 2;
    const Meta m = snapshot();
    if (m.bound && (m.unit == ParamUnit::Choice || m.unit == ParamUnit::Toggle)) return (int)(m.max - m.min) + 1;
    return AudioProcessorParameter::getNumSteps();
}

// ------------------------------------------------------------------ Processor
juce::AudioProcessor::BusesProperties Processor::makeBuses() {
    return BusesProperties()
        .withInput("Input", juce::AudioChannelSet::mono(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true);
}

Processor::Processor() : AudioProcessor(makeBuses()) {
    addParameter(inGain_ = new juce::AudioParameterFloat(juce::ParameterID{"input_gain", 1}, "Input Gain", -24.0f, 24.0f, 0.0f));
    addParameter(outGain_ = new juce::AudioParameterFloat(juce::ParameterID{"output_gain", 1}, "Output Gain", -36.0f, 12.0f, 0.0f));
    addParameter(parMix_ = new juce::AudioParameterFloat(juce::ParameterID{"parallel_mix", 1}, "Parallel A/B Mix", 0.0f, 1.0f, 0.5f));
    for (int s = 0; s < Engine::kMaxSlots; ++s) {
        bypassParams_[(size_t)s] = new SlotParameter(engine_, s, -1);
        addParameter(bypassParams_[(size_t)s]);
        for (int p = 0; p < kParamsPerSlot; ++p) {
            slotParams_[(size_t)s][(size_t)p] = new SlotParameter(engine_, s, p);
            addParameter(slotParams_[(size_t)s][(size_t)p]);
        }
    }
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
        int size = 0;
        const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
        juce::String json = juce::String::fromUTF8(data, size);
        auto parsed = juce::JSON::parse(json);
        juce::String name = parsed.getProperty("name", BinaryData::originalFilenames[i]).toString();
        factory_.push_back({juce::String(BinaryData::originalFilenames[i]) + "|" + name, json});
    }
    // order by file name (01_, 02_ ...), display the preset name
    std::sort(factory_.begin(), factory_.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& f : factory_) f.first = f.first.fromFirstOccurrenceOf("|", false, false);
    engine_.prepare(48000.0, 512);
    if (!factory_.empty()) { juce::String err; loadPresetJson(factory_[0].second, err); }
    startTimerHz(20);
}

Processor::~Processor() { stopTimer(); }

bool Processor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

void Processor::prepareToPlay(double sr, int block) {
    engine_.prepare(sr, block);
    lastLatency_ = engine_.latencySamples();
    setLatencySamples(lastLatency_);
    prepared_ = true;
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.isController()) engine_.handleMidi(0xB0 | (m.getChannel() - 1), m.getControllerNumber(), m.getControllerValue());
    }
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm()) engine_.setTempo(*bpm);
    engine_.setInputGainDb(inGain_->get());
    engine_.setOutputGainDb(outGain_->get());
    engine_.setParallelMix(parMix_->get());
    // host automation -> live effect parameters (audio-thread safe path)
    for (int s = 0; s < Engine::kMaxSlots; ++s) {
        if (bypassParams_[(size_t)s]->consumeDirty()) engine_.setBypass(s, bypassParams_[(size_t)s]->getValue() >= 0.5f);
        for (int p = 0; p < kParamsPerSlot; ++p) {
            auto* sp = slotParams_[(size_t)s][(size_t)p];
            if (sp->consumeDirty()) engine_.setParameterRT(s, p, sp->getValue());
        }
    }
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const float* in[2] = {buffer.getReadPointer(0), buffer.getReadPointer(numIn > 1 ? 1 : 0)};
    float* out[2] = {buffer.getWritePointer(0), buffer.getWritePointer(numOut > 1 ? 1 : 0)};
    engine_.process(in, numIn, out, numOut, buffer.getNumSamples());
}

void Processor::timerCallback() {
    engine_.collectGarbage();
    const int lat = engine_.latencySamples();
    if (lat != lastLatency_) { lastLatency_ = lat; setLatencySamples(lat); } // e.g. oversampling changed
    // mirror effect parameter values (MIDI / internal changes) into the host-visible parameters
    for (int s = 0; s < Engine::kMaxSlots; ++s) {
        Effect* fx = engine_.effect(s);
        if (!fx) continue; // unbound host parameters keep whatever the host set
        bypassParams_[(size_t)s]->mirror(engine_.isBypassed(s) ? 1.0f : 0.0f);
        for (int p = 0; p < std::min(kParamsPerSlot, fx->params().size()); ++p) {
            auto* sp = slotParams_[(size_t)s][(size_t)p];
            if (sp->consumeHostChange()) fx->params().setNormalized(p, sp->getValue()); // host wins
            else sp->mirror(fx->params().getNormalized(p));                            // MIDI / UI / internal
        }
    }
}

void Processor::refreshBindings() {
    for (int s = 0; s < Engine::kMaxSlots; ++s) {
        Effect* fx = engine_.effect(s);
        bypassParams_[(size_t)s]->bind(fx);
        for (auto* p : slotParams_[(size_t)s]) p->bind(fx);
    }
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withParameterInfoChanged(true));
    structureVersion.fetch_add(1);
}

int Processor::addEffect(const juce::String& typeId, Lane lane) {
    auto fx = createEffect(typeId.toStdString());
    if (!fx) return -1;
    const int slot = engine_.addEffect(std::move(fx), lane);
    refreshBindings();
    return slot;
}

void Processor::removeEffect(int slot) { engine_.removeEffect(slot); refreshBindings(); }
void Processor::moveEffect(int slot, Lane lane, int index) { engine_.moveEffect(slot, lane, index); refreshBindings(); }

void Processor::setEffectParam(int slot, int param, float plain) {
    Effect* fx = engine_.effect(slot);
    if (!fx || param < 0 || param >= fx->params().size()) return;
    fx->params().set(param, plain); // immediate (atomic) for responsiveness
    if (param < kParamsPerSlot) {
        auto* sp = slotParams_[(size_t)slot][(size_t)param];
        const float n = fx->params().getNormalized(param);
        sp->beginChangeGesture();
        sp->setValueNotifyingHost(n);
        sp->endChangeGesture();
    }
}

bool Processor::loadPresetJson(const juce::String& json, juce::String& error) {
    Json j;
    std::string err;
    if (!Json::parse(json.toStdString(), j, &err)) { error = err; return false; }
    auto rep = presetFromJson(engine_, j);
    if (!rep.ok) { error = rep.error; return false; }
    presetName_ = j.at("name").asString();
    inGain_->setValueNotifyingHost(inGain_->convertTo0to1(engine_.inputGainDb()));
    outGain_->setValueNotifyingHost(outGain_->convertTo0to1(engine_.outputGainDb()));
    parMix_->setValueNotifyingHost(engine_.parallelMix());
    for (const auto& w : rep.warnings) error += juce::String(w) + "\n";
    refreshBindings();
    return true;
}

juce::String Processor::currentPresetJson(const juce::String& name) const {
    return presetToJson(const_cast<Engine&>(engine_), name.toStdString()).dump(2);
}

juce::StringArray Processor::factoryPresetNames() const {
    juce::StringArray s;
    for (const auto& f : factory_) s.add(f.first);
    return s;
}

int Processor::getNumPrograms() { return std::max(1, (int)factory_.size()); }
const juce::String Processor::getProgramName(int i) { return i >= 0 && i < (int)factory_.size() ? factory_[(size_t)i].first : "Init"; }
void Processor::setCurrentProgram(int i) {
    if (i < 0 || i >= (int)factory_.size()) return;
    currentProgram_ = i;
    juce::String err;
    loadPresetJson(factory_[(size_t)i].second, err);
}

void Processor::getStateInformation(juce::MemoryBlock& dest) {
    // Host changes that the audio thread may not have applied yet (e.g. transport stopped)
    for (int s = 0; s < Engine::kMaxSlots; ++s) {
        Effect* fx = engine_.effect(s); // message thread: owned pointer is stable here
        for (int p = 0; p < kParamsPerSlot; ++p) {
            auto* sp = slotParams_[(size_t)s][(size_t)p];
            if (sp->consumeHostChange() && fx && p < fx->params().size()) fx->params().setNormalized(p, sp->getValue());
        }
    }
    Json j = presetToJson(engine_, presetName_.toStdString());
    // values of host parameters that are not bound to an effect (kept so the host sees what it set)
    Json unbound = Json::object();
    for (int s = 0; s < Engine::kMaxSlots; ++s) {
        Effect* fx = engine_.effect(s);
        if (!fx) unbound["b" + std::to_string(s)] = bypassParams_[(size_t)s]->getValue();
        for (int p = 0; p < kParamsPerSlot; ++p)
            if (!fx || p >= fx->params().size())
                unbound[std::to_string(s * kParamsPerSlot + p)] = slotParams_[(size_t)s][(size_t)p]->getValue();
    }
    j[kHostParamsKey] = unbound;
    const auto s = j.dump(2);
    dest.replaceAll(s.data(), s.size());
}

void Processor::setStateInformation(const void* data, int size) {
    juce::String err;
    const juce::String json = juce::String::fromUTF8((const char*)data, size);
    if (!loadPresetJson(json, err)) return;
    Json j;
    if (Json::parse(json.toStdString(), j) && j.has(kHostParamsKey))
        for (const auto& [k, v] : j.at(kHostParamsKey).items()) {
            if (!k.empty() && k[0] == 'b') {
                const int s = std::atoi(k.c_str() + 1);
                if (s >= 0 && s < Engine::kMaxSlots && !engine_.effect(s)) bypassParams_[(size_t)s]->mirror((float)v.asNumber());
                continue;
            }
            const int id = std::atoi(k.c_str());
            const int s = id / kParamsPerSlot, p = id % kParamsPerSlot;
            if (s >= 0 && s < Engine::kMaxSlots) slotParams_[(size_t)s][(size_t)p]->mirror((float)v.asNumber());
        }
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

} // namespace mfx::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mfx::plugin::Processor(); }
