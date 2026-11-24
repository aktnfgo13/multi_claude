#include "dsp/filters/EQ.h"

namespace mfx {

static const char* kFilterTypes[] = {"Low Pass 12", "Low Pass 24", "High Pass 12", "High Pass 24",
                                     "Low Shelf", "High Shelf", "Peak", "Band Pass", "Notch"};

FilterBlock::FilterBlock() {
    ParamInfo t{"type", "Type", 0, 8, 0, ParamUnit::Choice};
    t.choices = kFilterTypes; t.numChoices = 9;
    params_.add(t);
    params_.add({"freq_hz", "Frequency", 20, 20000, 5000, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"q", "Q", 0.1f, 18, 0.7071f, ParamUnit::None, ParamScale::Log});
    params_.add({"gain_db", "Gain", -24, 24, 0, ParamUnit::Decibels});
}

void FilterBlock::updateDsp() {
    const double fs = spec_.sampleRate, f = p(kFreq), q = p(kQ), g = p(kGain);
    cascade_ = false;
    switch (pi(kType)) {
        case 0: f_.design(FilterType::LowPass, fs, f, q); break;
        case 1: f_.design(FilterType::LowPass, fs, f, 0.5412); f2_.design(FilterType::LowPass, fs, f, 1.3066); cascade_ = true; break;
        case 2: f_.design(FilterType::HighPass, fs, f, q); break;
        case 3: f_.design(FilterType::HighPass, fs, f, 0.5412); f2_.design(FilterType::HighPass, fs, f, 1.3066); cascade_ = true; break;
        case 4: f_.design(FilterType::LowShelf, fs, f, q, g); break;
        case 5: f_.design(FilterType::HighShelf, fs, f, q, g); break;
        case 6: f_.design(FilterType::Peak, fs, f, q, g); break;
        case 7: f_.design(FilterType::BandPass, fs, f, q); break;
        default: f_.design(FilterType::Notch, fs, f, q); break;
    }
}

void FilterBlock::processSub(AudioBlock& b) {
    f_.processBlock(b);
    if (cascade_) f2_.processBlock(b);
}

ParametricEQ::ParametricEQ() {
    params_.add({"low_cut_hz", "Low Cut", 10, 1000, 10, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"low_hz", "Low Freq", 30, 500, 100, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"low_db", "Low Gain", -18, 18, 0, ParamUnit::Decibels});
    params_.add({"p1_hz", "Peak 1 Freq", 40, 2000, 250, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"p1_db", "Peak 1 Gain", -18, 18, 0, ParamUnit::Decibels});
    params_.add({"p1_q", "Peak 1 Q", 0.2f, 10, 1, ParamUnit::None, ParamScale::Log});
    params_.add({"p2_hz", "Peak 2 Freq", 150, 6000, 800, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"p2_db", "Peak 2 Gain", -18, 18, 0, ParamUnit::Decibels});
    params_.add({"p2_q", "Peak 2 Q", 0.2f, 10, 1, ParamUnit::None, ParamScale::Log});
    params_.add({"p3_hz", "Peak 3 Freq", 500, 16000, 3000, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"p3_db", "Peak 3 Gain", -18, 18, 0, ParamUnit::Decibels});
    params_.add({"p3_q", "Peak 3 Q", 0.2f, 10, 1, ParamUnit::None, ParamScale::Log});
    params_.add({"high_hz", "High Freq", 1000, 16000, 6000, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"high_db", "High Gain", -18, 18, 0, ParamUnit::Decibels});
    params_.add({"high_cut_hz", "High Cut", 1000, 22000, 22000, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"output_db", "Output", -24, 24, 0, ParamUnit::Decibels});
}

void ParametricEQ::updateDsp() {
    const double fs = spec_.sampleRate;
    bands_[0].design(FilterType::HighPass, fs, p(kLowCut), 0.7071);
    bands_[1].design(FilterType::LowShelf, fs, p(kLowF), 0.7071, p(kLowG));
    bands_[2].design(FilterType::Peak, fs, p(kP1F), p(kP1Q), p(kP1G));
    bands_[3].design(FilterType::Peak, fs, p(kP2F), p(kP2Q), p(kP2G));
    bands_[4].design(FilterType::Peak, fs, p(kP3F), p(kP3Q), p(kP3G));
    bands_[5].design(FilterType::HighShelf, fs, p(kHighF), 0.7071, p(kHighG));
    bands_[6].design(FilterType::LowPass, fs, std::min((double)p(kHighCut), 0.45 * fs), 0.7071);
    out_ = dbToGain(p(kOut));
}

double ParametricEQ::responseDb(double f) const {
    double m = 1.0;
    for (const auto& b : bands_) m *= biquadMagnitude(b.coeffs(), spec_.sampleRate, f);
    return 20.0 * std::log10(std::max(m * out_, 1e-12));
}

void ParametricEQ::processSub(AudioBlock& b) {
    // Low cut at minimum and high cut at maximum are bypassed (true "off" positions).
    const bool lowCutOn = p(kLowCut) > 10.5f;
    const bool highCutOn = p(kHighCut) < 21500.0f;
    for (int c = 0; c < b.numChannels; ++c) {
        float* x = b.ch[c];
        for (int i = 0; i < b.numSamples; ++i) {
            float v = x[i];
            if (lowCutOn) v = bands_[0].process(v, c);
            for (int k = 1; k <= 5; ++k) v = bands_[k].process(v, c);
            if (highCutOn) v = bands_[6].process(v, c);
            x[i] = v * out_;
        }
    }
}

const double GraphicEQ::kCentres[kNumBands] = {31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

GraphicEQ::GraphicEQ() {
    static const char* ids[kNumBands] = {"b31", "b62", "b125", "b250", "b500", "b1k", "b2k", "b4k", "b8k", "b16k"};
    static const char* names[kNumBands] = {"31 Hz", "62 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "16 kHz"};
    for (int i = 0; i < kNumBands; ++i) params_.add({ids[i], names[i], -12, 12, 0, ParamUnit::Decibels});
    params_.add({"output_db", "Output", -24, 12, 0, ParamUnit::Decibels});
}

void GraphicEQ::updateDsp() {
    for (int i = 0; i < kNumBands; ++i)
        bands_[i].design(FilterType::Peak, spec_.sampleRate, std::min(kCentres[i], 0.45 * spec_.sampleRate), 1.41, p(i));
    out_ = dbToGain(p(kNumBands));
}

void GraphicEQ::processSub(AudioBlock& b) {
    for (int c = 0; c < b.numChannels; ++c) {
        float* x = b.ch[c];
        for (int i = 0; i < b.numSamples; ++i) {
            float v = x[i];
            for (auto& band : bands_) v = band.process(v, c);
            x[i] = v * out_;
        }
    }
}

} // namespace mfx
