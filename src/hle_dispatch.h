#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cpu::hle_dispatch {

// Function addresses only. The owner still serializes registration and protects
// mutable metadata (names, vector capacity, etc.) with its existing mutex.
// Slots are published once, never removed/replaced, and live for the process.
template<class Handler, size_t Capacity>
class Table {
    static_assert(std::atomic<Handler>::is_always_lock_free,
                  "Published HLE lookup requires lock-free function-pointer loads");
public:
    static constexpr size_t capacity = Capacity;
    static constexpr size_t storage_bytes = Capacity * sizeof(std::atomic<Handler>);

    // Writer-only: call under the registration mutex, AFTER metadata/stub writes.
    // Keeping the sequence check also prevents replacement of registered nulls.
    bool publish_next(uint32_t index, Handler handler) noexcept {
        if (index >= Capacity || index != next_) return false;
        slots_[index].store(handler, std::memory_order_release);
        ++next_;
        return true;
    }

    Handler find(uint32_t index) const noexcept {
        if (index >= Capacity) return nullptr;
        return slots_[index].load(std::memory_order_acquire);
    }
private:
    std::array<std::atomic<Handler>, Capacity> slots_{};
    uint32_t next_ = 0;  // writer-only; readers do not consult a mutable vector/count
};

template<class PublishedTable, class SlowLookup>
auto resolve(const PublishedTable& table, uint32_t index, bool optimized, SlowLookup&& slow_lookup) {
    if (optimized) {
        if (auto handler = table.find(index)) return handler;
    }
    // In-flight registration must not become a spurious bad-SVC failure. The
    // original locked lookup waits for the writer and decides missing/null IDs,
    // exactly as before. Reference mode always takes that path.
    return slow_lookup(index);
}

}  // namespace cpu::hle_dispatch
