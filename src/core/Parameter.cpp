#include "core/Parameter.h"

#include <cstdio>

namespace mfx {

const char* unitSuffix(ParamUnit u) {
    switch (u) {
        case ParamUnit::Percent: return "%";
        case ParamUnit::Decibels: return "dB";
        case ParamUnit::Hertz: return "Hz";
        case ParamUnit::Milliseconds: return "ms";
        case ParamUnit::Seconds: return "s";
        case ParamUnit::Ratio: return ":1";
        case ParamUnit::BPM: return "BPM";
        case ParamUnit::Semitones: return "st";
        default: return "";
    }
}

float paramToNormalized(const ParamInfo& in, float v) {
    if (in.max <= in.min) return 0.0f;
    if (in.scale == ParamScale::Log && in.min > 0.0f)
        return (float)(std::log(v / in.min) / std::log(in.max / in.min));
    return (v - in.min) / (in.max - in.min);
}

float paramFromNormalized(const ParamInfo& in, float n) {
    n = clampv(n, 0.0f, 1.0f);
    if (in.scale == ParamScale::Log && in.min > 0.0f) return (float)mapLog(n, in.min, in.max);
    return in.min + n * (in.max - in.min);
}

std::string formatParamValue(const ParamInfo& in, float v) {
    char buf[64];
    if (in.unit == ParamUnit::Choice && in.choices) {
        int idx = clampv((int)std::lround(v), 0, in.numChoices - 1);
        return in.choices[idx];
    }
    if (in.unit == ParamUnit::Toggle) return v >= 0.5f ? "On" : "Off";
    if (in.unit == ParamUnit::Hertz && v >= 1000.0f) {
        std::snprintf(buf, sizeof(buf), "%.2f kHz", v / 1000.0f);
        return buf;
    }
    const float a = std::fabs(v);
    const char* fmt = a >= 100.0f ? "%.0f %s" : (a >= 10.0f ? "%.1f %s" : "%.2f %s");
    std::snprintf(buf, sizeof(buf), fmt, v, unitSuffix(in.unit));
    std::string s(buf);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

} // namespace mfx
