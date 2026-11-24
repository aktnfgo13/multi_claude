// Minimal portable RIFF/WAVE reader/writer (PCM 16/24/32-bit int, 32/64-bit float; WAVE_FORMAT_EXTENSIBLE).
// Not for the audio thread.
#pragma once

#include <string>
#include <vector>

namespace mfx {

struct WavData {
    double sampleRate = 48000.0;
    std::vector<std::vector<float>> channels; // planar
    int numChannels() const { return (int)channels.size(); }
    size_t numFrames() const { return channels.empty() ? 0 : channels[0].size(); }
};

bool readWav(const std::string& path, WavData& out, std::string* error = nullptr);
/// bitsPerSample: 16, 24 (int PCM) or 32 (float).
bool writeWav(const std::string& path, const WavData& data, int bitsPerSample = 32, std::string* error = nullptr);

} // namespace mfx
