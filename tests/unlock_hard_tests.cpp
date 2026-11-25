#include "unlock_hard.h"
#include "cpu.h"
#include "elf_loader.h"
#include "log.h"
#include "memory.h"

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool okay, const char* why) {
    if (!okay) throw std::runtime_error(why);
}

constexpr u32 kLoadAddr = mem::ELF_BASE + 0x1be5f0u;
constexpr u32 kUnlockedHardAddr = mem::ELF_BASE + 0x1b8658u;
constexpr u32 kUnlockHardAddr = mem::ELF_BASE + 0x1bd164u;
constexpr u32 kClearNotifiedAddr = mem::ELF_BASE + 0x1bd398u;
constexpr u32 kFakeStubAddr = 0x70001000u;

std::map<std::string, u32> g_symbols;
u32 g_hook_addr = 0;
cpu::HleFn g_enter_fn = nullptr;
cpu::HleFn g_return_fn = nullptr;
bool g_allow_hook = true;

bool g_hard_unlocked = false;
bool g_notified_cleared = false;
std::vector<std::string> g_call_log;

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

u32 register_hle(const std::string&, HleFn fn) {
    g_return_fn = fn;
    return kFakeStubAddr;
}

bool hook_entry(u32 addr, const std::string&, HleFn fn) {
    if (!g_allow_hook) return false;
    g_hook_addr = addr;
    g_enter_fn = fn;
    return true;
}

u32 call(u32 fn, std::initializer_list<u32> args) {
    std::vector<u32> a(args);
    require(a.size() == 1 && a[0] != 0, "Settings methods take a non-null this pointer");
    if (fn == kUnlockedHardAddr) {
        g_call_log.push_back("unlockedHard");
        return g_hard_unlocked ? 1u : 0u;
    }
    if (fn == kUnlockHardAddr) {
        g_call_log.push_back("unlockHard");
        g_hard_unlocked = true;
        g_notified_cleared = false;
        return 0;
    }
    if (fn == kClearNotifiedAddr) {
        g_call_log.push_back("clearNotifiedUnlockedHard");
        g_notified_cleared = true;
        return 0;
    }
    throw std::runtime_error("unexpected guest call");
}
}  // namespace cpu

namespace {

void seed_valid_binary() {
    g_symbols = {
        {"_ZN8Settings4loadERKN5eastl12basic_stringIwN2im20StringEASTLAllocatorEEE", kLoadAddr},
        {"_ZNK8Settings12unlockedHardEv", kUnlockedHardAddr},
        {"_ZN8Settings10unlockHardEv", kUnlockHardAddr},
        {"_ZN8Settings25clearNotifiedUnlockedHardEv", kClearNotifiedAddr},
    };
    mem::w32(kLoadAddr, 0xe92d40f0u);
    mem::w32(kLoadAddr + 4, 0xe1a05001u);
    g_hook_addr = 0;
    g_enter_fn = nullptr;
    g_allow_hook = true;
    g_hard_unlocked = false;
    g_notified_cleared = false;
    g_call_log.clear();
}

void simulate_settings_load(u32 settings_ptr, u32 original_lr, u32 load_return_val) {
    FakeCpu cpu_state{};
    cpu_state.regs[0] = settings_ptr;
    cpu_state.regs[14] = original_lr;
    cpu::Ctx ctx{reinterpret_cast<cpu::Cpu*>(&cpu_state)};
    g_enter_fn(ctx);
    const u32 post_enter_lr = cpu_state.regs[14];
    cpu_state.regs[0] = load_return_val;
    if (post_enter_lr == kFakeStubAddr) {
        g_return_fn(ctx);
    }
    require(cpu_state.regs[0] == load_return_val, "Settings::load return value r0 must be preserved");
    require(cpu_state.regs[14] == original_lr, "Settings::load return lr must be restored");
}

void test_disabled_by_default() {
    seed_valid_binary();
    unlock_hard::set_requested(false);
    require(unlock_hard::install(), "install returns true when not requested");
    require(g_hook_addr == 0 && g_enter_fn == nullptr, "no hook installed when --unlock-hard is off");
}

void test_validation_rejects_mismatched_binary() {
    seed_valid_binary();
    unlock_hard::set_requested(true);
    mem::w32(kLoadAddr, 0xe1a00000u);  // wrong prologue word
    require(!unlock_hard::install(), "install rejects mismatched prologue");
    require(g_hook_addr == 0, "no hook installed on prologue mismatch");

    seed_valid_binary();
    g_symbols.erase("_ZN8Settings10unlockHardEv");
    require(!unlock_hard::install(), "install rejects missing symbol");
    require(g_hook_addr == 0, "no hook installed on missing symbol");
}

void test_unlocks_when_locked_and_is_oneshot() {
    seed_valid_binary();
    unlock_hard::set_requested(true);
    require(unlock_hard::install(), "install succeeds on matching binary");
    require(g_hook_addr == kLoadAddr && g_enter_fn != nullptr && g_return_fn != nullptr, "hook registered");

    simulate_settings_load(0x20001000u, 0x1020b010u, 1u);
    require(g_hard_unlocked && g_notified_cleared, "unlockHard and clearNotifiedUnlockedHard both ran");
    require(g_call_log.size() == 4 &&
                g_call_log[0] == "unlockedHard" &&
                g_call_log[1] == "unlockHard" &&
                g_call_log[2] == "clearNotifiedUnlockedHard" &&
                g_call_log[3] == "unlockedHard",
            "expected native call sequence");

    // Subsequent Settings::load must be a complete no-op.
    g_call_log.clear();
    simulate_settings_load(0x20001000u, 0x1020b010u, 1u);
    require(g_call_log.empty(), "second load does nothing once applied");
}

void test_noop_when_already_unlocked() {
    seed_valid_binary();
    g_hard_unlocked = true;
    unlock_hard::set_requested(true);
    require(unlock_hard::install(), "install succeeds");

    simulate_settings_load(0x20002000u, 0x1020b010u, 1u);
    require(g_call_log.size() == 1 && g_call_log[0] == "unlockedHard",
            "must only query unlockedHard() and perform zero writes when already unlocked");
    require(!g_notified_cleared, "must not call clearNotifiedUnlockedHard when already unlocked");
}

}  // namespace

int main() {
    try {
        require(mem::init(), "mem::init");
        mem::commit(kLoadAddr & ~0xFFFu, 0x1000u);
        test_disabled_by_default();
        test_validation_rejects_mismatched_binary();
        test_unlocks_when_locked_and_is_oneshot();
        test_noop_when_already_unlocked();
        std::cout << "unlock_hard_tests: ok\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
