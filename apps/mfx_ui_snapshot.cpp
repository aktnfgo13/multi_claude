// Developer tool (not shipped): renders the Visualize panel (AnalysisView) off-screen after feeding the
// engine a test signal, and writes a PNG. Used to check the waveform/spectrum drawing without a host.
//   mfx_ui_snapshot out.png [preset.json]
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "engine/EffectFactory.h"
#include "engine/Engine.h"
#include "presets/Preset.h"
#include "ui/AnalysisView.h"
#include "ui/LookAndFeel.h"

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    juce::ScopedJuceInitialiser_GUI gui;
    mfx::Engine engine;
    engine.prepare(48000, 256);
    if (argc > 2) mfx::loadPresetFile(engine, argv[2]);
    else engine.addEffect(mfx::createEffect("custom_drive"));
    mfx::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    mfx::ui::AnalysisView view(engine);
    view.setBounds(0, 0, 1100, 560);
    engine.analysisTap().setEnabled(true);
    // decaying two-note signal, 1.5 s
    const int fs = 48000, n = fs * 3 / 2;
    std::vector<float> x((size_t)n), l((size_t)n), r((size_t)n);
    for (int i = 0; i < n; ++i) {
        const double t = (double)i / fs;
        x[(size_t)i] = (float)(0.35 * std::exp(-2.0 * t) * (std::sin(2 * M_PI * 110 * t) + 0.5 * std::sin(2 * M_PI * 165 * t)));
    }
    // the view consumes the tap through its timer; here we drive it manually through visibility+timer-free path
    for (int p = 0; p < n; p += 256) {
        const float* in[1] = {x.data() + p};
        float* out[2] = {l.data() + p, r.data() + p};
        engine.process(in, 1, out, 2, std::min(256, n - p));
        if ((p / 256) % 16 == 0) view.pollForSnapshot();
    }
    view.pollForSnapshot();
    auto img = view.createComponentSnapshot(view.getLocalBounds());
    juce::File f(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]));
    f.deleteFile();
    juce::FileOutputStream os(f);
    juce::PNGImageFormat png;
    const bool ok = png.writeImageToStream(img, os);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    return ok ? 0 : 1;
}
