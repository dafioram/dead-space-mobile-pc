#pragma once

#include <array>
#include <algorithm>
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

// Exclusive main-thread wall-time attribution, not GPU time or a sampling
// profiler. guest_hle_other includes uninstrumented guest/JIT, HLE and driver work.
enum class Stage : size_t { host_other, guest_hle_other, native_other, vertices,
    normals, scene_cache, fixtures, shadows, upload_draw, post, present, swap, limiter, count };
constexpr size_t stage_count = size_t(Stage::count);
const char* stage_name(size_t index);
struct StageData {
    std::array<uint64_t, stage_count> ns{}, calls{};
    uint64_t errors = 0;
    bool valid = false;
};
class StageClock {
public:
    void begin(uint64_t now);
    Stage enter(Stage stage, uint64_t now);
    void leave(Stage previous, uint64_t now);
    StageData finish(uint64_t now);
private:
    void charge(uint64_t now);
    StageData data_{};
    Stage current_ = Stage::host_other;
    uint64_t last_ = 0, depth_ = 0;
};
void stages_begin(uint64_t now);
StageData stages_finish(uint64_t now);
class StageScope {
public:
    explicit StageScope(Stage stage);
    ~StageScope();
    StageScope(const StageScope&) = delete;
    StageScope& operator=(const StageScope&) = delete;
private:
    StageClock* clock_ = nullptr;
    Stage previous_ = Stage::host_other;
};

// Updated at still-scene mutation sites, with no bounded scan or eviction.
struct SceneCacheGauge {
    uint64_t inserted = 0, live = 0, dead = 0, live_hits = 0, dead_hits = 0;
    uint64_t invalidated = 0, refused = 0, resets = 0, cleared_live = 0, cleared_dead = 0, errors = 0;
    void insert() { ++inserted; ++live; }
    void hit(bool is_dead) { ++(is_dead ? dead_hits : live_hits); }
    void retire();
    void reset();
};

struct Frame {
    uint64_t loop_ns = 0, work_ns = 0, game_render_ns = 0, swap_ns = 0, sleep_ns = 0, spin_ns = 0;
    StageData stages;
    uint64_t between_ns = 0, start_interval_ns = 0, swap_call_interval_ns = 0;
};
struct FrameSummary {
    uint64_t frames = 0, retained = 0, overwritten = 0;
    double loop_mean_ms = 0, p50_ms = 0, p95_ms = 0, p99_ms = 0, max_ms = 0;
    double work_ms = 0, game_render_ms = 0, swap_ms = 0, sleep_ms = 0, spin_ms = 0;
    std::array<double, stage_count> stage_mean_ms{}, stage_max_ms{};
    std::array<uint64_t, stage_count> stage_calls{};
    uint64_t stage_frames = 0, stage_errors = 0, loop_gt25 = 0, loop_gt33 = 0, loop_ge50 = 0;
    uint64_t work_gt16 = 0, work_gt25 = 0, work_gt33 = 0;
    uint64_t start_intervals = 0, start_gt25 = 0, start_gt33 = 0, start_ge50 = 0;
    double between_mean_ms = 0, between_max_ms = 0, start_mean_ms = 0, start_max_ms = 0;
    double swap_call_mean_ms = 0, swap_call_max_ms = 0;
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
    std::array<long double, stage_count> stage_sums_{};
    FrameSummary detail_{};
    long double between_sum_ = 0, start_sum_ = 0, swap_call_sum_ = 0;
    uint64_t swap_call_intervals_ = 0;
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

// Fixed-size diagnostic ownership counters. These observe host objects, not
// necessarily one kernel handle each. Windows timers now close on thread exit;
// retired_thread_timer counts failed closes only, never a successful release.
enum class Owner : size_t { wait_timer, retired_thread_timer, asset_file, mutex, condition,
    semaphore, java_object, local_ref, global_ref, thread_query, count };
void acquired(Owner owner) noexcept;
void released(Owner owner) noexcept;
enum class TimerError : size_t { create, arm, wait, close, count };
void timer_error(TimerError operation, uint32_t error) noexcept;
Metrics owner_sample();
struct OwnedResource {
    Owner owner;
    bool counted;
    explicit OwnedResource(Owner value) : owner(value), counted(enabled()) { if (counted) acquired(owner); }
    ~OwnedResource() { if (counted) released(owner); }
    OwnedResource(const OwnedResource&) = delete;
    OwnedResource& operator=(const OwnedResource&) = delete;
};

// Audio writes are observed on their existing producer thread. Values are
// bounded histograms/counters, not PCM or sound-event identification.
struct AudioWrite {
    uint64_t before_bytes, after_bytes, write_bytes, gap_ns, wait_ns;
};
class AudioWindow {
public:
    void add(AudioWrite write);
    Metrics metrics() const;
private:
    uint64_t writes_=0, before_min_=UINT64_MAX, before_max_=0, after_min_=UINT64_MAX, after_max_=0;
    uint64_t bytes_=0, gap_sum_=0, gap_max_=0, wait_sum_=0, wait_max_=0;
    std::array<uint64_t, 8> gaps_{}, waits_{};
};
void audio_configure(unsigned rate, unsigned channels, unsigned bytes_per_sample, unsigned device_frames);
void audio_write(AudioWrite write);

struct FrameRecord { uint64_t serial=0, when_ns=0, scene=0; Frame frame; };
class HitchWindow {
public:
    static constexpr size_t capacity=180, slow_capacity=16;
    void add(FrameRecord record);
    const FrameRecord& recent(size_t oldest_index) const;
    size_t recent_count() const { return size_t(std::min<uint64_t>(total_, capacity)); }
    size_t slow_count() const { return slow_count_; }
    const FrameRecord& slow(size_t index) const { return slow_[index]; }
    uint64_t dropped() const { return dropped_; }
    void reset_slow() { slow_count_=0; dropped_=0; }
private:
    std::array<FrameRecord,capacity> recent_{};
    std::array<FrameRecord,slow_capacity> slow_{};
    uint64_t total_=0, dropped_=0;
    size_t slow_count_=0;
};
uint64_t scene_serial();
bool take_capture_request();

struct Event { std::string type, label; uint64_t when_ns = 0; };
struct Events { std::vector<Event> entries; uint64_t dropped = 0; };
// Content is bounded/sanitized; no addresses, save contents or asset payloads.
void event(const std::string& type, const std::string& label);
Events take_events();
std::string command(const std::string& normalized);
}  // namespace perf
