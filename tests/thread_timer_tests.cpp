#include "hle/thread_timer.h"
#include "hle/timing.h"

#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
void require(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }

struct FakeTimer {
    using Handle = unsigned;
    static inline unsigned creates = 0, closes = 0, waits = 0;
    static inline bool create_ok = true, arm_ok = true;
    static inline int64_t requested = 0;
    static Handle create() { ++creates; return create_ok ? 1 : 0; }
    static void close(Handle handle) { require(handle == 1, "close owns created handle"); ++closes; }
    static bool wait(Handle handle, int64_t ns) {
        require(handle == 1, "wait uses owned handle");
        ++waits;
        requested = ns;
        return arm_ok;
    }
};

void ownership_and_failures() {
    using Timer = timing::detail::ThreadTimer<FakeTimer>;
    static_assert(!std::is_copy_constructible_v<Timer> && !std::is_copy_assignable_v<Timer>);
    static_assert(!std::is_move_constructible_v<Timer> && !std::is_move_assignable_v<Timer>);
    {
        Timer timer;
        require(timer.wait(1) && timer.wait(1234567), "reuse successful timer");
        require(FakeTimer::creates == 1 && FakeTimer::closes == 0 && FakeTimer::requested == 1234567,
                "no premature close, extra creation or duration change");
        FakeTimer::arm_ok = false;
        require(!timer.wait(1000000), "arming failure selects caller fallback");
        FakeTimer::arm_ok = true;
        require(timer.wait(1000000), "arming failure retains reusable timer");
    }
    require(FakeTimer::closes == 1, "successful timer closes exactly once");
    FakeTimer::create_ok = false;
    {
        Timer timer;
        require(!timer.wait(1000000) && !timer.wait(2000000), "creation failure selects caller fallback");
    }
    require(FakeTimer::creates == 2 && FakeTimer::closes == 1 && FakeTimer::waits == 4,
            "failed creation is not retried, waited or closed");
}

// Check TLS ownership deterministically as well as through OS handle counts.
struct TrackedTimer {
#ifdef _WIN32
    // The C++/pthread identity may already be retired during emulated TLS cleanup.
    using ThreadId = DWORD;
    static ThreadId current_thread() { return GetCurrentThreadId(); }
#else
    using ThreadId = std::thread::id;
    static ThreadId current_thread() { return std::this_thread::get_id(); }
#endif
    struct HandleData { ThreadId owner; };
    using Handle = HandleData*;
    static inline std::atomic<unsigned> creates{0}, closes{0}, waits{0}, wrong_thread{0};
    static Handle create() { ++creates; return new HandleData{current_thread()}; }
    static void close(Handle handle) {
        if (handle->owner != current_thread()) ++wrong_thread;
        ++closes;
        delete handle;
    }
    static bool wait(Handle handle, int64_t) {
        if (handle->owner != current_thread()) ++wrong_thread;
        ++waits;
        return true;
    }
};
void tracked_sleep() {
    thread_local timing::detail::ThreadTimer<TrackedTimer> timer;
    timer.wait(1);
    timer.wait(1000000);
}
template <class Work> void detached_worker(Work work) {
#ifdef _WIN32
    // MinGW's emulated TLS can be destroyed after a C++ at-thread-exit future
    // becomes ready. Wait for the native thread itself, not that early signal.
    std::atomic<HANDLE> native{nullptr};
    std::thread thread([work = std::move(work), &native] {
        HANDLE handle = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle,
                             SYNCHRONIZE, FALSE, 0)) handle = INVALID_HANDLE_VALUE;
        native = handle;
        work();
    });
    HANDLE handle = nullptr;
    while (!(handle = native.load())) std::this_thread::yield();
    if (handle == INVALID_HANDLE_VALUE) { thread.join(); require(false, "duplicate native thread handle"); }
    thread.detach();
    const auto result = WaitForSingleObject(handle, INFINITE);
    const bool closed = CloseHandle(handle) != 0;
    require(result == WAIT_OBJECT_0 && closed, "wait for detached native thread termination");
#else
    std::promise<void> done;
    auto exited = done.get_future();
    std::thread([work = std::move(work), done = std::move(done)]() mutable {
        work();
        done.set_value_at_thread_exit();
    }).detach();
    exited.wait();
#endif
}
void thread_local_lifetimes() {
    std::atomic<unsigned> ready{0};
    std::atomic<bool> release{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back([&] {
        tracked_sleep();
        ++ready;
        while (!release.load()) std::this_thread::yield();
    });
    while (ready.load() != 8) std::this_thread::yield();
    const auto live_creates = TrackedTimer::creates.load(), live_closes = TrackedTimer::closes.load();
    release = true;
    for (auto& thread : threads) thread.join();
    require(live_creates == 8 && live_closes == 0, "active threads keep their own timers");
    require(TrackedTimer::closes == 8 && TrackedTimer::waits == 16, "joined TLS closes once per thread");
    for (int i = 0; i < 8; ++i) detached_worker(tracked_sleep);
    require(TrackedTimer::creates == 16 && TrackedTimer::closes == 16 && TrackedTimer::waits == 32,
            "detached threads release their timers too");
    require(TrackedTimer::wrong_thread == 0, "no cross-thread timer wait or close");
}

void sleeps() {
    timing::sleep_ns(0);
    timing::sleep_ns(-1);
    timing::sleep_ns(1);
    timing::sleep_ns(1000000);
    timing::sleep_ns(1000000);
}
void serial_workers(unsigned count) {
    for (unsigned i = 0; i < count; ++i) { std::thread thread(sleeps); thread.join(); }
}
void concurrent_workers() {
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back(sleeps);
    for (auto& thread : threads) thread.join();
}
void detached_workers() {
    for (int i = 0; i < 8; ++i) detached_worker(sleeps);
}
#ifdef _WIN32
DWORD handle_count() {
    DWORD count = 0;
    require(GetProcessHandleCount(GetCurrentProcess(), &count) != 0, "query process handles");
    return count;
}
#endif
void platform_sleeps() {
    // Warm the thread/future runtime before comparing live process resources.
    serial_workers(2);
    concurrent_workers();
    detached_workers();
#ifdef _WIN32
    const auto before = handle_count();
#endif
    serial_workers(32);
    concurrent_workers();
#ifdef _WIN32
    const auto after = handle_count();
    std::cout << "Windows handles before=" << before << " after=" << after << '\n';
    require(after <= before + 2, "exited sleeping threads must not accumulate timer handles");
#endif
    // Exercise the actual detached sleep path too. Do not apply a total-process
    // handle budget here: some MinGW runtimes retain Events from detached helpers
    // even without sleeps. Exact detached TLS ownership is checked separately.
    detached_workers();
}
}  // namespace

int main() {
    try {
        ownership_and_failures();
        thread_local_lifetimes();
        platform_sleeps();
        std::cout << "Timer ownership/fallback and serial/concurrent/detached sleep tests passed; no game executed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
