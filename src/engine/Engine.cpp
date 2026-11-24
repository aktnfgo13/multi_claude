#include "engine/Engine.h"

#include <chrono>
#include <cstring>

namespace mfx {

const char* laneName(Lane l) {
    switch (l) {
        case Lane::Pre: return "pre";
        case Lane::A: return "a";
        case Lane::B: return "b";
        default: return "post";
    }
}

Engine::Engine() {
    for (auto& b : bypass_) b.store(false);
    msg_.parallel = false;
}

Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxBlockSize) {
    audioRunning_.store(false);
    fs_ = sampleRate;
    maxBlock_ = std::max(16, maxBlockSize);
    for (int c = 0; c < kMaxChannels; ++c) {
        main_[c].assign((size_t)maxBlock_, 0.0f);
        laneB_[c].assign((size_t)maxBlock_, 0.0f);
    }
    ProcessSpec spec{fs_, maxBlock_, 2};
    for (auto& fx : owned_) if (fx) fx->prepare(spec);
    for (int s = 0; s < kMaxSlots; ++s) {
        slotRT_[s] = SlotRT{};
        slotRT_[s].lastFx = owned_[s].get();
        const bool b = bypass_[s].load();
        slotRT_[s].wet = b ? 0.0f : 1.0f;      // start in the stored bypass state (no initial fade)
        slotRT_[s].wasFullyBypassed = b;
    }
    std::memset(laneDelay_, 0, sizeof(laneDelay_));
    std::memset(laneDelayPos_, 0, sizeof(laneDelayPos_));
    inGain_.reset(fs_, 0.02); inGain_.setImmediate(dbToGain(inGainDb_.load()));
    outGain_.reset(fs_, 0.02); outGain_.setImmediate(dbToGain(outGainDb_.load()));
    parMixS_.reset(fs_, 0.03); parMixS_.setImmediate(parMix_.load());
    fadeStep_ = (float)(1.0 / (0.003 * fs_));
    fadeState_ = FadeState::None; fade_ = 1.0f;
    rmsCoef_ = onePoleCoeff(0.3, fs_);
    inMs_ = outMs_ = cpuAvg_ = 0.0f;
    publishRouting();
}

// ------------------------------------------------------------------ routing (message thread)
int Engine::addEffect(std::unique_ptr<Effect> fx, Lane lane, int index) {
    if (!fx) return -1;
    const int l = (int)lane;
    int slot = -1;
    // Lowest free slot -> deterministic slot ids for presets / host automation. A slot whose previous
    // effect is still referenced by the live routing can be reused: the old effect stays alive in the
    // graveyard, and the audio thread resets the slot's real-time state when it sees the new effect.
    collectGarbage();
    for (int s = 0; s < kMaxSlots && slot < 0; ++s) if (!owned_[s]) slot = s;
    if (slot < 0) return -1;
    fx->prepare(ProcessSpec{fs_, maxBlock_, 2});
    owned_[slot] = std::move(fx);
    bypass_[slot].store(false);
    if (!audioRunning_.load()) { slotRT_[slot] = SlotRT{}; slotRT_[slot].lastFx = owned_[slot].get(); }
    int& cnt = msg_.count[(size_t)l];
    if (index < 0 || index > cnt) index = cnt;
    auto& ord = msg_.order[(size_t)l];
    for (int i = cnt; i > index; --i) ord[(size_t)i] = ord[(size_t)i - 1];
    ord[(size_t)index] = (int8_t)slot;
    ++cnt;
    publishRouting();
    return slot;
}

static bool removeFromLanes(Routing& r, int slot) {
    for (int l = 0; l < kNumLanes; ++l) {
        auto& ord = r.order[(size_t)l];
        int& cnt = r.count[(size_t)l];
        for (int i = 0; i < cnt; ++i)
            if (ord[(size_t)i] == slot) {
                for (int j = i; j < cnt - 1; ++j) ord[(size_t)j] = ord[(size_t)j + 1];
                --cnt;
                return true;
            }
    }
    return false;
}

bool Engine::removeEffect(int slot) {
    if (slot < 0 || slot >= kMaxSlots || !owned_[slot]) return false;
    removeFromLanes(msg_, slot);
    graveyard_.push_back({std::move(owned_[slot]), version_ + 1, slot});
    publishRouting();
    collectGarbage();
    return true;
}

bool Engine::moveEffect(int slot, Lane lane, int index) {
    if (slot < 0 || slot >= kMaxSlots || !owned_[slot]) return false;
    removeFromLanes(msg_, slot);
    const int l = (int)lane;
    int& cnt = msg_.count[(size_t)l];
    if (index < 0 || index > cnt) index = cnt;
    auto& ord = msg_.order[(size_t)l];
    for (int i = cnt; i > index; --i) ord[(size_t)i] = ord[(size_t)i - 1];
    ord[(size_t)index] = (int8_t)slot;
    ++cnt;
    publishRouting();
    return true;
}

void Engine::clear() {
    for (int s = 0; s < kMaxSlots; ++s)
        if (owned_[s]) graveyard_.push_back({std::move(owned_[s]), version_ + 1, s});
    msg_.count.fill(0);
    msg_.parallel = false;
    publishRouting();
    collectGarbage();
}

void Engine::setParallel(bool on) {
    msg_.parallel = on;
    publishRouting();
}

std::vector<int> Engine::laneSlots(Lane lane) const {
    std::vector<int> v;
    const int l = (int)lane;
    for (int i = 0; i < msg_.count[(size_t)l]; ++i) v.push_back(msg_.order[(size_t)l][(size_t)i]);
    return v;
}

std::vector<int> Engine::allSlotsInOrder() const {
    std::vector<int> v;
    for (int l = 0; l < kNumLanes; ++l) {
        auto s = laneSlots((Lane)l);
        v.insert(v.end(), s.begin(), s.end());
    }
    return v;
}

Lane Engine::laneOf(int slot) const {
    for (int l = 0; l < kNumLanes; ++l)
        for (int i = 0; i < msg_.count[(size_t)l]; ++i)
            if (msg_.order[(size_t)l][(size_t)i] == slot) return (Lane)l;
    return Lane::Pre;
}

int Engine::latencySamples() const {
    auto laneLat = [this](int l) {
        int s = 0;
        for (int i = 0; i < msg_.count[(size_t)l]; ++i) {
            const auto* fx = owned_[msg_.order[(size_t)l][(size_t)i]].get();
            if (fx) s += std::min(fx->latencySamples(), SlotRT::kDelay - 1);
        }
        return s;
    };
    const int a = laneLat(1), b = laneLat(2);
    return laneLat(0) + (msg_.parallel ? std::max(a, b) : a + b) + laneLat(3);
}

void Engine::publishRouting() {
    auto r = std::make_unique<Routing>(msg_);
    for (int s = 0; s < kMaxSlots; ++s) r->fx[(size_t)s] = owned_[s].get();
    r->version = ++version_;
    if (audioRunning_.load()) routing_.publish(std::move(r));
    else {
        routing_.setImmediate(std::move(r));
        audioVersion_.store(version_);
    }
    collectGarbage();
}

void Engine::collectGarbage() {
    routing_.collect();
    midi_.collect();
    const uint32_t av = audioVersion_.load();
    for (size_t i = 0; i < graveyard_.size();) {
        if (!audioRunning_.load() || av >= graveyard_[i].version) graveyard_.erase(graveyard_.begin() + (long)i);
        else ++i;
    }
    for (auto& fx : owned_) if (fx) fx->collectGarbage();
}

void Engine::setMidiMappings(const std::vector<MidiMapping>& maps) {
    midiMsg_ = maps;
    auto t = std::make_unique<MidiTable>();
    t->maps = maps;
    if (audioRunning_.load()) midi_.publish(std::move(t));
    else midi_.setImmediate(std::move(t));
}

MeterValues Engine::readMeters() {
    MeterValues m;
    m.inPeak = mInPeak_.exchange(0.0f);
    m.outPeak = mOutPeak_.exchange(0.0f);
    m.inRms = mInRms_.load();
    m.outRms = mOutRms_.load();
    m.inClip = mInClip_.exchange(false);
    m.outClip = mOutClip_.exchange(false);
    m.cpuLoad = mCpu_.load();
    m.cpuPeak = mCpuPeak_.exchange(0.0f);
    return m;
}

// ------------------------------------------------------------------ audio thread
void Engine::delayBuffer(float* data, int n, float* ring, int ringLen, int& pos, int delay) {
    for (int i = 0; i < n; ++i) {
        ring[pos] = data[i];
        data[i] = ring[(pos - delay + ringLen) % ringLen];
        pos = (pos + 1) % ringLen;
    }
}

void Engine::setParameterRT(int slot, int param, float normalized) {
    const Routing* r = routing_.active();
    if (!r || slot < 0 || slot >= kMaxSlots) return;
    if (Effect* fx = r->fx[(size_t)slot]) fx->params().setNormalized(param, normalized);
}

void Engine::handleMidi(int status, int d1, int d2) {
    if ((status & 0xF0) != 0xB0) return;
    const int ch = status & 0x0F;
    const MidiTable* t = midi_.active();
    const Routing* r = routing_.active();
    if (!t || !r) return;
    for (const auto& m : t->maps) {
        if (m.cc != d1 || (m.channel >= 0 && m.channel != ch)) continue;
        if (m.slot < 0 || m.slot >= kMaxSlots) continue;
        Effect* fx = r->fx[(size_t)m.slot];
        if (!fx) continue;
        if (m.param < 0) bypass_[m.slot].store(d2 >= 64);
        else fx->params().setNormalized(m.param, m.min + (m.max - m.min) * (float)d2 / 127.0f);
    }
}

void Engine::processSlot(const Routing& r, int slot, float** buf, bool& mono, int n) {
    Effect* fx = r.fx[(size_t)slot];
    if (!fx) return;
    SlotRT& s = slotRT_[slot];
    if (s.lastFx != fx) { // slot now holds a different effect: fresh real-time state
        const bool b = bypass_[slot].load(std::memory_order_relaxed);
        s.wet = b ? 0.0f : 1.0f;
        s.wasFullyBypassed = b;
        s.pos = 0;
        std::memset(s.dry, 0, sizeof(s.dry));
        s.lastFx = fx;
    }
    if (fx->createsStereo() && mono) { // topology based (independent of bypass) -> stable channel count
        std::memcpy(buf[1], buf[0], sizeof(float) * (size_t)n);
        mono = false;
    }
    const int nch = mono ? 1 : 2;
    const bool byp = bypass_[slot].load(std::memory_order_relaxed);
    const int lat = std::min(fx->latencySamples(), SlotRT::kDelay - 1);
    const float target = byp ? 0.0f : 1.0f;

    if (fx->hasTail() && lat == 0) { // trails: keep processing, fade the effect *input* instead of its output
        float dryLocalT[kMaxChannels][kChunk];
        float gR[kChunk];
        const float stepT = (float)(1.0 / (0.005 * fs_));
        for (int i = 0; i < n; ++i) {
            if (s.wet < target) s.wet = std::min(target, s.wet + stepT);
            else if (s.wet > target) s.wet = std::max(target, s.wet - stepT);
            gR[i] = s.wet;
        }
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < n; ++i) { dryLocalT[c][i] = buf[c][i]; buf[c][i] *= gR[i]; }
        fx->setTempo(bpm_.load(std::memory_order_relaxed));
        AudioBlock tb;
        tb.numChannels = nch; tb.numSamples = n; tb.ch[0] = buf[0]; tb.ch[1] = buf[1];
        fx->process(tb);
        // g = 1: normal output; g = 0: dry + tail of the effect fed with silence
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < n; ++i) buf[c][i] += (1.0f - gR[i]) * dryLocalT[c][i];
        s.wasFullyBypassed = false;
        return;
    }
    if (byp && s.wet <= 0.0f) { // fully bypassed: skip DSP, keep latency constant
        if (lat > 0) {
            int p0 = s.pos, p = 0;
            for (int c = 0; c < nch; ++c) { p = p0; delayBuffer(buf[c], n, s.dry[c], SlotRT::kDelay, p, lat); }
            s.pos = p;
        }
        s.wasFullyBypassed = true;
        return;
    }
    if (s.wasFullyBypassed) { fx->reset(); s.wasFullyBypassed = false; } // allocation-free reset
    fx->setTempo(bpm_.load(std::memory_order_relaxed));

    const bool crossfading = s.wet != target;
    // dry ring always fed so that a later bypass cross-fade is latency aligned (n <= kChunk)
    float dryLocal[kMaxChannels][kChunk];
    int p0 = s.pos, p = p0;
    for (int c = 0; c < nch; ++c) {
        p = p0;
        for (int i = 0; i < n; ++i) {
            s.dry[c][p] = buf[c][i];
            dryLocal[c][i] = s.dry[c][(p - lat + SlotRT::kDelay) % SlotRT::kDelay];
            p = (p + 1) % SlotRT::kDelay;
        }
    }
    s.pos = p;

    AudioBlock blk;
    blk.numChannels = nch;
    blk.numSamples = n;
    blk.ch[0] = buf[0];
    blk.ch[1] = buf[1];
    fx->process(blk);

    if (crossfading || target < 1.0f) {
        const float step = (float)(1.0 / (0.005 * fs_));
        for (int i = 0; i < n; ++i) {
            if (s.wet < target) s.wet = std::min(target, s.wet + step);
            else if (s.wet > target) s.wet = std::max(target, s.wet - step);
            for (int c = 0; c < nch; ++c) {
                const float d = dryLocal[c][i];
                buf[c][i] = d + s.wet * (buf[c][i] - d);
            }
        }
    }
}

void Engine::processLane(const Routing& r, int lane, float** buf, bool& mono, int n) {
    for (int i = 0; i < r.count[(size_t)lane]; ++i) processSlot(r, r.order[(size_t)lane][(size_t)i], buf, mono, n);
}

void Engine::process(const float* const* in, int numIn, float* const* out, int numOut, int numSamples) {
    ScopedNoDenormals noDenormals;
    const auto t0 = std::chrono::steady_clock::now();
    audioRunning_.store(true, std::memory_order_relaxed);
    if (midi_.hasPending()) midi_.swapIn();

    // routing hand-over with fade
    if (!routing_.active() && routing_.hasPending()) {
        routing_.swapIn();
        if (routing_.active()) audioVersion_.store(routing_.active()->version);
    } else if (fadeState_ == FadeState::None && routing_.hasPending()) {
        fadeState_ = FadeState::Out;
    }

    inGain_.setTarget(dbToGain(inGainDb_.load(std::memory_order_relaxed)));
    outGain_.setTarget(dbToGain(outGainDb_.load(std::memory_order_relaxed)));
    parMixS_.setTarget(parMix_.load(std::memory_order_relaxed));

    const int chunk = std::min(maxBlock_, kChunk);
    for (int start = 0; start < numSamples; start += chunk) {
        const int n = std::min(chunk, numSamples - start);
        float* buf[kMaxChannels] = {main_[0].data(), main_[1].data()};

        // ---- input
        const InputMode mode = (InputMode)inputMode_.load(std::memory_order_relaxed);
        bool mono = numIn < 2 || mode == InputMode::MonoLeft || mode == InputMode::MonoSum;
        for (int i = 0; i < n; ++i) {
            float l = in[0][start + i];
            float r = numIn > 1 ? in[1][start + i] : l;
            if (mode == InputMode::MonoSum && numIn > 1) l = 0.5f * (l + r);
            const float g = inGain_.next();
            buf[0][i] = l * g;
            buf[1][i] = (mono ? l : r) * g;
        }
        float pk = 0.0f;
        for (int c = 0; c < (mono ? 1 : 2); ++c)
            for (int i = 0; i < n; ++i) {
                const float a = std::fabs(buf[c][i]);
                pk = std::max(pk, a);
                inMs_ = a * a + rmsCoef_ * (inMs_ - a * a);
            }
        if (pk > mInPeak_.load(std::memory_order_relaxed)) mInPeak_.store(pk, std::memory_order_relaxed);
        if (pk >= 1.0f) mInClip_.store(true, std::memory_order_relaxed);
        mInRms_.store(std::sqrt(inMs_), std::memory_order_relaxed);

        // ---- effects
        const Routing* r = routing_.active();
        if (r) {
            processLane(*r, 0, buf, mono, n);
            if (r->parallel) {
                float* bufB[kMaxChannels] = {laneB_[0].data(), laneB_[1].data()};
                bool monoB = mono;
                for (int c = 0; c < kMaxChannels; ++c) std::memcpy(bufB[c], buf[c], sizeof(float) * (size_t)n);
                bool monoA = mono;
                // NB: lane B processing uses laneB_ as its buffer; processSlot's dry copies are local.
                processLane(*r, 1, buf, monoA, n);
                processLane(*r, 2, bufB, monoB, n);
                // latency alignment between lanes
                auto laneLat = [&](int l) {
                    int s = 0;
                    for (int i = 0; i < r->count[(size_t)l]; ++i)
                        if (const Effect* fx = r->fx[(size_t)r->order[(size_t)l][(size_t)i]]) s += std::min(fx->latencySamples(), SlotRT::kDelay - 1);
                    return s;
                };
                const int la = laneLat(1), lb = laneLat(2);
                const int diff = std::min(std::abs(la - lb), kLaneDelay - 1);
                if (diff > 0) {
                    const int which = la < lb ? 0 : 1;
                    float** tb = which == 0 ? buf : bufB;
                    for (int c = 0; c < kMaxChannels; ++c) {
                        int p = laneDelayPos_[which][c];
                        delayBuffer(tb[c], n, laneDelay_[which][c], kLaneDelay, p, diff);
                        laneDelayPos_[which][c] = p;
                    }
                }
                if (monoA && !monoB) std::memcpy(buf[1], buf[0], sizeof(float) * (size_t)n);
                if (monoB && !monoA) std::memcpy(bufB[1], bufB[0], sizeof(float) * (size_t)n);
                const bool st = !(monoA && monoB);
                for (int i = 0; i < n; ++i) {
                    const float m = parMixS_.next();
                    const float ga = std::min(1.0f, 2.0f * (1.0f - m)), gb = std::min(1.0f, 2.0f * m);
                    for (int c = 0; c < (st ? 2 : 1); ++c) buf[c][i] = buf[c][i] * ga + bufB[c][i] * gb;
                }
                mono = !st;
            } else {
                processLane(*r, 1, buf, mono, n);
                processLane(*r, 2, buf, mono, n);
            }
            processLane(*r, 3, buf, mono, n);
        }
        if (mono) std::memcpy(buf[1], buf[0], sizeof(float) * (size_t)n);

        // ---- routing fade
        if (fadeState_ != FadeState::None) {
            for (int i = 0; i < n; ++i) {
                if (fadeState_ == FadeState::Out) fade_ = std::max(0.0f, fade_ - fadeStep_);
                else if (fadeState_ == FadeState::In) fade_ = std::min(1.0f, fade_ + fadeStep_);
                buf[0][i] *= fade_;
                buf[1][i] *= fade_;
            }
            if (fadeState_ == FadeState::Out && fade_ <= 0.0f) {
                if (routing_.swapIn()) {
                    audioVersion_.store(routing_.active()->version);
                    fadeState_ = FadeState::In;
                }
            } else if (fadeState_ == FadeState::In && fade_ >= 1.0f) {
                fadeState_ = FadeState::None;
            }
        }

        // ---- output
        pk = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float g = outGain_.next();
            for (int c = 0; c < 2; ++c) {
                float v = buf[c][i] * g;
                if (!std::isfinite(v)) v = 0.0f; // NaN/Inf guard: never pass garbage to the host
                buf[c][i] = v;
                pk = std::max(pk, std::fabs(v));
            }
            const float a = std::fabs(buf[0][i]);
            outMs_ = a * a + rmsCoef_ * (outMs_ - a * a);
        }
        if (pk > mOutPeak_.load(std::memory_order_relaxed)) mOutPeak_.store(pk, std::memory_order_relaxed);
        if (pk >= 1.0f) mOutClip_.store(true, std::memory_order_relaxed);
        mOutRms_.store(std::sqrt(outMs_), std::memory_order_relaxed);

        if (numOut >= 2) {
            std::memcpy(out[0] + start, buf[0], sizeof(float) * (size_t)n);
            std::memcpy(out[1] + start, buf[1], sizeof(float) * (size_t)n);
        } else {
            for (int i = 0; i < n; ++i) out[0][start + i] = 0.5f * (buf[0][i] + buf[1][i]);
        }
    }

    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const float load = (float)(elapsed / (numSamples / fs_));
    cpuAvg_ = load + 0.9f * (cpuAvg_ - load);
    mCpu_.store(cpuAvg_, std::memory_order_relaxed);
    if (load > mCpuPeak_.load(std::memory_order_relaxed)) mCpuPeak_.store(load, std::memory_order_relaxed);
}

} // namespace mfx
