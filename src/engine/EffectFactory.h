// Registry of available effect types (used by presets, UI library, offline renderer).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/Effect.h"

namespace mfx {

struct EffectTypeInfo {
    std::string typeId, displayName, category;
};

std::unique_ptr<Effect> createEffect(const std::string& typeId);
const std::vector<EffectTypeInfo>& availableEffects();

} // namespace mfx
