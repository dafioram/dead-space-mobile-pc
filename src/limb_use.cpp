#include "limb_use.h"
#include "elf_loader.h"

namespace limb {
interact::Outcome use_outcome() {
    // An existing GameObjectLimb method proves the class' symbols are loaded, so that the absence of
    // onOpen means something; before that nothing is known and nothing is remembered.
    if (!elf::find("_ZN14GameObjectLimb8onUpdateEi")) return interact::Outcome::unverified;
    return elf::find("_ZN14GameObjectLimb6onOpenEv") ? interact::Outcome::unverified : interact::Outcome::declines;
}
}  // namespace limb
