#pragma once
#include "memory.h"

namespace limb_cleanup {
// Destroys severed limbs whose position or bounds are non-finite or absurd, through the game's own
// GameObject::destroy, so they cannot leave a stretched streak on screen. Limbs the player is holding with
// telekinesis are left alone. Returns how many were destroyed. Game thread only; call a few times a second.
int sweep(u32 world, u32 player);
}  // namespace limb_cleanup
