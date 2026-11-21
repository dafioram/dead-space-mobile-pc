#pragma once
#include "eligibility.h"

namespace limb {
// Severed limbs count as "usable" (the game's touch UI can tap them), but this core's GameObjectLimb has no
// onOpen of its own: pressing E on one is consumed ("accepted" in the trace) and nothing happens. A limb must
// therefore never take E from an object that does something, e.g. ammo dropped next to the corpse.
//   blocked: E would have no effect on a limb (the verified situation).
//   unknown: the core's symbols are not loaded yet, or its GameObjectLimb does have its own onOpen;
//            original behaviour is kept.
// A limb is still selected when nothing else usable is in reach (see interact::reachable).
interact::Eligibility use_eligibility();
}  // namespace limb
