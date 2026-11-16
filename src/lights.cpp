// Dynamic light sources for the native renderer, taken from game events (the game itself has no
// lights: its Weapon::cacheLight / GameObjectProjectile::initLight are empty).
//
//   weapon fired          -> short flash at the muzzle
//   Weapon::resolveImpact -> short flash where the shot landed
//   projectile in flight  -> a light that follows it (line gun bolts, enemy projectiles)
//   explosive detonates   -> a larger, longer flash
//
// Sources are kept in the game's physics/render space and handed to the renderer in eye space,
// using the gameplay camera's eye/target/up (input::camera).
#include "lights.h"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>
#include <string>

#include "cpu.h"
#include "elf_loader.h"
#include "input.h"
#include "log.h"
#include "memory.h"
#include "objects.h"
#include "scene_cache_lifetime.h"

namespace lights {
namespace {

struct Src {
    float p[3];
    float c[3];
    float radius;
    u32 born_ms;
    float life_ms;  // fades out linearly over its life
    u32 id;         // object the light follows (0 = none): one light per object, moved on each update
};
std::vector<Src> g_src;
std::map<u32, std::array<float, 3>> g_weapon_color;  // weapon object -> colour of its last shot
std::atomic<int> g_trace = 0;

float f32(u32 a) {
    u32 v = mem::r32(a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

void push(float x, float y, float z, const float* c, float radius, float life_ms, u32 id = 0) {
    if (id)
        for (Src& s : g_src)
            if (s.id == id) {
                s.p[0] = x, s.p[1] = y, s.p[2] = z;
                s.c[0] = c[0], s.c[1] = c[1], s.c[2] = c[2];
                s.radius = radius;
                s.born_ms = SDL_GetTicks();
                return;
            }
    if (g_src.size() >= 48) g_src.erase(g_src.begin());
    g_src.push_back({{x, y, z}, {c[0], c[1], c[2]}, radius, SDL_GetTicks(), life_ms, id});
}

const float kPlasma[3] = {0.45f, 0.85f, 1.0f}, kRipper[3] = {1.0f, 0.75f, 0.45f}, kLineGun[3] = {0.4f, 0.9f, 1.0f};
const float kCorer[3] = {1.0f, 0.6f, 0.3f}, kStasis[3] = {0.35f, 0.6f, 1.0f}, kEnemy[3] = {0.95f, 0.8f, 0.35f};
const float kExplosion[3] = {1.0f, 0.65f, 0.3f};

void on_fire(cpu::Ctx& c) {
    std::string name = cpu::current_hle_name(c);
    const float* col = name.find("Ripper") != std::string::npos    ? kRipper
                       : name.find("LineGun") != std::string::npos ? kLineGun
                       : name.find("Corer") != std::string::npos   ? kCorer
                       : name.find("Stasis") != std::string::npos  ? kStasis
                                                                   : kPlasma;
    g_weapon_color[c.reg(0)] = {col[0], col[1], col[2]};
    if (g_trace) LOGI("[lights] onFire weapon=%08x (%s)", c.reg(0), name.c_str());
    u32 pl = input::player();
    if (!pl) return;
    // muzzle: about chest height, an arm's length in front of the player
    objs::Vec3 p = objs::pos(pl), f = objs::facing(pl);
    push(p.x + f.x * 0.8f, p.y + 0.45f, p.z + f.z * 0.8f, col, 5.0f, 120.0f);
}

// Weapon::resolveImpact(void* object, const btVector3& position, const btVector3& normal, ...)
void on_impact(cpu::Ctx& c) {
    u32 a = c.reg(2), b = c.reg(3);
    if (!a || !b) return;
    float px = f32(a), py = f32(a + 4), pz = f32(a + 8), nx = f32(b), ny = f32(b + 4), nz = f32(b + 8);
    if (g_trace) LOGI("[lights] impact weapon=%08x at %.2f %.2f %.2f normal %.2f %.2f %.2f", c.reg(0), px, py, pz, nx, ny, nz);
    if (!std::isfinite(px + py + pz + nx + ny + nz)) return;
    auto it = g_weapon_color.find(c.reg(0));
    const float* col = it != g_weapon_color.end() ? it->second.data() : kPlasma;
    // lifted off the surface so the surface itself is lit
    push(px + nx * 0.25f, py + ny * 0.25f, pz + nz * 0.25f, col, 2.5f, 160.0f);
}

void on_projectile(cpu::Ctx& c) {
    u32 obj = c.reg(0);
    std::string cls = objs::class_name(obj);
    objs::Vec3 p = objs::pos(obj);
    if (!std::isfinite(p.x + p.y + p.z)) return;
    bool player_shot = cls.find("LineGun") != std::string::npos || cls.find("Corer") != std::string::npos;
    const float* col = cls.find("Corer") != std::string::npos ? kCorer : player_shot ? kLineGun : kEnemy;
    static int n = 0;
    if (g_trace && (n++ % 10) == 0) LOGI("[lights] projectile %08x %s at %.2f %.2f %.2f", obj, cls.c_str(), p.x, p.y, p.z);
    // follows the projectile; fades out quickly once it stops updating (hit something)
    const float dim[3] = {col[0] * 0.6f, col[1] * 0.6f, col[2] * 0.6f};  // in flight for a while: gentler than a flash
    push(p.x, p.y, p.z, dim, player_shot ? 3.5f : 3.0f, 120.0f, obj);
}

void on_explode(cpu::Ctx& c) {
    objs::Vec3 p = objs::pos(c.reg(0));
    if (g_trace) LOGI("[lights] explosion %08x at %.2f %.2f %.2f", c.reg(0), p.x, p.y, p.z);
    if (!std::isfinite(p.x + p.y + p.z)) return;
    push(p.x, p.y + 0.5f, p.z, kExplosion, 8.0f, 450.0f);
}

// ---- level scene nodes (m3g): flares the level registers with the world
using scene_cache::NodeRef;
using scene_cache::Token;
std::recursive_mutex g_scene_mutex;
scene_cache::LifetimesByOwner g_lifetimes;
Token g_scene_world;
bool g_lifetime_ready = false;
u32 fn_retain = 0, fn_release = 0;
std::array<u32, 7> g_node_vtables{};  // Node, Camera, Group, World, Mesh, SkinnedMesh, ParticleEffect
u32 g_fade_tick = 0;
unsigned g_rejected = 0, g_teardown_budget = 0, g_write_budget = 0, g_bad_header_budget = 0;
u32 g_teardown_world = 0;

bool mapped(u32 address, u32 size) {
    if (!address || !size || uint64_t(address) + size > (uint64_t(1) << 32)) return false;
    for (uint64_t a = address & ~uint64_t(mem::PAGE_SIZE - 1); a < uint64_t(address) + size; a += mem::PAGE_SIZE)
        if (!mem::is_mapped(u32(a))) return false;
    return true;
}

// All *_locked helpers require g_scene_mutex. Type/layout/mapping checks supplement
// native lifetime tokens; none of them can establish a lifetime on their own.
int node_kind_locked(NodeRef ref) {
    if (!g_lifetime_ready || ref.world != g_scene_world || !g_lifetimes.contains(ref) || !mapped(ref.node.address, 0x13c)) return -1;
    u32 vt = mem::r32(ref.node.address);
    for (size_t i = 0; i < g_node_vtables.size(); i++)
        if (vt == g_node_vtables[i]) return int(i);
    return -1;
}

void rejected_locked(NodeRef ref) {
    if (g_rejected++ < 8)
        LOGW("[scene-cache] rejected stale/unsupported node %08x generation=%llu world=%08x generation=%llu",
             ref.node.address, (unsigned long long)ref.node.generation, ref.world.address, (unsigned long long)ref.world.generation);
}

NodeRef reference_locked(u32 node) { return g_lifetimes.reference(g_scene_world, node); }
bool group_kind(int kind) { return kind == 2 || kind == 3; }
bool mesh_kind(int kind) { return kind == 4 || kind == 5; }

std::vector<NodeRef> children_locked(NodeRef parent, u32 limit) {
    std::vector<NodeRef> children;
    if (!group_kind(node_kind_locked(parent)) || !mapped(parent.node.address, 0x148)) return children;
    u32 b = mem::r32(parent.node.address + 0x13c), e = mem::r32(parent.node.address + 0x140);
    if (!scene_cache::vector_layout(b, e, e, 4)) { rejected_locked(parent); return children; }
    u32 count = std::min((e - b) / 4, limit);
    if (count && !mapped(b, count * 4)) { rejected_locked(parent); return children; }
    for (u32 i = 0; i < count; i++) {
        NodeRef child = reference_locked(mem::r32(b + i * 4));
        if (node_kind_locked(child) >= 0 && mem::r32(child.node.address + 0x128) == parent.node.address)
            children.push_back(child);
        else if (child.node.address) rejected_locked(child);
    }
    return children;
}

std::vector<NodeRef> ancestors_locked(NodeRef ref) {
    std::vector<NodeRef> chain;
    while (ref.node.address && chain.size() < 64) {
        if (node_kind_locked(ref) < 0 || std::find(chain.begin(), chain.end(), ref) != chain.end()) return {};
        chain.push_back(ref);
        u32 parent = mem::r32(ref.node.address + 0x128);
        if (!parent) return chain;
        ref = reference_locked(parent);
        if (!ref) return {};
    }
    return {};  // cyclic or overlong parent chain: never call into it
}

struct SceneNode {
    NodeRef ref;
    int kind;  // 0 direction flare, 1 side flare, 2 animated
};
std::vector<SceneNode> g_nodes;
struct NodeTransform { NodeRef ref; std::array<float, 16> matrix; };
std::unordered_map<u32, NodeTransform> g_previous_transforms;
u32 fn_transform_to = 0;
void clear_scene_locked();

bool select_world_locked(u32 world) {
    Token token = g_lifetimes.worlds.current(world);
    if (!g_lifetime_ready || !token) return false;
    if (token != g_scene_world) {
        clear_scene_locked();
        g_scene_world = token;
    }
    return true;
}

void note_node(u32 world, u32 node, int kind) {
    std::lock_guard lock(g_scene_mutex);
    if (!select_world_locked(world)) return;
    NodeRef ref = reference_locked(node);
    if (node_kind_locked(ref) < 0) { rejected_locked(ref); return; }
    if (std::none_of(g_nodes.begin(), g_nodes.end(), [&](const SceneNode& n) { return n.ref == ref && n.kind == kind; }))
        g_nodes.push_back({ref, kind});
}
std::vector<NodeRef> g_fade_nodes;  // native registrations, qualified by world AND node lifetime
// The game fades these effects (fog banks, light shafts) out by distance as the player walks into them,
// over a band about two metres wide: at a run that is an instant. Here the fade is spread over time.
// Each frame, after the game has set an effect's opacity (m3g::Node +0x130, 16.16; +0x12c = drawn at
// all) and before it is drawn, the value actually used moves toward the game's value at a limited
// speed, both ways.
struct FadeState {
    NodeRef ref;
    float shown = -1;  // opacity in use (-1: not started)
    u32 wrote_alpha = 0xffffffffu;
    u8 wrote_en = 0xff;
    float target = 1;
};
std::unordered_map<u32, FadeState> g_fade_state;
constexpr float kFadeSeconds = 1.4f;  // from fully shown to gone, or back

// Position data of the fog banks' meshes (m3g::Mesh: VertexBuffer +0x160, its positions array +0x44,
// that array's data +0x48): the renderer recognises their draws by it.
struct FogData { NodeRef ref; u32 data; };
std::vector<FogData> g_fog_data;
void note_fog_mesh(u32 node) {
    if (!mesh_kind(node_kind_locked(reference_locked(node))) || !mapped(node, 0x164)) return;
    u32 vb = mem::r32(node + 0x160);
    u32 va = mapped(vb, 0x48) ? mem::r32(vb + 0x44) : 0;
    u32 data = mapped(va, 0x4c) ? mem::r32(va + 0x48) : 0;
    if (data > 0x10000 && std::none_of(g_fog_data.begin(), g_fog_data.end(), [&](const FogData& f) { return f.data == data; }))
        g_fog_data.push_back({reference_locked(node), data});
}

void smooth_fades(cpu::Ctx& c) {
    std::lock_guard lock(g_scene_mutex);
    if (!g_lifetimes.worlds.contains(g_scene_world) || c.reg(0) != g_scene_world.address) return;
    g_fog_data.clear();
    u32 now = SDL_GetTicks();
    float dt = g_fade_tick && now - g_fade_tick < 250 ? float(now - g_fade_tick) / 1000.0f : 0.0f;
    g_fade_tick = now;
    auto one = [&](NodeRef ref) {
        if (node_kind_locked(ref) < 0) { rejected_locked(ref); return; }
        u32 node = ref.node.address;
        FadeState& st = g_fade_state[node];
        if (st.ref != ref) st = FadeState{ref};
        u32 a = mem::r32(node + 0x130);
        u8 en = mem::r8(node + 0x12c);
        // what the game wants: its own value, unless what is there is still what was written last frame
        if (a != st.wrote_alpha || en != st.wrote_en) st.target = en ? std::clamp(float(s32(a)) / 65536.0f, 0.0f, 1.0f) : 0.0f;
        if (st.shown < 0) st.shown = st.target;
        float step = dt / kFadeSeconds;
        st.shown = st.shown < st.target ? std::min(st.target, st.shown + step) : std::max(st.target, st.shown - step);
        st.wrote_alpha = u32(s32(st.shown * 65536.0f));
        st.wrote_en = st.shown > 0.004f ? 1 : 0;
        mem::w32(node + 0x130, st.wrote_alpha);
        mem::w8(node + 0x12c, st.wrote_en);
        if (g_trace && g_write_budget++ < 8)
            LOGI("[scene-cache] fade write node=%08x generation=%llu world-generation=%llu alpha=%08x enabled=%u",
                 node, (unsigned long long)ref.node.generation, (unsigned long long)ref.world.generation, st.wrote_alpha, unsigned(st.wrote_en));
        note_fog_mesh(node);
    };
    for (NodeRef n : g_fade_nodes) {
        int kind = node_kind_locked(n);
        if (kind < 0) { rejected_locked(n); continue; }
        if (!group_kind(kind)) {
            one(n);
            continue;
        }
        for (NodeRef child : children_locked(n, 32)) one(child);
    }
}

void on_reg_fade(cpu::Ctx& c) {
    std::lock_guard lock(g_scene_mutex);
    if (!select_world_locked(c.reg(0))) return;
    NodeRef ref = reference_locked(c.reg(1));
    if (node_kind_locked(ref) < 0) { rejected_locked(ref); return; }
    if (std::find(g_fade_nodes.begin(), g_fade_nodes.end(), ref) == g_fade_nodes.end()) g_fade_nodes.push_back(ref);
}
void on_reg_dir_flare(cpu::Ctx& c) { note_node(c.reg(0), c.reg(2), 0); }
void on_reg_side_flare(cpu::Ctx& c) { note_node(c.reg(0), c.reg(1), 1); }
void on_reg_animate(cpu::Ctx& c) { note_node(c.reg(0), c.reg(2), 2); }

// m3g::Node: parent +0x128, rendering enabled +0x12c (byte), alpha factor +0x130 (int, 16.16)
bool node_shown(NodeRef ref) {
    std::lock_guard lock(g_scene_mutex);
    auto chain = ancestors_locked(ref);
    if (chain.empty()) return false;
    for (NodeRef n : chain)
        if (!mem::r8(n.node.address + 0x12c)) return false;
    return true;
}
// Column-major world matrix of a scene node (m3g::Node::getTransformTo its root).
bool node_world(NodeRef ref, float m[16]) {
    if (!fn_transform_to) return false;
    std::vector<NodeRef> chain;
    {
        std::lock_guard lock(g_scene_mutex);
        chain = ancestors_locked(ref);
        if (chain.empty()) return false;
        for (NodeRef n : chain) {
            u32 refs = mem::r32(n.node.address + 4);
            if (!refs || refs >= 0x7fffffffu) return false;
        }
        // Verified native leaf: increments the reference only, with no locks,
        // callbacks or destructor dispatch. Never keep strong refs in a cache.
        for (NodeRef n : chain) cpu::call(fn_retain, {n.node.address});
    }
    // No registry mutex across native transforms/DECREF: they acquire native
    // locks and can invoke destructors. Temporary native refs protect the chain.
    thread_local u32 xf = mem::data_alloc(0x80);
    for (u32 i = 0; i < 16; i++) {
        float v = (i % 5 == 0) ? 1.0f : 0.0f;
        u32 b;
        std::memcpy(&b, &v, 4);
        mem::w32(xf + 8 + i * 4, b);
    }
    bool transformed = (cpu::call(fn_transform_to, {ref.node.address, chain.back().node.address, xf}) & 0xff) != 0;
    for (u32 i = 0; i < 16; i++) m[i] = f32(xf + 8 + i * 4);
    for (NodeRef n : chain) cpu::call(fn_release, {n.node.address});
    std::lock_guard lock(g_scene_mutex);
    return transformed && node_kind_locked(ref) >= 0 && std::isfinite(m[12] + m[13] + m[14]);
}

void probe_tree(NodeRef ref, int depth) {
    if (depth > 8) return;
    float m[16] = {};
    if (!node_world(ref, m)) return;
    std::vector<NodeRef> children;
    {
        std::lock_guard lock(g_scene_mutex);
        if (node_kind_locked(ref) < 0) return;
        LOGI("[lights] %*s%08x %s at %.2f %.2f %.2f shown %d", depth * 2, "", ref.node.address,
             cpu::symbolize(mem::r32(ref.node.address)).c_str(), m[12], m[13], m[14], mem::r8(ref.node.address + 0x12c));
        children = children_locked(ref, 40);
        if (mesh_kind(node_kind_locked(ref)) && mapped(ref.node.address, 0x164)) {
            u32 vb = mem::r32(ref.node.address + 0x160);
            u32 va = mapped(vb, 0x58) ? mem::r32(vb + 0x44) : 0;
            if (mapped(vb, 0x58))
                LOGI("[lights] %*s  vb %08x scale %.6f bias %.3f %.3f %.3f va %08x", depth * 2, "", vb,
                     f32(vb + 0x48), f32(vb + 0x4c), f32(vb + 0x50), f32(vb + 0x54), va);
            if (mapped(va, 0x60))
                LOGI("[lights] %*s  va: %08x %08x %08x %08x %08x %08x %08x %08x %08x", depth * 2, "",
                     mem::r32(va + 0x3c), mem::r32(va + 0x40), mem::r32(va + 0x44), mem::r32(va + 0x48),
                     mem::r32(va + 0x4c), mem::r32(va + 0x50), mem::r32(va + 0x54), mem::r32(va + 0x58), mem::r32(va + 0x5c));
        }
    }
    for (NodeRef child : children) probe_tree(child, depth + 1);
}

std::vector<NodeRef> quarantine_nodes() {
    std::lock_guard lock(g_scene_mutex);
    std::vector<NodeRef> nodes;
    if (!g_lifetimes.worlds.contains(g_scene_world) || !mapped(g_scene_world.address, 0x230)) return nodes;
    u32 qb = mem::r32(g_scene_world.address + 0x228), qe = mem::r32(g_scene_world.address + 0x22c);
    if (!scene_cache::vector_layout(qb, qe, qe, 4) || qe - qb > 4 * 2000 || (qe != qb && !mapped(qb, qe - qb))) return nodes;
    for (u32 q = qb; q < qe; q += 4) {
        NodeRef ref = reference_locked(mem::r32(q));
        if (node_kind_locked(ref) >= 0) nodes.push_back(ref);
    }
    return nodes;
}

std::vector<SceneNode> scene_nodes() {
    std::lock_guard lock(g_scene_mutex);
    return g_nodes;
}

void trace_nodes(objs::Vec3 p) {
    auto quarantine = quarantine_nodes();
    LOGI("[lights] %zu quarantine nodes", quarantine.size());
    int shown = 0;
    for (NodeRef ref : quarantine) {
        if (shown >= 60) break;
        float m[16];
        if (!node_world(ref, m)) continue;
        float d = std::sqrt((m[12] - p.x) * (m[12] - p.x) + (m[14] - p.z) * (m[14] - p.z));
        shown++;
        LOGI("[lights] qnode %08x shown %d at %.2f %.2f %.2f (%.1f m)", ref.node.address, node_shown(ref), m[12], m[13], m[14], d);
        if (shown <= 2) probe_tree(ref, 1);
    }
    int moving = 0;
    for (const SceneNode& n : scene_nodes()) {
        float m[16];
        if (!node_world(n.ref, m)) continue;
        std::lock_guard lock(g_scene_mutex);
        if (node_kind_locked(n.ref) < 0) continue;
        auto it = g_previous_transforms.find(n.ref.node.address);
        bool changed = it != g_previous_transforms.end() && it->second.ref == n.ref && std::memcmp(it->second.matrix.data(), m, sizeof m) != 0;
        NodeTransform previous{n.ref, {}};
        std::memcpy(previous.matrix.data(), m, sizeof m);
        g_previous_transforms[n.ref.node.address] = previous;
        if (!changed || moving++ > 10) continue;
        auto chain = ancestors_locked(n.ref);
        bool under_q = std::any_of(chain.begin(), chain.end(), [&](NodeRef ancestor) {
            return std::find(quarantine.begin(), quarantine.end(), ancestor) != quarantine.end();
        });
        float d = std::sqrt((m[12] - p.x) * (m[12] - p.x) + (m[14] - p.z) * (m[14] - p.z));
        LOGI("[lights] moving node %08x generation=%llu kind=%d underq=%d shown=%d at %.2f %.2f %.2f (%.1f m) x %.2f %.2f %.2f y %.2f %.2f %.2f z %.2f %.2f %.2f",
             n.ref.node.address, (unsigned long long)n.ref.node.generation, n.kind, under_q, node_shown(n.ref), m[12], m[13], m[14], d,
             m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]);
    }
}

// ---- ceiling strip lights
// The small strip lights along the corridors exist twice in a level: the normal (white) ones in the level
// mesh, and a red copy per section that the game fades in during a lockdown (GameWorld's "quarantine
// nodes", +0x228: one m3g::Group per section holding one Mesh with all of that section's light quads).
// The fixtures' positions are read from those meshes once per level; each becomes a small point light
// whose colour follows the lockdown level (GameWorld +0x28c, 0 = normal .. 1 = lockdown).
struct Fixture {
    float p[3];
    float n[3] = {0, 0, 0};  // the lamp's plane normal (either side), or zero if unknown
    float w = 1.0f;          // brightness (lamps merged into this one)
};
std::vector<Fixture> g_fixtures;
bool g_fixtures_ready = false;
std::vector<NodeRef> g_fixture_sources;

void collect_fixtures(NodeRef ref, int depth, std::vector<Fixture>& fixtures, std::vector<NodeRef>& sources) {
    if (depth > 6) return;
    std::vector<NodeRef> children;
    std::vector<std::array<float, 3>> vertices;
    {
        std::lock_guard lock(g_scene_mutex);
        int kind = node_kind_locked(ref);
        if (kind < 0) return;
        sources.push_back(ref);
        if (group_kind(kind)) children = children_locked(ref, 64);
        else {
            if (!mesh_kind(kind) || !mapped(ref.node.address, 0x164)) return;
            // Copy positions before the native call; retain no raw buffer/data pointer.
            u32 vb = mem::r32(ref.node.address + 0x160);
            u32 va = mapped(vb, 0x58) ? mem::r32(vb + 0x44) : 0;
            if (!mapped(va, 0x58)) return;
            u32 count = mem::r32(va + 0x3c), comps = mem::r32(va + 0x40), type = mem::r32(va + 0x44);
            u32 data = mem::r32(va + 0x48), stride = mem::r32(va + 0x54);
            if (!count || count > 20000 || comps != 3 || type != 2 || stride < 6 || stride > 64 || !mapped(data, count * stride)) return;
            float scale = f32(vb + 0x48), bias[3] = {f32(vb + 0x4c), f32(vb + 0x50), f32(vb + 0x54)};
            vertices.resize(count);
            for (u32 i = 0; i < count; i++)
                for (u32 k = 0; k < 3; k++) vertices[i][k] = float(int16_t(mem::r16(data + i * stride + k * 2))) * scale + bias[k];
        }
    }
    for (NodeRef child : children) collect_fixtures(child, depth + 1, fixtures, sources);
    if (vertices.empty()) return;
    float m[16];
    if (!node_world(ref, m)) return;
    // quads that belong to one fixture lie close together: group vertices within 0.9 m
    struct Cluster {
        float sum[3];
        int n;
        float v[8][3];
        int nv;
    };
    std::vector<Cluster> cl;
    for (const auto& o : vertices) {
        float w[3];
        for (int r = 0; r < 3; r++) w[r] = m[r] * o[0] + m[4 + r] * o[1] + m[8 + r] * o[2] + m[12 + r];
        Cluster* hit = nullptr;
        for (Cluster& c : cl) {
            float dx = c.sum[0] / float(c.n) - w[0], dy = c.sum[1] / float(c.n) - w[1], dz = c.sum[2] / float(c.n) - w[2];
            if (dx * dx + dy * dy + dz * dz < 0.9f * 0.9f) {
                hit = &c;
                break;
            }
        }
        if (!hit) {
            cl.push_back({{0, 0, 0}, 0, {}, 0});
            hit = &cl.back();
        }
        for (int r = 0; r < 3; r++) hit->sum[r] += w[r];
        if (hit->nv < 8) {
            for (int r = 0; r < 3; r++) hit->v[hit->nv][r] = w[r];
            hit->nv++;
        }
        hit->n++;
    }
    for (const Cluster& c : cl) {
        if (fixtures.size() >= 4000) break;
        Fixture f{{c.sum[0] / float(c.n), c.sum[1] / float(c.n), c.sum[2] / float(c.n)}};
        // the lamp's plane: the largest cross product among its first vertices
        float best = 0;
        for (int i = 1; i < c.nv; i++)
            for (int j = i + 1; j < c.nv; j++) {
                float a[3], b[3];
                for (int r = 0; r < 3; r++) a[r] = c.v[i][r] - c.v[0][r], b[r] = c.v[j][r] - c.v[0][r];
                float x[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
                float l = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
                if (l > best) best = l, f.n[0] = x[0], f.n[1] = x[1], f.n[2] = x[2];
            }
        if (best > 1e-6f)
            for (float& x : f.n) x /= best;
        fixtures.push_back(f);
    }
}

void update_fixtures(u32 world) {
    Token owner;
    {
        std::lock_guard lock(g_scene_mutex);
        if (!select_world_locked(world) || g_fixtures_ready) return;
        owner = g_scene_world;
    }
    auto roots = quarantine_nodes();
    std::vector<Fixture> fixtures;
    std::vector<NodeRef> sources;
    for (NodeRef ref : roots) collect_fixtures(ref, 0, fixtures, sources);
    // lamps a hand apart (the two rows of one strip) become one light: fewer, steadier lights
    size_t before = fixtures.size();
    for (size_t i = 0; i < fixtures.size(); i++)
        for (size_t j = i + 1; j < fixtures.size();) {
            Fixture &a = fixtures[i], &b = fixtures[j];
            float dx = a.p[0] - b.p[0], dy = a.p[1] - b.p[1], dz = a.p[2] - b.p[2];
            if (dx * dx + dy * dy + dz * dz < 0.8f * 0.8f) {
                for (int r = 0; r < 3; r++) a.p[r] = (a.p[r] * a.w + b.p[r]) / (a.w + 1.0f);
                a.w += 1.0f;
                fixtures.erase(fixtures.begin() + long(j));
            } else {
                j++;
            }
        }
    std::lock_guard lock(g_scene_mutex);
    if (g_scene_world != owner || !g_lifetimes.worlds.contains(owner)) return;
    if (std::any_of(sources.begin(), sources.end(), [](NodeRef ref) { return node_kind_locked(ref) < 0; })) return;
    g_fixtures = std::move(fixtures);
    g_fixture_sources = std::move(sources);
    g_fixtures_ready = true;
    LOGI("lights: %zu lamps merged into %zu lights", before, g_fixtures.size());
    LOGI("lights: %zu strip light fixtures in %zu lockdown sections", g_fixtures.size(), roots.size());
}

// ---- the RIG on the player's back
// The spine bar shows health: GameObjectPlayable::setDisplayedHealth picks one of three textures
// (above 50% / 25-50% / below 25% of maximum) and slides it down as health drops. The disc beside it is
// the stasis meter. Both become small lights that sit just behind the back and follow those values.
u32 g_slash_until = 0, g_slash_flash = 0;  // plasma saw: glow while the swing plays, flash when it connects
int n_nav_log = 0;
float g_stasis = -1;  // last GameObjectPlayable::setStasis value (-1: not seen yet = full)
u32 fn_health_max = 0, fn_stasis_max = 0, fn_is_tk = 0;

float call_f(u32 fn, u32 obj) {
    u32 v = cpu::call(fn, {obj});
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

void on_set_stasis(cpu::Ctx& c) {
    u32 v = c.reg(1);
    std::memcpy(&g_stasis, &v, 4);
}

void on_start_slash(cpu::Ctx&) { g_slash_until = SDL_GetTicks() + 900; }
void on_do_slash(cpu::Ctx&) {
    g_slash_flash = SDL_GetTicks();
    g_slash_until = std::max(g_slash_until, g_slash_flash + 300);
}

void on_player_post_camera(cpu::Ctx& c) {
    u32 pl = c.reg(0);
    if (!pl || !fn_health_max) return;
    if (input::world()) update_fixtures(input::world());
    objs::Vec3 p = objs::pos(pl), f = objs::facing(pl);
    if (!std::isfinite(p.x + p.y + p.z + f.x + f.z)) return;
    float hmax = call_f(fn_health_max, pl), health = f32(pl + 0x25c);
    float hf = hmax > 0 ? std::clamp(health / hmax, 0.0f, 1.0f) : 0.0f;
    static const float kHigh[3] = {0.25f, 0.95f, 1.0f}, kMid[3] = {1.0f, 0.8f, 0.2f}, kLow[3] = {1.0f, 0.15f, 0.1f};
    const float* hc = hf <= 0.25f ? kLow : hf <= 0.5f ? kMid : kHigh;
    // spine: runs from the waist to between the shoulders; the lit part ends lower as health drops
    // The glow stays on the spine: two small lights along the lit part of the meter, close to the back,
    // each reaching a hand's width or two (one light a metre wide tinted the whole suit).
    const float back = 0.24f, spine_lo = 0.05f, spine_len = 0.45f;
    float hk = 0.95f;
    const float hcol[3] = {hc[0] * hk, hc[1] * hk, hc[2] * hk};
    if (hf > 0) {
        const float lit = spine_len * hf;
        push(p.x - f.x * back, p.y + spine_lo + lit * 0.28f, p.z - f.z * back, hcol, 0.36f, 150.0f, 1);
        if (hf > 0.35f) push(p.x - f.x * back, p.y + spine_lo + lit * 0.78f, p.z - f.z * back, hcol, 0.36f, 150.0f, 5);
    }

    float smax = fn_stasis_max ? call_f(fn_stasis_max, pl) : 0;
    float sf = smax > 0 ? (g_stasis < 0 ? 1.0f : std::clamp(g_stasis / smax, 0.0f, 1.0f)) : 0.0f;
    bool tk = fn_is_tk && (cpu::call(fn_is_tk, {pl}) & 0xff);
    // stasis meter: dims as it empties; while kinesis is held the module pulses brighter
    float sk = 0.08f + 0.27f * sf;
    if (tk) sk = 0.75f + 0.25f * std::sin(float(SDL_GetTicks()) * 0.012f);
    const float scol[3] = {0.3f * sk, 0.85f * sk, 1.0f * sk};
    const float side = 0.17f;  // to the player's right
    if (smax > 0) push(p.x - f.x * back - f.z * side, p.y + 0.42f, p.z - f.z * back + f.x * side, scol, tk ? 1.2f : 0.3f, 150.0f, 2);

    // helmet visor: a faint glow in front of the face
    static const float kVisor[3] = {0.2f, 0.32f, 0.5f};  // the visor slits are blue-white
    push(p.x + f.x * 0.36f, p.y + 0.72f, p.z + f.z * 0.36f, kVisor, 0.5f, 150.0f, 3);

    u32 now = SDL_GetTicks();
    // plasma saw: the light sits on the glowing blade itself, where the renderer sees it drawn, and takes
    // its colour; no blade on screen, no light
    {
        float gp[3], gc[3];
        if (now < g_slash_until + 300 && render::player_glow(gp, gc)) {
            float k = 0.85f + 0.15f * std::sin(float(now) * 0.05f);  // flicker
            if (now - g_slash_flash < 120) k += 0.6f;
            const float saw[3] = {gc[0] * k, gc[1] * k, gc[2] * k};
            push(gp[0], gp[1], gp[2], saw, 2.4f, 100.0f, 4);
        }
    }

    // navigation line: lights along the path it draws on the floor (LocatorMesh at +0x544, active flag
    // +0xc; the path is the list of PathNodes at [+0x128]+0xc, node position +0x28)
    u32 mesh = mem::r32(pl + 0x544), path = mem::r32(pl + 0x128);
    int nav = 0;
    if (mesh && path && mem::r8(mesh + 0xc)) {
        static const float kNav[3] = {0.16f, 0.5f, 0.6f};
        const float step = 1.6f;
        float prev[3] = {p.x, p.y - 1.0f, p.z}, carry = step * 0.5f;
        u32 anchor = path + 0xc;
        int guard = 0;
        for (u32 it = mem::r32(anchor); it && it != anchor && guard < 64 && nav < 10; it = mem::r32(it), guard++) {
            u32 node = mem::r32(it + 8);
            if (!node) break;
            float q[3] = {f32(node + 0x28), f32(node + 0x2c), f32(node + 0x30)};
            if (!std::isfinite(q[0] + q[1] + q[2])) break;
            if (g_trace && (n_nav_log++ % 600) == 0) LOGI("[lights] nav node %.2f %.2f %.2f (player %.2f %.2f %.2f)", q[0], q[1], q[2], p.x, p.y, p.z);
            float d[3] = {q[0] - prev[0], 0, q[2] - prev[2]};
            float len = std::sqrt(d[0] * d[0] + d[2] * d[2]);
            while (len > 1e-3f && carry <= len && nav < 10) {
                float t = carry / len;
                push(prev[0] + d[0] * t, prev[1] + (q[1] - prev[1]) * t + 0.35f, prev[2] + d[2] * t, kNav, 1.5f, 150.0f, u32(10 + nav));
                nav++;
                carry += step;
            }
            carry -= len;
            prev[0] = q[0], prev[1] = q[1], prev[2] = q[2];
        }
    }
    static int n = 0;
    if (g_trace && (n % 60) == 0) {
        LOGI("[lights] %zu scene nodes", scene_nodes().size());
        trace_nodes(p);
    }
    if (g_trace && (n++ % 60) == 0) LOGI("[lights] nav lights %d, slash %d", nav, now < g_slash_until);
    if (g_trace && (n % 60) == 1) LOGI("[lights] rig: health %.0f/%.0f stasis %.0f/%.0f kinesis %d", health, hmax, g_stasis, smax, tk);
}

void clear_scene_locked() {
    g_scene_world = {};
    g_nodes.clear();
    g_previous_transforms.clear();
    g_fade_nodes.clear();
    g_fade_state.clear();
    g_fog_data.clear();
    g_fixtures.clear();
    g_fixture_sources.clear();
    g_fixtures_ready = false;
    g_fade_tick = 0;
    g_rejected = g_write_budget = 0;
}

void retire_node_locked(u32 address) {
    std::erase_if(g_nodes, [&](const SceneNode& n) { return n.ref.node.address == address; });
    std::erase_if(g_fade_nodes, [&](NodeRef n) { return n.node.address == address; });
    g_fade_state.erase(address);
    g_previous_transforms.erase(address);
    if (std::any_of(g_fixture_sources.begin(), g_fixture_sources.end(), [&](NodeRef n) { return n.node.address == address; })) {
        g_fixtures.clear();
        g_fixture_sources.clear();
        g_fixtures_ready = false;
    }
    std::erase_if(g_fog_data, [&](const FogData& f) { return f.ref.node.address == address; });
    g_lifetimes.nodes.retire(address);
}

void on_world_constructed(cpu::Ctx& c) {
    std::lock_guard lock(g_scene_mutex);
    if (g_scene_world.address == c.reg(0)) clear_scene_locked();
    Token token = g_lifetimes.worlds.begin(c.reg(0));
    g_teardown_world = 0;
    g_teardown_budget = 0;
    g_bad_header_budget = 0;
    LOGI("[scene-cache] world constructed %08x generation=%llu", token.address, (unsigned long long)token.generation);
    if(perf::enabled())perf::event("world-constructed",std::to_string(token.generation));
}

void on_world_destroyed(cpu::Ctx& c) {
    std::lock_guard lock(g_scene_mutex);
    Token token = g_lifetimes.worlds.current(c.reg(0));
    g_teardown_world = c.reg(0);
    g_teardown_budget = 0;
    LOGI("[scene-cache] world retiring %08x generation=%llu cached-nodes=%zu fade-roots=%zu fade-states=%zu fixtures=%zu",
         token.address, (unsigned long long)token.generation, g_nodes.size(), g_fade_nodes.size(), g_fade_state.size(), g_fixtures.size());
    if (g_scene_world == token) clear_scene_locked();
    if(perf::enabled())perf::event("world-retiring",std::to_string(token.generation));
    g_lifetimes.worlds.retire(c.reg(0));
}

void on_node_constructed(cpu::Ctx& c) {
    std::lock_guard lock(g_scene_mutex);
    retire_node_locked(c.reg(0));  // also discard an old observation if an address is reused
    g_lifetimes.nodes.begin(c.reg(0), false);
}

void on_node_initialized(cpu::Ctx& c) {
    std::lock_guard lock(g_scene_mutex);
    g_lifetimes.nodes.publish(c.reg(4));  // common Node constructor return: r4=this
}

void on_node_destroyed(cpu::Ctx& c) {
    std::lock_guard lock(g_scene_mutex);
    retire_node_locked(c.reg(0));  // before the derived destructor releases children/buffers
}

void on_object_destroyed(cpu::Ctx& c) {
    std::lock_guard lock(g_scene_mutex);
    u32 object = c.reg(0);
    if (g_lifetimes.nodes.current(object)) retire_node_locked(object);
    if (!mapped(object, 0x3c)) return;
    u32 tracks = mem::r32(object + 8), tracks_end = mem::r32(object + 0xc), tracks_cap = mem::r32(object + 0x10);
    u32 data = mem::r32(object + 0x24), end = mem::r32(object + 0x28), cap = mem::r32(object + 0x2c);
    // The string storage uses two-byte elements (including the shared empty
    // string's begin==end, capacity==begin+2), not pointer-vector alignment.
    bool sane = scene_cache::vector_layout(tracks, tracks_end, tracks_cap, 4) && scene_cache::vector_layout(data, end, cap, 2);
    if (!sane && g_bad_header_budget++ < 8)
        LOGW("[scene-cache] INVALID Object3D header object=%08x tracks=%08x/%08x/%08x storage=%08x/%08x/%08x teardown-world=%08x; native cleanup continues",
             object, tracks, tracks_end, tracks_cap, data, end, cap, g_teardown_world);
    // Read-only header evidence, never a reason to skip native destruction/free.
    if ((!sane || g_trace || g_teardown_world) && g_teardown_budget++ < 32)
        LOGI("[scene-cache] %s object=%08x vtable=%08x tracks=%08x/%08x/%08x storage=%08x/%08x/%08x headers=%s teardown-world=%08x",
             cpu::current_hle_name(c).c_str(), object, mem::r32(object), tracks, tracks_end, tracks_cap,
             data, end, cap, sane ? "ordered" : "INVALID", g_teardown_world);
}

void on_model_unload(cpu::Ctx& c) {
    std::lock_guard lock(g_scene_mutex);
    if ((g_trace || g_teardown_world) && g_teardown_budget++ < 32)
        LOGI("[scene-cache] model unload=%08x teardown-world=%08x", c.reg(0), g_teardown_world);
}

bool install_scene_lifetimes() {
    const struct {
        const char* symbol;
        u32 image_offset, offset, first, second;
        cpu::HleFn handler;
    } hooks[] = {
        {"_ZN9GameWorldC1EP15btDynamicsWorld", 0x22b7a4, 0, 0xe92d45f0, 0xed2d8b02, on_world_constructed},
        {"_ZN9GameWorldD1Ev", 0x22518c, 0xc, 0xe2833008, 0xe1a04000, on_world_destroyed},
        {"_ZN3m3g4NodeC1Ev", 0x367724, 0, 0xe92d4030, 0xe24dd00c, on_node_constructed},
        {"_ZN3m3g4NodeC2Ev", 0x367818, 0, 0xe92d4030, 0xe24dd00c, on_node_constructed},
        {"_ZN3m3g4NodeC1Ev", 0x367724, 0x58, 0xe1a00004, 0xe28dd00c, on_node_initialized},
        {"_ZN3m3g4NodeC2Ev", 0x367818, 0x58, 0xe1a00004, 0xe28dd00c, on_node_initialized},
        {"_ZN3m3g4NodeD1Ev", 0x367fc8, 8, 0xe5905138, 0xe1a04000, on_node_destroyed},
        {"_ZN3m3g4NodeD2Ev", 0x368028, 8, 0xe5905138, 0xe1a04000, on_node_destroyed},
        {"_ZN3m3g4NodeD0Ev", 0x368088, 8, 0xe5905138, 0xe1a04000, on_node_destroyed},
        {"_ZN3m3g5GroupD1Ev", 0x3624cc, 8, 0xe590413c, 0xe5905140, on_node_destroyed},
        {"_ZN3m3g5GroupD2Ev", 0x362400, 8, 0xe590413c, 0xe5905140, on_node_destroyed},
        {"_ZN3m3g5GroupD0Ev", 0x36290c, 8, 0xe590413c, 0xe5905140, on_node_destroyed},
        {"_ZN3m3g4MeshD1Ev", 0x366280, 8, 0xe1a06000, 0xe5900160, on_node_destroyed},
        {"_ZN3m3g4MeshD2Ev", 0x3663f4, 8, 0xe1a06000, 0xe5900160, on_node_destroyed},
        {"_ZN3m3g11SkinnedMeshD1Ev", 0x369918, 8, 0xe1a05000, 0xe3a07000, on_node_destroyed},
        {"_ZN9particles14ParticleEffectD1Ev", 0x32d0f4, 8, 0xe5902178, 0xe1a04000, on_node_destroyed},
        {"_ZN3m3g6CameraD1Ev", 0x361b7c, 0x18, 0xe1a04000, 0xe3a01000, on_node_destroyed},
        {"_ZN3m3g6CameraD0Ev", 0x361d64, 0x18, 0xe1a04000, 0xe3a01000, on_node_destroyed},
        {"_ZN3m3g5WorldD1Ev", 0x36f254, 8, 0xe1a04000, 0xe590014c, on_node_destroyed},
        {"_ZN3m3g5WorldD0Ev", 0x36f334, 8, 0xe1a04000, 0xe590014c, on_node_destroyed},
        {"_ZN3m3g8Object3DD1Ev", 0x369170, 8, 0xe5905038, 0xe1a04000, on_object_destroyed},
        {"_ZN3m3g8Object3DD2Ev", 0x369238, 8, 0xe5905038, 0xe1a04000, on_object_destroyed},
        {"_ZN3m3g11IndexBufferD1Ev", 0x3649dc, 8, 0xe1a04000, 0xe590006c, on_object_destroyed},
        {"_ZN3m3g11IndexBufferD0Ev", 0x364bfc, 8, 0xe1a04000, 0xe590006c, on_object_destroyed},
        {"_ZN5Model6unloadEv", 0x5e520, 0, 0xe92d4030, 0xe5903004, on_model_unload},
    };
    // Validate the complete set before patching anything. Hooks placed after
    // PC-relative prologues still see r0=this and precede native cleanup.
    for (const auto& hook : hooks) {
        u32 fn = elf::find(hook.symbol);
        if (fn != mem::ELF_BASE + hook.image_offset || mem::r32(fn + hook.offset) != hook.first || mem::r32(fn + hook.offset + 4) != hook.second) {
            LOGE("[scene-cache] unsupported lifecycle site %s; custom scene caches unavailable", hook.symbol);
            return false;
        }
    }
    fn_retain = elf::find("_ZNK4midp16ReferenceCounted7_incRefEv");
    fn_release = elf::find("_ZN4midp6DECREFEPKNS_16ReferenceCountedE");
    if (fn_retain != mem::ELF_BASE + 0x36f5f4 || mem::r32(fn_retain) != 0xe5903004 || mem::r32(fn_retain + 4) != 0xe2833001 ||
        mem::r32(fn_retain + 8) != 0xe5803004 || mem::r32(fn_retain + 12) != 0xe12fff1e || fn_release != mem::ELF_BASE + 0x3da64) return false;
    const char* types[] = {"_ZTVN3m3g4NodeE", "_ZTVN3m3g6CameraE", "_ZTVN3m3g5GroupE", "_ZTVN3m3g5WorldE", "_ZTVN3m3g4MeshE", "_ZTVN3m3g11SkinnedMeshE", "_ZTVN9particles14ParticleEffectE"};
    for (size_t i = 0; i < g_node_vtables.size(); i++) {
        u32 vt = elf::find(types[i]);
        if (!vt) return false;
        g_node_vtables[i] = vt + 8;
    }
    for (const auto& hook : hooks)
        if (!cpu::hook_entry(mem::ELF_BASE + hook.image_offset + hook.offset, hook.symbol, hook.handler)) return false;
    g_lifetime_ready = true;
    LOGI("[scene-cache] native world/node generations and pre-destruction retirement installed");
    return true;
}

}  // namespace

void set_trace(bool on) { g_trace = on ? 1 : 0; }

void add(float x, float y, float z, float r, float g, float b, float radius, float life_ms) {
    const float c[3] = {r, g, b};
    push(x, y, z, c, radius, life_ms);
}

void eye_space(std::vector<render::Light>& out) {
    u32 now = SDL_GetTicks();
    g_src.erase(std::remove_if(g_src.begin(), g_src.end(), [&](const Src& s) { return float(now - s.born_ms) >= s.life_ms; }),
                g_src.end());
    float e[3] = {}, t[3] = {}, up[3] = {};
    bool cam = input::camera(e, t, up);
    static int n = 0;
    if (g_trace && !g_src.empty() && (n++ % 30) == 0)
        LOGI("[lights] %zu sources, camera %s eye %.2f %.2f %.2f target %.2f %.2f %.2f up %.2f %.2f %.2f; first at %.2f %.2f %.2f",
             g_src.size(), cam ? "ok" : "stale", e[0], e[1], e[2], t[0], t[1], t[2], up[0], up[1], up[2], g_src[0].p[0], g_src[0].p[1], g_src[0].p[2]);
    if (!cam) return;
    std::vector<Src> fixed;  // (the fixtures go to the renderer at the start of the frame: fixtures_world)
    if (g_src.empty() && fixed.empty()) return;
    // right-handed look-at: the camera looks down -Z
    float f[3] = {t[0] - e[0], t[1] - e[1], t[2] - e[2]};
    float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl < 1e-6f) return;
    for (float& v : f) v /= fl;
    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (sl < 1e-6f) return;
    for (float& v : s) v /= sl;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    // newest first: the renderer keeps a limited number
    for (auto it = g_src.rbegin(); it != g_src.rend(); ++it) {
        float d[3] = {it->p[0] - e[0], it->p[1] - e[1], it->p[2] - e[2]};
        float k = 1.0f - float(now - it->born_ms) / it->life_ms;
        render::Light l;
        l.pos[0] = s[0] * d[0] + s[1] * d[1] + s[2] * d[2];
        l.pos[1] = u[0] * d[0] + u[1] * d[1] + u[2] * d[2];
        l.pos[2] = -(f[0] * d[0] + f[1] * d[1] + f[2] * d[2]);
        l.radius = it->radius;
        for (int i = 0; i < 3; i++) l.color[i] = it->c[i] * k;
        out.push_back(l);
    }
    // the strip lights last: they are the first to go when there are too many lights
    for (const Src& fx : fixed) {
        float d[3] = {fx.p[0] - e[0], fx.p[1] - e[1], fx.p[2] - e[2]};
        render::Light l;
        l.pos[0] = s[0] * d[0] + s[1] * d[1] + s[2] * d[2];
        l.pos[1] = u[0] * d[0] + u[1] * d[1] + u[2] * d[2];
        l.pos[2] = -(f[0] * d[0] + f[1] * d[1] + f[2] * d[2]);
        l.radius = fx.radius;
        for (int i = 0; i < 3; i++) l.color[i] = fx.c[i];
        out.push_back(l);
    }
}

std::string debug_fade_nodes(float x, float y, float z) {
    std::vector<NodeRef> roots;
    {
        std::lock_guard lock(g_scene_mutex);
        roots = g_fade_nodes;
    }
    std::string out = std::to_string(roots.size()) + " proximity-fade nodes";
    struct E {
        float d;
        std::string s;
    };
    std::vector<E> v;
    for (NodeRef ref : roots) {
        float m[16];
        if (!node_world(ref, m)) continue;
        std::lock_guard lock(g_scene_mutex);
        int kind = node_kind_locked(ref);
        if (kind < 0) continue;
        u32 n = ref.node.address;
        float d = std::sqrt((m[12] - x) * (m[12] - x) + (m[13] - y) * (m[13] - y) + (m[14] - z) * (m[14] - z));
        char b[160];
        std::snprintf(b, sizeof b, " | %08x %s at %.2f %.2f %.2f (%.1f m) enabled %d alpha %08x shown %d", n, cpu::symbolize(mem::r32(n)).c_str(), m[12], m[13], m[14], d,
                       mem::r8(n + 0x12c), mem::r32(n + 0x130), node_shown(ref));
        std::string line = b;
        if (group_kind(kind)) {
            for (NodeRef child : children_locked(ref, 6)) {
                u32 ch = child.node.address;
                char cbuf[96];
                std::snprintf(cbuf, sizeof cbuf, " [child %08x %s en %d a %08x]", ch, cpu::symbolize(mem::r32(ch)).c_str(), mem::r8(ch + 0x12c), mem::r32(ch + 0x130));
                line += cbuf;
            }
        }
        v.push_back({d, line});
    }
    std::sort(v.begin(), v.end(), [](const E& a, const E& b) { return a.d < b.d; });
    for (size_t i = 0; i < v.size() && i < 6; i++) out += v[i].s;
    return out;
}

int debug_show_nodes(float x, float z, float radius) {
    int n = 0;
    for (const SceneNode& sn : scene_nodes()) {
        float m[16];
        if (!node_world(sn.ref, m)) continue;
        if (std::fabs(m[12] - x) > radius || std::fabs(m[14] - z) > radius) continue;
        std::lock_guard lock(g_scene_mutex);
        auto chain = ancestors_locked(sn.ref);
        for (size_t i = 0; i < chain.size(); i++) {
            u32 node = chain[i].node.address;
            if (!mem::r8(node + 0x12c)) {
                LOGI("[lights] showing node %08x (level %zu above %08x, kind %d)", node, i, sn.ref.node.address, sn.kind);
                mem::w8(node + 0x12c, 1);
                n++;
            }
        }
    }
    return n;
}

float lockdown_level() {
    std::lock_guard lock(g_scene_mutex);
    u32 world = input::world();
    if (!world || g_lifetimes.worlds.current(world) != g_scene_world || !g_scene_world || !mapped(world, 0x290)) return 0;
    float level = f32(world + 0x28c);
    return std::isfinite(level) ? std::clamp(level, 0.0f, 1.0f) : 0.0f;
}

void fixtures_world(const float at[3], std::vector<render::Light>& out) {
    std::lock_guard lock(g_scene_mutex);
    u32 world = input::world();
    if (!world || !g_fixtures_ready || !g_scene_world || g_lifetimes.worlds.current(world) != g_scene_world) return;
    float level = lockdown_level();
    static const float kNormal[3] = {0.34f, 0.38f, 0.42f}, kLockdown[3] = {0.95f, 0.10f, 0.07f};
    const float kRange = 34.0f;  // the camera's far plane is at 30 m
    size_t first = out.size();
    for (const Fixture& f : g_fixtures) {
        float dx = f.p[0] - at[0], dy = f.p[1] - at[1], dz = f.p[2] - at[2], d2 = dx * dx + dy * dy + dz * dz;
        if (d2 >= kRange * kRange) continue;
        float k = std::clamp((kRange - std::sqrt(d2)) / 2.0f, 0.0f, 1.0f) * std::min(1.6f, 0.75f + 0.25f * f.w);
        render::Light l;
        for (int i = 0; i < 3; i++) {
            l.wpos[i] = f.p[i];
            l.wdir[i] = f.n[i];
            l.color[i] = (kNormal[i] + (kLockdown[i] - kNormal[i]) * level) * k;
        }
        l.radius = 3.2f;
        out.push_back(l);
    }
    std::sort(out.begin() + long(first), out.end(), [&](const render::Light& a, const render::Light& b) {
        auto d = [&](const render::Light& l) {
            return (l.wpos[0] - at[0]) * (l.wpos[0] - at[0]) + (l.wpos[1] - at[1]) * (l.wpos[1] - at[1]) + (l.wpos[2] - at[2]) * (l.wpos[2] - at[2]);
        };
        return d(a) < d(b);
    });
}

void debug_list(std::vector<DebugLight>& out) {
    u32 now = SDL_GetTicks();
    for (const Src& s : g_src) {
        if (float(now - s.born_ms) >= s.life_ms) continue;
        DebugLight d;
        for (int i = 0; i < 3; i++) d.pos[i] = s.p[i], d.color[i] = s.c[i];
        d.radius = s.radius;
        d.kind = s.id == 1 || s.id == 5 ? "RIG HEALTH" : s.id == 2 ? "STASIS MODULE" : s.id == 3 ? "VISOR" : s.id == 4 ? "SAW" : s.id >= 10 && s.id < 20 ? "NAV LINE" : s.id ? "PROJECTILE" : "FLASH";
        out.push_back(d);
    }
    // lit this frame by the renderer: alarm spotlights and the nearest strip lights
    const std::vector<render::Light>& lit = render::debug_frame_lights();
    for (const render::Light& l : lit) {
        DebugLight d;
        for (int i = 0; i < 3; i++) d.pos[i] = l.wpos[i], d.color[i] = l.color[i], d.dir[i] = l.wdir[i];
        d.radius = l.radius;
        d.spot = l.cos_outer > 0;
        d.cos_outer = l.cos_outer;
        d.tile = l.tile;
        d.kind = d.spot ? (l.gobo > 0 ? "FAN LIGHT" : "ALARM SPOT") : l.glow ? "PANEL GLOW" : "STRIP LIGHT";
        out.push_back(d);
    }
    // every other strip light fixture of the level (not lit: too far from the camera, or over the limit)
    std::lock_guard lock(g_scene_mutex);
    u32 world = input::world();
    if (world && g_scene_world && g_fixtures_ready && g_lifetimes.worlds.current(world) == g_scene_world && mapped(world, 0x290)) {
        float level = std::clamp(f32(world + 0x28c), 0.0f, 1.0f);
        for (const Fixture& f : g_fixtures) {
            bool is_lit = false;
            for (const render::Light& l : lit)
                if (l.cos_outer <= 0 && !l.glow && std::fabs(l.wpos[0] - f.p[0]) < 0.01f && std::fabs(l.wpos[1] - f.p[1]) < 0.01f && std::fabs(l.wpos[2] - f.p[2]) < 0.01f) is_lit = true;
            if (is_lit) continue;
            DebugLight d;
            for (int i = 0; i < 3; i++) d.pos[i] = f.p[i];
            d.color[0] = 0.34f + 0.61f * level, d.color[1] = 0.38f - 0.28f * level, d.color[2] = 0.42f - 0.35f * level;
            d.radius = 3.2f;
            d.active = false;
            d.kind = "STRIP LIGHT";
            out.push_back(d);
        }
    }
}

bool camera_matrix(float view[16], float target[3]) {
    float e[3], t[3], up[3];
    if (!input::camera(e, t, up)) return false;
    float f[3] = {t[0] - e[0], t[1] - e[1], t[2] - e[2]};
    float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl < 1e-6f) return false;
    for (float& v : f) v /= fl;
    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (sl < 1e-6f) return false;
    for (float& v : s) v /= sl;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    for (int c = 0; c < 3; c++) {
        view[c * 4] = s[c], view[c * 4 + 1] = u[c], view[c * 4 + 2] = -f[c], view[c * 4 + 3] = 0;
    }
    view[12] = -(s[0] * e[0] + s[1] * e[1] + s[2] * e[2]);
    view[13] = -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]);
    view[14] = f[0] * e[0] + f[1] * e[1] + f[2] * e[2];
    view[15] = 1;
    std::memcpy(target, t, 12);
    return true;
}

void install() {
    if (!install_scene_lifetimes()) LOGE("[scene-cache] lifetime tracking unavailable; no custom cached-node access will run");
    fn_transform_to = elf::find("_ZNK3m3g4Node14getTransformToEPKS0_PNS_9TransformE");
    fn_health_max = elf::find("_ZNK18GameObjectPlayable12getHealthMaxEv");
    fn_stasis_max = elf::find("_ZNK18GameObjectPlayable12getStasisMaxEv");
    fn_is_tk = elf::find("_ZNK18GameObjectPlayable16isTelekinesisingEv");
    const struct {
        const char* sym;
        const char* name;
        cpu::HleFn fn;
    } hooks[] = {
        {"_ZN18WeaponPlasmaCutter6onFireEv", "WeaponPlasmaCutter::onFire", on_fire},
        {"_ZN12WeaponRipper6onFireEv", "WeaponRipper::onFire", on_fire},
        {"_ZN13WeaponLineGun6onFireEv", "WeaponLineGun::onFire", on_fire},
        {"_ZN11WeaponCorer6onFireEv", "WeaponCorer::onFire", on_fire},
        {"_ZN12WeaponStasis6onFireEv", "WeaponStasis::onFire", on_fire},
        {"_ZN6Weapon13resolveImpactEPvRK9btVector3S3_S3_ii", "Weapon::resolveImpact", on_impact},
        {"_ZN20GameObjectProjectile8onUpdateEi", "GameObjectProjectile::onUpdate", on_projectile},
        {"_ZN19GameObjectExplosive13reallyExplodeEv", "GameObjectExplosive::reallyExplode", on_explode},
        {"_ZN9GameWorld26registerNodeDirectionFlareEPK8CullNodePN3m3g4NodeEa", "GameWorld::registerNodeDirectionFlare", on_reg_dir_flare},
        {"_ZN9GameWorld21registerProximityFadeEPN3m3g4NodeE", "GameWorld::registerProximityFade", on_reg_fade},
        {"_ZN9GameWorld6renderERKN5eastl13intrusive_ptrIN3m3g8RendererEEE", "GameWorld::render", smooth_fades},
        {"_ZN9GameWorld21registerNodeSideFlareEPN3m3g4NodeE", "GameWorld::registerNodeSideFlare", on_reg_side_flare},
        {"_ZN9GameWorld19registerNodeAnimateEPK8CullNodePN3m3g4NodeE", "GameWorld::registerNodeAnimate", on_reg_animate},
        {"_ZN18GameObjectPlayable10startSlashEv", "GameObjectPlayable::startSlash", on_start_slash},
        {"_ZN18GameObjectPlayable7doSlashEv", "GameObjectPlayable::doSlash", on_do_slash},
        {"_ZN18GameObjectPlayable9setStasisEf", "GameObjectPlayable::setStasis", on_set_stasis},
        {"_ZN18GameObjectPlayable18onUpdatePostCameraEi", "GameObjectPlayable::onUpdatePostCamera", on_player_post_camera},
    };
    for (auto& h : hooks) {
        u32 a = elf::find(h.sym);
        if (!a || !cpu::hook_entry(a, h.name, h.fn)) LOGW("lights: cannot hook %s", h.name);
    }
}

bool is_fog_vertices(u32 guest_addr) {
    std::lock_guard lock(g_scene_mutex);
    return g_lifetimes.worlds.contains(g_scene_world) &&
           std::any_of(g_fog_data.begin(), g_fog_data.end(), [&](const FogData& f) { return f.data == guest_addr && node_kind_locked(f.ref) >= 0; });
}

void performance_snapshot(perf::Metrics& out) {
    std::lock_guard lock(g_scene_mutex);
    out.insert(out.end(), {{"cached_nodes",g_nodes.size()}, {"previous_transforms",g_previous_transforms.size()},
        {"fade_nodes",g_fade_nodes.size()}, {"fade_states",g_fade_state.size()}, {"fog_observations",g_fog_data.size()},
        {"fixtures",g_fixtures.size()}, {"fixture_sources",g_fixture_sources.size()}});
}
}  // namespace lights
