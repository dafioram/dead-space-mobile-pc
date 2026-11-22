#pragma once
#include <cmath>

#include "memory.h"

namespace limb_cleanup {

// Severed limbs sometimes end up with a NaN position or bounds (or a finite but absurd position such as
// 3e7 m) after a fight. The game never expires them (the dumps show them until the level is reloaded) and
// their skinned mesh then stretches across the screen as a long thin streak.
// Levels are a few hundred metres across, so anything beyond this is not a real position.
constexpr float kMaxCoordinate = 1000.0f;

inline bool sane(float v) { return std::isfinite(v) && std::fabs(v) <= kMaxCoordinate; }

// True when the limb's position or bounds cannot be real. Pure; no game memory is read.
inline bool broken(const float pos[3], const float lo[3], const float hi[3]) {
    for (int i = 0; i < 3; ++i)
        if (!sane(pos[i]) || !sane(lo[i]) || !sane(hi[i])) return true;
    return false;
}

// GameObject::destroy in the verified core, or 0 when the core's code is not the verified one. destroy sets the
// "destroyed" bit (bit 0 of the flag word at +0x48), hides the render node, releases the sound and removes
// the object from its world, and returns at once when that bit is already set, so a second call is harmless.
u32 destroy_function();
}  // namespace limb_cleanup
