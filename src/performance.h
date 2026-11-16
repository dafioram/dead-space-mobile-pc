#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace perf {
extern std::atomic<bool> active;
inline bool enabled() { return active.load(std::memory_order_relaxed); }
// Startup-only opt-in, before guest allocation/thread creation. No runtime toggle.
void enable();
uint64_t now_ns();
inline uint64_t stamp() { return enabled() ? now_ns() : 0; }

struct Metric { const char* name; uint64_t value; };
using Metrics = std::vector<Metric>;
constexpr size_t cache_scan_limit = 4096;

// Used only under the owning allocator lock. Bytes mean allocated block size,
// including size-class rounding, not original malloc request size.
struct AllocationGauge {
    uint64_t bytes = 0, blocks = 0, peak_bytes = 0, errors = 0;
    void add(uint64_t size);
    void remove(uint64_t size);
};

struct Frame {
    uint64_t loop_ns = 0, work_ns = 0, game_render_ns = 0, swap_ns = 0, sleep_ns = 0, spin_ns = 0;
};
struct FrameSummary {
    uint64_t frames = 0, retained = 0, overwritten = 0;
    double loop_mean_ms = 0, p50_ms = 0, p95_ms = 0, p99_ms = 0, max_ms = 0;
    double work_ms = 0, game_render_ms = 0, swap_ms = 0, sleep_ms = 0, spin_ms = 0;
};
class FrameWindow {
public:
    static constexpr size_t capacity = 4096;
    void add(Frame frame);
    FrameSummary summary() const;
    void reset();
private:
    std::array<uint64_t, capacity> loops_{};
    uint64_t frames_ = 0;
    std::array<long double, 6> sums_{};
};
double cpu_percent(uint64_t previous, uint64_t current, uint64_t elapsed_ns);

struct ProcessSample {
    bool cpu_valid = false, memory_valid = false;
    uint64_t cpu_ns = 0, resident_bytes = 0, peak_resident_bytes = 0, private_bytes = 0;
    const char* private_kind = "unavailable";
};
ProcessSample process_sample();
struct ThreadSample { uint64_t id = 0, generation = 0, cpu_ns = 0; bool valid = false; };
// Registry holds at most 64 query handles/clock IDs. Only opted-in guest threads.
void thread_enter();
void thread_leave();
std::vector<ThreadSample> thread_samples();
uint64_t thread_queries_dropped();

struct JitSample { uint64_t instances = 0, configured_bytes = 0; };
void jit_created(uint64_t configured_bytes);
void jit_destroyed(uint64_t configured_bytes);
JitSample jit_sample();
void audio_observe(uint64_t queued, uint64_t threshold, uint64_t underruns);
Metrics audio_sample();

struct Event { std::string type, label; uint64_t when_ns = 0; };
struct Events { std::vector<Event> entries; uint64_t dropped = 0; };
// Content is bounded/sanitized; no addresses, save contents or asset payloads.
void event(const std::string& type, const std::string& label);
Events take_events();
std::string command(const std::string& normalized);
}  // namespace perf
