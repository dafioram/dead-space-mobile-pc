#include "door_lock.h"
#include "elf_loader.h"

namespace door {
namespace {
struct Symbols {
    u32 table, apply, unlock;
};
// Cached once every symbol was found (a lookup made before the guest library loaded is retried).
const Symbols& symbols() {
    static Symbols s{};
    if (!s.table || !s.apply || !s.unlock)
        s = {elf::find("_ZTV14GameObjectDoor"), elf::find("_ZN14GameObjectDoor16applyLockedStateEv"),
             elf::find("_ZN14GameObjectDoor8onUnlockERKN5eastl12basic_stringIwN2im20StringEASTLAllocatorEEE")};
    return s;
}

constexpr u32 kLockFlag = 0x1ec;

// Verify the baseline layout from the game's own code every call (never cached, so a different core is not
// trusted): applyLockedState reads the byte at +0x1ec (ldrb r3, [r0, #0x1ec]) and onUnlock clears it
// (strb r4, [r0, #0x1ec]).
bool supported(const Symbols& s) {
    return s.table && s.apply == mem::ELF_BASE + 0x121900 && s.unlock == mem::ELF_BASE + 0x123ee8 &&
           mem::range_mapped(s.apply + 4, 4) && mem::r32(s.apply + 4) == 0xe5d031ec &&
           mem::range_mapped(s.unlock + 0x1c, 4) && mem::r32(s.unlock + 0x1c) == 0xe5c041ec;
}
}  // namespace

interact::Eligibility lock_eligibility(u32 object) {
    const Symbols& sym = symbols();
    if (!supported(sym) || !mem::range_mapped(object, kLockFlag + 1) || mem::r32(object) != sym.table + 8)
        return interact::Eligibility::unknown;
    return mem::r8(object + kLockFlag) ? interact::Eligibility::blocked : interact::Eligibility::available;
}
}  // namespace door
