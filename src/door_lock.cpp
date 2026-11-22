#include "door_lock.h"
#include "elf_loader.h"

namespace door {
namespace {
struct Symbols {
    u32 table, on_open;
};
// Cached once every symbol was found (a lookup made before the guest library loaded is retried).
const Symbols& symbols() {
    static Symbols s{};
    if (!s.table || !s.on_open)
        s = {elf::find("_ZTV14GameObjectDoor"), elf::find("_ZN14GameObjectDoor6onOpenEv")};
    return s;
}

constexpr u32 kState = 0x1e4, kLockFlag = 0x1ec, kLockCount = 0x170, kHazardLock = 0x3c0;

// The code is re-read on every call (never cached), so a different core is not trusted.
bool supported(const Symbols& s) {
    if (!s.table || s.on_open != mem::ELF_BASE + 0x123c54) return false;
    for (const Word& w : kOnOpenWords)
        if (!mem::range_mapped(s.on_open + w.offset, 4) || mem::r32(s.on_open + w.offset) != w.word) return false;
    return true;
}
}  // namespace

interact::Outcome open_outcome(u32 object) {
    const Symbols& sym = symbols();
    if (!supported(sym) || !mem::range_mapped(object, kHazardLock + 1) || mem::r32(object) != sym.table + 8)
        return interact::Outcome::unverified;
    // Mirrors GameObjectDoor::onOpen.
    if (mem::r32(object + kState) - 1u > 1u) return interact::Outcome::declines;  // not closed: ignores E
    const bool locked = mem::r8(object + kLockFlag) != 0 || int32_t(mem::r32(object + kLockCount)) > 0 ||
                        mem::r8(object + kHazardLock) != 0;
    return locked ? interact::Outcome::declines : interact::Outcome::acts;
}
}  // namespace door
