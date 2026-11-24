#include "dsp/gain/Utility.h"

namespace mfx {

float volumeTaper(float percent) {
    const float v = clampv(percent / 100.0f, 0.0f, 1.0f);
    return v * v * v; // cubic taper: 50% -> -18 dB, perceptually smooth, exact 0 at 0
}

GainUtility::GainUtility() {
    params_.add({"gain", "Gain", -60, 24, 0, ParamUnit::Decibels});
    params_.add({"volume", "Volume", 0, 100, 100, ParamUnit::Percent});
    params_.add({"invert", "Phase Invert", 0, 1, 0, ParamUnit::Toggle});
    params_.add({"invert_r", "Invert Right", 0, 1, 0, ParamUnit::Toggle});
}

void GainUtility::onParametersChanged(bool immediate) {
    const float g = dbToGain(params_.get(kGain)) * volumeTaper(params_.get(kVolume));
    if (immediate) g_.setImmediate(g); else g_.setTarget(g);
    const float inv = params_.getInt(kInvert) ? -1.0f : 1.0f;
    sign_[0] = inv;
    sign_[1] = inv * (params_.getInt(kInvertRight) ? -1.0f : 1.0f);
}

void GainUtility::onProcess(AudioBlock& b) {
    for (int i = 0; i < b.numSamples; ++i) {
        const float g = g_.next();
        for (int c = 0; c < b.numChannels; ++c) b.ch[c][i] *= g * sign_[c];
    }
}

Pan::Pan() {
    params_.add({"pan", "Pan", -100, 100, 0, ParamUnit::Percent});
    params_.add({"width", "Width", 0, 200, 100, ParamUnit::Percent});
}

void Pan::onParametersChanged(bool immediate) {
    const float p = params_.get(0) / 100.0f, w = params_.get(1) / 100.0f;
    if (immediate) { pan_.setImmediate(p); width_.setImmediate(w); }
    else { pan_.setTarget(p); width_.setTarget(w); }
}

void Pan::onProcess(AudioBlock& b) {
    if (b.numChannels < 2) return; // engine always gives 2 channels to stereo creators
    for (int i = 0; i < b.numSamples; ++i) {
        const float p = pan_.next(), w = width_.next();
        float l = b.ch[0][i], r = b.ch[1][i];
        // mid/side width
        const float m = 0.5f * (l + r), s = 0.5f * (l - r) * w;
        l = m + s; r = m - s;
        // constant-power pan law (-3 dB centre, normalised so centre = unity)
        const double ang = (p + 1.0) * kPi * 0.25;
        const float gl = (float)(std::cos(ang) * 1.41421356), gr = (float)(std::sin(ang) * 1.41421356);
        b.ch[0][i] = l * gl;
        b.ch[1][i] = r * gr;
    }
}

} // namespace mfx
