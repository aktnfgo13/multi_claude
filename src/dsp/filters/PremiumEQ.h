// Premium Parametric EQ (v0.7, type "eq_parametric_premium"). "parametric_eq" / "graphic_eq" / "filter" unchanged.
//
// 5 bands, each Off / Bell / Low Shelf / High Shelf / HPF 12 / HPF 24 / LPF 12 / LPF 24 / Notch.
// Topology: trapezoidal (TPT) state-variable filter with output mixing (A. Simper, "Linear trap SVF",
// Cytomic technical paper 2013 / V. Zavalishin, "The Art of VA Filter Design"). Static response equals the
// RBJ/bilinear biquad with pre-warped cut-off; the SVF is chosen because its states stay valid while
// coefficients move (per-sample coefficient ramps, no zipper, no transient from TDF-II state mismatch).
//  * smoothing: frequency (log domain), gain (dB) and Q (log) follow one-pole 20 ms targets evaluated every
//    16 samples; g, k, m0..m2 are ramped linearly per sample between those points,
//  * type changes and Off/On cross-fade 20 ms between the old and new filter of the band (dual filter),
//  * limits: 10 Hz <= f <= 0.49 fs (tan() bounded), 0.1 <= Q <= 18, gain +/-24 dB (documented reasons),
//  * responseDb(): exact magnitude of the digital filter (analogue prototype evaluated at j tan(w/2)/g),
//    targetResponseDb(): same from the parameter targets (thread-safe for the UI).
// Parameter order keeps all continuous controls inside the first 16 host-automation slots.
#pragma once

#include "core/Effect.h"

namespace mfx {

class PremiumEQ : public Effect {
public:
    static constexpr int kBands = 5;
    enum Type { kOff, kBell, kLowShelf, kHighShelf, kHp12, kHp24, kLp12, kLp24, kNotch, kNumTypes };
    // index = band * 3 + {0 freq, 1 gain, 2 q}; then output; then band types
    static constexpr int kOutput = kBands * 3;
    static constexpr int kTypeBase = kOutput + 1;
    PremiumEQ();
    const char* typeId() const override { return "eq_parametric_premium"; }
    const char* displayName() const override { return "Premium EQ"; }
    const char* category() const override { return "EQ"; }
    /// Magnitude (dB) of the filter currently running (after smoothing).
    double responseDb(double f) const;
    /// Magnitude (dB) for the parameter targets at sample rate fs (UI / tests; reads only the ParameterSet).
    static double targetResponseDb(const ParameterSet& ps, double fs, double f);

    struct Coeffs { float g = 0, k = 1, k2 = 1, m0 = 1, m1 = 0, m2 = 0; bool cascade = false; float a1 = 1, b1 = 1; void prep(); };
    static Coeffs design(int type, double fs, double f, double gainDb, double q);
    static double magnitude(const Coeffs& c, double fs, double f);

protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;

private:
    struct Filter {
        Coeffs cur, from, to;           // per-sample ramp from -> to over one control block
        float s1[kMaxChannels][2] = {}, s2[kMaxChannels][2] = {};
        int type = kOff;
        bool moving = false;
        void clearState() { for (auto& a : s1) a[0] = a[1] = 0; for (auto& a : s2) a[0] = a[1] = 0; }
    };
    struct Band {
        Filter f[2];                    // active + fading-out filter for type cross-fades
        int active = 0;
        int xfade = 0;                  // samples left in the cross-fade (fading f[1 - active] out)
        double logF = 0, gain = 0, logQ = 0, tLogF = 0, tGain = 0, tLogQ = 0;
        int pendingType = kOff;
    };
    static float tick(Filter& fl, const Coeffs& c, float x, int ch);
    void controlUpdate(bool snap);
    Band bands_[kBands];
    float out_ = 1.0f, outTarget_ = 1.0f, outStep_ = 0.0f;
    float smoothA_ = 0.0f;
    int ctl_ = 0, xfadeLen_ = 960;
    static constexpr int kCtl = 16;
};

} // namespace mfx
