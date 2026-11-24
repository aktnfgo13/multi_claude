#include "ui/ParamPanel.h"

#include "dsp/cabinet/Cabinet.h"
#include "dsp/drive/Drives.h"
#include "dsp/utility/Tuner.h"
#include "neural/NamAmp.h"
#include "ui/LookAndFeel.h"

namespace mfx::ui {

ParamPanel::ParamPanel(plugin::Processor& p) : proc_(p) { startTimerHz(15); }
ParamPanel::~ParamPanel() { stopTimer(); }

void ParamPanel::setSlot(int slot) {
    slot_ = slot;
    fx_ = proc_.engine().effect(slot);
    build();
}

void ParamPanel::build() {
    controls_.clear();
    fileButton_.reset();
    status_.reset();
    if (!fx_) { repaint(); return; }
    auto& ps = fx_->params();
    for (int i = 0; i < ps.size(); ++i) {
        const auto& in = ps.info(i);
        Control c;
        c.param = i;
        c.label = std::make_unique<juce::Label>(juce::String(), in.name);
        c.label->setJustificationType(juce::Justification::centred);
        c.label->setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(*c.label);
        const int slot = slot_;
        if (in.unit == ParamUnit::Choice) {
            c.combo = std::make_unique<juce::ComboBox>();
            for (int k = 0; k < in.numChoices; ++k) c.combo->addItem(in.choices[k], k + 1);
            c.combo->setSelectedId(ps.getInt(i) + 1, juce::dontSendNotification);
            c.combo->onChange = [this, slot, i, cb = c.combo.get()] { proc_.setEffectParam(slot, i, (float)(cb->getSelectedId() - 1)); };
            addAndMakeVisible(*c.combo);
        } else if (in.unit == ParamUnit::Toggle) {
            c.toggle = std::make_unique<juce::ToggleButton>("On");
            c.toggle->setToggleState(ps.getInt(i) != 0, juce::dontSendNotification);
            c.toggle->onClick = [this, slot, i, t = c.toggle.get()] { proc_.setEffectParam(slot, i, t->getToggleState() ? 1.0f : 0.0f); };
            addAndMakeVisible(*c.toggle);
        } else {
            c.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            c.slider->setRange(0.0, 1.0);
            c.slider->setValue(ps.getNormalized(i), juce::dontSendNotification);
            c.slider->setDoubleClickReturnValue(true, ps.toNormalized(i, in.def));
            c.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
            c.slider->setColour(juce::Slider::rotarySliderFillColourId, colours::forCategory(fx_->category()));
            Effect* fx = fx_;
            c.slider->textFromValueFunction = [fx, i](double v) { return juce::String(fx->params().formatValue(i, fx->params().fromNormalized(i, (float)v))); };
            c.slider->valueFromTextFunction = [fx, i](const juce::String& t) { return (double)fx->params().toNormalized(i, t.getFloatValue()); };
            c.slider->onValueChange = [this, slot, i, s = c.slider.get(), fx] {
                proc_.setEffectParam(slot, i, fx->params().fromNormalized(i, (float)s->getValue()));
            };
            c.slider->updateText();
            addAndMakeVisible(*c.slider);
        }
        controls_.push_back(std::move(c));
    }
    const std::string type = fx_->typeId();
    if (type == "cabinet" || type == "nam_amp" || type == "ddsp_drive") {
        const juce::String label = type == "cabinet" ? "Load IR (.wav)..." : type == "nam_amp" ? "Load .nam model..." : "Load knob LUT (.json)...";
        const juce::String pattern = type == "cabinet" ? "*.wav" : type == "nam_amp" ? "*.nam" : "*.json";
        fileButton_ = std::make_unique<juce::TextButton>(label);
        fileButton_->onClick = [this, pattern] {
            chooser_ = std::make_unique<juce::FileChooser>("Choose file", juce::File(), pattern);
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
                const auto f = fc.getResult();
                if (!f.existsAsFile() || !fx_) return;
                std::string err;
                const bool ok = fx_->setExtraState(f.getFullPathName().toStdString(), &err);
                if (status_) status_->setText(ok ? "Loaded: " + f.getFileName() : "Error: " + juce::String(err), juce::dontSendNotification);
                if (auto* cab = dynamic_cast<Cabinet*>(fx_); cab && ok) proc_.setEffectParam(slot_, Cabinet::kSource, 1.0f);
            });
        };
        addAndMakeVisible(*fileButton_);
    }
    status_ = std::make_unique<juce::Label>();
    status_->setFont(juce::FontOptions(12.0f));
    status_->setColour(juce::Label::textColourId, colours::textDim);
    if (auto* nam = dynamic_cast<NamAmp*>(fx_)) status_->setText(nam->info().status, juce::dontSendNotification);
    if (auto* cab = dynamic_cast<Cabinet*>(fx_)) status_->setText(cab->hasIR() ? "IR: " + juce::String(cab->irName()) : "No IR loaded (built-in simulation available)", juce::dontSendNotification);
    addAndMakeVisible(*status_);
    resized();
    repaint();
}

void ParamPanel::paint(juce::Graphics& g) {
    g.fillAll(colours::panel);
    g.setColour(colours::text);
    g.setFont(juce::FontOptions(16.0f));
    if (!fx_) {
        g.setColour(colours::textDim);
        g.drawText("Select a block in the chain (click \"+\" to add effects)", getLocalBounds(), juce::Justification::centred);
        return;
    }
    g.drawText(juce::String(fx_->displayName()) + "  ·  " + fx_->category(), 12, 6, getWidth() - 24, 22, juce::Justification::centredLeft);
    if (auto* t = dynamic_cast<Tuner*>(fx_)) {
        const auto r = t->analyze();
        g.setFont(juce::FontOptions(28.0f));
        g.setColour(r.valid && std::fabs(r.cents) < 3 ? colours::accent : colours::warn);
        juce::String s = r.valid ? juce::String(r.noteName()) + juce::String(r.midiNote / 12 - 1) + "  " + juce::String(r.cents, 1) + " ct  (" + juce::String(r.frequency, 1) + " Hz)" : "--";
        g.drawText(s, getLocalBounds().removeFromBottom(48), juce::Justification::centred);
    }
}

void ParamPanel::resized() {
    auto area = getLocalBounds().reduced(10).withTrimmedTop(26);
    auto bottom = area.removeFromBottom(30);
    if (fileButton_) fileButton_->setBounds(bottom.removeFromLeft(200).reduced(2));
    if (status_) status_->setBounds(bottom.reduced(6, 2));
    const int cellW = 96, cellH = juce::jmin(120, area.getHeight());
    const int perRow = juce::jmax(1, area.getWidth() / cellW);
    for (size_t i = 0; i < controls_.size(); ++i) {
        auto& c = controls_[i];
        const int col = (int)i % perRow, row = (int)i / perRow;
        auto cell = juce::Rectangle<int>(area.getX() + col * cellW, area.getY() + row * cellH, cellW, cellH).reduced(3);
        c.label->setBounds(cell.removeFromTop(18));
        if (c.slider) c.slider->setBounds(cell);
        if (c.combo) c.combo->setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 26));
        if (c.toggle) c.toggle->setBounds(cell.withSizeKeepingCentre(cell.getWidth() - 10, 26));
    }
}

void ParamPanel::timerCallback() {
    if (slot_ >= 0 && proc_.engine().effect(slot_) != fx_) { setSlot(-1); return; }
    if (!fx_) return;
    // reflect external changes (automation / MIDI)
    for (auto& c : controls_) {
        const auto& ps = fx_->params();
        if (c.slider && !c.slider->isMouseButtonDown()) c.slider->setValue(ps.getNormalized(c.param), juce::dontSendNotification);
        if (c.combo) c.combo->setSelectedId(ps.getInt(c.param) + 1, juce::dontSendNotification);
        if (c.toggle) c.toggle->setToggleState(ps.getInt(c.param) != 0, juce::dontSendNotification);
    }
    if (dynamic_cast<Tuner*>(fx_)) repaint();
}

} // namespace mfx::ui
