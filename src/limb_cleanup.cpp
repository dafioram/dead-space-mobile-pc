#include "limb_cleanup.h"
#include "cpu.h"
#include "elf_loader.h"
#include "limb_cleanup_rules.h"
#include "log.h"
#include "objects.h"

#include <unordered_map>

namespace limb_cleanup {
namespace {
// Class names come from symbolizing the vtable (slow), so remember the answer per vtable.
bool is_limb(u32 obj) {
    static std::unordered_map<u32, bool> by_vtable;
    const u32 vtable = mem::r32(obj);
    auto it = by_vtable.find(vtable);
    if (it == by_vtable.end()) it = by_vtable.emplace(vtable, objs::class_name(obj) == "GameObjectLimb").first;
    return it->second;
}
}  // namespace

int sweep(u32 world, u32 player) {
    if (!world) return 0;
    const u32 destroy = destroy_function();
    if (!destroy) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            LOGW("[limb-cleanup] GameObject::destroy is not the verified one; cleanup disabled");
        }
        return 0;
    }
    static u32 get_target = elf::find("_ZNK18GameObjectPlayable20getTelekinesisTargetEv");
    const u32 held = (get_target && player) ? cpu::call(get_target, {player}) : 0;
    int destroyed = 0;
    for (u32 obj : objs::world_objects(world)) {
        if (!is_limb(obj) || obj == held || (objs::flags(obj) & 1)) continue;  // bit 0: already destroyed
        const objs::Vec3 p = objs::pos(obj);
        const objs::Box b = objs::bounds(obj);
        const float pos[3] = {p.x, p.y, p.z}, lo[3] = {b.lo.x, b.lo.y, b.lo.z}, hi[3] = {b.hi.x, b.hi.y, b.hi.z};
        if (!broken(pos, lo, hi)) continue;
        // The limb's own expiry state (GameObjectLimb::onUpdate): age in ms at +0x23c, "frozen" at +0x238 and
        // the flag bytes at +0x240/+0x241/+0x242/+0x244. Logged so a trace shows why the game never expired it.
        LOGW("[limb-cleanup] destroying GameObjectLimb %08x pos %g %g %g box %g %g %g .. %g %g %g age=%d frozen=%u "
             "flags=%02x/%02x/%02x/%02x",
             obj, double(p.x), double(p.y), double(p.z), double(b.lo.x), double(b.lo.y), double(b.lo.z),
             double(b.hi.x), double(b.hi.y), double(b.hi.z), int(mem::r32(obj + 0x23c)), mem::r32(obj + 0x238),
             unsigned(mem::r8(obj + 0x240)), unsigned(mem::r8(obj + 0x241)), unsigned(mem::r8(obj + 0x242)),
             unsigned(mem::r8(obj + 0x244)));
        cpu::call(destroy, {obj});
        ++destroyed;
    }
    return destroyed;
}
}  // namespace limb_cleanup
