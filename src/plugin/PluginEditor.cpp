#include "plugin/PluginEditor.h"

#include "core/BuildInfo.h"

namespace mfx::plugin {

using namespace mfx::ui;

void MeterBar::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(colours::panelLight);
    g.fillRoundedRectangle(r, 3.0f);
    auto toX = [&](float lin) {
        const float db = lin > 0 ? 20.0f * std::log10(lin) : -100.0f;
        return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f) * (r.getWidth() - 12.0f);
    };
    g.setColour(colours::accent.withAlpha(0.45f));
    g.fillRoundedRectangle(r.withWidth(toX(rms_)).reduced(0, 2), 2.0f);
    g.setColour(peak_ > 0.89f ? colours::warn : colours::accent);
    g.fillRect(r.getX() + toX(peak_), r.getY() + 2, 2.0f, r.getHeight() - 4);
    g.setColour(clip_ ? colours::clip : colours::outline);
    g.fillEllipse(r.getRight() - 10, r.getCentreY() - 4, 8, 8);
}

Editor::Editor(Processor& p) : AudioProcessorEditor(p), proc_(p), chain_(p), panel_(p) {
    setLookAndFeel(&lnf_);
    addAndMakeVisible(presets_);
    presets_.addItemList(proc_.factoryPresetNames(), 1);
    presets_.setTextWhenNothingSelected("Presets");
    presets_.onChange = [this] {
        const int i = presets_.getSelectedId() - 1;
        if (i >= 0) proc_.setCurrentProgram(i);
        chain_.setSelected(-1);
        chain_.rebuild();
    };
    for (auto* b : {&save_, &load_, &parallel_}) addAndMakeVisible(*b);
    save_.onClick = [this] { savePreset(); };
    load_.onClick = [this] { loadPresetFile(); };
    parallel_.setClickingTogglesState(true);
    parallel_.onClick = [this] { proc_.engine().setParallel(parallel_.getToggleState()); proc_.refreshBindings(); chain_.rebuild(); };

    addAndMakeVisible(inputMode_);
    inputMode_.addItemList({"Input: Auto", "Input: Mono (L)", "Input: Mono (L+R)", "Input: Stereo"}, 1);
    inputMode_.setSelectedId((int)proc_.engine().inputMode() + 1, juce::dontSendNotification);
    inputMode_.onChange = [this] { proc_.engine().setInputMode((Engine::InputMode)(inputMode_.getSelectedId() - 1)); };

    auto setupKnob = [this](juce::Slider& s, juce::Label& l, const juce::String& name, const juce::String& paramId) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 18);
        auto* param = dynamic_cast<juce::AudioParameterFloat*>(proc_.getParameters()[paramId == "input_gain" ? 0 : paramId == "output_gain" ? 1 : 2]);
        s.setRange(param->range.start, param->range.end, 0.1);
        s.setValue(param->get(), juce::dontSendNotification);
        s.onValueChange = [&s, param] { param->setValueNotifyingHost(param->convertTo0to1((float)s.getValue())); };
        s.setDoubleClickReturnValue(true, param->convertFrom0to1(static_cast<juce::AudioProcessorParameter*>(param)->getDefaultValue()));
        l.setText(name, juce::dontSendNotification);
        l.setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(s);
        addAndMakeVisible(l);
    };
    setupKnob(inGain_, inLabel_, "IN", "input_gain");
    setupKnob(outGain_, outLabel_, "OUT", "output_gain");
    setupKnob(parMix_, mixLabel_, "A/B", "parallel_mix");
    for (auto* l : {&cpu_, &latency_}) { l->setFont(juce::FontOptions(12.0f)); l->setColour(juce::Label::textColourId, colours::textDim); addAndMakeVisible(*l); }
    addAndMakeVisible(inMeter_);
    addAndMakeVisible(outMeter_);
    addAndMakeVisible(chain_);
    addAndMakeVisible(panel_);
    chain_.onSelectionChanged = [this](int slot) { panel_.setSlot(slot); };
    chain_.onStructureChanged = [this] { chain_.rebuild(); };

    setResizable(true, true);
    setResizeLimits(980, 600, 2560, 1600);
    setSize(1180, 720);
    presets_.setSelectedId(proc_.getCurrentProgram() + 1, juce::dontSendNotification);
    // show the first block's parameters right away
    const auto order = proc_.engine().allSlotsInOrder();
    if (!order.empty()) chain_.setSelected(order.front());
    startTimerHz(30);
}

Editor::~Editor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void Editor::paint(juce::Graphics& g) {
    g.fillAll(colours::background);
    g.setColour(colours::text);
    g.setFont(juce::FontOptions(18.0f).withStyle("Bold"));
    g.drawText("MFX Claude", 14, 10, 160, 28, juce::Justification::centredLeft);
    g.setColour(colours::textDim);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText(juce::String("v") + build::version + " " + build::gitCommit, (float)getWidth() - 230.0f, (float)getHeight() - 16.0f, 220.0f, 14.0f, juce::Justification::centredRight);
    g.setColour(colours::textDim);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(proc_.presetName(), 14, 34, 150, 16, juce::Justification::centredLeft);
    g.setColour(colours::outline);
    g.drawHorizontalLine(52, 170.0f, (float)getWidth() - 10.0f);
}

void Editor::resized() {
    auto r = getLocalBounds().reduced(10);
    auto top = r.removeFromTop(84);
    auto row1 = top.removeFromTop(42);
    row1.removeFromLeft(170);
    presets_.setBounds(row1.removeFromLeft(240).reduced(2, 7));
    save_.setBounds(row1.removeFromLeft(64).reduced(2, 7));
    load_.setBounds(row1.removeFromLeft(64).reduced(2, 7));
    parallel_.setBounds(row1.removeFromLeft(120).reduced(2, 7));
    inputMode_.setBounds(row1.removeFromLeft(170).reduced(2, 7));
    auto row2 = top;
    row2.removeFromLeft(170);
    for (auto [s, l] : {std::pair{&inGain_, &inLabel_}, std::pair{&outGain_, &outLabel_}, std::pair{&parMix_, &mixLabel_}}) {
        auto c = row2.removeFromLeft(130);
        l->setBounds(c.removeFromLeft(30));
        s->setBounds(c.reduced(0, 2));
    }
    auto meters = row2.removeFromLeft(220).reduced(8, 4);
    inMeter_.setBounds(meters.removeFromTop(meters.getHeight() / 2).reduced(0, 3));
    outMeter_.setBounds(meters.reduced(0, 3));
    cpu_.setBounds(row2.removeFromTop(row2.getHeight() / 2).withTrimmedLeft(8));
    latency_.setBounds(row2.withTrimmedLeft(8));
    r.removeFromTop(6);
    const int chainH = juce::jlimit(220, 340, r.getHeight() / 2);
    chain_.setBounds(r.removeFromTop(chainH));
    r.removeFromTop(8);
    panel_.setBounds(r);
    chain_.rebuild();
}

void Editor::timerCallback() {
    const auto m = proc_.engine().readMeters();
    inMeter_.set(m.inPeak, m.inRms, m.inClip);
    outMeter_.set(m.outPeak, m.outRms, m.outClip);
    cpuShown_ = 0.85f * cpuShown_ + 0.15f * m.cpuLoad;
    cpu_.setText("CPU " + juce::String(100.0f * cpuShown_, 1) + " %  (peak " + juce::String(100.0f * m.cpuPeak, 0) + " %)", juce::dontSendNotification);
    const int lat = proc_.engine().latencySamples();
    latency_.setText("Latency " + juce::String(lat) + " smp / " + juce::String(1000.0 * lat / proc_.engine().sampleRate(), 2) + " ms", juce::dontSendNotification);
    if (proc_.structureVersion.load() != lastStructure_) {
        lastStructure_ = proc_.structureVersion.load();
        presets_.setSelectedId(proc_.getCurrentProgram() + 1, juce::dontSendNotification);
        parallel_.setToggleState(proc_.engine().isParallel(), juce::dontSendNotification);
        chain_.rebuild();
        panel_.setSlot(chain_.selectedSlot());
        repaint();
    }
}

void Editor::savePreset() {
    chooser_ = std::make_unique<juce::FileChooser>("Save preset", juce::File(), "*.json");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        f = f.withFileExtension("json");
        f.replaceWithText(proc_.currentPresetJson(f.getFileNameWithoutExtension()));
    });
}

void Editor::loadPresetFile() {
    chooser_ = std::make_unique<juce::FileChooser>("Load preset", juce::File(), "*.json");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (!f.existsAsFile()) return;
        juce::String err;
        if (!proc_.loadPresetJson(f.loadFileAsString(), err) || err.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Preset", err.isEmpty() ? "Could not load preset" : err);
        chain_.setSelected(-1);
        chain_.rebuild();
        repaint();
    });
}

} // namespace mfx::plugin
