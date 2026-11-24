#include "platform/WavFile.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace mfx {

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool readWav(const std::string& path, WavData& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    std::ifstream f(path, std::ios::binary);
    if (!f) return fail("cannot open file");
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
        return fail("not a RIFF/WAVE file");
    size_t pos = 12;
    int fmtTag = 0, nch = 0, bits = 0;
    uint32_t sr = 0;
    const uint8_t* pcm = nullptr;
    size_t pcmBytes = 0;
    while (pos + 8 <= d.size()) {
        const uint8_t* ck = d.data() + pos;
        const uint32_t sz = rd32(ck + 4);
        const size_t body = pos + 8;
        if (body + sz > d.size() && std::memcmp(ck, "data", 4) != 0) break;
        if (std::memcmp(ck, "fmt ", 4) == 0 && sz >= 16) {
            fmtTag = rd16(d.data() + body);
            nch = rd16(d.data() + body + 2);
            sr = rd32(d.data() + body + 4);
            bits = rd16(d.data() + body + 14);
            if (fmtTag == 0xFFFE && sz >= 26) fmtTag = rd16(d.data() + body + 24); // extensible subformat
        } else if (std::memcmp(ck, "data", 4) == 0) {
            pcm = d.data() + body;
            pcmBytes = std::min<size_t>(sz, d.size() - body);
        }
        pos = body + sz + (sz & 1);
    }
    if (!pcm || nch <= 0 || sr == 0) return fail("missing fmt or data chunk");
    const int bps = bits / 8;
    if (!((fmtTag == 1 && (bits == 16 || bits == 24 || bits == 32)) || (fmtTag == 3 && (bits == 32 || bits == 64))))
        return fail("unsupported sample format");
    const size_t frames = pcmBytes / (size_t)(bps * nch);
    out.sampleRate = sr;
    out.channels.assign((size_t)nch, std::vector<float>(frames));
    for (size_t i = 0; i < frames; ++i) {
        for (int c = 0; c < nch; ++c) {
            const uint8_t* s = pcm + (i * nch + c) * bps;
            float v = 0.0f;
            if (fmtTag == 1) {
                if (bits == 16) v = (int16_t)rd16(s) / 32768.0f;
                else if (bits == 24) {
                    int32_t x = (s[0] << 8) | (s[1] << 16) | (s[2] << 24);
                    v = (float)(x >> 8) / 8388608.0f;
                } else v = (float)((int32_t)rd32(s) / 2147483648.0);
            } else if (bits == 32) { uint32_t u = rd32(s); std::memcpy(&v, &u, 4); }
            else { double dv; std::memcpy(&dv, s, 8); v = (float)dv; }
            out.channels[(size_t)c][i] = v;
        }
    }
    return true;
}

static void wr32(std::ofstream& f, uint32_t v) { uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}; f.write((char*)b, 4); }
static void wr16(std::ofstream& f, uint16_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; f.write((char*)b, 2); }

bool writeWav(const std::string& path, const WavData& data, int bits, std::string* error) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot create file"; return false; }
    const int nch = data.numChannels();
    const size_t frames = data.numFrames();
    const int bps = bits / 8;
    const uint32_t dataBytes = (uint32_t)(frames * nch * bps);
    const uint16_t tag = bits == 32 ? 3 : 1;
    f.write("RIFF", 4); wr32(f, 36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); wr32(f, 16); wr16(f, tag); wr16(f, (uint16_t)nch); wr32(f, (uint32_t)data.sampleRate);
    wr32(f, (uint32_t)(data.sampleRate * nch * bps)); wr16(f, (uint16_t)(nch * bps)); wr16(f, (uint16_t)bits);
    f.write("data", 4); wr32(f, dataBytes);
    std::vector<uint8_t> buf(frames * nch * bps);
    uint8_t* o = buf.data();
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < nch; ++c) {
            float v = data.channels[(size_t)c][i];
            if (bits == 32) { std::memcpy(o, &v, 4); o += 4; }
            else {
                v = std::max(-1.0f, std::min(1.0f, v));
                if (bits == 16) { int16_t s = (int16_t)std::lround(v * 32767.0f); o[0] = (uint8_t)s; o[1] = (uint8_t)(s >> 8); o += 2; }
                else { int32_t s = (int32_t)std::lround(v * 8388607.0f); o[0] = (uint8_t)s; o[1] = (uint8_t)(s >> 8); o[2] = (uint8_t)(s >> 16); o += 3; }
            }
        }
    f.write((char*)buf.data(), (std::streamsize)buf.size());
    return (bool)f;
}

} // namespace mfx
