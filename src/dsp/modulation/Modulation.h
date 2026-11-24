// Modulation effects: Chorus, Flanger, Vibrato (modulated delay lines), Phaser (modulated all-pass
// cascade with feedback), Tremolo (amplitude / optional stereo-pan, tempo sync).
#pragma once

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"
#include "dsp/utility/DelayLine.h"

namespace mfx {

/// Shared modulated-delay core for chorus / flanger / vibrato.
class ModDelayEffect : public Effect {
protected:
    struct Cfg { float baseMs, depthMs, rateHz, feedback, mix, spread; };
    virtual Cfg cfg() const = 0;
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
    DelayLine line_;
    LFO lfo_;
    LinearSmoothed base_, depth_, mix_, fb_;
    float fbState_[kMaxChannels] = {0, 0};
    float spread_ = 0.25f;
    Biquad fbLp_;
};

class Chorus : public ModDelayEffect {
public:
    Chorus();
    const char* typeId() const override { return "chorus"; }
    const char* displayName() const override { return "Chorus"; }
    const char* category() const override { return "Modulation"; }
    bool createsStereo() const override { return true; }
protected:
    Cfg cfg() const override;
};

class Flanger : public ModDelayEffect {
public:
    Flanger();
    const char* typeId() const override { return "flanger"; }
    const char* displayName() const override { return "Flanger"; }
    const char* category() const override { return "Modulation"; }
    bool createsStereo() const override { return true; }
protected:
    Cfg cfg() const override;
};

class Vibrato : public ModDelayEffect {
public:
    Vibrato();
    const char* typeId() const override { return "vibrato"; }
    const char* displayName() const override { return "Vibrato"; }
    const char* category() const override { return "Modulation"; }
protected:
    Cfg cfg() const override;
};

class Phaser : public Effect {
public:
    enum P { kStages, kRate, kDepth, kCentre, kFeedback, kMix, kStereo };
    Phaser();
    const char* typeId() const override { return "phaser"; }
    const char* displayName() const override { return "Phaser"; }
    const char* category() const override { return "Modulation"; }
    bool createsStereo() const override { return true; }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    static constexpr int kMaxStages = 12;
    float z_[kMaxChannels][kMaxStages] = {};
    float fb_[kMaxChannels] = {0, 0};
    float coef_[kMaxChannels] = {0, 0};
    LFO lfo_;
    LinearSmoothed mix_;
    int stages_ = 6;
    uint32_t counter_ = 0; // global sample counter: coefficient updates independent of host block size
    float depth_ = 0.7f, centre_ = 800.0f, feedback_ = 0.3f, stereo_ = 0.25f;
};

class Tremolo : public Effect {
public:
    enum P { kRate, kDepth, kShape, kStereo, kSync };
    Tremolo();
    const char* typeId() const override { return "tremolo"; }
    const char* displayName() const override { return "Tremolo"; }
    const char* category() const override { return "Modulation"; }
    bool createsStereo() const override { return true; }
    void setTempo(double bpm) override { bpm_ = bpm; }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override { lfo_.reset(); }
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    LFO lfo_;
    LinearSmoothed depth_;
    double bpm_ = 120.0;
};

/// Tempo-sync note divisions (in beats). Index 0 = "Off".
double syncDivisionBeats(int index);
extern const char* kSyncChoices[];
constexpr int kNumSyncChoices = 10;

} // namespace mfx
