#pragma once
#include "outcome.h"
#include "memory.h"

namespace pickup {
// Read-only inventory query for the verified baseline ammo layout; game thread only.
// Unknown/non-ammo/unsupported objects retain their original interaction behavior.
interact::Outcome ammo_outcome(u32 object, u32 player);
}  // namespace pickup
