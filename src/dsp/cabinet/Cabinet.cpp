#include "dsp/cabinet/Cabinet.h"

#include "dsp/utility/Resample.h"
#include "platform/WavFile.h"

namespace mfx {

static const char* kSources[] = {"Built-in Sim", "User IR"};
static const char* kSimModels[] = {"4x12 Closed", "2x12 Open", "1x12 Combo"};

Cabinet::Cabinet() {
    for (auto& r : retired_) r.store(nullptr);
    ParamInfo s{"source", "Source", 0, 1, 0, ParamUnit::Choice}; s.choices = kSources; s.numChoices = 2;
    params_.add(s);
    ParamInfo m{"sim_model", "Sim Model", 0, 2, 0, ParamUnit::Choice}; m.choices = kSimModels; m.numChoices = 3;
    params_.add(m);
    params_.add({"low_cut_hz", "Low Cut", 20, 500, 60, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"high_cut_hz", "High Cut", 2000, 20000, 12000, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"ir_level_db", "IR Level", -24, 24, 0, ParamUnit::Decibels});
    params_.add({"mix", "IR Mix", 0, 100, 100, ParamUnit::Percent});
    params_.add({"normalize", "Normalize", 0, 1, 1, ParamUnit::Toggle});
}

Cabinet::~Cabinet() {
    delete active_;
    delete pending_.load();
    for (auto& r : retired_) delete r.load();
}

void Cabinet::collectGarbage() {
    for (auto& r : retired_) delete r.exchange(nullptr);
}

bool Cabinet::loadIR(const std::vector<float>& ir, double irSampleRate, const std::string& name) {
    if (ir.empty() || irSampleRate <= 0) return false;
    rawIr_ = ir;
    rawSr_ = irSampleRate;
    irName_ = name;
    if (prepared_) publishKernel();
    return true;
}

bool Cabinet::loadIRFile(const std::string& path, std::string* error) {
    WavData w;
    if (!readWav(path, w, error)) return false;
    if (w.numFrames() == 0) { if (error) *error = "empty IR"; return false; }
    irPath_ = path;
    const auto slash = path.find_last_of("/\\");
    return loadIR(w.channels[0], w.sampleRate, slash == std::string::npos ? path : path.substr(slash + 1));
}

bool Cabinet::setExtraState(const std::string& state, std::string* error) {
    if (state.empty()) return true;
    return loadIRFile(state, error);
}

void Cabinet::publishKernel() {
    collectGarbage();
    std::vector<float> ir = resampleOffline(rawIr_, rawSr_, spec_.sampleRate);
    const size_t maxLen = (size_t)(kMaxIrSeconds * spec_.sampleRate);
    if (ir.size() > maxLen) ir.resize(maxLen);
    if (params_.getInt(kNormalize)) {
        double e = 0.0;
        for (float v : ir) e += (double)v * v;
        if (e > 0.0) { const float s = (float)(1.0 / std::sqrt(e)); for (auto& v : ir) v *= s; }
    }
    auto k = ConvolutionKernel::build(ir.data(), (int)ir.size());
    delete pending_.exchange(k.release()); // replaces a not-yet-consumed kernel (message thread owns it)
}

void Cabinet::prepareDsp(const ProcessSpec& spec) {
    for (auto& c : conv_) c.prepare((int)(kMaxIrSeconds * spec.sampleRate));
    fadeStep_ = (float)(1.0 / (0.005 * spec.sampleRate));
    // re-prepare with a new sample rate: rebuild the kernel synchronously (not running)
    delete active_; active_ = nullptr;
    delete pending_.exchange(nullptr);
    prepared_ = true;
    if (!rawIr_.empty()) {
        publishKernel();
        active_ = pending_.exchange(nullptr);
        for (auto& c : conv_) c.setKernel(active_);
    }
}

void Cabinet::resetDsp() {
    for (auto& c : conv_) c.reset();
    lowCut_.reset(); highCut_.reset();
    for (auto& s : sim_) s.reset();
    fadeState_ = 0; fade_ = 1.0f;
}

void Cabinet::updateDsp() {
    const double fs = spec_.sampleRate;
    lowCut_.design(FilterType::HighPass, fs, p(kLowCut), 0.7071);
    highCut_.design(FilterType::LowPass, fs, std::min((double)p(kHighCut), 0.45 * fs), 0.7071);
    level_ = dbToGain(p(kIrLevel));
    mix_ = p(kMix) / 100.0f;
    // Built-in cabinet: speaker resonance, cone break-up peaks, enclosure notch, steep top roll-off
    const int m = pi(kSimModel);
    simCount_ = 6;
    if (m == 0) {        // closed back 4x12: tight low resonance, strong low-mids, presence peak
        sim_[0].design(FilterType::HighPass, fs, 75, 1.1);
        sim_[1].design(FilterType::Peak, fs, 110, 1.4, 4.0);
        sim_[2].design(FilterType::Peak, fs, 420, 1.2, -4.5);
        sim_[3].design(FilterType::Peak, fs, 2400, 1.6, 5.0);
        sim_[4].design(FilterType::LowPass, fs, 5200, 0.9);
        sim_[5].design(FilterType::LowPass, fs, 7000, 0.6);
    } else if (m == 1) { // open back 2x12: less low end, airier upper mids
        sim_[0].design(FilterType::HighPass, fs, 95, 0.7);
        sim_[1].design(FilterType::Peak, fs, 140, 1.0, 2.0);
        sim_[2].design(FilterType::Peak, fs, 600, 1.0, -2.5);
        sim_[3].design(FilterType::Peak, fs, 3000, 1.4, 4.0);
        sim_[4].design(FilterType::LowPass, fs, 6200, 0.8);
        sim_[5].design(FilterType::LowPass, fs, 8500, 0.6);
    } else {             // 1x12 combo: boxy mids, earlier roll-off
        sim_[0].design(FilterType::HighPass, fs, 110, 0.8);
        sim_[1].design(FilterType::Peak, fs, 180, 1.2, 2.5);
        sim_[2].design(FilterType::Peak, fs, 900, 1.0, 2.0);
        sim_[3].design(FilterType::Peak, fs, 2000, 1.8, 3.0);
        sim_[4].design(FilterType::LowPass, fs, 4800, 0.9);
        sim_[5].design(FilterType::LowPass, fs, 6500, 0.6);
    }
}

void Cabinet::processSub(AudioBlock& b) {
    // kernel hand-over (lock free)
    if (fadeState_ == 0 && pending_.load(std::memory_order_acquire) != nullptr) fadeState_ = 1;
    const bool useIr = pi(kSource) == 1;
    const bool lowCutOn = p(kLowCut) > 20.5f, highCutOn = p(kHighCut) < 19900.0f;
    for (int i = 0; i < b.numSamples; ++i) {
        if (fadeState_ == 1) {
            fade_ -= fadeStep_;
            if (fade_ <= 0.0f) {
                fade_ = 0.0f;
                ConvolutionKernel* k = pending_.exchange(nullptr, std::memory_order_acq_rel);
                if (k) {
                    ConvolutionKernel* old = active_;
                    active_ = k;
                    for (auto& c : conv_) c.setKernel(active_);
                    if (old) {
                        bool stored = false;
                        for (auto& r : retired_) {
                            ConvolutionKernel* expected = nullptr;
                            if (r.compare_exchange_strong(expected, old)) { stored = true; break; }
                        }
                        if (!stored) { // retire list full (GC not run): undo swap, retry next block
                            active_ = old;
                            for (auto& c : conv_) c.setKernel(old);
                            ConvolutionKernel* expected = nullptr;
                            if (!pending_.compare_exchange_strong(expected, k)) retired_[kRetired - 1].store(k); // never freed on audio thread
                        }
                    }
                }
                fadeState_ = 2;
            }
        } else if (fadeState_ == 2) {
            fade_ += fadeStep_;
            if (fade_ >= 1.0f) { fade_ = 1.0f; fadeState_ = 0; }
        }
        for (int c = 0; c < b.numChannels; ++c) {
            const float x = b.ch[c][i];
            float w;
            if (useIr && active_) w = conv_[c].process(x) * level_;
            else {
                w = x;
                for (int s = 0; s < simCount_; ++s) w = sim_[s].process(w, c);
                w *= level_;
            }
            if (lowCutOn) w = lowCut_.process(w, c);   // extreme positions = true bypass
            if (highCutOn) w = highCut_.process(w, c);
            const float y = x + mix_ * (w - x);
            b.ch[c][i] = y * (useIr ? fade_ : 1.0f);
        }
    }
}

} // namespace mfx
