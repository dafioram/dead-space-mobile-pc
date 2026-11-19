#include "ammo_pickup.h"
#include "cpu.h"
#include "elf_loader.h"

#include <cstring>

namespace pickup {
namespace {
bool mapped(u32 address, u32 size) {
    if (address < 0x10000 || !size || uint64_t(address) + size > (1ull << 32)) return false;
    for (uint64_t page = address & ~(mem::PAGE_SIZE - 1); page < uint64_t(address) + size; page += mem::PAGE_SIZE)
        if (!mem::is_mapped(u32(page))) return false;
    return true;
}
bool supported(u32 query, u32 table) {
    const u32 open = elf::find("_ZN16GameObjectPickup6onOpenEv");
    const u32 collect = elf::find("_ZN18GameObjectPlayable6pickupER4Itema");
    // Check the native caller's Item-at-0x1ec, inventory-at-0x530 and query ABI.
    // Generic imported cores must not inherit unverified object-layout assumptions.
    return query == mem::ELF_BASE + 0x9b160 && table &&
           open == mem::ELF_BASE + 0x13027c && collect == mem::ELF_BASE + 0x193524 &&
           mapped(open + 0x2c, 4) && mem::r32(open + 0x2c) == 0xe2841f7b &&
           mapped(collect + 0x168, 0x24) && mem::r32(collect + 0x168) == 0xe5950530 &&
           mem::r32(collect + 0x188) == 0xebfc1eab &&
           mapped(query, 0xe8) && mem::r32(query) == 0xe92d47f0 &&
           mem::r32(query + 0x20) == 0xe5953004 && mem::r32(query + 0xe4) == 0xe5951004;
}
}  // namespace

Eligibility ammo_eligibility(u32 object, u32 player) {
    const u32 query = elf::find("_ZNK9Inventory10canAddItemE4ItemPK18GameObjectPlayable");
    const u32 table = elf::find("_ZTV16GameObjectPickup");
    if (!supported(query, table) || !mapped(object, 0x1fc) || !mapped(player, 0x534) ||
        mem::r32(object) != table + 8 || mem::r32(object + 0x10) != mem::r32(player + 0x10))
        return Eligibility::unknown;
    const u32 item = object + 0x1ec, type = mem::r32(item + 4);
    // Matches native Item::isAmmo(): the four ordinary weapon-ammo types only.
    if (type < 7 || type > 10 || !mem::r32(item + 8)) return Eligibility::unknown;
    const u32 inventory = mem::r32(player + 0x530);
    if (!mapped(inventory, 0x1c)) return Eligibility::unknown;
    const u32 begin = mem::r32(inventory + 4), end = mem::r32(inventory + 8);
    if (end < begin || (end - begin) % 16 || end - begin > 100 * 16 ||
        (end != begin && !mapped(begin, end - begin))) return Eligibility::unknown;
    // Nontrivial by-value Item is passed indirectly in the guest ABI. Match the
    // game's caller using a reusable 16-byte copy, never the live pickup payload.
    static u32 copy = mem::data_alloc(16, 8);
    std::memcpy(mem::host(copy), mem::host(item), 16);
    return (cpu::call(query, {inventory, copy, player}) & 0xff) ? Eligibility::available : Eligibility::blocked;
}
}  // namespace pickup
