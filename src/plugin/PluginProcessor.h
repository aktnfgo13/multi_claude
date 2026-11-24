// JUCE wrapper around mfx::Engine. The DSP core never includes JUCE.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

#include "engine/Engine.h"

namespace mfx::plugin {

/// Host-automatable parameter bound to (slot, parameter index). Name follows the effect currently in the slot.
class SlotParameter : public juce::AudioProcessorParameter {
public:
    SlotParameter(Engine& e, int slot, int index) : engine_(e), slot_(slot), index_(index) {}
    float getValue() const override { return value_.load(); }
    /// Any thread. Discrete targets (bypass, choices, toggles) are quantised so that the stored
    /// state always reproduces the value; the bound effect is updated immediately (atomic).
    void setValue(float v) override;
    bool isDiscrete() const override;
    int getNumSteps() const override;
    float getDefaultValue() const override { return 0.5f; }
    juce::String getName(int maxLen) const override;
    juce::String getLabel() const override { return {}; }
    juce::String getText(float v, int) const override;
    float getValueForText(const juce::String& t) const override { return t.getFloatValue(); }
    bool isAutomatable() const override { return true; }

    /// message thread: bind to an effect parameter (or unbind with nullptr) and mirror its value
    void bind(Effect* fx);
    void mirror(float normalized) { value_.store(normalized); }
    bool consumeDirty() { return dirty_.exchange(false); }
    int slot() const { return slot_; }
    int index() const { return index_; }

private:
    Engine& engine_;
    int slot_, index_;
    std::atomic<float> value_{0.5f};
    std::atomic<bool> dirty_{false};
    std::atomic<Effect*> fx_{nullptr}; // display only (names / text), message thread updates
};

class Processor : public juce::AudioProcessor, private juce::Timer {
public:
    static constexpr int kParamsPerSlot = 16;
    Processor();
    ~Processor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MFX Claude"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- message-thread API used by the editor
    Engine& engine() { return engine_; }
    int addEffect(const juce::String& typeId, Lane lane);
    void removeEffect(int slot);
    void moveEffect(int slot, Lane lane, int index);
    void setEffectParam(int slot, int param, float plainValue); // routes through host parameter (undo/automation)
    void refreshBindings();
    bool loadPresetJson(const juce::String& json, juce::String& error);
    juce::String currentPresetJson(const juce::String& name) const;
    juce::StringArray factoryPresetNames() const;
    juce::String presetName() const { return presetName_; }
    std::atomic<int> structureVersion{0}; // editor rebuilds when this changes

private:
    static BusesProperties makeBuses();
    void timerCallback() override;
    Engine engine_;
    std::array<std::array<SlotParameter*, kParamsPerSlot>, Engine::kMaxSlots> slotParams_{};
    std::array<SlotParameter*, Engine::kMaxSlots> bypassParams_{};
    juce::AudioParameterFloat* inGain_ = nullptr;
    juce::AudioParameterFloat* outGain_ = nullptr;
    juce::AudioParameterFloat* parMix_ = nullptr;
    int lastLatency_ = -1;
    int currentProgram_ = 0;
    juce::String presetName_ = "Init";
    static constexpr const char* kHostParamsKey = "host_params_unbound";
    std::vector<std::pair<juce::String, juce::String>> factory_; // name, json
    bool prepared_ = false;
};

} // namespace mfx::plugin
