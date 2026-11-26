// Premium spatial effects (v0.9). New types only: "reverb" (5 legacy modes) and "reverb_plate_premium" are unchanged.
//
// Shared pieces (small on purpose):
//  * WetDucker      - input-envelope wet-only ducking (dry never touched)
//  * Shelf1         - first-order bilinear shelving filter (per-band loop gains for frequency-dependent RT60)
//  * FdnCore        - 16-line FDN: fast Walsh-Hadamard mixing (orthonormal, lossless), per-line 3-band decay
//                     (gain + low/high first-order shelves from RT60 x multipliers), smooth-random delay
//                     modulation, smoothed size, stereo input diffusion, orthogonal L/R output taps, Freeze
//                     (input muted, decay filters bypassed by a gain ramp -> lossless circulation), energy guard
//  * PitchShifter   - dual-tap delay-line pitch shifter with complementary sin^2 windows (ratio exact,
//                     latency ~ window/2, no FFT: ARM friendly)
//  * StretchedAp    - H(z) = (a + z^-K) / (1 + a z^-K) dispersive all-pass for the spring model
#pragma once

#include <array>
#include <cstdint>

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"
#include "dsp/modulation/PremiumMod.h" // ModLfo
#include "dsp/utility/DelayLine.h"

namespace mfx {

struct WetDucker {
    float env = 0, gain = 1, att = 0, rel = 0, thr = 0.1f, depth = 1.0f, gAtt = 0, gRel = 0;
    void prepare(double fs) {
        att = 1.0f - (float)std::exp(-1.0 / (0.005 * fs)); rel = 1.0f - (float)std::exp(-1.0 / (0.25 * fs));
        gAtt = 1.0f - (float)std::exp(-1.0 / (0.02 * fs)); gRel = 1.0f - (float)std::exp(-1.0 / (0.35 * fs));
    }
    void set(float amountDb, float thresholdDb) { depth = dbToGain(-amountDb); thr = dbToGain(thresholdDb); }
    void reset() { env = 0; gain = 1; }
    /// x = dry input level (max |L|,|R|); returns the wet gain (1 = no ducking)
    inline float next(float x) {
        env += (x > env ? att : rel) * (x - env);
        // soft ratio above the threshold: full depth reached 12 dB over it
        float t = env <= thr ? 0.0f : std::min(1.0f, 20.0f * std::log10(env / thr) / 12.0f);
        const float target = 1.0f + (depth - 1.0f) * t;
        gain += (target < gain ? gAtt : gRel) * (target - gain);
        return gain;
    }
};

struct Shelf1 { // first-order shelf: low (DC gain G) or high (Nyquist gain G), unity elsewhere
    float b0 = 1, b1 = 0, a1 = 0;
    float z[2] = {0, 0};
    void design(double fs, double fc, double G, bool high) {
        const double t = std::tan(kPi * std::min(fc, 0.45 * fs) / fs);
        double B0, B1, A1;
        if (!high) { // H(s) = (s + G wc) / (s + wc)
            const double n = 1.0 / (1.0 + t);
            B0 = (1.0 + G * t) * n; B1 = (G * t - 1.0) * n; A1 = (t - 1.0) * n;
        } else {     // H(s) = (G s + wc) / (s + wc)
            const double n = 1.0 / (1.0 + t);
            B0 = (G + t) * n; B1 = (t - G) * n; A1 = (t - 1.0) * n;
        }
        b0 = (float)B0; b1 = (float)B1; a1 = (float)A1;
    }
    inline float process(float x, int c) { const float y = b0 * x + z[c]; z[c] = b1 * x - a1 * y; return y; }
    void reset() { z[0] = z[1] = 0; }
};

class FdnCore {
public:
    static constexpr int kN = 16;
    void prepare(double fs, double maxSize);
    void reset();
    /// decay (mid RT60 s), bass / treble RT60 multipliers, crossover (Hz) low / high, size factor (0.4..1.6)
    void setDecay(double rt60, double bassMult, double trebleMult, double xLow, double xHigh);
    void setSize(double size, bool immediate);
    void setDiffusion(double amount);    // 0..1
    void setModulation(double depthMs, double rateHz);
    void setFreeze(bool on);
    /// process one stereo sample of (pre-delayed, filtered) input; extra = additional mono injection (shimmer)
    void process(float inL, float inR, float extra, float& outL, float& outR);
    double freezeAmount() const { return freeze_; }
    double lineLength(int i) const { return len_[i]; }
    double meanLoopGain() const { return meanGain_; }
    size_t memoryBytes() const { return memBytes_; }
private:
    void updateGains();
    DelayLine lines_[kN / 2];      // two lines per DelayLine (channels 0 / 1)
    DelayLine diff_[4];            // stereo input diffusion
    float diffLen_[4] = {};
    float diffG_ = 0.6f;
    Shelf1 lowSh_[kN], highSh_[kN];
    ModLfo mod_;
    static constexpr double kMaxSlew = 0.06; // samples per sample (about 1 semitone of Doppler at most)
    double fs_ = 48000, len_[kN] = {}, lenTarget_[kN] = {}, lenStep_[kN] = {}, lenA_ = 0, modDepth_ = 0;
    float g_[kN] = {};
    double rt60_ = 2.5, bassMult_ = 1.2, trebMult_ = 0.5, xLow_ = 250, xHigh_ = 4000, meanGain_ = 0.9;
    double freeze_ = 0, freezeTarget_ = 0, freezeStep_ = 0;
    float guard_ = 1.0f;
    int ctl_ = 0;
    size_t memBytes_ = 0;
};

class PitchShifter {
public:
    void prepare(double fs, double windowMs);
    void reset();
    void setRatio(double r) { ratio_ = r; }
    void setPhaseOffset(double p) { offset_ = p; }
    float process(float x, int c);   // channels 0 / 1 have independent window phases (offset)
    void advance();
    int latencySamples() const { return (int)(dMin_ + 0.5 * w_); }
private:
    DelayLine buf_;
    double w_ = 2400, dMin_ = 4, ratio_ = 2.0, phase_ = 0, offset_ = 0.25;
};

struct StretchedAp { // (a + z^-K) / (1 + a z^-K); state ring of K samples
    static constexpr int kMaxK = 64;
    float ring[kMaxK] = {};
    int pos = 0;
    inline float process(float x, float a, int K) {
        const float d = ring[pos];            // = x[n-K] - a y[n-K]
        const float y = a * x + d;
        ring[pos] = x - a * y;
        if (++pos >= K) pos = 0;
        return y;
    }
    void reset() { for (auto& r : ring) r = 0; pos = 0; }
};

// ------------------------------------------------------------------------------------------------ Hall
class PremiumHall : public Effect {
public:
    enum P { kDecay, kPreDelay, kSize, kBass, kTreble, kEarly, kDiffusion, kModDepth, kModRate, kLowCut, kHighCut,
             kWidth, kMix, kFreeze, kDuck, kDuckThr, kLevel, kNumParams };
    PremiumHall();
    const char* typeId() const override { return "reverb_hall_premium"; }
    const char* displayName() const override { return "Premium Hall"; }
    const char* category() const override { return "Reverb"; }
    bool createsStereo() const override { return true; }
    bool hasTail() const override { return true; }
    const FdnCore& core() const { return fdn_; }
    float duckGain() const { return duck_.gain; }
    size_t memoryBytes() const { return fdn_.memoryBytes() + pre_.bytes(); }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    static constexpr int kEr = 12;
    FdnCore fdn_;
    DelayLine pre_;               // stereo pre-delay + early reflections
    Biquad inHp_, inLp_;
    OnePoleLP erLp_;
    WetDucker duck_;
    LinearSmoothed mix_, width_, level_, early_, preD_, erScale_;
    float erGain_[2][kEr] = {};
    double erMs_[2][kEr] = {};
};

// ------------------------------------------------------------------------------------------------ Spring
class PremiumSpring : public Effect {
public:
    enum P { kDecay, kMix, kTone, kPreDelay, kDrive, kCharacter, kWidth, kDuck, kDuckThr, kLevel, kNumParams };
    static constexpr int kLfStages = 60, kHfStages = 20;
    PremiumSpring();
    const char* typeId() const override { return "reverb_spring_premium"; }
    const char* displayName() const override { return "Premium Spring"; }
    const char* category() const override { return "Reverb"; }
    bool createsStereo() const override { return true; }
    bool hasTail() const override { return true; }
    /// chirp transition frequency (Hz) and loop delay (samples) of tank t
    double transitionHz(int t) const { return spec_.sampleRate / (2.0 * K_[t]); }
    double loopDelay(int t) const { return td_[t]; }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    StretchedAp lf_[2][kLfStages];
    float hfZ_[2][kHfStages] = {};
    DelayLine loop_, loopH_, pre_;
    Biquad lpC_[2], lpC2_[2], hpH_, outHp_, dc_;
    OnePoleLP tone_;
    ModLfo jitter_;
    WetDucker duck_;
    LinearSmoothed mix_, width_, level_;
    int K_[2] = {5, 5};
    double td_[2] = {2000, 2200}, tdH_[2] = {900, 1000};
    float a_[2] = {0.6f, 0.62f}, g_[2] = {0.8f, 0.8f}, gH_[2] = {0.6f, 0.6f}, drive_ = 1.0f, driveN_ = 1.0f;
    int preSamples_ = 0;
};

// ------------------------------------------------------------------------------------------------ Shimmer
class PremiumShimmer : public Effect {
public:
    enum P { kDecay, kShimmer, kInterval, kStructure, kTone, kSize, kModDepth, kPreDelay, kLowCut, kWidth, kMix,
             kFreeze, kDuck, kDuckThr, kLevel, kNumParams };
    enum Structure { kFeedback, kPre };
    PremiumShimmer();
    const char* typeId() const override { return "reverb_shimmer"; }
    const char* displayName() const override { return "Premium Shimmer"; }
    const char* category() const override { return "Reverb"; }
    bool createsStereo() const override { return true; }
    bool hasTail() const override { return true; }
    const FdnCore& core() const { return fdn_; }
    int shifterLatency() const { return shift_.latencySamples(); }
    size_t memoryBytes() const { return fdn_.memoryBytes(); }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    FdnCore fdn_;
    PitchShifter shift_;
    DelayLine pre_;
    Biquad inHp_, shHp_, shLp_;
    WetDucker duck_;
    LinearSmoothed mix_, width_, level_, amount_;
    float fbL_ = 0, fbR_ = 0, regEnv_ = 0, regA_ = 0, regR_ = 0;
    int structure_ = kFeedback, preSamples_ = 0;
};

} // namespace mfx
