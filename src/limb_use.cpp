#include "limb_use.h"
#include "elf_loader.h"

namespace limb {
interact::Eligibility use_eligibility() {
    // An existing GameObjectLimb method proves the class' symbols are loaded, so that the absence of
    // onOpen means something; before that nothing is known and nothing is remembered.
    if (!elf::find("_ZN14GameObjectLimb8onUpdateEi")) return interact::Eligibility::unknown;
    return elf::find("_ZN14GameObjectLimb6onOpenEv") ? interact::Eligibility::unknown : interact::Eligibility::blocked;
}
}  // namespace limb
