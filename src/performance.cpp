#include "performance.h"
#include <algorithm>
#include <chrono>
#include <mutex>

namespace perf {
std::atomic<bool> active{false};
namespace {
std::atomic<uint64_t> jit_count{0}, jit_bytes{0};
std::atomic<uint64_t> audio_queue{0}, audio_threshold{0}, audio_underruns{0}, audio_writes{0};
std::mutex event_lock;
std::vector<Event> events;
uint64_t events_dropped = 0;
std::string label(const std::string& input) {
    std::string out;
    for (unsigned char ch : input) {
        if (out.size() == 48) break;
        out += (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
               (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' ? char(ch) : '_';
    }
    return out;
}
}
void enable() { active.store(true, std::memory_order_relaxed); }
uint64_t now_ns() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
void AllocationGauge::add(uint64_t size) {
    bytes += size;
    ++blocks;
    peak_bytes = std::max(peak_bytes, bytes);
}
void AllocationGauge::remove(uint64_t size) {
    if (!blocks || bytes < size) { ++errors; return; }
    bytes -= size;
    --blocks;
}
void FrameWindow::add(Frame f) {
    loops_[frames_ % capacity] = f.loop_ns;
    ++frames_;
    const uint64_t values[] = {f.loop_ns, f.work_ns, f.game_render_ns, f.swap_ns, f.sleep_ns, f.spin_ns};
    for (size_t i = 0; i < sums_.size(); ++i) sums_[i] += values[i];
}
FrameSummary FrameWindow::summary() const {
    FrameSummary s;
    s.frames = frames_;
    if (!frames_) return s;
    s.retained = std::min<uint64_t>(frames_, capacity);
    s.overwritten = frames_ - s.retained;
    auto sorted = loops_;
    std::sort(sorted.begin(), sorted.begin() + size_t(s.retained));
    auto percentile = [&](uint64_t percent) {
        size_t rank = size_t((s.retained * percent + 99) / 100 - 1);
        return double(sorted[rank]) / 1e6;
    };
    s.p50_ms = percentile(50); s.p95_ms = percentile(95); s.p99_ms = percentile(99);
    s.max_ms = double(sorted[size_t(s.retained) - 1]) / 1e6;
    double* means[] = {&s.loop_mean_ms, &s.work_ms, &s.game_render_ms, &s.swap_ms, &s.sleep_ms, &s.spin_ms};
    for (size_t i = 0; i < sums_.size(); ++i) *means[i] = double(sums_[i] / frames_ / 1e6L);
    return s;
}
void FrameWindow::reset() { frames_ = 0; sums_.fill(0); }
double cpu_percent(uint64_t previous, uint64_t current, uint64_t elapsed_ns) {
    if (!elapsed_ns || current < previous) return 0;
    return double(current - previous) * 100.0 / double(elapsed_ns);
}
void jit_created(uint64_t size) {
    jit_count.fetch_add(1, std::memory_order_relaxed);
    jit_bytes.fetch_add(size, std::memory_order_relaxed);
}
void jit_destroyed(uint64_t size) {
    jit_bytes.fetch_sub(size, std::memory_order_relaxed);
    jit_count.fetch_sub(1, std::memory_order_relaxed);
}
JitSample jit_sample() { return {jit_count.load(std::memory_order_relaxed), jit_bytes.load(std::memory_order_relaxed)}; }
void audio_observe(uint64_t queued, uint64_t threshold, uint64_t underruns) {
    audio_queue.store(queued, std::memory_order_relaxed);
    audio_threshold.store(threshold, std::memory_order_relaxed);
    audio_underruns.store(underruns, std::memory_order_relaxed);
    audio_writes.fetch_add(1, std::memory_order_relaxed);
}
Metrics audio_sample() {
    return {{"audio_queue_last_write_bytes", audio_queue.load(std::memory_order_relaxed)},
            {"audio_backpressure_threshold_bytes", audio_threshold.load(std::memory_order_relaxed)},
            {"audio_underruns", audio_underruns.load(std::memory_order_relaxed)},
            {"audio_observed_writes", audio_writes.load(std::memory_order_relaxed)}};
}
void event(const std::string& type, const std::string& text) {
    if (!enabled()) return;
    std::lock_guard lock(event_lock);
    if (events.size() >= 32) { ++events_dropped; return; }
    events.push_back({label(type), label(text), now_ns()});
}
Events take_events() {
    std::lock_guard lock(event_lock);
    Events result;
    result.entries.swap(events);
    result.dropped = events_dropped;
    events_dropped = 0;
    return result;
}
std::string command(const std::string& text) {
    if (!enabled()) return "performance diagnostics disabled; start with --perf (startup-only)";
    if (text == "/perf" || text == "/perf_status") return "performance diagnostics on; interval in startup log; /perf mark LABEL";
    if (text.starts_with("/perf_mark_") && text.size() > 11) {
        auto marker = label(text.substr(11));
        event("mark", marker);
        return "performance marker: " + marker;
    }
    return "usage: /perf status | /perf mark LABEL (letters/numbers, max 48 characters)";
}
}  // namespace perf
