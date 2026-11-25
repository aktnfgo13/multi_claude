#include "ui/AnalysisView.h"

#include <cmath>

#include "ui/LookAndFeel.h"

namespace mfx::ui {

namespace {
const juce::Colour kInColour{0xff8fa3c9};  // chain input
const juce::Colour kOutColour = colours::accent; // chain output
using Src = AnalysisWorker::Source;
using Ch = AnalysisWorker::Channel;
} // namespace

AnalysisView::AnalysisView(Engine& engine) : engine_(engine), specIn_(kPoints, -140.0f), specOut_(kPoints, -140.0f) {
    show_.addItemList({"Input", "Output", "Overlay"}, 1);
    show_.setSelectedId(3, juce::dontSendNotification);
    chan_.addItemList({"Mono (L+R)", "Left", "Right", "Stereo L/R"}, 1);
    chan_.setSelectedId(1, juce::dontSendNotification);
    fft_.addItemList({"FFT 2048", "FFT 4096"}, 1);
    fft_.setSelectedId(2, juce::dontSendNotification);
    fft_.onChange = [this] { std::fill(specIn_.begin(), specIn_.end(), -140.0f); std::fill(specOut_.begin(), specOut_.end(), -140.0f); };
    pause_.setClickingTogglesState(true);
    pause_.onClick = [this] {
        worker_.setPaused(pause_.getToggleState());
        pause_.setButtonText(pause_.getToggleState() ? "Resume" : "Pause");
    };
    timeZoom_.setSliderStyle(juce::Slider::LinearHorizontal);
    timeZoom_.setRange(5.0, 2000.0, 1.0);
    timeZoom_.setSkewFactorFromMidPoint(100.0);
    timeZoom_.setValue(50.0, juce::dontSendNotification);
    timeZoom_.setTextValueSuffix(" ms");
    timeZoom_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 18);
    ampZoom_.setSliderStyle(juce::Slider::LinearHorizontal);
    ampZoom_.setRange(1.0, 32.0, 0.1);
    ampZoom_.setSkewFactorFromMidPoint(4.0);
    ampZoom_.setValue(1.0, juce::dontSendNotification);
    ampZoom_.setTextValueSuffix(" x");
    ampZoom_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 18);
    timeLabel_.setText("Time", juce::dontSendNotification);
    ampLabel_.setText("Amp", juce::dontSendNotification);
    for (auto* l : {&timeLabel_, &ampLabel_, &status_}) {
        l->setFont(juce::FontOptions(12.0f));
        l->setColour(juce::Label::textColourId, colours::textDim);
        addAndMakeVisible(*l);
    }
    for (juce::Component* c : {(juce::Component*)&show_, (juce::Component*)&chan_, (juce::Component*)&fft_,
                               (juce::Component*)&pause_, (juce::Component*)&timeZoom_, (juce::Component*)&ampZoom_})
        addAndMakeVisible(*c);
}

AnalysisView::~AnalysisView() {
    stopTimer();
    engine_.analysisTap().setEnabled(false);
}

void AnalysisView::visibilityChanged() { updateActive(); }

void AnalysisView::updateActive() {
    const bool on = isShowing();
    engine_.analysisTap().setEnabled(on);
    if (on) startTimerHz(30);
    else stopTimer();
}

void AnalysisView::timerCallback() {
    if (!isShowing()) { updateActive(); return; }
    tick();
}

void AnalysisView::tick() {
    worker_.poll(engine_.analysisTap());
    if (!worker_.paused()) {
        const int n = fft_.getSelectedId() == 1 ? 2048 : 4096;
        const auto ch = chan_.getSelectedId() == 2 ? Ch::Left : chan_.getSelectedId() == 3 ? Ch::Right : Ch::Mono;
        worker_.smoothedSpectrum(Src::Input, ch, n, kPoints, kFLo, kFHi, specIn_.data());
        worker_.smoothedSpectrum(Src::Output, ch, n, kPoints, kFLo, kFHi, specOut_.data());
    }
    const auto inf = worker_.info();
    juce::String s;
    if (inf.hasData)
        s << juce::String(inf.sampleRate / 1000.0, 1) << " kHz  |  in " << (inf.inChannels > 1 ? "stereo" : "mono")
          << ", out " << (inf.outChannels > 1 ? "stereo" : "mono") << "  |  chain latency " << inf.latency
          << " smp (aligned)  |  dropped " << (int)inf.dropped << (worker_.paused() ? "  |  PAUSED" : "");
    else
        s = "waiting for audio (play the host / input)";
    status_.setText(s, juce::dontSendNotification);
    repaint(waveArea_.getUnion(specArea_));
}

void AnalysisView::resized() {
    auto r = getLocalBounds().reduced(6);
    auto bar = r.removeFromTop(30);
    show_.setBounds(bar.removeFromLeft(110).reduced(2, 3));
    chan_.setBounds(bar.removeFromLeft(130).reduced(2, 3));
    fft_.setBounds(bar.removeFromLeft(100).reduced(2, 3));
    pause_.setBounds(bar.removeFromLeft(80).reduced(2, 3));
    timeLabel_.setBounds(bar.removeFromLeft(40));
    timeZoom_.setBounds(bar.removeFromLeft(200).reduced(2, 4));
    ampLabel_.setBounds(bar.removeFromLeft(38));
    ampZoom_.setBounds(bar.removeFromLeft(180).reduced(2, 4));
    status_.setBounds(r.removeFromBottom(18));
    r.removeFromTop(4);
    waveArea_ = r.removeFromTop(r.getHeight() / 2).reduced(0, 2);
    specArea_ = r.reduced(0, 2);
}

void AnalysisView::paint(juce::Graphics& g) {
    g.fillAll(colours::panel);
    paintWaveform(g, waveArea_.toFloat());
    paintSpectrum(g, specArea_.toFloat());
}

void AnalysisView::drawWave(juce::Graphics& g, juce::Rectangle<float> a, Src src, Ch ch, juce::Colour col) {
    const auto inf = worker_.info();
    if (!inf.hasData) return;
    const int frames = (int)std::round(timeZoom_.getValue() * 0.001 * inf.sampleRate);
    const int cols = std::max(1, (int)a.getWidth());
    mn_.resize((size_t)cols); mx_.resize((size_t)cols);
    if (!worker_.waveform(src, ch, frames, cols, mn_.data(), mx_.data())) return;
    const float zoom = (float)ampZoom_.getValue();
    const float cy = a.getCentreY(), h = a.getHeight() * 0.5f;
    auto y = [&](float v) { return juce::jlimit(a.getY(), a.getBottom(), cy - juce::jlimit(-1.0f, 1.0f, v * zoom) * h); };
    juce::Path p;
    for (int i = 0; i < cols; ++i) {
        const float x = a.getX() + (float)i;
        if (i == 0) p.startNewSubPath(x, y(mx_[0]));
        p.lineTo(x, y(mx_[(size_t)i]));
    }
    for (int i = cols - 1; i >= 0; --i) p.lineTo(a.getX() + (float)i, y(mn_[(size_t)i]));
    p.closeSubPath();
    g.setColour(col.withAlpha(0.35f));
    g.fillPath(p);
    g.setColour(col);
    g.strokePath(p, juce::PathStrokeType(1.0f));
}

void AnalysisView::paintWaveform(juce::Graphics& g, juce::Rectangle<float> a) {
    g.setColour(colours::background);
    g.fillRoundedRectangle(a, 4.0f);
    const int show = show_.getSelectedId(), chanSel = chan_.getSelectedId();
    std::vector<std::pair<juce::Rectangle<float>, Ch>> lanes;
    if (chanSel == 4) {
        auto t = a;
        lanes.push_back({t.removeFromTop(a.getHeight() * 0.5f), Ch::Left});
        lanes.push_back({t, Ch::Right});
    } else {
        lanes.push_back({a, chanSel == 2 ? Ch::Left : chanSel == 3 ? Ch::Right : Ch::Mono});
    }
    for (auto& [r, ch] : lanes) {
        g.setColour(colours::outline);
        g.drawHorizontalLine((int)r.getCentreY(), r.getX(), r.getRight());
        if (show == 1 || show == 3) drawWave(g, r.reduced(0, 3), Src::Input, ch, kInColour);
        if (show == 2 || show == 3) drawWave(g, r.reduced(0, 3), Src::Output, ch, kOutColour);
        g.setColour(colours::textDim);
        g.setFont(juce::FontOptions(11.0f));
        const char* nm = ch == Ch::Left ? "L" : ch == Ch::Right ? "R" : "L+R";
        g.drawText(nm, r.reduced(6, 4), juce::Justification::topLeft);
    }
    // legend (identity is never colour-only: names drawn next to the swatches)
    auto lg = a.reduced(8, 6).removeFromTop(14).removeFromRight(170);
    g.setFont(juce::FontOptions(11.0f));
    if (show == 1 || show == 3) { g.setColour(kInColour); g.fillRect(lg.removeFromLeft(10).reduced(0, 3)); g.setColour(colours::text); g.drawText("Input", lg.removeFromLeft(70), juce::Justification::centredLeft); }
    if (show == 2 || show == 3) { g.setColour(kOutColour); g.fillRect(lg.removeFromLeft(10).reduced(0, 3)); g.setColour(colours::text); g.drawText("Output", lg.removeFromLeft(70), juce::Justification::centredLeft); }
}

void AnalysisView::paintSpectrum(juce::Graphics& g, juce::Rectangle<float> a) {
    g.setColour(colours::background);
    g.fillRoundedRectangle(a, 4.0f);
    auto plot = a.reduced(34, 8).withTrimmedBottom(10);
    auto xOf = [&](float f) { return plot.getX() + plot.getWidth() * std::log(f / kFLo) / std::log(kFHi / kFLo); };
    auto yOf = [&](float db) { return plot.getY() + plot.getHeight() * (kDbHi - juce::jlimit(kDbLo, kDbHi, db)) / (kDbHi - kDbLo); };
    g.setFont(juce::FontOptions(10.0f));
    for (float f : {50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f}) {
        const float x = xOf(f);
        g.setColour(colours::outline.withAlpha(0.6f));
        g.drawVerticalLine((int)x, plot.getY(), plot.getBottom());
        g.setColour(colours::textDim);
        g.drawText(f >= 1000.f ? juce::String((int)(f / 1000.f)) + "k" : juce::String((int)f), (int)x - 16, (int)plot.getBottom() + 1, 32, 10, juce::Justification::centred);
    }
    for (float db = 0.f; db >= kDbLo; db -= 24.f) {
        const float y = yOf(db);
        g.setColour(colours::outline.withAlpha(0.6f));
        g.drawHorizontalLine((int)y, plot.getX(), plot.getRight());
        g.setColour(colours::textDim);
        g.drawText(juce::String((int)db), (int)a.getX() + 2, (int)y - 6, 30, 12, juce::Justification::centredRight);
    }
    g.drawText("dBFS", (int)a.getX() + 2, (int)plot.getY() - 2, 30, 10, juce::Justification::centredRight);
    const auto inf = worker_.info();
    const float fMax = inf.sampleRate > 0 ? (float)(0.5 * inf.sampleRate) : kFHi;
    auto draw = [&](const std::vector<float>& s, juce::Colour c) {
        juce::Path p;
        bool started = false;
        for (int i = 0; i < kPoints; ++i) {
            const float f = AnalysisWorker::pointFrequency(i, kPoints, kFLo, kFHi);
            if (f > fMax) break;
            const float x = xOf(f), y = yOf(s[(size_t)i]);
            if (!started) { p.startNewSubPath(x, y); started = true; } else p.lineTo(x, y);
        }
        g.setColour(c);
        g.strokePath(p, juce::PathStrokeType(2.0f));
    };
    const int show = show_.getSelectedId();
    if (inf.hasData) {
        if (show == 1 || show == 3) draw(specIn_, kInColour);
        if (show == 2 || show == 3) draw(specOut_, kOutColour);
    }
    auto lg = plot.reduced(6, 2).removeFromTop(14).removeFromRight(230);
    g.setFont(juce::FontOptions(11.0f));
    if (show == 1 || show == 3) { g.setColour(kInColour); g.fillRect(lg.removeFromLeft(10).reduced(0, 3)); g.setColour(colours::text); g.drawText("Input", lg.removeFromLeft(60), juce::Justification::centredLeft); }
    if (show == 2 || show == 3) { g.setColour(kOutColour); g.fillRect(lg.removeFromLeft(10).reduced(0, 3)); g.setColour(colours::text); g.drawText("Output", lg.removeFromLeft(60), juce::Justification::centredLeft); }
    g.setColour(colours::textDim);
    g.drawText("Hann, log freq", lg, juce::Justification::centredRight);
}

} // namespace mfx::ui
