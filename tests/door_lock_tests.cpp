#include "door_lock.h"
#include "elf_loader.h"
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
using E = interact::Eligibility;
void require(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
std::unordered_map<std::string, u32> symbols;
constexpr u32 kTable = mem::ELF_BASE + 0x1100, kApply = mem::ELF_BASE + 0x121900, kUnlock = mem::ELF_BASE + 0x123ee8;
struct Candidate {
    bool usable, overlap;
    E eligibility;
};

// The reported problem: a locked door nearest the line of sight must not take E from the power node lock
// (or any other usable object) that can actually unlock it.
void selection() {
    std::vector<Candidate> c{{true, true, E::blocked}, {true, true, E::available}};
    require(interact::reachable(c) == &c[1], "locked door yields to the power node lock beside it");
    c.erase(c.begin() + 1);
    require(interact::reachable(c) == &c[0], "a locked door alone still gets E, so its feedback plays");
    c = {{true, true, E::available}, {true, true, E::blocked}};
    require(interact::reachable(c) == &c[0], "an unlocked door keeps its normal ranking");
    c = {{true, true, E::blocked}, {true, true, E::blocked}, {true, true, E::unknown}};
    require(interact::reachable(c) == &c[2], "unverified objects are preferred over locked doors");
    c = {{true, false, E::available}, {true, true, E::blocked}};
    require(interact::reachable(c) == &c[1], "an out-of-reach alternative is not chosen over the locked door");
}

void adapter() {
    require(mem::init(), "reserve synthetic memory; no guest/JIT execution");
    mem::commit(mem::ELF_BASE, 0x140000);
    // Signature-only synthetic words, not a copied game routine or executable fixture.
    mem::w32(kApply + 4, 0xe5d031ec);     // ldrb r3, [r0, #0x1ec]
    mem::w32(kUnlock + 0x1c, 0xe5c041ec);  // strb r4, [r0, #0x1ec]
    const u32 door = mem::heap_alloc(0x400);
    mem::w32(door, kTable + 8);
    mem::w32(door + 0x1e4, 2);

    // Symbols not resolved yet (library not loaded): unknown, and not remembered as absent.
    require(door::lock_eligibility(door) == E::unknown, "unresolved symbols keep original behaviour");
    symbols = {{"_ZTV14GameObjectDoor", kTable},
               {"_ZN14GameObjectDoor16applyLockedStateEv", kApply},
               {"_ZN14GameObjectDoor8onUnlockERKN5eastl12basic_stringIwN2im20StringEASTLAllocatorEEE", kUnlock}};

    mem::w32(door + 0x1ec, 0x00000001);
    require(door::lock_eligibility(door) == E::blocked, "lock flag set: locked");
    mem::w32(door + 0x1ec, 0x00000000);
    require(door::lock_eligibility(door) == E::available, "lock flag clear: unlocked");
    // Only the byte at +0x1ec is the flag (the game reads it with ldrb), not the whole word.
    mem::w32(door + 0x1ec, 0xa37a6500);
    require(door::lock_eligibility(door) == E::available, "neighbouring bytes are not the flag");
    mem::w32(door + 0x1ec, 0x00000100);
    require(door::lock_eligibility(door) == E::available, "byte +0x1ed is not the flag");
    mem::w32(door + 0x1ec, 0xa37a6530);
    require(door::lock_eligibility(door) == E::blocked, "any nonzero flag byte is locked");
    mem::w32(door + 0x1ec, 0x00000080);
    require(door::lock_eligibility(door) == E::blocked, "high bit of the flag byte counts");
    // The door state does not decide it: a locked door and an unlocked one both read state 2 in the game.
    mem::w32(door + 0x1ec, 1);
    for (u32 state : {0u, 2u, 6u, 8u}) {
        mem::w32(door + 0x1e4, state);
        require(door::lock_eligibility(door) == E::blocked, "state does not change a set lock flag");
    }
    mem::w32(door + 0x1ec, 0);
    require(door::lock_eligibility(door) == E::available, "state does not change a clear lock flag");
    mem::w32(door + 0x1e4, 2);

    // read-only
    mem::w32(door + 0x1ec, 1);
    const u32 before = mem::r32(door + 0x1ec) ^ mem::r32(door + 0x1e4);
    (void)door::lock_eligibility(door);
    require((mem::r32(door + 0x1ec) ^ mem::r32(door + 0x1e4)) == before && mem::r32(door + 0x1ec) == 1,
            "the query never writes to the door");

    // exact class only
    mem::w32(door, kTable + 0x10);
    require(door::lock_eligibility(door) == E::unknown, "other vtable (not exactly GameObjectDoor)");
    mem::w32(door, kTable + 8);

    // unverified layout: each verified word must match
    mem::w32(kApply + 4, 0xe5d031ed);
    require(door::lock_eligibility(door) == E::unknown, "applyLockedState does not read the expected byte");
    mem::w32(kApply + 4, 0xe5d031ec);
    mem::w32(kUnlock + 0x1c, 0xe5c041ed);
    require(door::lock_eligibility(door) == E::unknown, "onUnlock does not clear the expected byte");
    mem::w32(kUnlock + 0x1c, 0xe5c041ec);
    require(door::lock_eligibility(door) == E::blocked, "layout restored");

    // bad objects
    require(door::lock_eligibility(0) == E::unknown, "null object");
    require(door::lock_eligibility(0xfffffff0) == E::unknown, "range past 4 GiB");
    require(door::lock_eligibility(mem::ELF_BASE + 0x13fe20) == E::unknown, "flag byte on an unmapped page");
}
}  // namespace
namespace elf {
u32 find(const std::string& name) {
    auto it = symbols.find(name);
    return it == symbols.end() ? 0 : it->second;
}
}  // namespace elf
int main() {
    try {
        selection();
        adapter();
        std::cout << "Door lock selection and adapter tests passed; no game executed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
