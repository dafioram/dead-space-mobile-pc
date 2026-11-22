#include "rig_credits.h"

#include <atomic>
#include <limits>
#include <string>

#include "cpu.h"
#include "elf_loader.h"
#include "guest_util.h"
#include "log.h"

namespace rig_credits {
namespace {

// Guest ABI boundary, from Hud::onUpdate: world +4, RIG layout +0x1354;
// GameWorld's player +0xd4, playable inventory +0x530, wallet credits +0x18.
// These are reads only, and are enabled only for the verified instruction layout.
constexpr u32 kHudWorld = 4, kHudRigLayout = 0x1354, kWorldPlayer = 0xd4,
              kPlayerInventory = 0x530, kInventoryCredits = 0x18;
u32 fn_in_rig = 0, fn_set_text = 0, fn_assign = 0;
u32 g_name = 0, g_text = 0, g_string = 0;
std::atomic<bool> g_layout_added{false};

bool word(u32 object, u32 offset, u32& value) {
    if (!object || object > std::numeric_limits<u32>::max() - offset) return false;
    const u32 address = object + offset;
    if (!mem::range_mapped(address, 4)) return false;
    value = mem::r32(address);
    return true;
}

void on_hud_update(cpu::Ctx& c) {
    if (!g_layout_added || !(cpu::call(fn_in_rig, {c.reg(0)}) & 0xff)) return;
    u32 layout, world, player, inventory, credits;
    // Layout is a non-polymorphic native object (0x2c bytes). Obtain it anew
    // from this live Hud; never retain a layout/entity pointer across visits.
    if (!word(c.reg(0), kHudRigLayout, layout) || !mem::range_mapped(layout, 0x2c) ||
        !word(c.reg(0), kHudWorld, world) || !word(world, kWorldPlayer, player) ||
        !word(player, kPlayerInventory, inventory) || !word(inventory, kInventoryCredits, credits)) return;
    if (!g_string) {
        // One persistent native string/buffer, not one allocation per frame or
        // menu visit. Native assign/setMutableText manage their own storage.
        g_string = guest::wstring("0000000000");
        if (!g_string) return;
        g_text = mem::heap_alloc(22);
        const char* name = "PORT_CREDIT_AMOUNT";
        g_name = mem::data_alloc(u32(std::char_traits<char>::length(name) * 2 + 2));
        for (u32 i = 0; name[i]; ++i) mem::w16(g_name + 2 * i, u16(name[i]));
        mem::w16(g_name + u32(std::char_traits<char>::length(name) * 2), 0);
    }
    const std::string digits = std::to_string(credits);
    for (size_t i = 0; i < digits.size(); ++i) mem::w16(g_text + u32(2 * i), u16(digits[i]));
    mem::w16(g_text + u32(2 * digits.size()), 0);
    cpu::call(fn_assign, {g_string, g_text, g_text + u32(2 * digits.size())});
    cpu::call(fn_set_text, {layout, g_name, g_string});
}

}  // namespace

// Called by the layout patch only after it has successfully added the counter.
void layout_added() { g_layout_added = true; }

bool install() {
    const u32 update = elf::find("_ZN3Hud8onUpdateEi");
    const u32 use_credits = elf::find("_ZN9Inventory10useCreditsEj");
    fn_in_rig = elf::find("_ZNK3Hud7isInRigEv");
    fn_set_text = elf::find("_ZN2im6layout6Layout14setMutableTextEPKwRKN5eastl12basic_stringIwNS_20StringEASTLAllocatorEEE");
    fn_assign = elf::find("_ZN5eastl12basic_stringIwN2im20StringEASTLAllocatorEE6assignEPKwS5_");
    if (update != mem::ELF_BASE + 0x74364 || use_credits != mem::ELF_BASE + 0x9ac54 ||
        !fn_in_rig || !fn_set_text || !fn_assign ||
        mem::r32(update + 4) != 0xe5903004 ||     // ldr r3, [r0, #4]: world
        mem::r32(update + 0x17c) != 0xe3a01d4d || // mov r1, #0x1340
        mem::r32(update + 0x188) != 0xe2811014 || // add r1, r1, #0x14: RIG layout
        mem::r32(update + 0x19c) != 0xe59370d4 || // ldr r7, [r3, #0xd4]: player
        mem::r32(update + 0x274) != 0xe5973530 || // ldr r3, [r7, #0x530]: inventory
        mem::r32(use_credits + 4) != 0xe5903018) { // ldr r3, [r0, #0x18]: credits
        LOGW("RIG credits: unsupported game layout; counter disabled");
        return false;
    }
    return cpu::hook_entry(update, "Hud::onUpdate (RIG credits)", on_hud_update);
}

}  // namespace rig_credits
