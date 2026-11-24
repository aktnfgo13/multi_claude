// Lock-free hand-over of immutable objects from the message thread to the audio thread.
//   message thread : publish(), collect()
//   audio thread   : hasPending(), swapIn(), active()
// The audio thread never allocates or frees; retired objects are freed by collect() on the message thread.
#pragma once

#include <atomic>
#include <memory>

namespace mfx {

template <typename T>
class RtPublisher {
public:
    ~RtPublisher() {
        delete active_;
        delete pending_.load();
        delete staged_;
        for (auto& r : retired_) delete r.load();
    }
    // ---- message thread
    void publish(std::unique_ptr<T> obj) {
        collect();
        delete pending_.exchange(obj.release(), std::memory_order_acq_rel);
    }
    void collect() {
        for (auto& r : retired_) delete r.exchange(nullptr, std::memory_order_acq_rel);
    }
    /// Only when the audio thread is guaranteed not to run (prepare / offline setup).
    void setImmediate(std::unique_ptr<T> obj) {
        delete pending_.exchange(nullptr);
        delete staged_; staged_ = nullptr;
        delete active_;
        active_ = obj.release();
        collect();
    }
    // ---- audio thread
    bool hasPending() const { return staged_ != nullptr || pending_.load(std::memory_order_acquire) != nullptr; }
    /// Swap the pending object in. Returns true if the active object changed.
    bool swapIn() {
        T* p = staged_ ? staged_ : pending_.exchange(nullptr, std::memory_order_acq_rel);
        staged_ = nullptr;
        if (!p) return false;
        if (active_) {
            for (auto& r : retired_) {
                T* expected = nullptr;
                if (r.compare_exchange_strong(expected, active_, std::memory_order_acq_rel)) { active_ = p; return true; }
            }
            staged_ = p; // retire list full: try again next block (never free on the audio thread)
            return false;
        }
        active_ = p;
        return true;
    }
    T* active() const { return active_; }

private:
    T* active_ = nullptr;
    T* staged_ = nullptr;
    std::atomic<T*> pending_{nullptr};
    std::atomic<T*> retired_[4] = {};
};

} // namespace mfx
