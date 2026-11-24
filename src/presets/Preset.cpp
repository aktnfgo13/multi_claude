#include "presets/Preset.h"

#include "engine/EffectFactory.h"

namespace mfx {

static Lane laneFromString(const std::string& s) {
    if (s == "a") return Lane::A;
    if (s == "b") return Lane::B;
    if (s == "post") return Lane::Post;
    return Lane::Pre;
}

Json presetToJson(const Engine& e, const std::string& name) {
    Json j = Json::object();
    j["format"] = "mfx-preset";
    j["version"] = 1;
    j["name"] = name;
    j["input_gain_db"] = e.inputGainDb();
    j["output_gain_db"] = e.outputGainDb();
    j["tempo_bpm"] = e.tempo();
    j["parallel"] = e.isParallel();
    j["parallel_mix"] = e.parallelMix();
    Json chain = Json::array();
    const auto order = e.allSlotsInOrder();
    for (int slot : order) {
        const Effect* fx = e.effect(slot);
        if (!fx) continue;
        Json b = Json::object();
        b["type"] = fx->typeId();
        b["lane"] = laneName(e.laneOf(slot));
        b["bypass"] = e.isBypassed(slot);
        Json p = Json::object();
        for (int i = 0; i < fx->params().size(); ++i) p[fx->params().info(i).id] = fx->params().get(i);
        b["params"] = p;
        const std::string st = fx->getExtraState();
        if (!st.empty()) b["state"] = st;
        chain.push(b);
    }
    j["chain"] = chain;
    Json midi = Json::array();
    for (const auto& m : e.midiMappings()) {
        Json mj = Json::object();
        mj["cc"] = m.cc; mj["channel"] = m.channel;
        int idx = 0;
        for (size_t k = 0; k < order.size(); ++k) if (order[k] == m.slot) idx = (int)k;
        mj["slot_index"] = idx;
        const Effect* fx = e.effect(m.slot);
        mj["param"] = m.param < 0 ? std::string("bypass") : (fx ? std::string(fx->params().info(m.param).id) : std::string());
        mj["min"] = m.min; mj["max"] = m.max;
        midi.push(mj);
    }
    j["midi"] = midi;
    return j;
}

PresetLoadReport presetFromJson(Engine& e, const Json& j) {
    PresetLoadReport rep;
    if (j.at("format").asString() != "mfx-preset") { rep.error = "not an mfx-preset"; return rep; }
    const int ver = (int)j.at("version").asNumber(0);
    if (ver < 1 || ver > 1) rep.warnings.push_back("unknown preset version " + std::to_string(ver));
    e.clear();
    e.setInputGainDb((float)j.at("input_gain_db").asNumber(0));
    e.setOutputGainDb((float)j.at("output_gain_db").asNumber(0));
    if (j.has("tempo_bpm")) e.setTempo(j.at("tempo_bpm").asNumber(120));
    e.setParallelMix((float)j.at("parallel_mix").asNumber(0.5));
    std::vector<int> slots;
    for (const auto& b : j.at("chain").elements()) {
        const std::string type = b.at("type").asString();
        auto fx = createEffect(type);
        if (!fx) { rep.warnings.push_back("unknown effect type '" + type + "' skipped"); slots.push_back(-1); continue; }
        for (const auto& [k, v] : b.at("params").items()) {
            const int idx = fx->params().indexOf(k.c_str());
            if (idx < 0) { rep.warnings.push_back(type + ": unknown parameter '" + k + "' ignored"); continue; }
            fx->params().set(idx, (float)v.asNumber());
        }
        const int slot = e.addEffect(std::move(fx), laneFromString(b.at("lane").asString()));
        if (slot < 0) { rep.warnings.push_back("chain full, '" + type + "' skipped"); slots.push_back(-1); continue; }
        if (b.has("state")) {
            std::string err;
            if (!e.effect(slot)->setExtraState(b.at("state").asString(), &err))
                rep.warnings.push_back(type + ": could not restore state '" + b.at("state").asString() + "': " + err);
        }
        e.setBypass(slot, b.at("bypass").asBool(false));
        slots.push_back(slot);
    }
    e.setParallel(j.at("parallel").asBool(false));
    std::vector<MidiMapping> maps;
    for (const auto& m : j.at("midi").elements()) {
        const int si = (int)m.at("slot_index").asNumber(-1);
        if (si < 0 || si >= (int)slots.size() || slots[(size_t)si] < 0) continue;
        MidiMapping mm;
        mm.cc = (int)m.at("cc").asNumber(0);
        mm.channel = (int)m.at("channel").asNumber(-1);
        mm.slot = slots[(size_t)si];
        const std::string pid = m.at("param").asString();
        mm.param = pid == "bypass" ? -1 : e.effect(mm.slot)->params().indexOf(pid.c_str());
        if (pid != "bypass" && mm.param < 0) { rep.warnings.push_back("midi: unknown param " + pid); continue; }
        mm.min = (float)m.at("min").asNumber(0);
        mm.max = (float)m.at("max").asNumber(1);
        maps.push_back(mm);
    }
    e.setMidiMappings(maps);
    rep.ok = true;
    return rep;
}

PresetLoadReport loadPresetFile(Engine& e, const std::string& path) {
    Json j;
    std::string err;
    if (!Json::load(path, j, &err)) { PresetLoadReport r; r.error = err; return r; }
    return presetFromJson(e, j);
}

bool savePresetFile(const Engine& e, const std::string& name, const std::string& path) {
    return presetToJson(e, name).save(path);
}

Json describeEffect(const Effect& fx) {
    Json d = Json::object();
    d["type"] = fx.typeId();
    d["name"] = fx.displayName();
    d["category"] = fx.category();
    d["latency_samples"] = fx.latencySamples();
    d["creates_stereo"] = fx.createsStereo();
    Json ps = Json::array();
    for (int i = 0; i < fx.params().size(); ++i) {
        const auto& in = fx.params().info(i);
        Json p = Json::object();
        p["id"] = in.id; p["name"] = in.name; p["min"] = in.min; p["max"] = in.max; p["default"] = in.def;
        p["unit"] = unitSuffix(in.unit);
        p["scale"] = in.scale == ParamScale::Log ? "log" : "linear";
        if (in.choices) { Json c = Json::array(); for (int k = 0; k < in.numChoices; ++k) c.push(in.choices[k]); p["choices"] = c; }
        if (in.unit == ParamUnit::Toggle) p["toggle"] = true;
        ps.push(p);
    }
    d["params"] = ps;
    return d;
}

} // namespace mfx
