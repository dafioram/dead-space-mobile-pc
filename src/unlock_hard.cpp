#include "unlock_hard.h"

#include <cstdlib>
#include <vector>

#include "cpu.h"
#include "elf_loader.h"
#include "log.h"
#include "memory.h"

namespace unlock_hard {
namespace {

constexpr u32 kSettingsLoadOffset = 0x1be5f0;
constexpr u32 kSettingsLoadWord0 = 0xe92d40f0;  // push {r4, r5, r6, r7, lr}
constexpr u32 kSettingsLoadWord1 = 0xe1a05001;  // mov  r5, r1
constexpr u32 kUnlockedHardOffset = 0x1b8658;
constexpr u32 kUnlockHardOffset = 0x1bd164;
constexpr u32 kClearNotifiedHardOffset = 0x1bd398;

struct Frame {
    u32 settings_ptr = 0;
    u32 saved_lr = 0;
};

bool g_requested = false;
bool g_applied = false;
u32 g_return_stub = 0;
u32 g_fn_unlocked_hard = 0;
u32 g_fn_unlock_hard = 0;
u32 g_fn_clear_notified_hard = 0;
thread_local std::vector<Frame> g_frames;

void on_settings_load_enter(cpu::Ctx& c) {
    if (g_applied) return;
    g_frames.push_back({c.reg(0), c.lr()});
    c.set_reg(14, g_return_stub);
}

void on_settings_load_return(cpu::Ctx& c) {
    if (g_frames.empty()) {
        LOGE("unlock-hard: Settings::load return context missing");
        std::abort();
    }
    const Frame frame = g_frames.back();
    g_frames.pop_back();
    const u32 load_ret = c.reg(0);

    if (!g_applied && frame.settings_ptr != 0) {
        g_applied = true;
        const bool already_unlocked = (cpu::call(g_fn_unlocked_hard, {frame.settings_ptr}) & 0xff) != 0;
        if (already_unlocked) {
            LOGI("unlock-hard: Hard difficulty already unlocked; settings unchanged");
        } else {
            cpu::call(g_fn_unlock_hard, {frame.settings_ptr});
            cpu::call(g_fn_clear_notified_hard, {frame.settings_ptr});
            const bool now_unlocked = (cpu::call(g_fn_unlocked_hard, {frame.settings_ptr}) & 0xff) != 0;
            if (now_unlocked) {
                LOGI("unlock-hard: Hard difficulty unlocked via native Settings::unlockHard");
            } else {
                LOGW("unlock-hard: native Settings::unlockHard completed but unlockedHard() still returned false");
            }
        }
    }

    c.ret(load_ret);
    c.set_reg(14, frame.saved_lr);
}

}  // namespace

void set_requested(bool enabled) { g_requested = enabled; }
bool requested() { return g_requested; }

bool install() {
    g_applied = false;
    g_frames.clear();
    if (!g_requested) return true;

    const u32 fn_load = elf::find("_ZN8Settings4loadERKN5eastl12basic_stringIwN2im20StringEASTLAllocatorEEE");
    const u32 fn_unlocked = elf::find("_ZNK8Settings12unlockedHardEv");
    const u32 fn_unlock = elf::find("_ZN8Settings10unlockHardEv");
    const u32 fn_clear_notified = elf::find("_ZN8Settings25clearNotifiedUnlockedHardEv");

    if (fn_load != mem::ELF_BASE + kSettingsLoadOffset ||
        fn_unlocked != mem::ELF_BASE + kUnlockedHardOffset ||
        fn_unlock != mem::ELF_BASE + kUnlockHardOffset ||
        fn_clear_notified != mem::ELF_BASE + kClearNotifiedHardOffset) {
        return false;
    }
    if (!mem::range_mapped(fn_load, 8) ||
        mem::r32(fn_load) != kSettingsLoadWord0 ||
        mem::r32(fn_load + 4) != kSettingsLoadWord1) {
        return false;
    }

    g_fn_unlocked_hard = fn_unlocked;
    g_fn_unlock_hard = fn_unlock;
    g_fn_clear_notified_hard = fn_clear_notified;
    if (!g_return_stub) g_return_stub = cpu::register_hle("Settings::load return (unlock-hard)", on_settings_load_return);
    if (!g_return_stub) return false;
    if (!cpu::hook_entry(fn_load, "Settings::load (unlock-hard)", on_settings_load_enter)) return false;

    LOGD("unlock-hard: armed post-Settings::load unlock hook");
    return true;
}

}  // namespace unlock_hard
