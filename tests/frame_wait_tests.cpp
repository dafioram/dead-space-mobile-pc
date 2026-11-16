#include "frame_wait.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
void require(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }

// Microsecond counter; every counter read advances simulated execution by 1 us.
struct Host {
    uint64_t tick = 0, timer_advance = 0, sleep_extra = 0;
    bool timer_ok = true, diagnostics = true, early = false;
    std::vector<uint64_t> waits;
    std::vector<uint32_t> sleeps;
    uint64_t now() { return tick++; }
    uint64_t stamp() { return diagnostics ? tick * 1000 : 0; }
    void delay_ms(uint32_t ms) { sleeps.push_back(ms); tick += ms * 1000 + sleep_extra; }
    bool wait_timer(uint64_t duration) {
        waits.push_back(duration);
        tick += timer_advance;
        if (timer_ok) tick += duration / 10 - (early ? 50 : 0);
        return timer_ok;
    }
};

frame_wait::Timings original(Host& host, uint64_t deadline, uint64_t now) {
    frame_wait::Timings out;
    if (now >= deadline) return out;
    const uint64_t ms = (deadline - now) * 1000 / 1000000;
    if (ms > 1) {
        auto start = host.stamp();
        host.delay_ms(uint32_t(ms - 1));
        out.sleep_ns += host.stamp() - start;
    }
    auto start = host.stamp();
    while (host.now() < deadline) {}
    out.spin_ns += host.stamp() - start;
    return out;
}

void policy() {
    frame_wait::Mode mode = frame_wait::Mode::Legacy;
    require(frame_wait::parse_mode("timer", mode) && mode == frame_wait::Mode::Timer, "parse timer");
    require(frame_wait::parse_mode("legacy", mode) && mode == frame_wait::Mode::Legacy, "parse legacy");
    require(!frame_wait::parse_mode("", mode) && !frame_wait::parse_mode("timre", mode), "reject missing/unknown mode");
    require(mode == frame_wait::Mode::Legacy, "invalid mode does not change selection");

    for (auto diagnostic : {false, true}) {
        for (uint64_t duration : {0, 1, 100, 199, 200, 201, 999, 1000, 1999, 2000, 16666, 1000000}) {
            for (uint64_t oversleep : {0, 5000}) {
                Host before, after;
                before.diagnostics = after.diagnostics = diagnostic;
                before.sleep_extra = after.sleep_extra = oversleep;
                auto expected = original(before, duration, 0);
                auto got = frame_wait::until(after, duration, 1000000, 0, false);
                require(before.tick == after.tick && before.sleeps == after.sleeps && after.waits.empty(), "legacy clock/wait path unchanged");
                require(expected.sleep_ns == got.sleep_ns && expected.spin_ns == got.spin_ns, "legacy phase measurements unchanged");
            }
        }
    }
    for (uint64_t duration : {1, 100, 200, 201, 1000, 16666, 1000000}) {
        Host host;
        auto t = frame_wait::until(host, duration, 1000000, 0, true);
        require(host.tick > duration, "timer never releases before original deadline");
        require(host.sleeps.empty(), "timer path does not also use coarse SDL sleep");
        require(host.waits.size() == (duration > 200 ? 1u : 0u), "wait only beyond finishing margin");
        require(t.spin_ns <= 201000, "normally at most 0.2 ms finishing spin");
        if (duration > 200) require(host.waits[0] == (duration - 200) * 10, "relative 100 ns duration excludes guard");
    }
    {
        Host host;
        host.early = true;
        frame_wait::until(host, 16666, 1000000, 0, true);
        require(host.tick > 16666, "early OS wake still finishes at deadline");
    }
    {
        Host host;
        host.timer_advance = 5000;
        auto t = frame_wait::until(host, 16666, 1000000, 0, true);
        require(t.spin_ns == 1000 && host.waits.size() == 1, "late OS wake never adds a second frame delay");
    }
    for (uint64_t failed_time : {0, 8000, 20000}) {
        Host host;
        host.timer_ok = false;
        host.timer_advance = failed_time;
        frame_wait::until(host, 16666, 1000000, 0, true);
        require(host.tick > 16666 && host.waits.size() == 1, "failed timer falls back without releasing early");
        require(host.tick <= std::max<uint64_t>(16666, failed_time) + 2, "failure uses remaining time, not original duration");
    }
    {
        Host host;
        auto t = frame_wait::until(host, 100, 1000000, 150, true);
        require(host.tick == 0 && host.waits.empty() && !t.sleep_ns && !t.spin_ns, "already late never waits");
    }
    {
        Host host;
        // 10 MHz QPC: 1.2345 ms remaining -> 1.0345 ms timer duration.
        frame_wait::until(host, 12345, 10000000, 0, true);
        require(host.waits[0] == 10345, "non-microsecond clock conversion");
    }
    {
        Host host;
        host.diagnostics = false;
        auto t = frame_wait::until(host, 16666, 1000000, 0, true);
        require(host.tick > 16666 && !t.sleep_ns && !t.spin_ns, "diagnostics do not control wait policy");
    }
}

void platform() {
    frame_wait::Timer disabled(false);
    require(!disabled.available() && !disabled.wait(10000), "disabled timer never creates/waits");
    disabled.close(); disabled.close();
    frame_wait::Timer timer(true);
#ifdef _WIN32
    if (timer.available()) {
        require(!timer.wait(0) && !timer.wait(uint64_t(-1)), "invalid relative durations rejected");
        require(timer.wait(20000) && timer.wait(10000), "actual auto-reset OS timer can be rearmed");
        DWORD before = 0, after = 0;
        require(GetProcessHandleCount(GetCurrentProcess(), &before), "handle query before");
        for (int i = 0; i < 64; ++i) { frame_wait::Timer temporary(true); }
        require(GetProcessHandleCount(GetCurrentProcess(), &after) && before == after, "timer ownership releases OS handles");
        std::cout << "Windows high-resolution timer exercised (no SDL/window/game)\n";
    } else {
        require(timer.error() != 0, "unsupported Windows reports creation error");
        std::cout << "Windows high-resolution timer unavailable; legacy fallback covered\n";
    }
#else
    require(!timer.available() && !timer.wait(10000), "non-Windows preserves legacy backend");
#endif
    timer.close(); timer.close();
    require(!timer.available() && !timer.wait(10000), "explicit close is idempotent and disables wait");
}
}

int main() {
    try {
        policy(); platform();
        std::cout << "frame wait synthetic tests passed; no game executed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
