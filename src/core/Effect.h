// Common interface of every DSP block.
//
// Threading contract
//   - prepare()/reset() and construction: message (non real-time) thread, never concurrently with process().
//   - params().set(): any thread (atomic).
//   - process(): audio thread. Must not allocate, lock, or do I/O.
#pragma once

#include <string>
#include <vector>

#include "core/Common.h"
#include "core/Parameter.h"

namespace mfx {

class Effect {
public:
    virtual ~Effect() = default;

    virtual const char* typeId() const = 0;
    virtual const char* displayName() const = 0;
    virtual const char* category() const = 0;

    void prepare(const ProcessSpec& s) {
        spec_ = s;
        onPrepare(s);
        params_.consumeDirty();
        onParametersChanged(true);
        onReset();
    }
    void reset() {
        params_.consumeDirty();
        onParametersChanged(true);
        onReset();
    }
    /// block.numChannels may be 1 (mono path) or 2.
    void process(AudioBlock& block) {
        if (params_.consumeDirty()) onParametersChanged(false);
        onProcess(block);
    }

    /// Fixed processing latency in samples at the prepared sample rate.
    virtual int latencySamples() const { return 0; }
    /// True when a mono input may produce a stereo output (chorus, stereo delay, reverb, pan...).
    virtual bool createsStereo() const { return false; }
    /// True for effects whose output continues after the input stops (delay, reverb). The engine keeps
    /// processing them while bypassed (input faded to silence) so tails ring out ("trails").
    /// Only honoured for zero-latency effects.
    virtual bool hasTail() const { return false; }
    /// Host tempo for synced effects (called on audio thread before process()).
    virtual void setTempo(double /*bpm*/) {}

    /// Non-parameter state (e.g. IR / model file path). Message thread only.
    virtual std::string getExtraState() const { return {}; }
    /// Returns false (and leaves the effect usable) when the state cannot be applied.
    virtual bool setExtraState(const std::string& /*state*/, std::string* /*error*/ = nullptr) { return true; }
    /// Where to look for external files referenced by a state (preset folder, user libraries), and where
    /// to report non-fatal problems (e.g. a file that was found but differs from the saved one).
    struct StateContext {
        std::vector<std::string> searchDirs;
        std::vector<std::string>* warnings = nullptr;
    };
    /// Message thread. Default: ignores the context.
    virtual bool setExtraStateWithContext(const std::string& state, const StateContext& /*ctx*/, std::string* error = nullptr) {
        return setExtraState(state, error);
    }
    /// Message thread: free resources retired by the audio thread (IR / model swaps).
    virtual void collectGarbage() {}

    ParameterSet& params() { return params_; }
    const ParameterSet& params() const { return params_; }
    const ProcessSpec& spec() const { return spec_; }

    // convenience
    void setParam(const char* id, float v) { params_.set(params_.indexOf(id), v); }
    float getParam(const char* id) const { int i = params_.indexOf(id); return i >= 0 ? params_.get(i) : 0.0f; }

protected:
    virtual void onPrepare(const ProcessSpec& spec) = 0;
    virtual void onReset() = 0;
    /// Read params_ and update DSP targets. immediate=true -> jump without smoothing.
    virtual void onParametersChanged(bool immediate) = 0;
    virtual void onProcess(AudioBlock& block) = 0;

    ParameterSet params_;
    ProcessSpec spec_;
};

} // namespace mfx
