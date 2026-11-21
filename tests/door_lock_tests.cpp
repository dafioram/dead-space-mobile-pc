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
constexpr u32 kTable = mem::ELF_BASE + 0x1100, kOnOpen = mem::ELF_BASE + 0x123c54;
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

void write_signature() {
    for (const door::Word& w : door::kOnOpenWords) mem::w32(kOnOpen + w.offset, w.word);
}

void adapter() {
    require(mem::init(), "reserve synthetic memory; no guest/JIT execution");
    mem::commit(mem::ELF_BASE, 0x140000);
    // Signature-only synthetic words, not a copied game routine or executable fixture.
    write_signature();
    const u32 obj = mem::heap_alloc(0x400);
    auto clear_object = [&] {
        for (u32 o = 0; o < 0x400; o += 4) mem::w32(obj + o, 0);
        mem::w32(obj, kTable + 8);
        mem::w32(obj + 0x1e4, 2);  // closed (the state both locked and unlocked closed doors had in game)
    };
    clear_object();

    // Symbols not resolved yet (library not loaded): unknown, and not remembered as absent.
    require(door::open_eligibility(obj) == E::unknown, "unresolved symbols keep original behaviour");
    symbols = {{"_ZTV14GameObjectDoor", kTable}, {"_ZN14GameObjectDoor6onOpenEv", kOnOpen}};

    require(door::open_eligibility(obj) == E::available, "closed, unlocked door: E opens it");
    // state: onOpen only acts in states 1 and 2
    for (u32 state : {1u, 2u}) {
        mem::w32(obj + 0x1e4, state);
        require(door::open_eligibility(obj) == E::available, "states 1 and 2 are acted on");
    }
    for (u32 state : {0u, 3u, 4u, 5u, 6u, 7u, 8u, 0xffffffffu}) {
        mem::w32(obj + 0x1e4, state);
        require(door::open_eligibility(obj) == E::blocked, "any other state: E is ignored");
    }
    clear_object();

    // lock flag: the byte at +0x1ec (the game reads it with ldrb), not the whole word
    mem::w32(obj + 0x1ec, 0x00000001);
    require(door::open_eligibility(obj) == E::blocked, "lock flag set: locked");
    mem::w32(obj + 0x1ec, 0x00000080);
    require(door::open_eligibility(obj) == E::blocked, "high bit of the flag byte counts");
    mem::w32(obj + 0x1ec, 0xa37a6530);
    require(door::open_eligibility(obj) == E::blocked, "any nonzero flag byte is locked");
    mem::w32(obj + 0x1ec, 0xa37a6500);
    require(door::open_eligibility(obj) == E::available, "neighbouring bytes are not the flag");
    mem::w32(obj + 0x1ec, 0x00000100);
    require(door::open_eligibility(obj) == E::available, "byte +0x1ed is not the flag");
    mem::w32(obj + 0x1ec, 0);
    require(door::open_eligibility(obj) == E::available, "lock flag clear: unlocked");

    // lock count: a signed int, locked only above 0 (ble in the game)
    mem::w32(obj + 0x170, 1);
    require(door::open_eligibility(obj) == E::blocked, "lock count above zero: locked");
    mem::w32(obj + 0x170, 0x7fffffff);
    require(door::open_eligibility(obj) == E::blocked, "large positive lock count: locked");
    mem::w32(obj + 0x170, 0);
    require(door::open_eligibility(obj) == E::available, "lock count zero: not locked");
    mem::w32(obj + 0x170, 0xffffffff);
    require(door::open_eligibility(obj) == E::available, "negative lock count: not locked");
    mem::w32(obj + 0x170, 0xbf800000);
    require(door::open_eligibility(obj) == E::available, "negative lock count (float-looking value): not locked");
    mem::w32(obj + 0x170, 0);

    // third test: a byte at +0x3c0 (the game sign-extends it and tests for nonzero)
    mem::w32(obj + 0x3c0, 0x00000001);
    require(door::open_eligibility(obj) == E::blocked, "+0x3c0 set: locked");
    mem::w32(obj + 0x3c0, 0x00000080);
    require(door::open_eligibility(obj) == E::blocked, "+0x3c0 high bit set: locked");
    mem::w32(obj + 0x3c0, 0x00000100);
    require(door::open_eligibility(obj) == E::available, "byte +0x3c1 is not the flag");
    mem::w32(obj + 0x3c0, 0);

    // read-only
    clear_object();
    mem::w32(obj + 0x1ec, 1);
    mem::w32(obj + 0x3c0, 1);
    (void)door::open_eligibility(obj);
    require(mem::r32(obj + 0x1ec) == 1 && mem::r32(obj + 0x3c0) == 1 && mem::r32(obj + 0x1e4) == 2 &&
                mem::r32(obj + 0x170) == 0, "the query never writes to the door");
    clear_object();

    // exact class only
    mem::w32(obj, kTable + 0x10);
    require(door::open_eligibility(obj) == E::unknown, "other vtable (not exactly GameObjectDoor)");
    mem::w32(obj, kTable + 8);
    require(door::open_eligibility(obj) == E::available, "vtable restored");

    // unverified code: every verified word must match
    for (const door::Word& w : door::kOnOpenWords) {
        mem::w32(kOnOpen + w.offset, w.word ^ 1);
        require(door::open_eligibility(obj) == E::unknown, "a changed onOpen word is not trusted");
        mem::w32(kOnOpen + w.offset, w.word);
    }
    require(door::open_eligibility(obj) == E::available, "code restored");

    // bad objects
    require(door::open_eligibility(0) == E::unknown, "null object");
    require(door::open_eligibility(0xfffffff0) == E::unknown, "range past 4 GiB");
    // These objects carry the right vtable, so only the range check can reject them.
    mem::w32(mem::ELF_BASE + 0x13fe20, kTable + 8);
    require(door::open_eligibility(mem::ELF_BASE + 0x13fe20) == E::unknown, "object mostly beyond the mapping");
    // +0x1e4 and +0x1ec are mapped here but +0x3c0 is not: every field read must be covered by the range check
    mem::w32(mem::ELF_BASE + 0x13fd00, kTable + 8);
    mem::w32(mem::ELF_BASE + 0x13fd00 + 0x1e4, 2);
    require(door::open_eligibility(mem::ELF_BASE + 0x13fd00) == E::unknown, "partly mapped object");
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
        std::cout << "Door selection and onOpen-mirroring adapter tests passed; no game executed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
