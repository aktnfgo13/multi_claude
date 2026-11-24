#include "ui/LookAndFeel.h"

namespace mfx::ui {

juce::Colour colours::forCategory(const juce::String& c) {
    if (c == "Drive") return juce::Colour(0xffd9734a);
    if (c == "Amp") return juce::Colour(0xffc9a24a);
    if (c == "Cabinet") return juce::Colour(0xff9c7b5c);
    if (c == "Dynamics") return juce::Colour(0xff6aa4d8);
    if (c == "EQ") return juce::Colour(0xff8fc46a);
    if (c == "Modulation") return juce::Colour(0xffa77ad6);
    if (c == "Delay") return juce::Colour(0xff4fb3a9);
    if (c == "Reverb") return juce::Colour(0xff5d8fd1);
    return juce::Colour(0xff8d93a1);
}

LookAndFeel::LookAndFeel() {
    using namespace colours;
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::Slider::rotarySliderFillColourId, accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, outline);
    setColour(juce::Slider::thumbColourId, text);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, text);
    setColour(juce::TextButton::buttonColourId, panelLight);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::buttonOnColourId, accent.withAlpha(0.6f));
    setColour(juce::ComboBox::backgroundColourId, panelLight);
    setColour(juce::ComboBox::outlineColourId, outline);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.35f));
    setColour(juce::ToggleButton::textColourId, text);
    setColour(juce::ToggleButton::tickColourId, accent);
    setColour(juce::ListBox::backgroundColourId, panel);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) {
    const auto b = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(4.0f);
    const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float ang = start + pos * (end - start);
    juce::Path bg, fg;
    bg.addCentredArc(c.x, c.y, r - 3, r - 3, 0.0f, start, end, true);
    fg.addCentredArc(c.x, c.y, r - 3, r - 3, 0.0f, start, ang, true);
    g.setColour(colours::outline);
    g.strokePath(bg, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(s.findColour(juce::Slider::rotarySliderFillColourId));
    g.strokePath(fg, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colours::panelLight);
    g.fillEllipse(c.x - r + 8, c.y - r + 8, 2 * r - 16, 2 * r - 16);
    juce::Path needle;
    needle.addRoundedRectangle(-1.5f, -(r - 9), 3.0f, r * 0.45f, 1.5f);
    g.setColour(colours::text);
    g.fillPath(needle, juce::AffineTransform::rotation(ang).translated(c.x, c.y));
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    auto col = b.getToggleState() ? findColour(juce::TextButton::buttonOnColourId) : base;
    if (down) col = col.brighter(0.15f);
    else if (over) col = col.brighter(0.07f);
    g.setColour(col);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(colours::outline);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
}

juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int h) {
    return juce::Font(juce::FontOptions(juce::jmin(14.0f, (float)h * 0.55f)));
}

} // namespace mfx::ui
