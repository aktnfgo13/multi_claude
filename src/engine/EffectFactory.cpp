#include "engine/EffectFactory.h"

#include <functional>

#include "dsp/amp/AmpSim.h"
#include "dsp/amp/AB763Amp.h"
#include "dsp/reverb/PremiumPlate.h"
#include "dsp/delay/TapeEcho.h"
#include "dsp/delay/BbdDelay.h"
#include "dsp/dynamics/FetCompressor.h"
#include "dsp/cabinet/Cabinet.h"
#include "dsp/delay/Delay.h"
#include "dsp/drive/CustomDrive.h"
#include "dsp/drive/Drives.h"
#include "dsp/dynamics/Dynamics.h"
#include "dsp/filters/EQ.h"
#include "dsp/gain/Utility.h"
#include "dsp/modulation/Modulation.h"
#include "dsp/reverb/Reverb.h"
#include "dsp/utility/Tuner.h"
#include "neural/NamAmp.h"

namespace mfx {

namespace {
struct Entry {
    const char* id;
    std::function<std::unique_ptr<Effect>()> make;
};
template <typename T> std::unique_ptr<Effect> mk() { return std::make_unique<T>(); }

const std::vector<Entry>& entries() {
    static const std::vector<Entry> e = {
        {"utility", mk<GainUtility>}, {"pan", mk<Pan>}, {"tuner", mk<Tuner>},
        {"noise_gate", mk<NoiseGate>}, {"compressor", mk<Compressor>}, {"limiter", mk<Limiter>},
        {"comp_fet", mk<FetCompressor>},
        {"clean_boost", mk<CleanBoost>}, {"overdrive", mk<Overdrive>}, {"distortion", mk<Distortion>},
        {"fuzz", mk<Fuzz>}, {"custom_drive", mk<CustomDrive>}, {"ddsp_drive", mk<DdspDrive>},
        {"filter", mk<FilterBlock>}, {"parametric_eq", mk<ParametricEQ>}, {"graphic_eq", mk<GraphicEQ>},
        {"amp", mk<AmpSim>}, {"amp_ab763", mk<AB763Amp>}, {"nam_amp", mk<NamAmp>}, {"cabinet", mk<Cabinet>},
        {"chorus", mk<Chorus>}, {"flanger", mk<Flanger>}, {"phaser", mk<Phaser>},
        {"tremolo", mk<Tremolo>}, {"vibrato", mk<Vibrato>},
        {"delay", mk<Delay>}, {"reverb", mk<Reverb>}, {"reverb_plate_premium", mk<PremiumPlate>},
        {"delay_tape_premium", mk<TapeEcho>}, {"delay_bbd_premium", mk<BbdDelay>},
    };
    return e;
}
} // namespace

std::unique_ptr<Effect> createEffect(const std::string& typeId) {
    for (const auto& e : entries())
        if (typeId == e.id) return e.make();
    return nullptr;
}

const std::vector<EffectTypeInfo>& availableEffects() {
    static const std::vector<EffectTypeInfo> list = [] {
        std::vector<EffectTypeInfo> l;
        for (const auto& e : entries()) {
            auto fx = e.make();
            l.push_back({fx->typeId(), fx->displayName(), fx->category()});
        }
        return l;
    }();
    return list;
}

} // namespace mfx
