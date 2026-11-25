// Visualize panel: real-time waveform (top) and spectrum (bottom) of the chain input / output.
// Data path: Engine::analysisTap() (audio thread, lock free) -> AnalysisWorker (this component's
// timer, message thread) -> paint. The tap is enabled only while this panel is visible, so the
// effects view costs nothing; enabling it never changes the audio (tested in mfx_core).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "engine/AnalysisWorker.h"
#include "engine/Engine.h"

namespace mfx::ui {

class AnalysisView : public juce::Component, private juce::Timer {
public:
    explicit AnalysisView(Engine& engine);
    ~AnalysisView() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    /// Tools/tests: drain the tap and update spectra once (what the timer does), without needing visibility.
    void pollForSnapshot() { tick(); }

private:
    enum class Show { Input, Output, Overlay };
    enum class Chan { Mono, Left, Right, Stereo };
    void timerCallback() override;
    void tick();
    void updateActive();
    void paintWaveform(juce::Graphics&, juce::Rectangle<float>);
    void paintSpectrum(juce::Graphics&, juce::Rectangle<float>);
    void drawWave(juce::Graphics&, juce::Rectangle<float>, AnalysisWorker::Source, AnalysisWorker::Channel, juce::Colour);

    Engine& engine_;
    AnalysisWorker worker_;
    juce::ComboBox show_, chan_, fft_;
    juce::TextButton pause_{"Pause"};
    juce::Slider timeZoom_, ampZoom_;
    juce::Label timeLabel_, ampLabel_, status_;
    juce::Rectangle<int> waveArea_, specArea_;
    static constexpr int kPoints = 360;
    static constexpr float kFLo = 20.0f, kFHi = 20000.0f, kDbLo = -96.0f, kDbHi = 6.0f;
    std::vector<float> specIn_, specOut_;
    std::vector<float> mn_, mx_;
};

} // namespace mfx::ui
