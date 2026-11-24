// Minimal dark theme. Original design (no commercial GUI reproduced).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mfx::ui {

namespace colours {
const juce::Colour background{0xff141518};
const juce::Colour panel{0xff1d1f24};
const juce::Colour panelLight{0xff272a31};
const juce::Colour outline{0xff353943};
const juce::Colour text{0xffe7e9ee};
const juce::Colour textDim{0xff8d93a1};
const juce::Colour accent{0xff4fb3a9};
const juce::Colour warn{0xffe0a93b};
const juce::Colour clip{0xffe0533b};
juce::Colour forCategory(const juce::String& category);
} // namespace colours

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override;
};

} // namespace mfx::ui
