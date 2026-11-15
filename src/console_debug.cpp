#include "console_debug.h"

#include <algorithm>
#include <bit>
#include <cstdio>

#include "cpu.h"
#include "elf_loader.h"
#include "input.h"
#include "objects.h"

namespace console_debug {
namespace {

bool mapped(u32 address, size_t size) {
    if (address < 0x10000 || !size || size - 1 > std::numeric_limits<u32>::max() - address) return false;
    u64 end = u64(address) + size;
    for (u64 page = address & ~(mem::PAGE_SIZE - 1); page < end; page += mem::PAGE_SIZE)
        if (!mem::is_mapped(u32(page))) return false;
    return true;
}

bool wallet_layout_supported() {
    // The same wallet fields used by the existing developer rich command.
    // Check the native player->inventory read and both native wallet accesses.
    u32 hud = elf::find("_ZN3Hud8onUpdateEi");
    u32 use = elf::find("_ZN9Inventory10useCreditsEj");
    u32 add = elf::find("_ZN9Inventory7addItemER4ItemPK18GameObjectPlayable");
    return hud == mem::ELF_BASE + 0x74364 && use == mem::ELF_BASE + 0x9ac54 && add == mem::ELF_BASE + 0x9afc4 &&
           mem::r32(hud + 0x274) == 0xe5973530 && mem::r32(use + 4) == 0xe5903018 &&
           mem::r32(add + 0x15c) == 0xe5952014 && mem::r32(add + 0x16c) == 0xe5853014;
}

void set_wallet(const Request& request, u32 player, Output output) {
    if (!wallet_layout_supported()) return output("cannot set resources: unsupported native wallet layout");
    if (!mapped(player, 0x534)) return output("cannot set resources: player inventory reference unavailable");
    u32 inventory = mem::r32(player + 0x530);
    if (!mapped(inventory, 0x1c)) return output("cannot set resources: no current inventory");
    Wallet before{mem::r32(inventory + 0x18), mem::r32(inventory + 0x14)};
    Wallet after = set_resources(request, before);
    if (request.kind == Kind::credits || request.kind == Kind::rich) mem::w32(inventory + 0x18, after.credits);
    if (request.kind == Kind::nodes || request.kind == Kind::rich) mem::w32(inventory + 0x14, after.nodes);
    output("resources SET (not added): credits " + std::to_string(before.credits) + " -> " + std::to_string(after.credits) +
           "; nodes " + std::to_string(before.nodes) + " -> " + std::to_string(after.nodes));
    output("May autosave. Use --no-save to protect originals.");
}

void restore(u32 player, Output output) {
    u32 health_max = elf::find("_ZNK18GameObjectPlayable12getHealthMaxEv");
    u32 stasis_max = elf::find("_ZNK18GameObjectPlayable12getStasisMaxEv");
    u32 set_health = elf::find("_ZN18GameObjectPlayable9setHealthEfa");
    u32 set_stasis = elf::find("_ZN18GameObjectPlayable9setStasisEf");
    u32 refill = elf::find("_ZN18GameObjectPlayable15triggerO2RefillEv");
    if (!health_max || !stasis_max || !set_health || !set_stasis || !refill)
        return output("cannot restore: native health/stasis/oxygen functions unavailable");
    u32 health_bits = cpu::call(health_max, {player});
    u32 stasis_bits = cpu::call(stasis_max, {player});
    float health = std::bit_cast<float>(health_bits), stasis = std::bit_cast<float>(stasis_bits);
    if (!valid_restore_limits(health, stasis)) return output("cannot restore: invalid native suit limits");
    cpu::call(set_health, {player, health_bits, 1});  // native immediate health update, not a revival
    cpu::call(set_stasis, {player, stasis_bits});
    bool oxygen = (cpu::call(refill, {player}) & 0xff) != 0;
    char text[160];
    std::snprintf(text, sizeof text, "health restored to %.1f; stasis restored to %.1f", health, stasis);
    output(text);
    output(oxygen ? "oxygen: native refill started (normal refill timing)"
                  : "oxygen: unchanged; native refill not needed or unavailable in its current state");
    output("May autosave. Use --no-save to protect originals.");
}

bool enemy_layout_supported() {
    u32 base = elf::find("_ZTV10GameObject");
    u32 is_enemy = elf::find("_ZNK10GameObject12isNecromorphEv");
    u32 alive = elf::find("_ZNK10GameObject7isAliveEv");
    return base && is_enemy && alive && mapped(base, 8 + 0x114) &&
           mem::r32(base + 8 + 0x2c) == is_enemy && mem::r32(base + 8 + 0x110) == alive;
}

void enemies(u32 player, u32 world, float radius, Output output) {
    if (!enemy_layout_supported()) return output("cannot inspect enemies: unsupported native object layout");
    struct Enemy {
        u32 object;
        std::string name;
        double distance;
        bool alive, hidden;
    };
    std::vector<Enemy> found;
    objs::Vec3 origin = objs::pos(player);
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
        return output("cannot inspect enemies: invalid player position");
    const u32 base_pos = elf::find("_ZNK10GameObject6getPosEv");
    size_t unavailable = 0, living = 0;
    // Obtain fresh references on the game thread; never cache objects across commands.
    for (u32 object : objs::world_objects(world)) {
        if (object == player || !mapped(object, 0x5c)) continue;
        u32 vtable = mem::r32(object);
        if (!mapped(vtable, 0x114)) continue;
        u32 is_enemy = mem::r32(vtable + 0x2c), alive = mem::r32(vtable + 0x110);
        if (!mapped(is_enemy, 4) || !mapped(alive, 4) || !(cpu::call(is_enemy, {object}) & 0xff)) continue;
        // Do not misreport a bodyless base getPos fallback as world origin.
        if (!mem::r32(object + 0x58) && mem::r32(vtable + 0xc4) == base_pos) {
            unavailable++;
            continue;
        }
        auto position = objs::pos(object);
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
            unavailable++;
            continue;
        }
        double distance;
        if (!enemy_distance(origin.x, origin.y, origin.z, position.x, position.y, position.z, radius, distance)) continue;
        bool live = (cpu::call(alive, {object}) & 0xff) != 0;
        living += live;
        found.push_back({object, objs::class_name(object), distance, live, (objs::flags(object) & 8) != 0});
    }
    std::sort(found.begin(), found.end(), [](const Enemy& a, const Enemy& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.object < b.object;
    });
    size_t shown = std::min(found.size(), kEnemyRows);
    char text[192];
    std::snprintf(text, sizeof text, "enemies within %.1f m: %zu (%zu alive); showing nearest %zu; read-only",
                  radius, found.size(), living, shown);
    output(text);
    for (size_t i = 0; i < shown; ++i) {
        const auto& enemy = found[i];
        std::snprintf(text, sizeof text, "%08x %s %.1f m alive=%s hidden=%s", enemy.object, enemy.name.c_str(),
                      enemy.distance, enemy.alive ? "yes" : "no", enemy.hidden ? "yes" : "no");
        output(text);
    }
    if (unavailable) output(std::to_string(unavailable) + " native enemies had no position; not treated as origin");
}

}  // namespace

bool submit(const Request& request, Output output, std::string& error) {
    if (request.kind == Kind::none || !request.error.empty() || !output ||
        request.credits > kMaxResource || request.nodes > kMaxResource ||
        (request.kind == Kind::enemies && !valid_radius(request.radius))) {
        error = request.error.empty() ? "invalid debug request" : request.error;
        return false;
    }
    u32 player = input::player(), world = input::world(), epoch = input::context_epoch();
    if (!player || !world) {
        error = "no current player/level; enter gameplay before using this command";
        return false;
    }
    input::run_on_game_thread([request, output, player, world, epoch](u32 current_player) {
        bool same = same_context(player, world, epoch, current_player, input::world(), input::context_epoch());
        // Do not even query a replacement player's mutable state for a stale job.
        auto decision = execution(request, same, same && (!request.mutates() || input::debug_mutation_allowed(current_player)));
        if (decision == Execution::stale)
            return output("debug request discarded: player, world or input ownership changed; issue it again");
        if (decision == Execution::busy)
            return output("debug change refused: requires a living player in gameplay, outside menus/scripts/grapples");
        if (request.kind == Kind::enemies) enemies(current_player, world, request.radius, output);
        else if (request.kind == Kind::restore) restore(current_player, output);
        else set_wallet(request, current_player, output);
    });
    return true;
}

}  // namespace console_debug
