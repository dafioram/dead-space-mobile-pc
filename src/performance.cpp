#include "performance.h"
#include <algorithm>
#include <chrono>
#include <mutex>

namespace perf {
std::atomic<bool> active{false};
namespace {
std::atomic<uint64_t> jit_count{0}, jit_bytes{0};
std::atomic<uint64_t> audio_queue{0}, audio_threshold{0}, audio_underruns{0}, audio_writes{0};
std::array<std::atomic<uint64_t>, size_t(Owner::count)> owner_created{}, owner_closed{}, owner_live{};
std::array<std::atomic<uint64_t>, size_t(TimerError::count)> timer_errors{}, timer_last_error{};
std::atomic<unsigned> audio_rate{0}, audio_channels{0}, audio_sample_bytes{0}, audio_device_frames{0};
std::mutex audio_lock;
AudioWindow audio_window;
std::atomic<uint64_t> scene{0};
std::atomic<bool> capture_request{false};
thread_local StageClock stage_clock;
thread_local bool stage_recording = false;
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
const char* stage_name(size_t index) {
    static const char* names[] = {"host_other", "guest_hle_other", "native_other", "vertices",
        "normals", "scene_cache", "fixtures", "shadows", "upload_draw", "post", "present", "swap", "limiter"};
    static_assert(std::size(names) == stage_count);
    return names[index];
}
void StageClock::begin(uint64_t now) {
    data_ = {}; current_ = Stage::host_other; last_ = now; depth_ = 0;
    data_.valid = true;
}
void StageClock::charge(uint64_t now) {
    if (now < last_) { ++data_.errors; return; }
    data_.ns[size_t(current_)] += now - last_;
    last_ = now;
}
Stage StageClock::enter(Stage next, uint64_t now) {
    charge(now);
    const auto previous = current_;
    current_ = next; ++depth_; ++data_.calls[size_t(next)];
    return previous;
}
void StageClock::leave(Stage previous, uint64_t now) {
    charge(now);
    if (depth_) --depth_; else ++data_.errors;
    current_ = previous;
}
StageData StageClock::finish(uint64_t now) {
    charge(now);
    if (depth_) ++data_.errors;
    return data_;
}
void stages_begin(uint64_t now) {
    if (!enabled()) return;
    stage_clock.begin(now); stage_recording = true;
}
StageData stages_finish(uint64_t now) {
    if (!stage_recording) return {};
    stage_recording = false;
    return stage_clock.finish(now);
}
StageScope::StageScope(Stage stage) {
    if (enabled() && stage_recording) {
        clock_ = &stage_clock;
        previous_ = clock_->enter(stage, now_ns());
    }
}
StageScope::~StageScope() { if (clock_) clock_->leave(previous_, now_ns()); }
void SceneCacheGauge::retire() {
    if (!live) { ++errors; return; }
    --live; ++dead; ++invalidated;
}
void SceneCacheGauge::reset() {
    cleared_live += live; cleared_dead += dead; live = dead = 0; ++resets;
}
void FrameWindow::add(Frame f) {
    loops_[frames_ % capacity] = f.loop_ns;
    ++frames_;
    const uint64_t values[] = {f.loop_ns, f.work_ns, f.game_render_ns, f.swap_ns, f.sleep_ns, f.spin_ns};
    for (size_t i = 0; i < sums_.size(); ++i) sums_[i] += values[i];
    for (size_t i = 0; i < stage_count; ++i) {
        stage_sums_[i] += f.stages.ns[i];
        detail_.stage_max_ms[i] = std::max(detail_.stage_max_ms[i], double(f.stages.ns[i])/1e6);
        detail_.stage_calls[i] += f.stages.calls[i];
    }
    detail_.stage_errors += f.stages.errors;
    if (f.stages.valid) {
        ++detail_.stage_frames;
        uint64_t total=0; for (auto ns:f.stages.ns) total+=ns;
        detail_.stage_errors += total != f.loop_ns;
    }
    detail_.loop_gt25 += f.loop_ns > 25000000;
    detail_.loop_gt33 += f.loop_ns > 33333333;
    detail_.loop_ge50 += f.loop_ns >= 50000000;
    detail_.work_gt16 += f.work_ns > 16666667;
    detail_.work_gt25 += f.work_ns > 25000000;
    detail_.work_gt33 += f.work_ns > 33333333;
    between_sum_ += f.between_ns;
    detail_.between_max_ms = std::max(detail_.between_max_ms, double(f.between_ns)/1e6);
    if (f.start_interval_ns) {
        ++detail_.start_intervals; start_sum_ += f.start_interval_ns;
        detail_.start_max_ms = std::max(detail_.start_max_ms, double(f.start_interval_ns)/1e6);
        detail_.start_gt25 += f.start_interval_ns > 25000000;
        detail_.start_gt33 += f.start_interval_ns > 33333333;
        detail_.start_ge50 += f.start_interval_ns >= 50000000;
    }
    if (f.swap_call_interval_ns) {
        ++swap_call_intervals_; swap_call_sum_ += f.swap_call_interval_ns;
        detail_.swap_call_max_ms = std::max(detail_.swap_call_max_ms, double(f.swap_call_interval_ns)/1e6);
    }
}
FrameSummary FrameWindow::summary() const {
    FrameSummary s = detail_;
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
    for (size_t i = 0; i < stage_count; ++i) s.stage_mean_ms[i] = double(stage_sums_[i]/frames_/1e6L);
    s.between_mean_ms = double(between_sum_/frames_/1e6L);
    s.start_mean_ms = s.start_intervals ? double(start_sum_/s.start_intervals/1e6L) : 0;
    s.swap_call_mean_ms = swap_call_intervals_ ? double(swap_call_sum_/swap_call_intervals_/1e6L) : 0;
    return s;
}
void FrameWindow::reset() {
    frames_ = 0; sums_.fill(0); stage_sums_.fill(0); detail_ = {};
    between_sum_ = start_sum_ = swap_call_sum_ = 0; swap_call_intervals_ = 0;
}
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
    Metrics result{{"audio_queue_last_write_bytes", audio_queue.load(std::memory_order_relaxed)},
            {"audio_backpressure_threshold_bytes", audio_threshold.load(std::memory_order_relaxed)},
            {"audio_underruns", audio_underruns.load(std::memory_order_relaxed)},
            {"audio_observed_writes", audio_writes.load(std::memory_order_relaxed)}};
    AudioWindow window;
    { std::lock_guard lock(audio_lock); window=audio_window; audio_window=AudioWindow{}; }
    auto values=window.metrics();
    result.insert(result.end(),values.begin(),values.end());
    const uint64_t bytes_per_second=uint64_t(audio_rate.load())*audio_channels.load()*audio_sample_bytes.load();
    result.insert(result.end(),{{"rate_hz",audio_rate.load()},{"channels",audio_channels.load()},
        {"bytes_per_sample",audio_sample_bytes.load()},{"device_buffer_frames",audio_device_frames.load()}});
    const auto writes=std::find_if(values.begin(),values.end(),[](auto m){return std::string(m.name)=="window_writes";});
    result.push_back({"queue_duration_valid",bytes_per_second && writes!=values.end() && writes->value});
    for (auto name : {"queue_before_min_bytes","queue_before_max_bytes","queue_after_min_bytes","queue_after_max_bytes"}) {
        for (auto m:values) if(std::string(m.name)==name) {
            const char* unit=std::string(name)=="queue_before_min_bytes"?"queue_before_min_us":
                std::string(name)=="queue_before_max_bytes"?"queue_before_max_us":
                std::string(name)=="queue_after_min_bytes"?"queue_after_min_us":"queue_after_max_us";
            result.push_back({unit,bytes_per_second?m.value*1000000/bytes_per_second:0});
        }
    }
    return result;
}
void acquired(Owner owner) noexcept {
    if(!enabled())return;
    const auto i=size_t(owner);
    owner_created[i].fetch_add(1,std::memory_order_relaxed);
    owner_live[i].fetch_add(1,std::memory_order_relaxed);
}
void released(Owner owner) noexcept {
    if(!enabled())return;
    const auto i=size_t(owner);
    owner_closed[i].fetch_add(1,std::memory_order_relaxed);
    owner_live[i].fetch_sub(1,std::memory_order_relaxed);
}
Metrics owner_sample() {
    static const char* names[][3] = {
        {"wait_timer_created","wait_timer_closed","wait_timer_live"},
        {"exited_thread_timer_observed","exited_thread_timer_released","exited_thread_timer_retained"},
        {"asset_file_opened","asset_file_closed","asset_file_live"},
        {"mutex_created","mutex_destroyed","mutex_live"},
        {"condition_created","condition_destroyed","condition_live"},
        {"semaphore_created","semaphore_destroyed","semaphore_live"},
        {"java_object_created","java_object_destroyed","java_object_live"},
        {"local_ref_created","local_ref_deleted","local_ref_live"},
        {"global_ref_created","global_ref_deleted","global_ref_live"},
        {"diagnostic_thread_handle_created","diagnostic_thread_handle_closed","diagnostic_thread_handle_live"}};
    Metrics result;
    for(size_t i=0;i<size_t(Owner::count);++i) {
        result.push_back({names[i][0],owner_created[i].load()});
        result.push_back({names[i][1],owner_closed[i].load()});
        result.push_back({names[i][2],owner_live[i].load()});
    }
    static const char* error_names[]={"wait_timer_create_failures","wait_timer_arm_failures","wait_timer_wait_failures","wait_timer_close_failures"};
    static const char* last_names[]={"wait_timer_create_last_error","wait_timer_arm_last_error","wait_timer_wait_last_error","wait_timer_close_last_error"};
    for(size_t i=0;i<size_t(TimerError::count);++i) {
        result.push_back({error_names[i],timer_errors[i].load()});
        result.push_back({last_names[i],timer_last_error[i].load()});
    }
    return result;
}
void timer_error(TimerError operation,uint32_t error) noexcept {
    if (!enabled()) return;
    timer_last_error[size_t(operation)].store(error,std::memory_order_relaxed);
    timer_errors[size_t(operation)].fetch_add(1,std::memory_order_relaxed);
}
void AudioWindow::add(AudioWrite w) {
    ++writes_; bytes_+=w.write_bytes;
    before_min_=std::min(before_min_,w.before_bytes);before_max_=std::max(before_max_,w.before_bytes);
    after_min_=std::min(after_min_,w.after_bytes);after_max_=std::max(after_max_,w.after_bytes);
    gap_sum_+=w.gap_ns;gap_max_=std::max(gap_max_,w.gap_ns);
    wait_sum_+=w.wait_ns;wait_max_=std::max(wait_max_,w.wait_ns);
    auto bucket=[](uint64_t ns) {
        const uint64_t upper[]={1000000,2000000,5000000,10000000,20000000,50000000,100000000};
        size_t i=0;while(i<7 && ns>upper[i])++i;return i;
    };
    ++gaps_[bucket(w.gap_ns)];++waits_[bucket(w.wait_ns)];
}
Metrics AudioWindow::metrics() const {
    Metrics result{{"window_writes",writes_},{"window_write_bytes",bytes_},
        {"queue_before_min_bytes",writes_?before_min_:0},{"queue_before_max_bytes",before_max_},
        {"queue_after_min_bytes",writes_?after_min_:0},{"queue_after_max_bytes",after_max_},
        {"write_gap_mean_us",writes_?gap_sum_/writes_/1000:0},{"write_gap_max_us",gap_max_/1000},
        {"backpressure_mean_us",writes_?wait_sum_/writes_/1000:0},{"backpressure_max_us",wait_max_/1000}};
    static const char* gap_names[]={"gap_le1ms","gap_le2ms","gap_le5ms","gap_le10ms","gap_le20ms","gap_le50ms","gap_le100ms","gap_gt100ms"};
    static const char* wait_names[]={"wait_le1ms","wait_le2ms","wait_le5ms","wait_le10ms","wait_le20ms","wait_le50ms","wait_le100ms","wait_gt100ms"};
    for(size_t i=0;i<8;++i){result.push_back({gap_names[i],gaps_[i]});result.push_back({wait_names[i],waits_[i]});}
    return result;
}
void audio_configure(unsigned rate,unsigned channels,unsigned bytes,unsigned device_frames) {
    if(!enabled())return;
    audio_rate=rate;audio_channels=channels;audio_sample_bytes=bytes;audio_device_frames=device_frames;
}
void audio_write(AudioWrite write) {
    if(!enabled())return;
    std::lock_guard lock(audio_lock);audio_window.add(write);
}
void HitchWindow::add(FrameRecord record) {
    recent_[total_%capacity]=record;++total_;
    if(record.frame.loop_ns<50000000)return;
    if(slow_count_==slow_capacity){
        ++dropped_;
        auto smallest=std::min_element(slow_.begin(),slow_.end(),[](const auto& a,const auto& b){return a.frame.loop_ns<b.frame.loop_ns;});
        if(record.frame.loop_ns>smallest->frame.loop_ns)*smallest=record;
        return;
    }
    slow_[slow_count_++]=record;
}
const FrameRecord& HitchWindow::recent(size_t index) const {
    const auto first=total_>capacity?total_-capacity:0;
    return recent_[(first+index)%capacity];
}
uint64_t scene_serial(){return scene.load(std::memory_order_relaxed);}
bool take_capture_request(){return capture_request.exchange(false);}
void event(const std::string& type, const std::string& text) {
    if (!enabled()) return;
    if(type=="load-level")scene.fetch_add(1,std::memory_order_relaxed);
    if(type=="mark")capture_request.store(true);
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
