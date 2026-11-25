#include "dsp/reverb/PremiumPlate.h"

#include <algorithm>
#include <cmath>

namespace mfx {

// ------------------------------------------------------------------ MonoDelay
void MonoDelay::prepare(int maxDelay) {
    int size = 1;
    while (size < maxDelay + 4) size <<= 1;
    buf_.assign((size_t)size, 0.0f);
    mask_ = size - 1;
    w_ = 0;
}
void MonoDelay::reset() { std::fill(buf_.begin(), buf_.end(), 0.0f); w_ = 0; }
float MonoDelay::read(float d) const {
    d = std::max(d, 1.0f);
    const int di = (int)d;
    const float f = d - (float)di;
    const float* b = buf_.data();
    const float xm1 = b[(w_ - di + 1) & mask_], x0 = b[(w_ - di) & mask_];
    const float x1 = b[(w_ - di - 1) & mask_], x2 = b[(w_ - di - 2) & mask_];
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

// ------------------------------------------------------------------ plate
constexpr float PremiumPlate::kInLen[4];
constexpr float PremiumPlate::kModLen[2];
constexpr float PremiumPlate::kDel1[2];
constexpr float PremiumPlate::kAp2[2];
constexpr float PremiumPlate::kDel2[2];

namespace {
constexpr float kErMsL[8] = {7.1f, 11.3f, 17.9f, 23.4f, 31.7f, 41.2f, 52.9f, 67.1f};
constexpr float kErMsR[8] = {8.3f, 13.7f, 19.1f, 26.9f, 34.3f, 44.8f, 57.6f, 71.3f};
constexpr float kModExcursionRef = 16.0f; // samples at 29761 Hz (paper)
inline float flushNan(float v) { return std::isfinite(v) ? v : 0.0f; }
} // namespace

PremiumPlate::PremiumPlate() {
    params_.add({"decay_s", "Decay", 0.3f, 20.0f, 2.5f, ParamUnit::Seconds, ParamScale::Log});
    params_.add({"predelay_ms", "Pre-Delay", 0, 250, 20, ParamUnit::Milliseconds});
    params_.add({"size", "Size", 50, 150, 100, ParamUnit::Percent});
    params_.add({"damping_hz", "Damping", 1000, 18000, 6500, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"bass_mult", "Bass Decay", 0.5f, 2.0f, 1.2f, ParamUnit::Ratio});
    params_.add({"crossover_hz", "Crossover", 100, 1000, 300, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"diffusion", "Diffusion", 0, 100, 100, ParamUnit::Percent}); // 100 = paper coefficients
    params_.add({"mod_depth", "Mod Depth", 0, 100, 30, ParamUnit::Percent});
    params_.add({"mod_rate_hz", "Mod Rate", 0.1f, 3.0f, 0.8f, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"early", "Early", 0, 100, 15, ParamUnit::Percent});
    params_.add({"lowcut_hz", "Low Cut", 20, 500, 80, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"width", "Width", 0, 100, 100, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 30, ParamUnit::Percent});
}

void PremiumPlate::onPrepare(const ProcessSpec& spec) {
    fs_ = spec.sampleRate;
    scaleRef_ = fs_ / kRefRate;
    const float sMax = (float)scaleRef_ * maxSize_;
    const int exc = (int)std::ceil(kModExcursionRef * scaleRef_) + 4;
    pre_.prepare((int)std::ceil(0.25 * fs_) + 8);
    for (int k = 0; k < 4; ++k) inAp_[k].prepare((int)std::ceil(kInLen[k] * sMax) + 8);
    for (int h = 0; h < 2; ++h) {
        modAp_[h].prepare((int)std::ceil(kModLen[h] * sMax) + exc + 8);
        del1_[h].prepare((int)std::ceil(kDel1[h] * sMax) + 8);
        ap2_[h].prepare((int)std::ceil(kAp2[h] * sMax) + 8);
        del2_[h].prepare((int)std::ceil(kDel2[h] * sMax) + 8);
    }
    er_.prepare((int)std::ceil(0.0735 * maxSize_ * fs_) + 8);
    for (int k = 0; k < kEr; ++k) erGain_[k] = (k & 1 ? -1.0f : 1.0f) * std::pow(0.82f, (float)k) * 0.35f;
    size_.reset(fs_, 0.25);
    preDelay_.reset(fs_, 0.10);
    mix_.reset(fs_, 0.03);
    width_.reset(fs_, 0.03);
    early_.reset(fs_, 0.03);
}

size_t PremiumPlate::memoryBytes() const {
    size_t b = pre_.bytes() + er_.bytes();
    for (const auto& d : inAp_) b += d.bytes();
    for (int h = 0; h < 2; ++h) b += modAp_[h].bytes() + del1_[h].bytes() + ap2_[h].bytes() + del2_[h].bytes();
    return b;
}

void PremiumPlate::onReset() {
    pre_.reset(); er_.reset();
    for (auto& d : inAp_) d.reset();
    for (int h = 0; h < 2; ++h) {
        modAp_[h].reset(); del1_[h].reset(); ap2_[h].reset(); del2_[h].reset();
        dampState_[h] = lowState1_[h] = lowState2_[h] = tankOut_[h] = 0.0f;
    }
    lowCut_.reset();
    bwState_ = 0.0f;
    lfoPhase_ = 0.0;
}

void PremiumPlate::updateGains() {
    const double rt = std::max(0.05, (double)params_.get(kDecay));
    const double rtLow = rt * (double)params_.get(kBassMult);
    const double sc = scaleRef_ * size_.target();
    for (int h = 0; h < 2; ++h) {
        const double l1 = (kModLen[h] + kDel1[h]) * sc / fs_, l2 = (kAp2[h] + kDel2[h]) * sc / fs_;
        g1_[h] = (float)std::pow(10.0, -3.0 * l1 / rt);
        g2_[h] = (float)std::pow(10.0, -3.0 * l2 / rt);
        g1Low_[h] = (float)std::pow(10.0, -3.0 * l1 / rtLow);
        g2Low_[h] = (float)std::pow(10.0, -3.0 * l2 / rtLow);
    }
}

void PremiumPlate::onParametersChanged(bool immediate) {
    const float sz = params_.get(kSize) / 100.0f;
    const float pd = params_.get(kPreDelay) * 0.001f * (float)fs_;
    if (immediate) {
        size_.setImmediate(sz); preDelay_.setImmediate(pd);
        mix_.setImmediate(params_.get(kMix) / 100.0f); width_.setImmediate(params_.get(kWidth) / 100.0f);
        early_.setImmediate(params_.get(kEarly) / 100.0f);
    } else {
        size_.setTarget(sz); preDelay_.setTarget(pd);
        mix_.setTarget(params_.get(kMix) / 100.0f); width_.setTarget(params_.get(kWidth) / 100.0f);
        early_.setTarget(params_.get(kEarly) / 100.0f);
    }
    dampA_ = (float)std::exp(-kTwoPi * std::min((double)params_.get(kDamping), 0.45 * fs_) / fs_);
    xoverA_ = (float)std::exp(-kTwoPi * (double)params_.get(kCrossover) / fs_);
    lowCut_.design(FilterType::HighPass, fs_, params_.get(kLowCut), 0.7071);
    const float d = params_.get(kDiffusion) / 100.0f;
    inDiff1_ = 0.75f * d; inDiff2_ = 0.625f * d;
    decDiff1_ = 0.70f; decDiff2_ = 0.50f;
    modDepth_ = kModExcursionRef * (float)scaleRef_ * params_.get(kModDepth) / 100.0f;
    lfoInc_ = (double)params_.get(kModRate) / fs_;
    updateGains();
}

float PremiumPlate::allpass(MonoDelay& d, float len, float g, float x) const {
    const float out = d.read(len);
    const float w = x - g * out;
    d.push(w);
    return out + g * w;
}

void PremiumPlate::onProcess(AudioBlock& b) {
    const bool st = b.numChannels == 2;
    for (int i = 0; i < b.numSamples; ++i) {
        const float inL = b.ch[0][i], inR = st ? b.ch[1][i] : inL;
        const bool sizeMoving = size_.isSmoothing();
        const float sc = (float)scaleRef_ * size_.next();
        if (sizeMoving && (i & 63) == 0) updateGains();
        // ---- input: mono sum, low cut, bandwidth, pre-delay
        float x = lowCut_.process(0.5f * (inL + inR), 0);
        bwState_ += 0.9995f * (x - bwState_);
        pre_.push(bwState_);
        const float xp = pre_.read(std::max(1.0f, preDelay_.next()));
        // ---- early reflections (stereo taps of the pre-delayed input)
        er_.push(xp);
        float erL = 0.0f, erR = 0.0f;
        const float msToSmp = 0.001f * (float)fs_ * (sc / (float)scaleRef_);
        for (int k = 0; k < kEr; ++k) {
            erL += erGain_[k] * er_.read(kErMsL[k] * msToSmp);
            erR += erGain_[k] * er_.read(kErMsR[k] * msToSmp);
        }
        // ---- input diffusion
        float y = xp;
        y = allpass(inAp_[0], kInLen[0] * sc, inDiff1_, y);
        y = allpass(inAp_[1], kInLen[1] * sc, inDiff1_, y);
        y = allpass(inAp_[2], kInLen[2] * sc, inDiff2_, y);
        y = allpass(inAp_[3], kInLen[3] * sc, inDiff2_, y);
        // ---- tank (figure of eight); halves cross-feed their previous outputs
        const float prev0 = tankOut_[0], prev1 = tankOut_[1];
        const double ph = lfoPhase_;
        for (int h = 0; h < 2; ++h) {
            float t = y + (h == 0 ? prev1 : prev0);
            const float lfo = (float)std::sin(kTwoPi * (ph + 0.25 * h));
            t = allpass(modAp_[h], kModLen[h] * sc + modDepth_ * (1.0f + lfo) + 1.0f, -decDiff1_, t);
            del1_[h].push(t);
            float s = del1_[h].read(kDel1[h] * sc);
            dampState_[h] = s + dampA_ * (dampState_[h] - s);          // HF damping (one-pole LP)
            s = dampState_[h];
            lowState1_[h] = s + xoverA_ * (lowState1_[h] - s);         // low band (crossover)
            s = g1_[h] * s + (g1Low_[h] - g1_[h]) * lowState1_[h];     // frequency-dependent decay
            s = allpass(ap2_[h], kAp2[h] * sc, decDiff2_, s);
            del2_[h].push(s);
            float e = del2_[h].read(kDel2[h] * sc);
            lowState2_[h] = e + xoverA_ * (lowState2_[h] - e);
            e = g2_[h] * e + (g2Low_[h] - g2_[h]) * lowState2_[h];
            tankOut_[h] = flushDenorm(flushNan(e));
            dampState_[h] = flushDenorm(dampState_[h]);
            lowState1_[h] = flushDenorm(lowState1_[h]);
            lowState2_[h] = flushDenorm(lowState2_[h]);
        }
        lfoPhase_ += lfoInc_;
        lfoPhase_ -= std::floor(lfoPhase_);
        // ---- output taps (paper's tap set, scaled)
        auto rd = [&](const MonoDelay& d, float n) { return d.read(n * sc); };
        float wl = rd(del1_[1], 266.f) + rd(del1_[1], 2974.f) - rd(ap2_[1], 1913.f) + rd(del2_[1], 1996.f)
                 - rd(del1_[0], 1990.f) - rd(ap2_[0], 187.f) - rd(del2_[0], 1066.f);
        float wr = rd(del1_[0], 353.f) + rd(del1_[0], 3627.f) - rd(ap2_[0], 1228.f) + rd(del2_[0], 2673.f)
                 - rd(del1_[1], 2111.f) - rd(ap2_[1], 335.f) - rd(del2_[1], 121.f);
        const float er = early_.next();
        wl = 0.6f * wl + er * erL;
        wr = 0.6f * wr + er * erR;
        const float w = width_.next(), mix = mix_.next();
        const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * w;
        wl = flushNan(m + sd); wr = flushNan(m - sd);
        b.ch[0][i] = inL + mix * (wl - inL);
        if (st) b.ch[1][i] = inR + mix * (wr - inR);
    }
}

} // namespace mfx
