// Runs the actual host lights/cache handlers against synthetic memory and native
// call stand-ins. No game binary/instructions/assets, SDL, GL, JIT or gameplay.
#include <cstdarg>
#include <future>
#include <iostream>
#include <stdexcept>
#include "../src/lights.cpp"

namespace {
std::vector<u8> bytes(1 << 20);
std::array<bool, 256> pages;
u32 ticks = 1000, current_world = 0;
struct Registers {
    u32 values[16]{};
    cpu::Ctx context{reinterpret_cast<cpu::Cpu*>(this)};
    Registers(u32 r0 = 0, u32 r1 = 0) { values[0] = r0; values[1] = r1; }
};
unsigned retains = 0, releases = 0, transforms = 0;
bool retire_during_transform = false;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool unlocked_on_another_thread() {
    return std::async(std::launch::async, [] {
        bool acquired = lights::g_scene_mutex.try_lock();
        if (acquired) lights::g_scene_mutex.unlock();
        return acquired;
    }).get();
}
}

namespace mem {
u8* g_base = bytes.data();
std::array<u8*, (1u << 20)>* g_page_table = nullptr;
bool is_mapped(u32 address) { return address / PAGE_SIZE < pages.size() && pages[address / PAGE_SIZE]; }
u32 data_alloc(u32 size, u32) {
    static u32 next = 0xc0000;
    u32 address = next;
    next += size;
    require(next < bytes.size(), "synthetic scratch exhausted");
    return address;
}
}
namespace cpu {
u32 Ctx::reg(int i) const { return reinterpret_cast<const Registers*>(c)->values[i]; }
std::string current_hle_name(Ctx&) { return "synthetic destructor"; }
std::string symbolize(u32) { return "synthetic node type"; }
bool hook_entry(u32, const std::string&, HleFn) { throw std::runtime_error("test must not install executable instructions"); }
u32 call(u32 function, std::initializer_list<u32> arguments) {
    auto it = arguments.begin();
    u32 object = *it;
    if (function == 1) {
        ++retains;
        mem::w32(object + 4, mem::r32(object + 4) + 1);
    } else if (function == 2) {
        require(unlocked_on_another_thread(), "registry lock held across native release");
        ++releases;
        mem::w32(object + 4, mem::r32(object + 4) - 1);
    } else if (function == 3) {
        require(unlocked_on_another_thread(), "registry lock held across native transform");
        ++transforms;
        ++it;
        ++it;
        u32 xf = *it;
        for (u32 i = 0; i < 16; i++) {
            float f = i % 5 == 0 ? 1.0f : 0.0f;
            u32 bits;
            std::memcpy(&bits, &f, 4);
            mem::w32(xf + 8 + i * 4, bits);
        }
        if (retire_during_transform) {
            Registers regs(current_world);
            lights::on_world_destroyed(regs.context);
        }
        return 1;
    } else throw std::runtime_error("unexpected native call in synthetic test");
    return 0;
}
}
namespace elf {
u32 find(const std::string&) { return 0; }
}
namespace logging {
void write(char, const char*, ...) {}
}
uint32_t SDL_GetTicks() { return ticks; }
namespace input {
unsigned world() { return current_world; }
unsigned player() { return 0; }
bool camera(float[3], float[3], float[3]) { return false; }
}
namespace objs {
std::string class_name(u32) { return "synthetic object"; }
Vec3 pos(u32) { return {}; }
Vec3 facing(u32) { return {}; }
}
namespace render {
bool player_glow(float[3], float[3]) { return false; }
const std::vector<Light>& debug_frame_lights() { static std::vector<Light> lights; return lights; }
}

namespace {
constexpr u32 world_a = 0x20000, world_b = 0x22000, root_node = 0x30000, child_node = 0x32000;
void reset() {
    std::fill(bytes.begin(), bytes.end(), 0);
    pages.fill(true);
    lights::clear_scene_locked();
    lights::g_lifetimes = {};
    lights::g_lifetime_ready = true;
    for (u32 i = 0; i < lights::g_node_vtables.size(); i++) lights::g_node_vtables[i] = 0x60000 + i * 0x100;
    lights::fn_retain = 1;
    lights::fn_release = 2;
    lights::fn_transform_to = 3;
    retains = releases = transforms = 0;
    retire_during_transform = false;
    ticks = 1000;
    current_world = world_a;
    Registers world(world_a);
    lights::on_world_constructed(world.context);
}
void node(u32 address, int kind = 4, u32 parent = 0, u32 alpha = 0x10000, u8 enabled = 1) {
    Registers regs(address);
    lights::on_node_constructed(regs.context);
    mem::w32(address, lights::g_node_vtables[kind]);
    mem::w32(address + 4, 1);
    mem::w32(address + 0x128, parent);
    mem::w8(address + 0x12c, enabled);
    mem::w32(address + 0x130, alpha);
    regs.values[4] = address;
    lights::on_node_initialized(regs.context);
}
void fade(u32 address) {
    Registers regs(current_world, address);
    lights::on_reg_fade(regs.context);
}
void render() {
    Registers regs(current_world);
    lights::smooth_fades(regs.context);
}
void retire(u32 address) {
    Registers regs(address);
    lights::on_node_destroyed(regs.context);
}
void retired_resources() {
    reset();
    node(root_node);
    fade(root_node);
    auto old = lights::g_fade_nodes.at(0);
    render();
    lights::g_fixtures.push_back({{1,2,3}});
    lights::g_fixture_sources.push_back(old);
    lights::g_fixtures_ready = true;
    lights::g_fog_data.push_back({old, 0x50000});
    retire(root_node);
    require(lights::g_fade_nodes.empty() && lights::g_fade_state.empty() && lights::g_nodes.empty(), "retired node stayed cached");
    require(lights::g_fixtures.empty() && !lights::g_fixtures_ready && !lights::is_fog_vertices(0x50000), "derived cache survived retirement");
    node(root_node, 4, 0, 1234);
    require(!lights::g_lifetimes.contains(old), "recycled node revived old token");
    render();
    require(mem::r32(root_node + 0x130) == 1234 && lights::g_fade_state.empty(), "unregistered replacement was mutated");
    fade(root_node);
    render();
    require(lights::g_fade_state.at(root_node).ref != old, "replacement inherited prior fade state");
}
void world_reuse() {
    reset();
    node(root_node);
    fade(root_node);
    auto old = lights::g_fade_nodes.at(0);
    Registers regs(world_a);
    lights::on_world_destroyed(regs.context);
    require(!lights::g_scene_world && lights::g_fade_nodes.empty(), "world teardown did not retire caches");
    lights::on_world_constructed(regs.context);
    require(!lights::g_lifetimes.contains(old), "same-address world revived a reference");
    mem::w32(root_node + 0x130, 4321);
    render();
    require(mem::r32(root_node + 0x130) == 4321, "old world observation wrote into new world");
    fade(root_node);
    require(lights::g_fade_nodes.at(0).world != old.world, "new registration lacks new world generation");
    Registers next(world_b);
    lights::on_world_constructed(next.context);
    current_world = world_b;
    fade(root_node);
    auto newer = lights::g_fade_nodes.at(0);
    lights::on_world_destroyed(regs.context);
    require(lights::g_lifetimes.contains(newer) && lights::g_scene_world == newer.world, "old-world teardown cleared newer world");
}
void guarded_writes() {
    reset();
    node(root_node, 2);
    node(child_node, 4, root_node);
    mem::w32(root_node + 0x13c, 0x50000);
    mem::w32(root_node + 0x140, 0x50004);
    mem::w32(0x50000, child_node);
    fade(root_node);
    render();
    require(lights::g_fade_state.contains(child_node), "live owned child lost normal fading");
    retire(child_node);
    mem::w32(child_node, 0x10000);  // valid mapped storage, no longer a live Node
    mem::w32(child_node + 0x130, 0x12345678);
    render();
    require(mem::r32(child_node + 0x130) == 0x12345678, "stale child storage was overwritten");
    node(child_node, 4, 0, 4321);  // live Node, but not owned by this group
    render();
    require(mem::r32(child_node + 0x130) == 4321 && !lights::g_fade_state.contains(child_node), "foreign child got a fade write");
    mem::w32(child_node + 0x128, root_node);
    mem::w32(child_node, 0x10000);  // live token alone is insufficient
    render();
    require(mem::r32(child_node + 0x130) == 4321, "invalid vtable passed token-only gate");
    mem::w32(root_node + 0x13c, 0x50001);  // invalid container layout must not be traversed
    render();
    require(mem::r32(child_node + 0x130) == 4321, "malformed child vector was traversed");
}
void normal_fades() {
    reset();
    node(root_node, 4, 0, 0, 0);
    fade(root_node);
    fade(root_node);
    require(lights::g_fade_nodes.size() == 1, "duplicate native registration duplicated a fade");
    render();
    mem::w32(root_node + 0x130, 0x10000);
    mem::w8(root_node + 0x12c, 1);
    ticks += 100;
    render();
    require(mem::r32(root_node + 0x130) > 0 && mem::r32(root_node + 0x130) < 0x10000, "normal fade timing changed or disabled");
    require(mem::r8(root_node + 0x12c) == 1, "visible fading node was hidden");
    mem::w32(root_node + 0x130, 0);
    mem::w8(root_node + 0x12c, 0);
    ticks += 100;
    render();
    require(mem::r32(root_node + 0x130) == 0 && mem::r8(root_node + 0x12c) == 0, "native hide target was ignored");
    node(child_node, 6, 0, 0x10000);  // native ParticleEffect is a Mesh-derived Node
    fade(child_node);
    render();
    require(lights::g_fade_state.contains(child_node), "particle fade was disabled by the native type allowlist");
    lights::g_fog_data.push_back({lights::g_fade_nodes.front(), 0x50000});
    node(0x35000);  // unrelated construction must not erase the current fog classification
    require(lights::is_fog_vertices(0x50000), "unrelated construction dropped a live fog hint");
}
void transforms_and_construction() {
    reset();
    node(root_node, 2);
    node(child_node, 4, root_node);
    fade(child_node);
    auto ref = lights::g_fade_nodes.at(0);
    float matrix[16];
    require(lights::node_world(ref, matrix), "live transform refused");
    require(retains == 2 && releases == 2 && mem::r32(child_node + 4) == 1 && mem::r32(root_node + 4) == 1, "temporary native references leaked");
    mem::w32(root_node + 0x128, child_node);
    require(!lights::node_world(ref, matrix) && transforms == 1, "cycle reached native transform");
    mem::w32(root_node + 0x128, 0);
    retire_during_transform = true;
    require(!lights::node_world(ref, matrix), "result from a retired world was published");
    require(retains == releases, "world retirement leaked temporary native refs");
    reset();
    node(root_node);
    fade(root_node);
    auto old = lights::g_fade_nodes.at(0);
    Registers regs(root_node);
    lights::on_node_constructed(regs.context);
    require(!lights::g_lifetimes.contains(old) && !lights::g_lifetimes.nodes.current(root_node), "constructing address was made accessible");
    regs.values[4] = root_node;
    lights::on_node_initialized(regs.context);
    require(bool(lights::g_lifetimes.nodes.current(root_node)), "initialized node never became accessible");
    require(!lights::g_lifetimes.contains(old), "initialization revived an older handle");
}
void layout_and_mapping() {
    using scene_cache::vector_layout;
    require(vector_layout(0,0,0,4) && vector_layout(0x2000,0x2004,0x2010,4), "valid vector header rejected");
    require(!vector_layout(0x2001,0x2005,0x2011,4) && !vector_layout(0,4,4,4) && !vector_layout(0x3000,0x2000,0x4000,4), "invalid vector header accepted");
    require(!vector_layout(0xfffffff0,4,8,4) && !vector_layout(0x2000,0x2008,0x2004,4) && !vector_layout(0,0,0,0), "overflow/capacity vector check failed");
    reset();
    node(root_node);
    fade(root_node);
    mem::w32(root_node + 0x130, 4321);
    pages[root_node / mem::PAGE_SIZE] = false;
    render();
    require(mem::r32(root_node + 0x130) == 4321, "unmapped node was written");
    require(!lights::mapped(0xfffffff0,0x40), "wrapping span accepted");
}
void read_only_teardown_diagnostics() {
    reset();
    constexpr u32 object = 0x40000;
    Registers regs(object);
    mem::w32(object + 0x24, 0x50000);
    mem::w32(object + 0x28, 0x50000);
    mem::w32(object + 0x2c, 0x50002);  // legitimate native empty string, not a pointer vector
    auto before = bytes;
    lights::on_object_destroyed(regs.context);
    require(lights::g_bad_header_budget == 0 && bytes == before, "empty-string diagnostic misclassified or changed native storage");
    mem::w32(object + 0x24, 0x50001);
    before = bytes;
    lights::on_object_destroyed(regs.context);
    require(lights::g_bad_header_budget == 1 && bytes == before && !retains && !releases, "invalid header diagnostic mutated native ownership/storage");
}
}
int main() {
    try {
        retired_resources();
        world_reuse();
        guarded_writes();
        normal_fades();
        transforms_and_construction();
        layout_and_mapping();
        read_only_teardown_diagnostics();
        std::cout << "scene cache lifetime and production adapter tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
