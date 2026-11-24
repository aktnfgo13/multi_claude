// Parameter panel for the selected effect: knobs, choice boxes, toggles, plus IR / NAM / LUT loaders and
// a tuner read-out. Generated from the effect's ParamInfo, so every effect gets a working UI.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/PluginProcessor.h"

namespace mfx::ui {

class ParamPanel : public juce::Component, private juce::Timer {
public:
    explicit ParamPanel(plugin::Processor& p);
    ~ParamPanel() override;
    void setSlot(int slot);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void build();
    plugin::Processor& proc_;
    int slot_ = -1;
    Effect* fx_ = nullptr;
    struct Control {
        int param;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::Label> label;
    };
    std::vector<Control> controls_;
    std::unique_ptr<juce::TextButton> fileButton_;
    std::unique_ptr<juce::Label> status_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace mfx::ui
