#pragma once
#include "eligibility.h"
#include "memory.h"

namespace door {
// Read-only: would the game treat this GameObjectDoor as locked? Game thread only.
//   blocked:   the door's lock flag (+0x1ec) is set. GameObjectDoor::onLock/onUnlock own this byte and
//              applyLockedState reads it, so script locks and power-node locks both set it.
//   available: verified door whose lock flag is clear.
//   unknown:   not exactly a GameObjectDoor, or the core's layout is not the verified one.
// Doors locked by other means (applyLockedState also looks at +0x170 and +0x3c0) are not treated as
// blocked until their meaning is confirmed; that only keeps the original behaviour for them.
interact::Eligibility lock_eligibility(u32 object);
}  // namespace door
