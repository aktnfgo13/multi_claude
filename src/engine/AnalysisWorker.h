// Analysis worker (NOT real-time): drains an AnalysisTap, keeps a latency-aligned history of the chain
// input and output, and computes waveform (min/max per column) and spectrum (Hann window, 2048/4096 FFT,
// dBFS, log-frequency display points, visual smoothing) views. Owns no thread: the caller (UI timer or a
// worker thread) calls poll() and the view functions from one thread.
//
// Alignment: output frame t is produced from input frame t - L (L = chain latency reported in the
// packets). Views read the input history delayed by L, so input and output line up on screen.
#pragma once

#include <cstdint>
#include <vector>

#include "dsp/fft/FFT.h"
#include "engine/AnalysisTap.h"

namespace mfx {

class AnalysisWorker {
public:
    enum class Source { Input, Output };
    enum class Channel { Mono, Left, Right }; // Mono = (L + R) / 2

    struct Info {
        double sampleRate = 0.0;
        int inChannels = 0, outChannels = 0, latency = 0;
        uint64_t lastFrame = 0;   // frame index after the newest captured sample
        uint32_t dropped = 0;     // packets dropped by the tap (queue full)
        uint64_t framesCaptured = 0;
        bool hasData = false;
    };

    /// historySeconds of audio is kept at up to maxSampleRate (allocation happens here).
    explicit AnalysisWorker(double historySeconds = 2.0, double maxSampleRate = 192000.0);

    /// Drain the tap. When paused, packets are consumed but the history is frozen. Returns packets read.
    int poll(AnalysisTap& tap);
    void setPaused(bool p) { paused_ = p; }
    bool paused() const { return paused_; }
    void clear();
    Info info() const { return info_; }

    /// Number of aligned frames available for views.
    int availableFrames() const;
    /// Waveform of the most recent numFrames (aligned), reduced to `columns` min/max pairs.
    /// Returns false if not enough data. stereo views: call once per channel.
    bool waveform(Source s, Channel ch, int numFrames, int columns, float* outMin, float* outMax) const;
    /// Copy the most recent numFrames (aligned) of one source/channel. Returns false if not enough data.
    bool latest(Source s, Channel ch, int numFrames, float* dst) const;

    /// Spectrum of the most recent fftSize frames (2048 or 4096), Hann window, amplitude in dBFS
    /// (a full-scale sine reads 0 dBFS), sampled at `points` log-spaced frequencies in [fLo, fHi].
    /// Smoothing: per point exponential average in dB (attack/release time constants in update calls).
    bool spectrum(Source s, Channel ch, int fftSize, int points, float fLo, float fHi, float* outDb);
    /// Same as spectrum() then smoothed into `state` (size points; initialise with -120). attack/release in 0..1
    /// per call (fraction of the new value taken when rising / falling).
    bool smoothedSpectrum(Source s, Channel ch, int fftSize, int points, float fLo, float fHi,
                          float* state, float attack = 0.6f, float release = 0.15f);
    static float pointFrequency(int i, int points, float fLo, float fHi);

private:
    float sampleAt(Source s, Channel ch, int64_t ageFromNewest) const; // 0 = newest aligned frame
    void ensureFft(int n);

    int cap_ = 0;                   // ring length (frames)
    std::vector<float> ring_[2][kMaxChannels]; // [source][channel]
    int64_t written_ = 0;           // frames written into the rings
    int latency_ = 0;
    uint32_t generation_ = 0;
    bool paused_ = false;
    Info info_;
    // spectrum scratch
    int fftN_ = 0;
    FFT fft_;
    std::vector<float> win_, frame_, mag_;
    std::vector<FFT::cf> bins_, scratch_;
    float winSum_ = 1.0f;
};

} // namespace mfx
