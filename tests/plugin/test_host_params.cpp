// v0.8 host-parameter layout regression (K-048): runs the real plug-in Processor headless.
//  1. the v0.2-v0.7 layout (3 globals + per slot: bypass + 16 params) keeps every index / (slot, param) meaning
//  2. the extension (params 16..23 per slot = up to kMaxParams) is appended after it with version hint 2
//  3. a v0.7 session (legacy unbound keys) restores into the same host parameters
//  4. extension parameters automate the effect and survive a state round trip
#include <cstdio>
#include <juce_audio_processors/juce_audio_processors.h>
#include "plugin/PluginProcessor.h"

using mfx::plugin::Processor;
using mfx::plugin::SlotParameter;

static int failures = 0;
#define EXPECT(c, msg) do { if (!(c)) { std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } } while (0)

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    auto proc = std::make_unique<Processor>();
    auto& params = proc->getParameters();
    constexpr int kSlots = mfx::Engine::kMaxSlots, kLegacy = 3 + kSlots * 17;
    EXPECT(params.size() == kLegacy + kSlots * 8, "parameter count");
    std::printf("host parameters: %d (legacy block %d, extension %d)\n", params.size(), kLegacy, params.size() - kLegacy);
    // 1. legacy layout meaning
    for (int s = 0; s < kSlots; ++s) {
        auto* b = dynamic_cast<SlotParameter*>(params[3 + s * 17]);
        EXPECT(b && b->slot() == s && b->index() == -1, "bypass index");
        for (int p = 0; p < 16; ++p) {
            auto* sp = dynamic_cast<SlotParameter*>(params[3 + s * 17 + 1 + p]);
            EXPECT(sp && sp->slot() == s && sp->index() == p && sp->getVersionHint() == 0, "legacy slot param index");
        }
    }
    // 2. extension
    for (int s = 0; s < kSlots; ++s)
        for (int p = 16; p < 24; ++p) {
            auto* sp = dynamic_cast<SlotParameter*>(params[kLegacy + s * 8 + (p - 16)]);
            EXPECT(sp && sp->slot() == s && sp->index() == p && sp->getVersionHint() == 2, "extension index");
        }
    // 3. v0.7-style session: slot 1 parametric EQ, unbound legacy key for slot 2 param 3 ("35")
    {
        const char* v07 = R"({"format":"mfx-preset","version":1,"name":"v07","chain":[
            {"type":"utility","params":{}},{"type":"eq_parametric_premium","params":{"b1_db":6,"b2_type":5}}],
            "host_params_unbound":{"35":0.25,"b5":1.0}})";
        proc->setStateInformation(v07, (int)std::strlen(v07));
        auto* sp = dynamic_cast<SlotParameter*>(params[3 + 2 * 17 + 1 + 3]);
        EXPECT(sp && std::fabs(sp->getValue() - 0.25f) < 1e-6f, "legacy unbound key restored to the same host parameter");
        auto* eqGain = dynamic_cast<SlotParameter*>(params[3 + 1 * 17 + 1 + 1]); // slot 1, param 1 = b1_db
        auto* fx = proc->engine().effect(1);
        EXPECT(fx && eqGain && std::fabs(eqGain->getValue() - fx->params().getNormalized(1)) < 1e-6f, "legacy bound param mirrors effect");
        std::printf("legacy session: slot1=%s b1_db=%.2f b2_type=%d\n", fx ? fx->typeId() : "-", fx ? fx->params().get(1) : 0.f,
                    fx ? fx->params().getInt(16 + 1) : -1);
        EXPECT(fx && fx->params().getInt(16 + 1) == 5, "type param restored from preset json");
    }
    // 4. automate an extension parameter (EQ band 1 type, index 16) through the host interface
    {
        proc->prepareToPlay(48000.0, 256);
        auto* typeParam = dynamic_cast<SlotParameter*>(params[kLegacy + 1 * 8 + 0]); // slot 1, param 16
        EXPECT(typeParam != nullptr, "extension param exists");
        EXPECT(typeParam->getName(64).containsIgnoreCase("Band 1 Type"), "extension param named after the effect parameter");
        std::printf("slot1 p16 name: %s\n", typeParam->getName(64).toRawUTF8());
        typeParam->setValueNotifyingHost(typeParam->getValueForText("HPF 24"));
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        buf.clear();
        proc->processBlock(buf, midi);
        auto* fx = proc->engine().effect(1);
        EXPECT(fx && fx->params().getInt(16) == 5, "extension automation reaches the effect");
        // state round trip keeps the value; extension keys for unbound params use the new key format
        juce::MemoryBlock mb;
        proc->getStateInformation(mb);
        const juce::String json = mb.toString();
        EXPECT(json.contains("\"x0_16\""), "unbound extension key written");
        EXPECT(json.contains("\"35\""), "unbound legacy key still written in legacy format");
        auto p2 = std::make_unique<Processor>();
        p2->setStateInformation(mb.getData(), (int)mb.getSize());
        auto* fx2 = p2->engine().effect(1);
        EXPECT(fx2 && fx2->params().getInt(16) == 5, "extension value restored");
        auto* t2 = dynamic_cast<SlotParameter*>(p2->getParameters()[kLegacy + 8]);
        EXPECT(t2 && std::fabs(t2->getValue() - typeParam->getValue()) < 1e-6f, "extension host value restored");
        proc->releaseResources();
    }
    std::printf(failures == 0 ? "host parameter layout: PASS\n" : "host parameter layout: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
