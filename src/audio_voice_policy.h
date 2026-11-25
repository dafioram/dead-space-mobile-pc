// Pure SFX slot tracking, on-demand reclaim, and oldest-voice stealing policy
// for im::SoundManager's 6 native SFX slots.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace audio_voices {

constexpr std::size_t kNativeSfxSlots = 6;

struct SlotSnapshot {
    std::uint32_t sound_object = 0;
    std::uint32_t voice = 0;
    std::uint64_t born_seq = 0;
    bool playing = false;
};

enum class ReclaimKind {
    none,              // at least one slot is already empty
    reclaim_finished,  // all slots occupied, but one or more voices have finished playing
    steal_oldest,      // all slots actively playing; stop & release the oldest slot
};

struct ReclaimPlan {
    ReclaimKind kind = ReclaimKind::none;
    std::array<bool, kNativeSfxSlots> finish_mask{};
    int steal_slot = -1;
};

// Tracks monotonic birth order of active entries in SoundManager's 6 native SFX slots
// so voice stealing always evicts the oldest active sound rather than slot 0 repeatedly.
class SlotTracker {
public:
    void reset(std::uint32_t sm = 0) {
        sm_ = sm;
        seq_ = 0;
        slots_ = {};
    }

    // Synchronizes tracker state against live (sound_object, voice) pairs from SoundManager.
    // Returns true if at least one slot is currently empty (sound_object == 0).
    bool sync(std::uint32_t sm,
              const std::array<std::uint32_t, kNativeSfxSlots>& objects,
              const std::array<std::uint32_t, kNativeSfxSlots>& voices) {
        if (sm != sm_) reset(sm);
        bool has_empty = false;
        for (std::size_t i = 0; i < kNativeSfxSlots; ++i) {
            if (objects[i] == 0) {
                slots_[i] = {};
                has_empty = true;
            } else if (objects[i] != slots_[i].sound_object || voices[i] != slots_[i].voice) {
                slots_[i] = {objects[i], voices[i], ++seq_, true};
            }
        }
        return has_empty;
    }

    void clear_slot(std::size_t slot) {
        if (slot < kNativeSfxSlots) slots_[slot] = {};
    }

    std::uint64_t born_seq(std::size_t slot) const {
        return slot < kNativeSfxSlots ? slots_[slot].born_seq : 0;
    }

private:
    std::uint32_t sm_ = 0;
    std::uint64_t seq_ = 0;
    std::array<SlotSnapshot, kNativeSfxSlots> slots_{};
};

// Decides how to free an SFX slot when im::SoundManager::playSound() is about to scan slots 0..5.
// 1. If any slot is already empty -> no action needed.
// 2. Otherwise, reclaim every slot whose voice is null or no longer playing.
// 3. Otherwise (all 6 slots actively playing), steal the slot with the smallest born_seq.
inline ReclaimPlan plan_sfx_slot(const std::array<SlotSnapshot, kNativeSfxSlots>& slots) {
    for (std::size_t i = 0; i < kNativeSfxSlots; ++i) {
        if (slots[i].sound_object == 0) return {ReclaimKind::none, {}, -1};
    }

    ReclaimPlan plan{ReclaimKind::reclaim_finished, {}, -1};
    bool any_finished = false;
    for (std::size_t i = 0; i < kNativeSfxSlots; ++i) {
        if (slots[i].voice == 0 || !slots[i].playing) {
            plan.finish_mask[i] = true;
            any_finished = true;
        }
    }
    if (any_finished) return plan;

    int oldest = 0;
    for (std::size_t i = 1; i < kNativeSfxSlots; ++i) {
        if (slots[i].born_seq < slots[std::size_t(oldest)].born_seq) oldest = int(i);
    }
    return {ReclaimKind::steal_oldest, {}, oldest};
}

}  // namespace audio_voices
