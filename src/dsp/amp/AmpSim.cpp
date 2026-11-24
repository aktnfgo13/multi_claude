#include "dsp/amp/AmpSim.h"

namespace mfx {

static const char* kAmpModels[] = {"Clean", "Crunch", "High Gain", "Custom"};

// Character descriptions, NOT measurements of a specific product.
static const AmpModelDef kModels[AmpSim::kNumModels] = {
    // Clean: 2 stages, scooped tone stack early, big power-amp headroom, light sag
    {"Clean", 2, {-4, 6, 0, 0}, {20, 30, 0, 0}, {-2, -1, 0, 0}, {120, 200, 0, 0},
     14000, 0.9, 2.2, 0, 450, -7.0, 1.6, 0.25, 1200, 26.0},
    // Crunch: 3 stages, mild scoop, moderate headroom
    {"Crunch", 3, {0, 14, 6, 0}, {20, 120, 60, 0}, {-3, -4, 0, 0}, {150, 300, 0, 0},
     12000, 0.7, 2.0, 2, 650, -2.0, 1.0, 0.45, 2200, 13.0},
    // High gain: 4 cascaded stages, tight coupling filters, tone stack after last stage
    {"High Gain", 4, {2, 16, 14, 8}, {35, 300, 180, 90}, {-6, -6, -3, 0}, {250, 400, 300, 0},
     9000, 0.6, 1.8, 3, 700, -4.0, 0.9, 0.35, 0, 2.0},
    // Custom: neutral 3-stage reference
    {"Custom", 3, {0, 12, 8, 0}, {25, 90, 60, 0}, {-3, -2, 0, 0}, {150, 250, 0, 0},
     12000, 0.75, 2.0, 2, 550, -3.0, 1.1, 0.4, 1800, 13.0},
};

const AmpModelDef& ampModel(int i) { return kModels[clampv(i, 0, AmpSim::kNumModels - 1)]; }

AmpSim::AmpSim() {
    ParamInfo m{"model", "Model", 0, (float)(kNumModels - 1), 1, ParamUnit::Choice};
    m.choices = kAmpModels; m.numChoices = kNumModels;
    params_.add(m);
    params_.add({"gain", "Gain", 0, 100, 50, ParamUnit::Percent});
    params_.add({"bass", "Bass", 0, 100, 50, ParamUnit::Percent});
    params_.add({"mid", "Mid", 0, 100, 50, ParamUnit::Percent});
    params_.add({"treble", "Treble", 0, 100, 50, ParamUnit::Percent});
    params_.add({"presence", "Presence", 0, 100, 40, ParamUnit::Percent});
    params_.add({"depth", "Depth", 0, 100, 40, ParamUnit::Percent});
    params_.add({"master", "Master", 0, 100, 50, ParamUnit::Percent});
    params_.add({"sag", "Sag", 0, 100, 50, ParamUnit::Percent});
    params_.add({"bright", "Bright", 0, 1, 0, ParamUnit::Toggle});
    addCommonParams(0.0f, 1);
}

void AmpSim::prepareDsp(double, double) {}

void AmpSim::resetDsp() {
    for (Biquad* b : {&inHp_, &inLp_, &bright_, &tsLow_, &tsMid_, &tsHigh_, &presence_, &depth_, &otHp_, &otLp_, &spkRes_, &spkHigh_}) b->reset();
    for (int s = 0; s < kMaxStages; ++s) { coup_[s].reset(); cath_[s].reset(); miller_[s].reset(); }
    for (auto& ch : bias_) for (auto& b : ch) b = 0.0f;
    sagEnv_[0] = sagEnv_[1] = 0.0f;
}

void AmpSim::updateDsp(double fs, double fsOs) {
    model_ = clampv(params_.getInt(kModel), 0, kNumModels - 1);
    def_ = &kModels[model_];
    const auto& d = *def_;
    const double gain = knob(kGain) / 100.0, bass = knob(kBass) / 100.0, mid = knob(kMid) / 100.0;
    const double treb = knob(kTreble) / 100.0, pres = knob(kPresence) / 100.0, dep = knob(kDepth) / 100.0;
    const double master = knob(kMaster) / 100.0, sag = knob(kSag) / 100.0;

    inHp_.design(FilterType::HighPass1, fs, 25.0);
    inLp_.design(FilterType::LowPass1, fs, std::min(16000.0, 0.45 * fs)); // grid stopper + Miller of V1a
    const bool brightOn = params_.getInt(kBright) != 0 && d.brightHz > 0;
    // bright cap bypasses the gain pot: more treble boost at low gain settings
    bright_.design(FilterType::HighShelf1, fs, brightOn ? d.brightHz : 2000.0, 0.7071, brightOn ? 9.0 * (1.0 - gain) + 2.0 : 0.0);

    // Gain staging: stageGainDb = gain into the triode curve (curve units: grid conduction ~0.6..0.9).
    // Gain pot = audio taper on the stage-1 input (-24..+6 dB); stages >= 3 also track the gain knob.
    for (int s = 0; s < kMaxStages; ++s) {
        double gdb = d.stageGainDb[s];
        if (s == 0) gdb += -24.0 + 30.0 * gain;
        else if (s >= 2) gdb += -12.0 + 12.0 * gain;
        stageGain_[s] = (float)dbToGainD(gdb);
        coup_[s].design(FilterType::HighPass1, fsOs, std::max(5.0, d.couplingHz[s]));
        cath_[s].design(FilterType::LowShelf1, fsOs, std::max(20.0, d.cathodeHz[s]), 0.7071, d.cathodeShelfDb[s]);
        miller_[s].design(FilterType::LowPass1, fsOs, std::min(d.millerHz, 0.45 * fsOs));
    }
    tsLow_.design(FilterType::LowShelf, fsOs, 120.0, 0.6, 24.0 * (bass - 0.5));
    tsMid_.design(FilterType::Peak, fsOs, d.midHz, 0.8, d.midScoopDb + 20.0 * (mid - 0.5));
    tsHigh_.design(FilterType::HighShelf, fsOs, 2500.0, 0.6, 24.0 * (treb - 0.5));
    presence_.design(FilterType::HighShelf, fsOs, 3500.0, 0.7, 9.0 * pres);
    depth_.design(FilterType::Peak, fsOs, 90.0, 1.0, 9.0 * dep);

    master_ = (float)dbToGainD(-24.0 + 30.0 * master);
    sagDepth_ = (float)(d.sagMax * sag);
    powerA_ = (float)d.powerHeadroom;
    outNorm_ = (float)dbToGainD(d.outputTrimDb) / powerA_;

    otHp_.design(FilterType::HighPass, fs, 55.0, 0.6);
    otLp_.design(FilterType::LowPass1, fs, std::min(11000.0, 0.45 * fs));
    spkRes_.design(FilterType::Peak, fs, 105.0, 1.2, 3.0);
    spkHigh_.design(FilterType::HighShelf, fs, 3000.0, 0.7, 2.0);

    biasCharge_ = 1.0f - onePoleCoeff(0.003, fsOs);
    biasRelease_ = 1.0f - onePoleCoeff(0.12, fsOs);
    sagA_ = 1.0f - onePoleCoeff(0.015, fsOs);
    sagR_ = 1.0f - onePoleCoeff(0.25, fsOs);
}

float AmpSim::pre(float x, int ch) { return bright_.process(inLp_.process(inHp_.process(x, ch), ch), ch); }

float AmpSim::nonlinear(float u, int ch) {
    const auto& d = *def_;
    float v = u;
    const float gp = (float)d.gridLimit, gn = (float)d.cutoffLimit;
    for (int s = 0; s < d.stages; ++s) {
        v = miller_[s].process(cath_[s].process(coup_[s].process(v, ch), ch), ch) * stageGain_[s];
        float& b = bias_[ch][s];
        const float ve = v - b;
        // asymmetric triode transfer: soft grid conduction (+), harder cutoff (-), inverting
        const float y = ve >= 0.0f ? gp * nl::algebraicSigmoid(ve / gp, 2.5f) : gn * nl::algebraicSigmoid(ve / gn, 4.0f);
        const float over = std::min(std::max(0.0f, ve / gp - 0.5f), 3.0f);
        b += (over * 0.35f > b ? biasCharge_ : biasRelease_) * (over * 0.35f - b);
        v = -y;
        if (s == d.toneStackAfter) v = tsHigh_.process(tsMid_.process(tsLow_.process(v, ch), ch), ch);
    }
    // power section: NFB approximation (presence/depth) -> push-pull with supply sag
    v = depth_.process(presence_.process(v * master_, ch), ch);
    float& e = sagEnv_[ch];
    const float A = powerA_ * (1.0f - sagDepth_ * std::min(e, 1.0f));
    const float y = A * nl::algebraicSigmoid(v / A, 2.2f);
    const float lvl = std::fabs(y) / powerA_;
    e += (lvl > e ? sagA_ : sagR_) * (lvl - e);
    return y * outNorm_;
}

float AmpSim::post(float v, int ch) {
    v = otLp_.process(otHp_.process(v, ch), ch);
    return spkHigh_.process(spkRes_.process(v, ch), ch);
}

} // namespace mfx
