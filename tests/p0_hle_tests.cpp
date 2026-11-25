// Synthetic tests for P0 HLE stability fixes:
//   - printf wide %s / %ls formatting without heap over-read or truncation
//   - pthread_cond_broadcast without stolen wakeups across re-entering waiters
//   - pthread_mutex_destroy / pthread_cond_destroy concurrent teardown without UAF
#include "hle/hle.h"
#include "save_io.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

std::vector<u8> g_guest(1 << 20);
std::map<std::string, cpu::HleFn> g_imports;
std::atomic<u32> g_next_tid{10};
thread_local cpu::ThreadInfo t_info{};

struct Registers {
    u32 r[16]{};
    cpu::Ctx ctx{reinterpret_cast<cpu::Cpu*>(this)};
    Registers() { r[13] = 0x70000; }
};

void require(bool ok, const char* msg) {
    if (!ok) throw std::runtime_error(msg);
}

u32 invoke(const std::string& name, std::initializer_list<u32> args) {
    Registers regs;
    size_t i = 0;
    for (u32 a : args) regs.r[i++] = a;
    g_imports.at(name)(regs.ctx);
    return regs.r[0];
}

void write_cstr(u32 addr, const char* s) {
    std::memcpy(g_guest.data() + addr, s, std::strlen(s) + 1);
}

void write_wstr(u32 addr, const char* s) {
    size_t n = std::strlen(s);
    for (size_t i = 0; i <= n; i++) {
        u16 ch = u8(s[i]);
        std::memcpy(g_guest.data() + addr + i * 2, &ch, 2);
    }
}

void test_printf_wide_strings() {
    write_cstr(0x1000, "hi");
    {
        Registers regs;
        regs.r[0] = 0x1000;
        hle::ArgReader ar = hle::ArgReader::slots(regs.ctx, 0);
        std::string out = hle::format("%500s", ar);
        require(out.size() == 500, "narrow %500s must produce exact 500 chars without over-read");
        require(out.substr(498) == "hi", "narrow %500s must end with payload");
        require(out.substr(0, 498) == std::string(498, ' '), "narrow %500s must left-pad with spaces");
    }

    write_wstr(0x2000, "wide");
    {
        Registers regs;
        regs.r[0] = 0x2000;
        hle::ArgReader ar = hle::ArgReader::slots(regs.ctx, 0);
        std::string out = hle::format("%-400ls", ar);
        require(out.size() == 400, "wide %-400ls must produce exact 400 chars without truncation");
        require(out.substr(0, 4) == "wide", "wide %-400ls must start with payload");
        require(out.substr(4) == std::string(396, ' '), "wide %-400ls must right-pad with spaces");
    }
}

void test_cond_broadcast_no_stolen_wakeups() {
    constexpr u32 kMutex = 0x10000;
    constexpr u32 kCond = 0x10010;
    require(invoke("pthread_mutex_init", {kMutex, 0}) == 0, "mutex_init");
    require(invoke("pthread_cond_init", {kCond, 0}) == 0, "cond_init");

    for (int round = 0; round < 50; round++) {
        std::atomic<int> waiting{0};
        std::atomic<bool> go{false};
        std::atomic<int> woken{0};
        std::atomic<bool> hold_second{true};

        // Thread 1 wakes first and immediately re-enters cond_wait before Thread 2 wakes.
        std::thread t1([&] {
            t_info.tid = g_next_tid.fetch_add(1);
            invoke("pthread_mutex_lock", {kMutex});
            waiting.fetch_add(1);
            while (!go.load()) invoke("pthread_cond_wait", {kCond, kMutex});
            woken.fetch_add(1);
            // Re-enter wait while Thread 2 may still be waking from the same broadcast.
            if (hold_second.load()) {
                invoke("pthread_cond_wait", {kCond, kMutex});
            }
            invoke("pthread_mutex_unlock", {kMutex});
        });

        std::thread t2([&] {
            t_info.tid = g_next_tid.fetch_add(1);
            invoke("pthread_mutex_lock", {kMutex});
            waiting.fetch_add(1);
            while (!go.load()) invoke("pthread_cond_wait", {kCond, kMutex});
            woken.fetch_add(1);
            hold_second.store(false);
            invoke("pthread_cond_broadcast", {kCond});
            invoke("pthread_mutex_unlock", {kMutex});
        });

        while (waiting.load() < 2) std::this_thread::yield();
        invoke("pthread_mutex_lock", {kMutex});
        go.store(true);
        invoke("pthread_cond_broadcast", {kCond});
        invoke("pthread_mutex_unlock", {kMutex});

        t2.join();
        t1.join();
        require(woken.load() == 2, "both waiters must wake from broadcast without stolen wakeups");
    }

    invoke("pthread_cond_destroy", {kCond});
    invoke("pthread_mutex_destroy", {kMutex});
}

void test_concurrent_destroy_no_uaf() {
    constexpr u32 kMutex = 0x10100;
    constexpr u32 kCond = 0x10110;

    for (int iter = 0; iter < 300; iter++) {
        require(invoke("pthread_mutex_init", {kMutex, 0}) == 0, "mutex_init");
        require(invoke("pthread_cond_init", {kCond, 0}) == 0, "cond_init");
        std::atomic<bool> ready{false};
        std::atomic<bool> waiting{false};

        std::thread waiter([&] {
            t_info.tid = g_next_tid.fetch_add(1);
            invoke("pthread_mutex_lock", {kMutex});
            waiting.store(true);
            while (!ready.load()) invoke("pthread_cond_wait", {kCond, kMutex});
            invoke("pthread_mutex_unlock", {kMutex});
            // Standard POSIX pattern: destroy immediately after waking while signaler finishes.
            invoke("pthread_cond_destroy", {kCond});
            invoke("pthread_mutex_destroy", {kMutex});
        });

        while (!waiting.load()) std::this_thread::yield();
        invoke("pthread_mutex_lock", {kMutex});
        ready.store(true);
        invoke("pthread_cond_signal", {kCond});
        invoke("pthread_mutex_unlock", {kMutex});
        waiter.join();
    }
}

}  // namespace

namespace mem {
u8* g_base = ::g_guest.data();
std::array<u8*, (1u << 20)>* g_page_table = nullptr;
bool is_mapped(u32 address) { return address < ::g_guest.size(); }
u32 map_alloc(u32) { return 0; }
void map_free(u32, u32) {}
}  // namespace mem

namespace cpu {
u32 Ctx::reg(int i) const { return reinterpret_cast<const Registers*>(c)->r[i]; }
void Ctx::set_reg(int i, u32 v) { reinterpret_cast<Registers*>(c)->r[i] = v; }
ThreadInfo& thread() { return t_info; }
void thread_attach(u32, u32) {}
void thread_detach() {}
u32 call(u32, std::initializer_list<u32>) { return 0; }
std::string symbolize(u32) { return "stub"; }
}  // namespace cpu

namespace hle {
void reg(const char* name, cpu::HleFn fn) { g_imports[name] = fn; }
void set_errno(int) {}
std::FILE* stdio_host_file(u32) { return nullptr; }
int host_errno_to_guest(int e) { return e; }
}  // namespace hle

namespace save_io {
std::size_t fwrite(const void*, std::size_t, std::size_t, std::FILE*) { return 0; }
}  // namespace save_io

namespace logging {
bool g_verbose = false;
void write(char, const char*, ...) {}
}  // namespace logging

int main() {
    try {
        t_info.tid = 1;
        hle::register_pthread();
        hle::register_printf();
        test_printf_wide_strings();
        test_cond_broadcast_no_stolen_wakeups();
        test_concurrent_destroy_no_uaf();
        std::cout << "p0_hle_tests: ok\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
