#pragma once
#include "memory.h"

namespace pickup {
enum class Eligibility { unknown, available, blocked };
// Read-only inventory query for the verified baseline ammo layout; game thread only.
// Unknown/non-ammo/unsupported objects retain their original interaction behavior.
Eligibility ammo_eligibility(u32 object, u32 player);

// Candidates already have the existing geometric order. Retain the original
// blocked target only when no alternative exists, including its refusal feedback.
template <class Candidates> auto reachable(const Candidates& candidates) -> const typename Candidates::value_type* {
    const typename Candidates::value_type* blocked = nullptr;
    for (const auto& candidate : candidates) {
        if (!candidate.usable || !candidate.overlap) continue;
        if (candidate.ammo != Eligibility::blocked) return &candidate;
        if (!blocked) blocked = &candidate;
    }
    return blocked;
}
}  // namespace pickup
