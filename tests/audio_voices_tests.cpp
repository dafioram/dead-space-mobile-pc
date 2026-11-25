#include "audio_voice_policy.h"
#include "audio_voices.h"
#include "cpu.h"
#include "elf_loader.h"
#include "log.h"
#include "memory.h"

#include <array>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool okay, const char* why) {
    if (!okay) throw std::runtime_error(why);
}

std::map<std::string, u32> g_symbols;
u32 g_hook_addr = 0;
cpu::HleFn g_hook_fn = nullptr;
bool g_allow_hook = true;

constexpr u32 kFnIsPlaying = 0x101c7c20u;
constexpr u32 kFnStop = 0x101c7e98u;
constexpr u32 kFnRelease = 0x101c7de0u;

std::set<u32> g_playing_voices;
std::vector<u32> g_stopped;
std::vector<u32> g_released;
u32 g_is_playing_calls = 0;

struct FakeCpu {
    u32 regs[16]{};
};

}  // namespace

namespace logging {
bool g_verbose = false;
void write(char, const char*, ...) {}
}  // namespace logging

namespace elf {
u32 find(const std::string& name) {
    auto it = g_symbols.find(name);
    return it == g_symbols.end() ? 0 : it->second;
}
}  // namespace elf

namespace cpu {
u32 Ctx::reg(int i) const { return reinterpret_cast<const FakeCpu*>(c)->regs[i]; }
void Ctx::set_reg(int i, u32 v) { reinterpret_cast<FakeCpu*>(c)->regs[i] = v; }

bool hook_entry(u32 addr, const std::string&, HleFn fn) {
    if (!g_allow_hook) return false;
    g_hook_addr = addr;
    g_hook_fn = fn;
    return true;
}

u32 call(u32 fn, std::initializer_list<u32> args) {
    std::vector<u32> a(args);
    if (fn == kFnIsPlaying) {
        ++g_is_playing_calls;
        require(a.size() == 2 && a[1] == 0x3f800000u, "IsPlaying must receive (voice, 1.0f)");
        return g_playing_voices.count(a[0]) ? 1u : 0u;
    }
    if (fn == kFnStop) {
        require(a.size() == 1, "Stop must receive (voice)");
        g_stopped.push_back(a[0]);
        g_playing_voices.erase(a[0]);
        return 0;
    }
    if (fn == kFnRelease) {
        require(a.size() == 1, "ReleaseVoice must receive (voice)");
        g_released.push_back(a[0]);
        return 0;
    }
    throw std::runtime_error("unexpected guest call in audio_voices_tests");
}
}  // namespace cpu

namespace {

void seed_valid_binary() {
    g_symbols = {
        {"_ZN2im12SoundManager12PreloadMusicEv", mem::ELF_BASE + 0x1c8b60u},
        {"_ZN2im12SoundManager9playSoundERKN5eastl12basic_stringIwNS_20StringEASTLAllocatorEEERK9btVector3",
         mem::ELF_BASE + 0x1c9ac0u},
        {"_ZN11SoundEngine9IsPlayingEPN2EA5Audio4Core5VoiceEf", kFnIsPlaying},
        {"_ZN11SoundEngine4StopEPN2EA5Audio4Core5VoiceE", kFnStop},
        {"_ZN11SoundEngine12ReleaseVoiceEPN2EA5Audio4Core5VoiceE", kFnRelease},
    };
    const std::pair<u32, u32> words[] = {
        {0x1c8b60u, 0xe92d4010u}, {0x1c8b64u, 0xe59f303cu}, {0x1c8b68u, 0xe3a02002u},
        {0x1c8b6cu, 0xe1a04000u}, {0x1c8b70u, 0xe08f3003u}, {0x1c8b74u, 0xe5832018u},
        {0x1c8b78u, 0xe5900058u},
        {0x1c9b78u, 0xe59f3358u}, {0x1c9b7cu, 0xe08f3003u}, {0x1c9b80u, 0xe5932018u},
        {0x1c9b84u, 0xe2622006u}, {0x1c9b88u, 0xe3520000u}, {0x1c9b8cu, 0xdaffffd4u},
        {0x1c9b90u, 0xe5954064u}, {0x1c9b94u, 0xe3540000u}, {0x1c9b98u, 0x12853064u},
    };
    for (auto [off, w] : words) mem::w32(mem::ELF_BASE + off, w);
    g_hook_addr = 0;
    g_hook_fn = nullptr;
    g_allow_hook = true;
}

void test_policy() {
    using namespace audio_voices;

    // 1. Free slot available -> no reclaim/steal.
    std::array<SlotSnapshot, kNativeSfxSlots> s{};
    for (std::size_t i = 0; i < 5; ++i) s[i] = {u32(0x100 + i), u32(0x200 + i), i + 1, true};
    auto plan = plan_sfx_slot(s);
    require(plan.kind == ReclaimKind::none, "free slot available requires no reclaim or steal");

    // 2. All 6 slots occupied, slots 1 and 4 finished -> reclaim both, do not steal active voices.
    s[5] = {0x105, 0x205, 6, true};
    s[1].playing = false;
    s[4].voice = 0;
    plan = plan_sfx_slot(s);
    require(plan.kind == ReclaimKind::reclaim_finished &&
                !plan.finish_mask[0] && plan.finish_mask[1] && !plan.finish_mask[2] &&
                !plan.finish_mask[3] && plan.finish_mask[4] && !plan.finish_mask[5],
            "reclaims all finished/null-voice slots without stealing active voices");

    // 3. All 6 actively playing -> steals oldest born_seq (slot 3 with seq 10).
    for (std::size_t i = 0; i < kNativeSfxSlots; ++i) s[i] = {u32(0x100 + i), u32(0x200 + i), 20 + i, true};
    s[3].born_seq = 10;
    plan = plan_sfx_slot(s);
    require(plan.kind == ReclaimKind::steal_oldest && plan.steal_slot == 3,
            "steals slot with oldest birth sequence when all 6 are active");

    // 4. SlotTracker tracks birth order across reuse and resets on new SoundManager pointer.
    SlotTracker tracker;
    std::array<u32, kNativeSfxSlots> objs{1, 2, 3, 4, 5, 0};
    std::array<u32, kNativeSfxSlots> voices{11, 12, 13, 14, 15, 0};
    require(tracker.sync(0x5000, objs, voices), "sync reports free slot when slot 5 is empty");
    objs[5] = 6;
    voices[5] = 16;
    require(!tracker.sync(0x5000, objs, voices), "sync reports full when all 6 slots occupied");
    require(tracker.born_seq(0) < tracker.born_seq(5), "newer slot gets larger sequence number");

    // Replacing slot 0 gives it a newer birth sequence than slot 1.
    objs[0] = 7;
    voices[0] = 17;
    tracker.sync(0x5000, objs, voices);
    require(tracker.born_seq(1) < tracker.born_seq(0), "reused slot 0 becomes newer than untouched slot 1");

    // Changing SoundManager address resets sequence tracking cleanly.
    tracker.sync(0x6000, objs, voices);
    require(tracker.born_seq(0) == 1 && tracker.born_seq(5) == 6, "new SoundManager resets monotonic birth order");
}

void test_install_and_runtime_hook() {
    // Rejection cases: wrong PreloadMusic word leaves memory untouched and installs no hook.
    seed_valid_binary();
    mem::w32(mem::ELF_BASE + 0x1c8b68u, 0xe3a02003u);
    require(!audio_voices::install(), "unrecognized PreloadMusic instruction must fail");
    require(mem::r32(mem::ELF_BASE + 0x1c8b68u) == 0xe3a02003u && g_hook_fn == nullptr,
            "failed validation must leave code untouched and install no hook");

    // Wrong playSound word leaves PreloadMusic untouched.
    seed_valid_binary();
    mem::w32(mem::ELF_BASE + 0x1c9b84u, 0xe2622008u);
    require(!audio_voices::install(), "unrecognized playSound instruction must fail");
    require(mem::r32(mem::ELF_BASE + 0x1c8b68u) == 0xe3a02002u && g_hook_fn == nullptr,
            "failed playSound check must not partially patch PreloadMusic");

    // Failed hook install leaves PreloadMusic untouched.
    seed_valid_binary();
    g_allow_hook = false;
    require(!audio_voices::install(), "failed hook installation must fail cleanly");
    require(mem::r32(mem::ELF_BASE + 0x1c8b68u) == 0xe3a02002u,
            "failed hook installation must not patch PreloadMusic");

    // Valid binary succeeds: patches mov r2, #2 -> mov r2, #0 and hooks 0x1c9b80.
    seed_valid_binary();
    require(audio_voices::install(), "valid binary installs 6-slot patch and playSound hook");
    require(mem::r32(mem::ELF_BASE + 0x1c8b68u) == 0xe3a02000u, "PreloadMusic patched to mov r2, #0");
    require(g_hook_addr == mem::ELF_BASE + 0x1c9b80u && g_hook_fn != nullptr, "playSound hook installed at 0x1c9b80");

    // Exercise live hook against a synthetic guest SoundManager object.
    const u32 sm = mem::heap_alloc(0x100);
    auto set_slot = [&](std::size_t i, u32 meta, u32 obj, u32 voice, bool playing) {
        mem::w32(sm + 0x40u + u32(i) * 4u, meta);
        mem::w32(sm + 0x64u + u32(i) * 4u, obj);
        mem::w32(sm + 0x7cu + u32(i) * 4u, voice);
        if (playing) g_playing_voices.insert(voice);
        else g_playing_voices.erase(voice);
    };

    FakeCpu fake{};
    fake.regs[5] = sm;
    cpu::Ctx ctx{reinterpret_cast<cpu::Cpu*>(&fake)};

    // Fill 5 slots (slot 5 free): fast path does zero IsPlaying/Stop/Release calls.
    for (std::size_t i = 0; i < 5; ++i) set_slot(i, u32(i + 1), u32(0x1000 + i), u32(0x2000 + i), true);
    set_slot(5, 0, 0, 0, false);
    g_is_playing_calls = 0;
    g_stopped.clear();
    g_released.clear();
    g_hook_fn(ctx);
    require(g_is_playing_calls == 0 && g_stopped.empty() && g_released.empty(),
            "fast path skips guest calls when a free SFX slot exists");

    // Fill slot 5, mark slot 2 finished: hook reclaims slot 2 via ReleaseVoice (no Stop).
    set_slot(5, 6, 0x1005, 0x2005, true);
    g_playing_voices.erase(0x2002);
    g_hook_fn(ctx);
    require(g_stopped.empty(), "reclaiming a finished voice does not call Stop");
    require(g_released.size() == 1 && g_released[0] == 0x2002u, "finished slot 2 voice released");
    require(mem::r32(sm + 0x40u + 8u) == 0 && mem::r32(sm + 0x64u + 8u) == 0 && mem::r32(sm + 0x7cu + 8u) == 0,
            "finished slot 2 cleared in SoundManager");
    require(mem::r32(sm + 0x64u) == 0x1000u, "active slot 0 left intact when finished slot was available");

    // Refill slot 2 (now newer than slot 0), all 6 actively playing -> steals oldest (slot 0)!
    set_slot(2, 99, 0x1099, 0x2099, true);
    g_stopped.clear();
    g_released.clear();
    g_hook_fn(ctx);
    require(g_stopped.size() == 1 && g_stopped[0] == 0x2000u, "oldest active slot 0 stopped when all 6 playing");
    require(g_released.size() == 1 && g_released[0] == 0x2000u, "oldest active slot 0 released");
    require(mem::r32(sm + 0x40u) == 0 && mem::r32(sm + 0x64u) == 0 && mem::r32(sm + 0x7cu) == 0,
            "stolen slot 0 cleared for playSound to reuse");

    // Refill slot 0 (now newest!) -> next steal must evict slot 1, not slot 0 again!
    set_slot(0, 100, 0x1100, 0x2100, true);
    g_stopped.clear();
    g_released.clear();
    g_hook_fn(ctx);
    require(g_stopped.size() == 1 && g_stopped[0] == 0x2001u && g_released.size() == 1 && g_released[0] == 0x2001u,
            "subsequent steal evicts next-oldest slot 1 instead of thrashing slot 0");
}

}  // namespace

int main() {
    try {
        require(mem::init(), "guest memory init failed");
        mem::commit(mem::ELF_BASE + 0x1c8000u, 0x4000u);
        test_policy();
        test_install_and_runtime_hook();
        std::cout << "Audio voice slot unlock, reclaim, and oldest-steal tests passed; no game executed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
