// Real-time analysis tap: lock-free single-producer / single-consumer queue of fixed-size packets that
// carry the chain input (after input gain / input mode) and the chain output (after output gain) of the
// same frames. Producer = audio thread (Engine::process), consumer = one analysis/UI thread.
//
//  * All memory is allocated in the constructor (fixed capacity, independent of sample rate / block size).
//    Audio thread: no allocation, no locks, no I/O. If the queue is full the packet is dropped (counted);
//    the audio path is never affected.
//  * The tap only reads engine buffers: enabling/disabling it does not change the DSP output (tested).
//  * Sample rate / channel counts / frame index / chain latency travel with every packet, so a consumer
//    can align input and output (output frame t corresponds to input frame t - latency).
//  * prepare() only bumps the generation; packets of an older generation are ignored by consumers.
//
// Size: kFrames = 256 (= Engine::kChunk, one packet per internal chunk) x kCapacity = 64 packets
// = 16384 frames (341 ms @48 kHz, 85 ms @192 kHz) of headroom for a consumer polling at >= 30 Hz.
// Memory: 64 x 4 x 256 x 4 B = 256 KiB (+ header). A hardware build can shrink kCapacity.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "core/Common.h"

namespace mfx {

class AnalysisTap {
public:
    static constexpr int kFrames = 256;
    static constexpr uint32_t kCapacity = 64; // power of two

    struct Packet {
        uint64_t frameIndex = 0;   // index of the first frame since prepare()
        uint32_t generation = 0;
        int numFrames = 0;
        int inChannels = 1;        // 1 = mono chain input, 2 = stereo
        int outChannels = 2;       // 1 = mono chain output (L == R), 2 = stereo
        int latency = 0;           // chain latency (samples) at the time of the packet
        double sampleRate = 48000.0;
        float in[kMaxChannels][kFrames];
        float out[kMaxChannels][kFrames];
    };

    AnalysisTap() : slots_(new Packet[kCapacity]) {
        static_assert((kCapacity & (kCapacity - 1)) == 0, "capacity must be a power of two");
        static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
    }

    // ---- control (any thread)
    void setEnabled(bool on) noexcept { enabled_.store(on, std::memory_order_release); }
    bool isEnabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
    uint32_t droppedPackets() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    uint32_t generation() const noexcept { return generation_.load(std::memory_order_acquire); }

    // ---- engine (non-real-time, audio stopped)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        frame_ = 0;
        generation_.fetch_add(1, std::memory_order_acq_rel);
    }

    // ---- producer (audio thread)
    /// Frame counter advance for chunks that are not captured (tap disabled or queue full).
    void advance(int n) noexcept { frame_ += (uint64_t)n; }
    /// Reserve the next packet, or nullptr when disabled / full (packet dropped, frames still counted).
    Packet* beginWrite(int n) noexcept {
        if (!isEnabled()) return nullptr;
        const uint32_t w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) >= kCapacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        Packet* p = &slots_[w & (kCapacity - 1)];
        p->frameIndex = frame_;
        p->generation = generation_.load(std::memory_order_relaxed);
        p->numFrames = n < kFrames ? n : kFrames;
        p->sampleRate = sampleRate_;
        return p;
    }
    /// Publish the packet returned by beginWrite().
    void commit() noexcept { write_.store(write_.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    // ---- consumer (one analysis thread)
    const Packet* peek() const noexcept {
        const uint32_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return nullptr;
        return &slots_[r & (kCapacity - 1)];
    }
    void pop() noexcept { read_.store(read_.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

private:
    std::unique_ptr<Packet[]> slots_;
    alignas(64) std::atomic<uint32_t> write_{0};
    alignas(64) std::atomic<uint32_t> read_{0};
    std::atomic<uint32_t> dropped_{0}, generation_{0};
    std::atomic<bool> enabled_{false};
    double sampleRate_ = 48000.0; // producer side copy (written in prepare while audio is stopped)
    uint64_t frame_ = 0;          // producer only
};

} // namespace mfx
