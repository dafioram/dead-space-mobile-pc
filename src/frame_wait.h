#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace frame_wait {

enum class Mode { Timer, Legacy };
inline bool parse_mode(std::string_view text, Mode& mode) {
    if (text == "timer") mode = Mode::Timer;
    else if (text == "legacy") mode = Mode::Legacy;
    else return false;
    return true;
}

// One main-thread-owned, auto-reset timer. Unsupported systems retain the old
// SDL sleep/spin path; never fall back to a coarse Windows waitable timer.
class Timer {
public:
    explicit Timer(bool enabled);
    ~Timer();
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    bool available() const { return handle_ != nullptr; }
    uint32_t error() const { return error_; }
    bool wait(uint64_t duration_100ns);
    void close();
private:
    void* handle_ = nullptr;
    uint32_t error_ = 0;
};

struct Timings { uint64_t sleep_ns = 0, spin_ns = 0; };

// Host supplies the performance counter, optional diagnostic stamps, SDL delay
// and timer wait. The injected clock also lets tests exercise the production
// policy without assets, rendering, or scheduler-dependent timing assertions.
template<class Host>
Timings until(Host& host, uint64_t deadline, uint64_t frequency, uint64_t now, bool timer) {
    Timings out;
    if (now >= deadline) return out;
    if (timer) {
        const uint64_t guard = std::max<uint64_t>(1, frequency / 5000);  // 0.2 ms
        if (deadline - now > guard) {
            const uint64_t ticks = deadline - now - guard;
            // These are relative, sub-frame durations, not absolute counters.
            const auto duration = uint64_t(static_cast<long double>(ticks) * 10000000 / frequency);
            if (duration) {
                const auto start = host.stamp();
                timer = host.wait_timer(duration);
                out.sleep_ns += host.stamp() - start;
                if (!timer) now = host.now();  // failed wait may have consumed time
            }
        }
    }
    if (!timer) {
        if (now < deadline) {
            const uint64_t ms = (deadline - now) * 1000 / frequency;
            if (ms > 1) {
                const auto start = host.stamp();
                host.delay_ms(uint32_t(ms - 1));  // unchanged legacy policy
                out.sleep_ns += host.stamp() - start;
            }
        }
    }
    const auto start = host.stamp();
    while (host.now() < deadline) {}
    out.spin_ns += host.stamp() - start;
    return out;
}

}  // namespace frame_wait
