// Preset format (JSON, versioned):
// {
//   "format": "mfx-preset", "version": 1, "name": "...",
//   "input_gain_db": 0, "output_gain_db": 0, "tempo_bpm": 120,
//   "parallel": false, "parallel_mix": 0.5,
//   "chain": [ {"type": "custom_drive", "lane": "pre", "bypass": false,
//               "params": {"drive": 40, ...}, "state": "optional IR / model path"}, ... ],
//   "midi": [ {"cc": 11, "channel": -1, "slot_index": 0, "param": "drive", "min": 0, "max": 1} ]
// }
// Unknown parameter ids are ignored (forward compatible); missing ones keep defaults.
#pragma once

#include <string>
#include <vector>

#include "engine/Engine.h"
#include "presets/Json.h"

namespace mfx {

struct PresetLoadReport {
    bool ok = false;
    std::vector<std::string> warnings;
    std::string error;
};

Json presetToJson(const Engine& engine, const std::string& name);
/// Where external files referenced by a preset (cabinet IRs, ...) are looked for when their saved
/// absolute path no longer exists. loadPresetFile() adds the preset's own folder first.
struct PresetLoadContext {
    std::vector<std::string> searchDirs;
};
PresetLoadReport presetFromJson(Engine& engine, const Json& j, const PresetLoadContext& ctx = {});
PresetLoadReport loadPresetFile(Engine& engine, const std::string& path, const PresetLoadContext& ctx = {});
bool savePresetFile(const Engine& engine, const std::string& name, const std::string& path);

/// Describe all parameters of an effect type as JSON (used by docs, UI and the renderer).
Json describeEffect(const Effect& fx);

} // namespace mfx
