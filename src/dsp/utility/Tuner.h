// Chromatic tuner. The audio thread only copies (decimated) samples into a lock-free ring;
// pitch detection (YIN) runs on a non-real-time thread (UI timer / tests) via TunerAnalyzer.
// Tap tempo helper.
#pragma once

#include <atomic>
#include <vector>

#include "core/Effect.h"

namespace mfx {

struct PitchResult {
    bool valid = false;
    double frequency = 0.0;
    int midiNote = 0;
    double cents = 0.0;
    double clarity = 0.0; // 1 - YIN aperiodicity
    const char* noteName() const;
};

/// YIN (de Cheveigné & Kawahara 2002) with parabolic interpolation.
PitchResult detectPitchYin(const float* x, int n, double fs, double minHz = 30.0, double maxHz = 1500.0, double threshold = 0.12);

class Tuner : public Effect {
public:
    enum P { kMute, kReference };
    static constexpr int kDecim = 2;
    static constexpr int kRing = 8192;
    Tuner();
    const char* typeId() const override { return "tuner"; }
    const char* displayName() const override { return "Tuner"; }
    const char* category() const override { return "Utility"; }
    /// Non-RT: analyse the most recent window.
    PitchResult analyze() const;

protected:
    void onPrepare(const ProcessSpec&) override {}
    void onReset() override { w_.store(0); acc_ = 0; accN_ = 0; for (auto& r : ring_) r.store(0.0f, std::memory_order_relaxed); }
    void onParametersChanged(bool) override {}
    void onProcess(AudioBlock& b) override;

private:
    std::atomic<float> ring_[kRing]; // written by the audio thread, read by the UI thread (relaxed)
    std::atomic<uint32_t> w_{0};
    float acc_ = 0.0f;
    int accN_ = 0;
};

class TapTempo {
public:
    /// Register a tap at time t (seconds). Returns current BPM estimate (0 if not enough taps).
    double tap(double t);
    double bpm() const { return bpm_; }
    void reset() { n_ = 0; bpm_ = 0.0; }
private:
    double times_[8] = {};
    int n_ = 0;
    double bpm_ = 0.0;
};

} // namespace mfx
