#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/PluginProcessor.h"
#include "ui/AnalysisView.h"
#include "ui/ChainView.h"
#include "ui/LookAndFeel.h"
#include "ui/ParamPanel.h"

namespace mfx::plugin {

/// Simple level meter (peak + RMS, dBFS scale -60..+6, clip LED).
class MeterBar : public juce::Component {
public:
    void set(float peak, float rms, bool clip, bool hot = false) {
        peak_ = peak; rms_ = rms;
        clip_ = clip || clipHold_ > 0; clipHold_ = clip ? 40 : std::max(0, clipHold_ - 1);
        hot_ = hot || hotHold_ > 0; hotHold_ = hot ? 30 : std::max(0, hotHold_ - 1);
        repaint();
    }
    void setNominalMarkerDb(float db) { nominalDb_ = db; }
    void paint(juce::Graphics&) override;
private:
    float peak_ = 0, rms_ = 0, nominalDb_ = 1000.0f; // > +6 = no marker
    bool clip_ = false, hot_ = false;
    int clipHold_ = 0, hotHold_ = 0;
};

class Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor& p);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void savePreset();
    void loadPresetFile();
    Processor& proc_;
    ui::LookAndFeel lnf_;
    juce::ComboBox presets_;
    juce::TextButton save_{"Save"}, load_{"Load"}, parallel_{"Parallel A/B"};
    juce::TextButton effectsTab_{"Effects"}, visualizeTab_{"Visualize"};
    void showVisualize(bool v);
    juce::ComboBox inputMode_;
    juce::Slider inGain_, outGain_, parMix_;
    juce::Label inLabel_, outLabel_, mixLabel_, cpu_, latency_, srcLevel_;
    MeterBar inMeter_, outMeter_;
    ui::ChainView chain_;
    ui::ParamPanel panel_;
    ui::AnalysisView analysis_;
    std::unique_ptr<juce::FileChooser> chooser_;
    int lastStructure_ = -1;
    float cpuShown_ = 0.0f, srcPeakHold_ = 0.0f;
};

} // namespace mfx::plugin
