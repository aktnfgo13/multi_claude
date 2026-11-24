// Lock-free parameter storage. Writers: any thread (UI, host automation, MIDI). Reader: audio thread.
#pragma once

#include <atomic>
#include <cstring>
#include <string>

#include "core/Common.h"

namespace mfx {

enum class ParamUnit { None, Percent, Decibels, Hertz, Milliseconds, Seconds, Ratio, Choice, Toggle, BPM, Semitones };
enum class ParamScale { Linear, Log };

struct ParamInfo {
    const char* id = "";      // stable identifier used in presets
    const char* name = "";    // display name
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    ParamUnit unit = ParamUnit::None;
    ParamScale scale = ParamScale::Linear;
    const char* const* choices = nullptr; // for Choice
    int numChoices = 0;
    bool isDiscrete() const { return unit == ParamUnit::Choice || unit == ParamUnit::Toggle; }
};

const char* unitSuffix(ParamUnit u);

class ParameterSet {
public:
    static constexpr int kMaxParams = 24;

    int add(const ParamInfo& info) {
        if (count_ >= kMaxParams) return -1;
        infos_[count_] = info;
        values_[count_].store(info.def, std::memory_order_relaxed);
        return count_++;
    }
    int size() const { return count_; }
    const ParamInfo& info(int i) const { return infos_[i]; }

    int indexOf(const char* id) const {
        for (int i = 0; i < count_; ++i)
            if (std::strcmp(infos_[i].id, id) == 0) return i;
        return -1;
    }

    /// Thread-safe set (plain value, clamped / rounded for discrete).
    void set(int i, float v) {
        if (i < 0 || i >= count_) return;
        const auto& in = infos_[i];
        if (!std::isfinite(v)) return;
        v = clampv(v, in.min, in.max);
        if (in.isDiscrete()) v = std::round(v);
        values_[i].store(v, std::memory_order_relaxed);
        dirty_.fetch_or(1u, std::memory_order_release);
    }
    float get(int i) const { return values_[i].load(std::memory_order_relaxed); }
    int getInt(int i) const { return (int)std::lround(get(i)); }

    void setNormalized(int i, float n) { set(i, fromNormalized(i, n)); }
    float getNormalized(int i) const { return toNormalized(i, get(i)); }

    float toNormalized(int i, float v) const {
        const auto& in = infos_[i];
        if (in.max <= in.min) return 0.0f;
        if (in.scale == ParamScale::Log && in.min > 0.0f)
            return (float)(std::log(v / in.min) / std::log(in.max / in.min));
        return (v - in.min) / (in.max - in.min);
    }
    float fromNormalized(int i, float n) const {
        const auto& in = infos_[i];
        n = clampv(n, 0.0f, 1.0f);
        if (in.scale == ParamScale::Log && in.min > 0.0f) return (float)mapLog(n, in.min, in.max);
        return in.min + n * (in.max - in.min);
    }

    /// Audio thread: returns true once after any write.
    bool consumeDirty() { return dirty_.exchange(0u, std::memory_order_acquire) != 0u; }

    std::string formatValue(int i, float v) const;

private:
    ParamInfo infos_[kMaxParams];
    std::atomic<float> values_[kMaxParams];
    std::atomic<uint32_t> dirty_{1u};
    int count_ = 0;
};

} // namespace mfx
