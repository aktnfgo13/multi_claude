#include "dsp/reverb/Reverb.h"

namespace mfx {

static const char* kTypes[] = {"Room", "Hall", "Plate", "Spring", "Ambient"};

// FDN line lengths (ms) at size = 50 % (mutually prime-ish when converted to samples)
static const float kLensMs[5][Reverb::kLines] = {
    {11.3f, 13.7f, 17.1f, 19.9f, 23.3f, 26.9f, 29.7f, 33.1f},   // Room
    {31.7f, 37.1f, 41.3f, 47.9f, 53.3f, 59.9f, 67.1f, 73.3f},   // Hall
    {9.1f, 11.9f, 14.3f, 16.7f, 19.1f, 22.3f, 24.7f, 27.1f},    // Plate (dense)
    {0, 0, 0, 0, 0, 0, 0, 0},                                    // Spring (separate algorithm)
    {43.1f, 51.7f, 59.3f, 67.9f, 76.1f, 83.3f, 91.7f, 99.1f},   // Ambient
};
static const float kDiffMs[Reverb::kDiff] = {4.77f, 3.59f, 12.73f, 9.31f};

Reverb::Reverb() {
    ParamInfo t{"type", "Type", 0, 4, 1, ParamUnit::Choice}; t.choices = kTypes; t.numChoices = 5; params_.add(t);
    params_.add({"decay_s", "Decay", 0.2f, 20, 2.2f, ParamUnit::Seconds, ParamScale::Log});
    params_.add({"predelay_ms", "Pre-Delay", 0, 200, 15, ParamUnit::Milliseconds});
    params_.add({"size", "Size", 0, 100, 50, ParamUnit::Percent});
    params_.add({"damping", "Damping", 0, 100, 40, ParamUnit::Percent});
    params_.add({"low_cut_hz", "Low Cut", 20, 1000, 120, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"high_cut_hz", "High Cut", 1000, 20000, 9000, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"modulation", "Modulation", 0, 100, 25, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 25, ParamUnit::Percent});
    params_.add({"width", "Width", 0, 100, 100, ParamUnit::Percent});
}

void Reverb::onPrepare(const ProcessSpec& spec) {
    const double fs = spec.sampleRate;
    for (auto& l : lines_) l.prepare((int)(0.16 * fs) + 64); // 99.1 ms * 1.5 + modulation
    for (int i = 0; i < kDiff; ++i) diff_[i].prepare((int)(0.02 * fs));
    pre_.prepare((int)(0.3 * fs));
    springLine_.prepare((int)(0.06 * fs));
    lfo_.prepare(fs);
    lfo_.setRate(0.7);
    mix_.reset(fs, 0.03); width_.reset(fs, 0.03);
}

void Reverb::onReset() {
    for (auto& l : lines_) l.reset();
    for (auto& d : diff_) d.reset();
    pre_.reset(); springLine_.reset();
    for (auto& d : damp_) d.reset();
    springLp_.reset();
    for (auto& ch : apZ_) for (auto& z : ch) z = 0.0f;
    inHp_.reset(); inLp_.reset();
    lfo_.reset();
}

void Reverb::onParametersChanged(bool immediate) {
    const double fs = spec_.sampleRate;
    type_ = clampv(params_.getInt(kType), 0, 4);
    const double T = params_.get(kDecay);
    const double size = 0.5 + params_.get(kSize) / 100.0; // 0.5 .. 1.5
    const double damping = params_.get(kDamping) / 100.0;
    if (type_ != Spring) {
        for (int i = 0; i < kLines; ++i) {
            len_[i] = (float)(kLensMs[type_][i] * 0.001 * fs * size);
            gain_[i] = (float)std::pow(10.0, -3.0 * len_[i] / (fs * T));
            // HF damping inside the loop (cutoff falls with damping, plate stays brighter)
            const double fc = (type_ == Plate ? 14000.0 : 11000.0) * std::pow(0.12, damping);
            damp_[i].setCutoff(fs, std::min(fc, 0.45 * fs));
        }
        diffG_ = type_ == Plate ? 0.75f : (type_ == Room ? 0.6f : 0.7f);
        for (int i = 0; i < kDiff; ++i) diffLen_[i] = std::max(2, (int)(kDiffMs[i] * 0.001 * fs * (type_ == Room ? 0.7 : 1.0)));
        modAmt_ = (float)(params_.get(kModulation) / 100.0 * (type_ == Ambient ? 0.004 : 0.0012) * fs);
        erLevel_ = type_ == Room ? 0.5f : (type_ == Hall ? 0.3f : 0.0f);
        static const float erMs[kER] = {4.3f, 8.9f, 13.1f, 18.7f, 24.1f, 31.3f};
        const double erScale = type_ == Hall ? 1.8 * size : size;
        for (int i = 0; i < kER; ++i) {
            erTap_[i] = std::max(1, (int)(erMs[i] * 0.001 * fs * erScale));
            erGain_[i] = 0.8f * std::pow(0.82f, (float)i) * (i % 2 ? -1.0f : 1.0f);
        }
    } else {
        springLen_[0] = (int)(0.0331 * fs * size);
        springLen_[1] = (int)(0.0412 * fs * size);
        springFb_ = (float)clampv(std::pow(10.0, -3.0 * 0.037 / T), 0.0, 0.97);
        apA_ = -0.55f - 0.15f * (float)(1.0 - damping);
        springLp_.setCutoff(fs, 4500.0 * std::pow(0.4, damping));
    }
    preSamples_ = (int)(params_.get(kPreDelay) * 0.001 * fs);
    inHp_.design(FilterType::HighPass, fs, params_.get(kLowCut), 0.7071);
    inLp_.design(FilterType::LowPass, fs, std::min((double)params_.get(kHighCut), 0.45 * fs), 0.7071);
    const float mx = params_.get(kMix) / 100.0f, w = params_.get(kWidth) / 100.0f;
    if (immediate) { mix_.setImmediate(mx); width_.setImmediate(w); }
    else { mix_.setTarget(mx); width_.setTarget(w); }
}

void Reverb::processFdn(float inL, float inR, float& outL, float& outR) {
    float x = 0.5f * (inL + inR);
    // pre-delay
    pre_.push(x, 0);
    const float xp = preSamples_ > 0 ? pre_.readInt(preSamples_, 0) : x;
    float er = 0.0f, erR = 0.0f;
    if (erLevel_ > 0.0f) {
        for (int i = 0; i < kER; ++i) {
            const float v = pre_.readInt(preSamples_ + erTap_[i], 0) * erGain_[i];
            if (i % 2) erR += v; else er += v;
        }
    }
    pre_.advance();
    // input diffusion (Schroeder all-passes)
    float d = xp;
    for (int i = 0; i < kDiff; ++i) {
        const float z = diff_[i].readInt(diffLen_[i], 0);
        const float y = -diffG_ * d + z;
        diff_[i].push(d + diffG_ * y, 0);
        diff_[i].advance();
        d = y;
    }
    // FDN read (modulated)
    float o[kLines];
    for (int i = 0; i < kLines; ++i) {
        const float m = modAmt_ > 0.0f ? modAmt_ * lfo_.valueAt(i * 0.125) : 0.0f;
        o[i] = lines_[i].read(std::max(2.0f, len_[i] + m), 0);
    }
    // Householder mixing: y = o - (2/N) * sum(o)
    float s = 0.0f;
    for (float v : o) s += v;
    s *= 2.0f / kLines;
    for (int i = 0; i < kLines; ++i) {
        float v = (o[i] - s);
        v = damp_[i].process(v, 0) * gain_[i];
        const float inj = (i & 1) ? -d : d;
        lines_[i].push(flushDenorm(v + inj * 0.5f), 0);
        lines_[i].advance();
    }
    lfo_.advance();
    outL = 0.35f * (o[0] - o[2] + o[4] - o[6] + o[1] + o[5]) + erLevel_ * er;
    outR = 0.35f * (o[1] - o[3] + o[5] - o[7] + o[2] + o[6]) + erLevel_ * erR;
}

void Reverb::processSpring(float inL, float inR, float& outL, float& outR) {
    const float x = 0.5f * (inL + inR);
    pre_.push(x, 0);
    const float xp = preSamples_ > 0 ? pre_.readInt(preSamples_, 0) : x;
    pre_.advance();
    float out[2];
    for (int c = 0; c < 2; ++c) {
        const float back = springLine_.readInt(springLen_[c], c);
        float v = xp + springFb_ * springLp_.process(back, c);
        // dispersive all-pass chain -> characteristic chirp / "drip"
        for (int k = 0; k < kSpringAP; ++k) {
            const float y = apA_ * v + apZ_[c][k];
            apZ_[c][k] = v - apA_ * y;
            v = y;
        }
        springLine_.push(flushDenorm(std::tanh(v)), c);
        out[c] = back;
    }
    springLine_.advance();
    outL = out[0];
    outR = out[1];
}

void Reverb::onProcess(AudioBlock& b) {
    const bool st = b.numChannels == 2;
    for (int i = 0; i < b.numSamples; ++i) {
        const float inL = b.ch[0][i], inR = st ? b.ch[1][i] : inL;
        float l = inHp_.process(inL, 0), r = inHp_.process(inR, 1);
        l = inLp_.process(l, 0); r = inLp_.process(r, 1);
        float wl, wr;
        if (type_ == Spring) processSpring(l, r, wl, wr);
        else processFdn(l, r, wl, wr);
        const float w = width_.next(), mix = mix_.next();
        const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * w;
        wl = m + sd; wr = m - sd;
        b.ch[0][i] = inL + mix * (wl - inL);
        if (st) b.ch[1][i] = inR + mix * (wr - inR);
    }
}

} // namespace mfx
