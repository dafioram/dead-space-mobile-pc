#include "ammo_pickup.h"
#include "cpu.h"
#include "elf_loader.h"

#include <cstring>

namespace pickup {
namespace {
struct Symbols {
    u32 query, table, open, collect;
};
// Looked up by name, so cached: this runs for every overlapping pickup whenever candidates are
// gathered (including the per-frame F10 overlay). The cache is only kept once every symbol was
// found, so a lookup made before the guest library was loaded is retried instead of remembered
// as absent. Game thread only, like the query itself.
const Symbols& symbols() {
    static Symbols s{};
    if (!s.query || !s.table || !s.open || !s.collect)
        s = {elf::find("_ZNK9Inventory10canAddItemE4ItemPK18GameObjectPlayable"),
             elf::find("_ZTV16GameObjectPickup"),
             elf::find("_ZN16GameObjectPickup6onOpenEv"),
             elf::find("_ZN18GameObjectPlayable6pickupER4Itema")};
    return s;
}

bool supported(const Symbols& s) {
    const u32 query = s.query, table = s.table, open = s.open, collect = s.collect;
    // Check the native caller's Item-at-0x1ec, inventory-at-0x530 and query ABI.
    // Generic imported cores must not inherit unverified object-layout assumptions.
    // The words are read every call (not cached) so a changed core is never trusted.
    return query == mem::ELF_BASE + 0x9b160 && table &&
           open == mem::ELF_BASE + 0x13027c && collect == mem::ELF_BASE + 0x193524 &&
           mem::range_mapped(open + 0x2c, 4) && mem::r32(open + 0x2c) == 0xe2841f7b &&
           mem::range_mapped(collect + 0x168, 0x24) && mem::r32(collect + 0x168) == 0xe5950530 &&
           mem::r32(collect + 0x188) == 0xebfc1eab &&
           mem::range_mapped(query, 0xe8) && mem::r32(query) == 0xe92d47f0 &&
           mem::r32(query + 0x20) == 0xe5953004 && mem::r32(query + 0xe4) == 0xe5951004;
}
}  // namespace

interact::Eligibility ammo_eligibility(u32 object, u32 player) {
    const Symbols& sym = symbols();
    if (!supported(sym) || !mem::range_mapped(object, 0x1fc) || !mem::range_mapped(player, 0x534) ||
        mem::r32(object) != sym.table + 8 || mem::r32(object + 0x10) != mem::r32(player + 0x10))
        return interact::Eligibility::unknown;
    const u32 item = object + 0x1ec, type = mem::r32(item + 4);
    // Matches native Item::isAmmo(): the four ordinary weapon-ammo types only.
    if (type < 7 || type > 10 || !mem::r32(item + 8)) return interact::Eligibility::unknown;
    const u32 inventory = mem::r32(player + 0x530);
    if (!mem::range_mapped(inventory, 0x1c)) return interact::Eligibility::unknown;
    const u32 begin = mem::r32(inventory + 4), end = mem::r32(inventory + 8);
    if (end < begin || (end - begin) % 16 || end - begin > 100 * 16 ||
        (end != begin && !mem::range_mapped(begin, end - begin))) return interact::Eligibility::unknown;
    // Nontrivial by-value Item is passed indirectly in the guest ABI. Match the
    // game's caller using a reusable 16-byte copy, never the live pickup payload.
    static u32 copy = mem::data_alloc(16, 8);
    std::memcpy(mem::host(copy), mem::host(item), 16);
    return (cpu::call(sym.query, {inventory, copy, player}) & 0xff) ? interact::Eligibility::available : interact::Eligibility::blocked;
}
}  // namespace pickup
