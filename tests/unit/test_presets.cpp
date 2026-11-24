#include <filesystem>

#include "TestFramework.h"
#include "TestUtil.h"
#include "engine/EffectFactory.h"
#include "presets/Preset.h"

using namespace mfx;
using namespace mfxtest;

TEST(unit, json_parse_dump_roundtrip) {
    Json j;
    std::string err;
    CHECK(Json::parse(R"({"a": [1, 2.5, -3e-2, true, null, "x\"yé"], "b": {"c": {}}})", j, &err));
    CHECK(j.at("a").size() == 6);
    CHECK_NEAR(j.at("a")[2].asNumber(), -0.03, 1e-12);
    CHECK(j.at("a")[5].asString() == "x\"y\xC3\xA9");
    Json k;
    CHECK(Json::parse(j.dump(), k));
    CHECK(k.dump() == j.dump());
    CHECK(!Json::parse("{\"a\": }", k, &err));
    CHECK(!Json::parse("[1, 2", k, &err));
}

TEST(unit, preset_roundtrip_preserves_chain) {
    Engine e;
    e.prepare(48000.0, 256);
    auto d = createEffect("custom_drive");
    d->setParam("drive", 77.0f);
    d->setParam("oversampling", 3.0f);
    const int s0 = e.addEffect(std::move(d));
    e.addEffect(createEffect("chorus"), Lane::A);
    e.addEffect(createEffect("reverb"), Lane::B);
    e.addEffect(createEffect("limiter"), Lane::Post);
    e.setParallel(true);
    e.setParallelMix(0.3f);
    e.setBypass(s0, true);
    e.setInputGainDb(3.0f);
    e.setMidiMappings({{-1, 11, s0, 0, 0.0f, 1.0f}});
    const Json j = presetToJson(e, "test");

    Engine f;
    f.prepare(48000.0, 256);
    auto rep = presetFromJson(f, j);
    CHECK(rep.ok);
    CHECK(rep.warnings.empty());
    CHECK(f.isParallel());
    CHECK_NEAR(f.parallelMix(), 0.3, 1e-6);
    CHECK_NEAR(f.inputGainDb(), 3.0, 1e-6);
    const auto order = f.allSlotsInOrder();
    CHECK(order.size() == 4);
    CHECK(std::string(f.effect(order[0])->typeId()) == "custom_drive");
    CHECK_NEAR(f.effect(order[0])->getParam("drive"), 77.0, 1e-6);
    CHECK(f.isBypassed(order[0]));
    CHECK(f.laneOf(order[2]) == Lane::B);
    CHECK(f.midiMappings().size() == 1);
    CHECK(presetToJson(f, "test").dump() == j.dump());
}

TEST(unit, preset_unknown_entries_are_reported_not_fatal) {
    Json j;
    CHECK(Json::parse(R"({"format":"mfx-preset","version":1,"chain":[
        {"type":"does_not_exist"},
        {"type":"delay","params":{"time_ms":250,"bogus":1}}]})", j));
    Engine e; e.prepare(48000, 128);
    auto rep = presetFromJson(e, j);
    CHECK(rep.ok);
    CHECK(rep.warnings.size() == 2);
    CHECK_NEAR(e.effect(e.allSlotsInOrder()[0])->getParam("time_ms"), 250.0, 1e-4);
}

TEST(unit, factory_presets_load_without_warnings) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(MFX_SOURCE_DIR) / "presets" / "factory";
    int n = 0;
    for (const auto& ent : fs::directory_iterator(dir)) {
        if (ent.path().extension() != ".json") continue;
        Engine e; e.prepare(48000, 256);
        auto rep = loadPresetFile(e, ent.path().string());
        CHECK_MSG(rep.ok, ent.path().string() << ": " << rep.error);
        CHECK_MSG(rep.warnings.empty(), ent.path().string() << ": " << (rep.warnings.empty() ? "" : rep.warnings[0]));
        ++n;
    }
    CHECK(n >= 4);
}

TEST(unit, every_effect_describes_valid_parameters) {
    for (const auto& t : availableEffects()) {
        auto fx = createEffect(t.typeId);
        CHECK(fx != nullptr);
        CHECK(fx->params().size() > 0);
        for (int i = 0; i < fx->params().size(); ++i) {
            const auto& in = fx->params().info(i);
            CHECK_MSG(in.min < in.max && in.def >= in.min && in.def <= in.max, t.typeId << "." << in.id);
            CHECK(fx->params().indexOf(in.id) == i); // unique ids
            if (in.unit == ParamUnit::Choice) CHECK(in.choices && in.numChoices == (int)(in.max - in.min) + 1);
        }
    }
}
