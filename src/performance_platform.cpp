#include "performance.h"
#include <algorithm>
#include <mutex>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <fstream>
#include <pthread.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

namespace perf {
namespace {
std::mutex lock;
uint64_t dropped = 0;
uint64_t generation = 0;
struct Entry {
    uint64_t id, generation;
#ifdef _WIN32
    HANDLE handle;
#else
    clockid_t clock;
#endif
};
std::vector<Entry> threads;
uint64_t current_id() {
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return uint64_t(syscall(SYS_gettid));
#endif
}
#ifdef _WIN32
uint64_t ticks(FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32 | t.dwLowDateTime) * 100; }
#else
uint64_t ticks(timespec t) { return uint64_t(t.tv_sec) * 1000000000ull + uint64_t(t.tv_nsec); }
#endif
}
ProcessSample process_sample() {
    ProcessSample s;
#ifdef _WIN32
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        s.cpu_valid = true; s.cpu_ns = ticks(kernel) + ticks(user);
    }
    PROCESS_MEMORY_COUNTERS_EX m{};
    m.cb = sizeof m;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof m)) {
        s.memory_valid = true; s.private_bytes = m.PrivateUsage;
        s.resident_bytes = m.WorkingSetSize; s.peak_resident_bytes = m.PeakWorkingSetSize;
        s.private_kind = "committed";
    }
#else
    timespec t{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t) == 0) { s.cpu_valid = true; s.cpu_ns = ticks(t); }
    // Private RSS is deliberately NOT described as committed/private bytes on Linux.
    std::ifstream status("/proc/self/status");
    std::string line;
    for (unsigned n = 0; n < 128 && std::getline(status, line); ++n) {
        auto value = [&](const char* key, uint64_t& target) {
            if (!line.starts_with(key)) return false;
            try { target = std::stoull(line.substr(std::char_traits<char>::length(key))) * 1024; }
            catch (...) { return false; }
            return true;
        };
        if (value("VmRSS:", s.resident_bytes)) s.memory_valid = true;
        value("VmHWM:", s.peak_resident_bytes);
    }
#endif
    return s;
}
void thread_enter() {
    if (!enabled()) return;
    std::lock_guard guard(lock);
    uint64_t id = current_id();
    if (std::any_of(threads.begin(), threads.end(), [&](auto e) { return e.id == id; })) return;
    if (threads.size() == 64) { ++dropped; return; }
#ifdef _WIN32
    HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, DWORD(id));
    if (!h) { ++dropped; return; }
    threads.push_back({id, ++generation, h});
#else
    clockid_t clock;
    if (pthread_getcpuclockid(pthread_self(), &clock) != 0) { ++dropped; return; }
    threads.push_back({id, ++generation, clock});
#endif
}
void thread_leave() {
    if (!enabled()) return;
    std::lock_guard guard(lock);
    uint64_t id = current_id();
    auto it = std::find_if(threads.begin(), threads.end(), [&](auto e) { return e.id == id; });
    if (it == threads.end()) return;
#ifdef _WIN32
    CloseHandle(it->handle);
#endif
    threads.erase(it);
}
std::vector<ThreadSample> thread_samples() {
    std::lock_guard guard(lock);
    std::vector<ThreadSample> result;
    result.reserve(threads.size());
    for (const auto& e : threads) {
        ThreadSample s; s.id = e.id; s.generation = e.generation;
#ifdef _WIN32
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetThreadTimes(e.handle, &created, &exited, &kernel, &user)) {
            s.valid = true; s.cpu_ns = ticks(kernel) + ticks(user);
        }
#else
        timespec t{};
        if (clock_gettime(e.clock, &t) == 0) { s.valid = true; s.cpu_ns = ticks(t); }
#endif
        result.push_back(s);
    }
    return result;
}
uint64_t thread_queries_dropped() { std::lock_guard guard(lock); return dropped; }
}  // namespace perf
