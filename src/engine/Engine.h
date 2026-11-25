// Real-time audio engine: signal routing, slots, bypass, meters, CPU load, latency, MIDI CC map.
//
// Routing topology (split / merge):
//     input -> [Pre chain] -> split -> [Lane A] --+
//                                  \-> [Lane B] ---> merge (A/B balance) -> [Post chain] -> output
//   With parallel disabled the lanes are simply processed in series (Pre, A, B, Post).
//
// Threading
//   message thread : add/remove/move effects, routing, presets, latency queries, collectGarbage()
//   any thread     : bypass, parameter values, gains, tempo (atomics)
//   audio thread   : process(), handleMidi(), setParameterRT()
// Routing changes are published lock-free (RtPublisher) and applied behind a 3 ms fade-out / fade-in.
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "engine/AnalysisTap.h"
#include "engine/RtPublisher.h"

namespace mfx {

enum class Lane : int { Pre = 0, A = 1, B = 2, Post = 3 };
constexpr int kNumLanes = 4;
const char* laneName(Lane l);

struct Routing {
    static constexpr int kMaxSlots = 16;
    std::array<std::array<int8_t, kMaxSlots>, kNumLanes> order{}; // slot ids per lane
    std::array<int, kNumLanes> count{};
    bool parallel = false;
    std::array<Effect*, kMaxSlots> fx{}; // raw pointers valid while this routing is live
    uint32_t version = 0;
};

struct MeterValues {
    float inPeak = 0, inRms = 0, outPeak = 0, outRms = 0; // linear; "in" = chain input (after input gain)
    bool inClip = false, outClip = false;
    float srcPeak = 0;          // host/device input before input gain and input mode (linear)
    bool inHot = false;         // chain input peak >= InputLevels::kHotPeak (-3 dBFS) since last read
    bool inNonFinite = false;   // NaN/Inf input samples were replaced by 0 since last read
    float cpuLoad = 0, cpuPeak = 0; // fraction of real-time budget
};

/// Input level reference (digital). Used by meters / UI only — never changes the signal.
struct InputLevels {
    static constexpr float kNominalRmsDb = -20.0f; // provisional nominal guitar level at the chain input
    static constexpr float kHotPeak = 0.70794578f; // -3 dBFS
    static constexpr float kClipPeak = 1.0f;       // 0 dBFS
};

struct MidiMapping {
    int channel = -1;   // -1 = omni, else 0..15
    int cc = 0;
    int slot = 0;
    int param = 0;      // -1 = bypass toggle (>= 64 -> bypassed)
    float min = 0.0f, max = 1.0f; // normalized range of the target parameter
};

class Engine {
public:
    static constexpr int kMaxSlots = Routing::kMaxSlots;
    static constexpr int kChunk = 256; // internal processing chunk (host blocks are split)
    enum class InputMode { Auto, MonoLeft, MonoSum, Stereo };

    Engine();
    ~Engine();

    // ---------------- message thread
    void prepare(double sampleRate, int maxBlockSize);
    double sampleRate() const { return fs_; }
    int maxBlockSize() const { return maxBlock_; }

    /// Returns slot id (0..15) or -1 if full. index -1 = append.
    int addEffect(std::unique_ptr<Effect> fx, Lane lane = Lane::Pre, int index = -1);
    bool removeEffect(int slot);
    bool moveEffect(int slot, Lane lane, int index);
    void clear();
    void setParallel(bool on);
    bool isParallel() const { return msg_.parallel; }
    Effect* effect(int slot) const { return (slot >= 0 && slot < kMaxSlots) ? owned_[slot].get() : nullptr; }
    /// Ordered slot ids for a lane (message-thread view).
    std::vector<int> laneSlots(Lane lane) const;
    /// All slots in processing order (Pre, A, B, Post).
    std::vector<int> allSlotsInOrder() const;
    Lane laneOf(int slot) const;
    int latencySamples() const;
    void collectGarbage();

    // ---------------- any thread
    void setBypass(int slot, bool b) {
        if (slot < 0 || slot >= kMaxSlots) return;
        bypass_[slot].store(b);
        if (!audioRunning_.load()) { slotRT_[slot].wet = b ? 0.0f : 1.0f; slotRT_[slot].wasFullyBypassed = b; } // no fade when stopped
    }
    bool isBypassed(int slot) const { return slot >= 0 && slot < kMaxSlots && bypass_[slot].load(); }
    void setInputGainDb(float db) { inGainDb_.store(db); }
    void setOutputGainDb(float db) { outGainDb_.store(db); }
    float inputGainDb() const { return inGainDb_.load(); }
    float outputGainDb() const { return outGainDb_.load(); }
    void setParallelMix(float m) { parMix_.store(clampv(m, 0.0f, 1.0f)); }
    float parallelMix() const { return parMix_.load(); }
    void setTempo(double bpm) { if (bpm > 20 && bpm < 400) bpm_.store(bpm); }
    double tempo() const { return bpm_.load(); }
    void setInputMode(InputMode m) { inputMode_.store((int)m); }
    InputMode inputMode() const { return (InputMode)inputMode_.load(); }
    /// Read and reset peak-hold values.
    MeterValues readMeters();
    /// Real-time analysis tap (chain input / output). Disabled by default; never changes the output.
    AnalysisTap& analysisTap() { return tap_; }

    /// MIDI mapping table (message thread publishes, audio thread reads).
    void setMidiMappings(const std::vector<MidiMapping>& maps);
    std::vector<MidiMapping> midiMappings() const { return midiMsg_; }

    // ---------------- audio thread
    /// in/out may alias. numIn/numOut: 1 or 2.
    void process(const float* const* in, int numIn, float* const* out, int numOut, int numSamples);
    void handleMidi(int status, int data1, int data2);
    /// Real-time safe parameter set addressing the *live* routing (host automation).
    void setParameterRT(int slot, int param, float normalized);

private:
    struct SlotRT {
        float wet = 1.0f;          // bypass crossfade gain (1 = effect active)
        bool wasFullyBypassed = false;
        static constexpr int kDelay = 256;
        float dry[kMaxChannels][kDelay] = {};
        int pos = 0;
        const Effect* lastFx = nullptr; // audio thread resets the slot state when the slot gets a new effect
    };
    struct MidiTable { std::vector<MidiMapping> maps; };

    void publishRouting();
    void processLane(const Routing& r, int lane, float** buf, bool& mono, int n);
    void processSlot(const Routing& r, int slot, float** buf, bool& mono, int n);
    static void delayBuffer(float* data, int n, float* ring, int ringLen, int& pos, int delay);

    double fs_ = 48000.0;
    int maxBlock_ = 512;
    std::unique_ptr<Effect> owned_[kMaxSlots];
    struct Grave { std::unique_ptr<Effect> fx; uint32_t version; int slot; };
    std::vector<Grave> graveyard_;
    Routing msg_;
    uint32_t version_ = 0;
    RtPublisher<Routing> routing_;
    std::atomic<uint32_t> audioVersion_{0};
    std::atomic<bool> audioRunning_{false};

    std::atomic<bool> bypass_[kMaxSlots];
    SlotRT slotRT_[kMaxSlots];

    // routing change fade
    enum class FadeState { None, Out, In };
    FadeState fadeState_ = FadeState::None;
    float fade_ = 1.0f, fadeStep_ = 0.01f;

    // buffers
    std::vector<float> main_[kMaxChannels], laneB_[kMaxChannels];
    static constexpr int kLaneDelay = 1024;
    float laneDelay_[2][kMaxChannels][kLaneDelay] = {};
    int laneDelayPos_[2][kMaxChannels] = {};

    std::atomic<float> inGainDb_{0.0f}, outGainDb_{0.0f}, parMix_{0.5f};
    std::atomic<double> bpm_{120.0};
    std::atomic<int> inputMode_{(int)InputMode::Auto};
    LinearSmoothed inGain_, outGain_, parMixS_;

    // meters
    std::atomic<float> mInPeak_{0}, mOutPeak_{0}, mInRms_{0}, mOutRms_{0}, mCpu_{0}, mCpuPeak_{0};
    std::atomic<bool> mInClip_{false}, mOutClip_{false}, mInHot_{false}, mInNonFinite_{false};
    std::atomic<float> mSrcPeak_{0};
    AnalysisTap tap_;
    static int routingLatency(const Routing& r);
    float inMs_ = 0.0f, outMs_ = 0.0f, rmsCoef_ = 0.0f, cpuAvg_ = 0.0f;

    std::vector<MidiMapping> midiMsg_;
    RtPublisher<MidiTable> midi_;
};

} // namespace mfx
