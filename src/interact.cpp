#include "interact.h"
#include "ammo_pickup.h"
#include "door_lock.h"
#include "limb_use.h"
#include "game_layout.h"
#include "outcome.h"
#include "interaction_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "cpu.h"
#include "devmode.h"
#include "elf_loader.h"
#include "guest_util.h"
#include "hle/gles.h"
#include "log.h"
#include "objects.h"

namespace interact {

namespace {

using objs::Box;
using objs::Vec3;

// ---- the player's hitbox, in the view frame (x right, y up, z forward; origin at the player
// position, which is about 1 m above the floor)
constexpr float kBack = 0.3f;     // starts a little behind the player's centre
constexpr float kReach = 1.8f;    // ... and reaches this far forward
constexpr float kHalfWidth = 0.7f;
constexpr float kBottom = -1.05f;  // floor
constexpr float kTop = 1.3f;       // above the head

// ---- object zones: bounding box grown to at least kMinSize on each axis, plus kPad all round
constexpr float kMinSize = 0.5f;
constexpr float kPad = 0.1f;

// ---- kinesis (when nothing is in reach): cone along the line of sight
constexpr float kKinesisRange = 9.0f;
constexpr float kKinesisCos = 0.97f;  // ~14 degrees

struct ClassRule {
    const char* name;  // class name (exact)
    bool skip;         // never used with E
    float pad;         // extra padding for this class
};
// Classes whose zone needs adjusting. Shoot switches are triggered by shooting them, not by E.
const ClassRule kRules[] = {
    {"GameObjectShootSwitch", true, 0},  // (covered ones are opened with E, then cut / shot)
    {"GameObjectPickup", false, 0.15f},
};
// Objects without physics bounds (control panels, stores, power node locks, O2 stations, some lift
// stops...) report no bounds. Their zone is built around their touch-region markers (where the
// game puts the tap icon, e.g. a console's screen): kNoBodyWidth square, from kNoBodyBelow under
// the marker to kNoBodyAbove over it, along the marker's model axes. Without markers,
// around the object's position (which is at the floor) up to kNoBodyHeight.
constexpr float kNoBodyWidth = 1.0f, kNoBodyBelow = 1.2f, kNoBodyAbove = 0.4f, kNoBodyHeight = 2.0f;
const ClassRule* rule_for(const std::string& cls) {
    for (auto& r : kRules)
        if (cls == r.name) return &r;
    return nullptr;
}

View view_of(u32 player, float look_yaw) {
    return interaction_view(objs::pos(player), objs::facing(player), objs::up(player), look_yaw);
}

std::vector<Zone> zones_of(u32 obj, const ClassRule* rule) {
    Box b = objs::bounds(obj);
    std::vector<Zone> zones;
    if (objs::empty(b)) {
        float h = kNoBodyWidth * 0.5f;
        auto regions = objs::regions(obj);
        if (regions.empty()) {
            Vec3 p = objs::pos(obj);
            b = {{p.x - h, p.y, p.z - h}, {p.x + h, p.y + kNoBodyHeight, p.z + h}};
            zones.push_back({world_view(), b});
        } else {
            // Each marker supplies its own world orientation. Keep the corrected
            // anchor and rotate the existing dimensions, including "below", with
            // the terminal. Do not join separate markers through empty space.
            for (const auto& region : regions)
                zones.push_back({region_frame(region), {{-h, -kNoBodyBelow, -h}, {h, kNoBodyAbove, h}}});
        }
    } else {
        // Physics getBounds is already a world-space AABB; never rotate it twice.
        zones.push_back({world_view(), b});
    }
    float pad = kPad + (rule ? rule->pad : 0);
    for (auto& zone : zones) {
        float* lo = &zone.box.lo.x;
        float* hi = &zone.box.hi.x;
        for (int k = 0; k < 3; k++) {
            float c = (lo[k] + hi[k]) * 0.5f, h = std::max((hi[k] - lo[k]) * 0.5f, kMinSize * 0.5f) + pad;
            lo[k] = c - h;
            hi[k] = c + h;
        }
    }
    return zones;
}

struct Cand {
    u32 obj;
    std::string cls;
    std::vector<Zone> zones;
    Box local_zone;
    bool selectable, telekinesis_target, in_hitbox;
    float aim_score;  // lower is preferred: distance from the line of sight
    Outcome outcome = Outcome::unverified;  // what the game would do on E: act, or decline (full ammo, locked door, limb)
};

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

void emit(bool report, const std::string& s) {
    if (!report) return;
    LOGI("[interact] %s", s.c_str());
    if (dev::active()) dev::out(s);
}

u32 g_evbuf = 0, g_vt_action = 0;
bool send_use(u32 obj) {
    if (!g_evbuf) {
        g_evbuf = mem::heap_alloc(32, 8);
        g_vt_action = elf::find("_ZTV11ActionEvent") + 8;
    }
    // ActionEvent(1008, 16): what tapping the object's icon sends (InputForwarderTapToAction)
    std::memset(mem::host(g_evbuf), 0, 32);
    mem::w32(g_evbuf, g_vt_action);
    mem::w32(g_evbuf + 4, 1008);
    mem::w32(g_evbuf + 8, 16);
    return cpu::call(mem::r32(mem::r32(obj) + layout::slot::kOnEvent), {obj, g_evbuf}) & 0xff;
}

// What would the game do on E for this candidate? Only checked for candidates that can actually be selected.
Outcome outcome_of(const Cand& c, u32 player_object) {
    if (!c.selectable || !c.in_hitbox) return Outcome::unverified;
    if (c.cls == "GameObjectPickup") return pickup::ammo_outcome(c.obj, player_object);
    if (c.cls == "GameObjectDoor") return door::open_outcome(c.obj);
    if (c.cls == "GameObjectLimb") return limb::use_outcome();
    return Outcome::unverified;
}

const char* outcome_text(const Cand& c) {
    const bool is_door = c.cls == "GameObjectDoor", is_limb = c.cls == "GameObjectLimb";
    switch (c.outcome) {
    case Outcome::declines:
        return is_door   ? "door: E would be refused or ignored (locked, or not closed); other usable objects are preferred"
               : is_limb ? "limb: E has no effect on it; other usable objects are preferred"
                         : "ammo pickup: the game would refuse it";
    case Outcome::acts: return is_door ? "door: closed and unlocked" : "ammo pickup: accepts at least part";
    default: return "";
    }
}

std::vector<Cand> gather(const View& v, u32 world, u32 player_object, bool report) {
    std::vector<Cand> out;
    Zone player{v, {{-kHalfWidth, kBottom, -kBack}, {kHalfWidth, kTop, kReach}}};
    for (u32 o : objs::world_objects(world)) {
        if (!objs::is_interactive_class(o)) continue;
        Cand c{o, objs::class_name(o), {}, {}, false, false, false, 0};
        const ClassRule* rule = rule_for(c.cls);
        bool hidden = objs::is_hidden(o);
        c.selectable = !hidden && !(rule && rule->skip) && (objs::is_interactive(o) || objs::is_interactive_plus(o));
        c.telekinesis_target = !hidden && objs::is_telekinesisable(o);
        if (!c.selectable && !c.telekinesis_target && !report) continue;
        bool found = false;
        for (const auto& zone : zones_of(o, rule)) {
            if (!zone_within_radius(zone, v.p, kKinesisRange + 4)) continue;
            Box zone_box = zone_bounds(zone, v);
            bool touches_hitbox = zones_overlap(zone, player);
            // Nearest the line of sight first, among overlapping markers if any.
            float gap = zone_box.lo.x > 0 ? zone_box.lo.x : zone_box.hi.x < 0 ? -zone_box.hi.x : 0;
            float aim_score = gap * 2.0f + std::max(zone_box.lo.z, 0.0f) * 0.5f;
            if (!found || (touches_hitbox && !c.in_hitbox) || (touches_hitbox == c.in_hitbox && aim_score < c.aim_score)) {
                c.local_zone = zone_box;
                c.aim_score = aim_score;
            }
            c.in_hitbox |= touches_hitbox;
            c.zones.push_back(zone);
            found = true;
        }
        if (!found) continue;
        c.outcome = outcome_of(c, player_object);
        out.push_back(c);
    }
    // stable: equal aim scores keep world order, so the pick and the F10 overlay do not flicker between frames
    std::stable_sort(out.begin(), out.end(), [](const Cand& a, const Cand& b) { return a.aim_score < b.aim_score; });
    return out;
}

const Cand* pick(const std::vector<Cand>& cands, const View& v, bool& telekinesis) {
    telekinesis = false;
    if (const auto* candidate = best_selectable(cands)) return candidate;
    // telekinesis: zone centre inside the cone along the line of sight
    const Cand* best = nullptr;
    float best_cos = kKinesisCos;
    for (auto& c : cands) {
        if (!c.telekinesis_target) continue;
        Vec3 m{(c.local_zone.lo.x + c.local_zone.hi.x) * 0.5f, (c.local_zone.lo.y + c.local_zone.hi.y) * 0.5f - 0.5f, (c.local_zone.lo.z + c.local_zone.hi.z) * 0.5f};
        float len = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z);
        if (len < 0.1f || len > kKinesisRange || m.z <= 0) continue;
        // horizontal angle only (the camera pitch is not part of the view); vertical within 3 m
        float hl = std::sqrt(m.x * m.x + m.z * m.z), cs = m.z / hl;
        if (cs > best_cos && std::fabs(m.y) < 3.0f) {
            best_cos = cs;
            best = &c;
        }
    }
    (void)v;
    telekinesis = best != nullptr;
    return best;
}

void report_all(const std::vector<Cand>& cands, const View& v) {
    emit(true, fmt("player %.2f %.2f %.2f looking %.2f %.2f %.2f up %.2f %.2f %.2f; hitbox %.1f..%.1f m ahead, %.1f m wide",
                   v.p.x, v.p.y, v.p.z, v.f.x, v.f.y, v.f.z, v.u.x, v.u.y, v.u.z, -kBack, kReach, 2 * kHalfWidth));
    int n = 0;
    for (auto& c : cands) {
        if (n++ >= 25) break;
        emit(true, fmt("  %08x %-26s selectable=%d telekinesisTarget=%d in-hitbox=%d aimScore=%.2f local x %.2f..%.2f y %.2f..%.2f z "
                       "%.2f..%.2f isInteractive=%d isInteractivePlus=%d useEnabled=%d",
                       c.obj, c.cls.c_str(), c.selectable, c.telekinesis_target, c.in_hitbox, c.aim_score, c.local_zone.lo.x, c.local_zone.hi.x, c.local_zone.lo.y,
                        c.local_zone.hi.y, c.local_zone.lo.z, c.local_zone.hi.z, objs::is_interactive(c.obj), objs::is_interactive_plus(c.obj),
                       objs::use_enabled(c.obj)));
        if (c.outcome != Outcome::unverified) emit(true, std::string("    ") + outcome_text(c));
    }
}

}  // namespace

bool use(u32 player, u32 world, float look_yaw, bool report) {
    if (!player || !world) return false;
    View v = view_of(player, look_yaw);
    auto cands = gather(v, world, player, report);
    if (report) report_all(cands, v);
    bool telekinesis;
    const Cand* c = pick(cands, v, telekinesis);
    if (!c) {
        emit(report, "E: nothing in front of the player");
        return false;
    }
    bool ok = send_use(c->obj);
    emit(report, fmt("E: %s %s %08x -> %s", telekinesis ? "telekinesis" : "use", c->cls.c_str(), c->obj, ok ? "accepted" : "refused"));
    return ok;
}

namespace {
// world-space box edges, projected and queued as overlay lines
void draw_box(u32 world, const Vec3 c[8], unsigned rgba) {
    static const int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                      {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    float sx[8], sy[8];
    bool ok[8];
    for (int i = 0; i < 8; i++) ok[i] = objs::to_screen(world, c[i], sx[i], sy[i]);
    for (auto& e : kEdges)
        if (ok[e[0]] && ok[e[1]]) hle::overlay_line(sx[e[0]], sy[e[0]], sx[e[1]], sy[e[1]], rgba);
}
}  // namespace

void draw_debug(u32 player, u32 world, float look_yaw) {
    if (!player || !world) return;
    View v = view_of(player, look_yaw);
    auto cands = gather(v, world, player, false);
    bool telekinesis;
    const Cand* best = pick(cands, v, telekinesis);
    for (auto& c : cands) {
        if (!c.selectable && !c.telekinesis_target) continue;
        unsigned col = &c == best ? 0x30ff30ffu : c.selectable ? 0xffffffb0u : 0xffc030b0u;
        for (const auto& zone : c.zones) {
            if (!zone_within_radius(zone, v.p, 10)) continue;
            Vec3 corners[8];
            zone_corners(zone, corners);
            draw_box(world, corners, col);
        }
    }
    // hitbox (view frame -> world)
    Vec3 corners[8];
    for (int i = 0; i < 8; i++) {
        float x = i & 1 ? kHalfWidth : -kHalfWidth, y = i & 2 ? kTop : kBottom, z = i & 4 ? kReach : -kBack;
        corners[i] = view_world(v, {x, y, z});
    }
    draw_box(world, corners, 0x30e0ffd0u);
}

std::string pick_name(u32 player, u32 world, float look_yaw) {
    if (!player || !world) return "";
    View v = view_of(player, look_yaw);
    auto cands = gather(v, world, player, false);
    bool telekinesis;
    const Cand* c = pick(cands, v, telekinesis);
    return c ? fmt("%s %08x", c->cls.c_str(), c->obj) : "";
}

Approach approach_point(u32 obj, float dist) {
    // This diagnostic helper still approaches on the world floor. Use the first
    // real zone, not a union that could put the target between separate markers.
    auto zones = zones_of(obj, rule_for(objs::class_name(obj)));
    Box z = zone_bounds(zones.front(), world_view());
    Vec3 c{(z.lo.x + z.hi.x) * 0.5f, (z.lo.y + z.hi.y) * 0.5f, (z.lo.z + z.hi.z) * 0.5f};
    Vec3 f = objs::facing(obj);
    // distance from the zone centre to its edge along the facing
    float ext = std::fabs(f.x) * (z.hi.x - z.lo.x) * 0.5f + std::fabs(f.z) * (z.hi.z - z.lo.z) * 0.5f;
    float d = ext + dist;
    // stand on the floor under the zone: player position is ~1 m above the floor
    float floor_y = z.lo.y + kPad;
    return {{c.x + f.x * d, floor_y + 1.0f, c.z + f.z * d}, c};
}

void repair_objective_panel(u32 world) {
    static u32 fn_target = elf::find("_ZNK9GameWorld18getObjectiveTargetEv"),
               fn_state = elf::find("_ZN20GameObjectSwitchable15stateTransitionENS_5StateE");
    static u32 stuck = 0;
    static int count = 0;
    if (!world || !fn_target || !fn_state) return;
    u32 t = cpu::call(fn_target, {world});
    // useEnabled (+0x174). A panel restored from a checkpoint as "switched" (initPostCheckpoint ->
    // setOnState) is unusable while its switch state +0x1e4 may still read 0.
    bool used_panel = t && objs::class_name(t) == "GameObjectControlPanel" && !objs::use_enabled(t);
    if (!used_panel) {
        stuck = 0;
        count = 0;
        return;
    }
    if (t != stuck) stuck = t, count = 0;
    if (++count < 6) return;  // ~3 s
    Vec3 p = objs::pos(t);
    LOGW("objective points at control panel %08x (%.1f %.1f %.1f), which the save marks as already used; "
         "switching it back on",
         t, p.x, p.y, p.z);
    cpu::call(fn_state, {t, 0});
    stuck = 0;
    count = 0;
}

void report_encounters(u32 player, u32 world) {
    // Encounter scripts (lockdowns, waves) end when every spawn zone that belongs to them is done
    // (Script::checkForActiveSpawns): zone state +0x174 = 0 idle, 1 waiting to spawn, 2 waiting for
    // its spawned enemy (+0x18c) to die, 3 waiting for every enemy of its type to be gone.
    static u32 fn_member =
        elf::find("_ZN10GameObject14isScriptMemberERKN5eastl12basic_stringIwN2im20StringEASTLAllocatorEEE");
    if (!fn_member) return;
    auto all = objs::world_objects(world);
    Vec3 p = objs::pos(player);
    for (u32 sc : all) {
        std::string cls = objs::class_name(sc);
        if (cls.rfind("Script", 0) != 0) continue;
        u32 info = mem::r32(sc + 0x2c), name = info >= 0x10000 ? mem::r32(info + 0xc4) : 0;
        if (name < 0x10000) continue;
        std::string n;
        for (u32 q = mem::r32(name), e = mem::r32(name + 4); q && q < e && n.size() < 40; q += 2) n += char(mem::r16(q));
        std::vector<std::string> zones;
        for (u32 o : all) {
            if (!(guest::vcall(o, 0x40) & 0xff) || !(cpu::call(fn_member, {o, name}) & 0xff)) continue;
            s32 st = s32(mem::r32(o + 0x174));
            if (st == 0) continue;
            Vec3 q = objs::pos(o);
            std::string z = fmt("    zone %08x state=%d at %.1f %.1f %.1f (%.1f m away)", o, st, q.x, q.y, q.z,
                                std::sqrt((q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z)));
            if (u32 e = mem::r32(o + 0x18c); e >= 0x10000 && mem::r32(e) >= 0x10000) {
                Vec3 ep = objs::pos(e);
                z += fmt("; spawned %s %08x alive=%d flags=%x at %.1f %.1f %.1f", objs::class_name(e).c_str(), e,
                         guest::vcall(e, 0x110) & 0xff, objs::flags(e), ep.x, ep.y, ep.z);
            }
            zones.push_back(z);
        }
        if (zones.empty()) continue;
        emit(true, fmt("encounter %s (%s) state=%d still has active spawn zones:", n.c_str(), cls.c_str(),
                       s32(mem::r32(sc + 0x1a4))));
        for (auto& z : zones) emit(true, z);
    }
}

std::string preview(u32 player, u32 world, float look_yaw) {
    if (!player || !world) return "no player";
    report_encounters(player, world);
    View v = view_of(player, look_yaw);
    auto cands = gather(v, world, player, true);
    report_all(cands, v);
    bool telekinesis;
    const Cand* c = pick(cands, v, telekinesis);
    return c ? fmt("E would %s %s %08x", telekinesis ? "telekinesis" : "use", c->cls.c_str(), c->obj) : "E would do nothing";
}

}  // namespace interact
