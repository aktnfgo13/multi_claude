// Offline (non-real-time) band-limited resampling, Kaiser windowed sinc. Used for IR loading.
#pragma once

#include <vector>

namespace mfx {

std::vector<float> resampleOffline(const std::vector<float>& in, double srIn, double srOut, int halfTaps = 32);

} // namespace mfx
