#include "audio_voices.h"
#include "audio_voice_policy.h"

#include <array>
#include <cstddef>

#include "cpu.h"
#include "elf_loader.h"
#include "log.h"
#include "memory.h"

namespace audio_voices {
namespace {

struct Word {
    u32 offset;
    u32 expected;
};

// im::SoundManager::PreloadMusic() (0x1c8b60):
//   0x1c8b68: mov r2, #2 -> patched to mov r2, #0 so im::channelMod stays 0 (6 native SFX slots).
constexpr u32 kPreloadMusicOffset = 0x1c8b60;
constexpr u32 kChannelModMovOffset = 0x1c8b68;
constexpr u32 kMovR2Zero = 0xe3a02000;
constexpr Word kPreloadWords[] = {
    {0x1c8b60, 0xe92d4010},  // push {r4, lr}
    {0x1c8b64, 0xe59f303c},  // ldr r3, [pc, #0x3c]
    {0x1c8b68, 0xe3a02002},  // mov r2, #2  (throttles 6 slots -> 4)
    {0x1c8b6c, 0xe1a04000},  // mov r4, r0
    {0x1c8b70, 0xe08f3003},  // add r3, pc, r3
    {0x1c8b74, 0xe5832018},  // str r2, [r3, #0x18] (im::channelMod)
    {0x1c8b78, 0xe5900058},  // ldr r0, [r0, #0x58]
};

// im::SoundManager::playSound(name, pos) (0x1c9ac0):
// Hook at 0x1c9b80 (after PC-relative load at 0x1c9b78/0x1c9b7c, after SoundObject validation
// and type != 2 check, right before the SFX slot scan at 0x1c9b90).
// At 0x1c9b80: r5 = im::SoundManager*, r6 = SoundObject*.
constexpr u32 kPlaySoundOffset = 0x1c9ac0;
constexpr u32 kPlaySoundHookOffset = 0x1c9b80;
constexpr Word kPlaySoundWords[] = {
    {0x1c9b78, 0xe59f3358},  // ldr r3, [pc, #0x358]
    {0x1c9b7c, 0xe08f3003},  // add r3, pc, r3
    {0x1c9b80, 0xe5932018},  // ldr r2, [r3, #0x18] (hooked insn 0, non-PC-relative)
    {0x1c9b84, 0xe2622006},  // rsb r2, r2, #6      (hooked insn 1, non-PC-relative)
    {0x1c9b88, 0xe3520000},  // cmp r2, #0
    {0x1c9b8c, 0xdaffffd4},  // ble return -1
    {0x1c9b90, 0xe5954064},  // ldr r4, [r5, #0x64] (slot 0 SoundObject*)
    {0x1c9b94, 0xe3540000},  // cmp r4, #0
    {0x1c9b98, 0x12853064},  // addne r3, r5, #0x64
};

// im::SoundManager SFX slot array offsets (6 pointers each).
constexpr u32 kSfxMetaOffset = 0x40;
constexpr u32 kSfxObjectsOffset = 0x64;
constexpr u32 kSfxVoicesOffset = 0x7c;
constexpr u32 kSfxStructSpan = kSfxVoicesOffset + u32(kNativeSfxSlots) * 4;
constexpr u32 kFloatOneBits = 0x3f800000u;  // 1.0f passed to SoundEngine::IsPlaying(voice, 1.0f)

u32 fn_is_playing = 0;
u32 fn_stop = 0;
u32 fn_release_voice = 0;
SlotTracker g_tracker;

template <std::size_t N>
bool words_match(const Word (&words)[N]) {
    for (const Word& w : words) {
        const u32 addr = mem::ELF_BASE + w.offset;
        if (!mem::range_mapped(addr, 4) || mem::r32(addr) != w.expected) return false;
    }
    return true;
}

void clear_guest_slot(u32 sm, std::size_t slot) {
    const u32 off = u32(slot) * 4;
    mem::w32(sm + kSfxMetaOffset + off, 0);
    mem::w32(sm + kSfxObjectsOffset + off, 0);
    mem::w32(sm + kSfxVoicesOffset + off, 0);
    g_tracker.clear_slot(slot);
}

void on_play_sfx(cpu::Ctx& c) {
    const u32 sm = c.reg(5);
    if (!sm || !mem::range_mapped(sm, kSfxStructSpan)) return;

    std::array<u32, kNativeSfxSlots> objects{};
    std::array<u32, kNativeSfxSlots> voices{};
    for (std::size_t i = 0; i < kNativeSfxSlots; ++i) {
        objects[i] = mem::r32(sm + kSfxObjectsOffset + u32(i) * 4);
        voices[i] = mem::r32(sm + kSfxVoicesOffset + u32(i) * 4);
    }

    // Fast path: at least one slot is already free, so no guest calls needed.
    if (g_tracker.sync(sm, objects, voices)) return;

    // All 6 slots are occupied: query live playback status before choosing reclaim vs steal.
    std::array<SlotSnapshot, kNativeSfxSlots> snapshots{};
    for (std::size_t i = 0; i < kNativeSfxSlots; ++i) {
        const bool playing =
            voices[i] != 0 && (cpu::call(fn_is_playing, {voices[i], kFloatOneBits}) & 0xff) != 0;
        snapshots[i] = {objects[i], voices[i], g_tracker.born_seq(i), playing};
    }

    const ReclaimPlan plan = plan_sfx_slot(snapshots);
    if (plan.kind == ReclaimKind::reclaim_finished) {
        int reclaimed = 0;
        for (std::size_t i = 0; i < kNativeSfxSlots; ++i) {
            if (!plan.finish_mask[i]) continue;
            if (voices[i] != 0) cpu::call(fn_release_voice, {voices[i]});
            clear_guest_slot(sm, i);
            ++reclaimed;
        }
        LOGD("audio voices: reclaimed %d finished SFX slot(s)", reclaimed);
    } else if (plan.kind == ReclaimKind::steal_oldest && plan.steal_slot >= 0) {
        const std::size_t v = std::size_t(plan.steal_slot);
        if (voices[v] != 0) {
            cpu::call(fn_stop, {voices[v]});
            cpu::call(fn_release_voice, {voices[v]});
        }
        clear_guest_slot(sm, v);
        LOGD("audio voices: stole oldest SFX slot %zu (obj=%08x voice=%08x)", v, objects[v], voices[v]);
    }
}

}  // namespace

bool install() {
    g_tracker.reset();
    const u32 fn_preload = elf::find("_ZN2im12SoundManager12PreloadMusicEv");
    const u32 fn_play = elf::find(
        "_ZN2im12SoundManager9playSoundERKN5eastl12basic_stringIwNS_20StringEASTLAllocatorEEERK9btVector3");
    const u32 is_playing = elf::find("_ZN11SoundEngine9IsPlayingEPN2EA5Audio4Core5VoiceEf");
    const u32 stop = elf::find("_ZN11SoundEngine4StopEPN2EA5Audio4Core5VoiceE");
    const u32 release = elf::find("_ZN11SoundEngine12ReleaseVoiceEPN2EA5Audio4Core5VoiceE");

    if (fn_preload != mem::ELF_BASE + kPreloadMusicOffset ||
        fn_play != mem::ELF_BASE + kPlaySoundOffset ||
        !is_playing || !stop || !release) {
        return false;
    }

    // Validate both sites completely before writing or hooking anything.
    if (!words_match(kPreloadWords) || !words_match(kPlaySoundWords)) return false;

    fn_is_playing = is_playing;
    fn_stop = stop;
    fn_release_voice = release;

    if (!cpu::hook_entry(mem::ELF_BASE + kPlaySoundHookOffset,
                         "SoundManager::playSound (slot reclaim)", on_play_sfx)) {
        return false;
    }
    mem::w32(mem::ELF_BASE + kChannelModMovOffset, kMovR2Zero);
    LOGI("audio voices: unlocked 6 native SFX slots with on-demand reclaim and oldest-voice stealing");
    return true;
}

}  // namespace audio_voices
