#include "performance_runtime.h"
#include "memory.h"
#include "render/native.h"
#include "hle/gles.h"
#include "texpack.h"
#include "lights.h"
#include "log.h"
#include "settings.h"
#include "config.h"
#include <SDL.h>
#include <algorithm>
#include <map>
#include <sstream>
#include <exception>

namespace perf {
namespace {
FrameWindow frames;
uint64_t last_time = 0, reports = 0;
ProcessSample previous;
using ThreadKey = std::pair<uint64_t,uint64_t>;  // OS ID + registration generation, protects ID reuse
std::map<ThreadKey, uint64_t> previous_threads;
uint32_t window_state = 0;
int effective_cap = 0;
bool fast_forward = false, turbo = false, failed = false;
void metrics_line(const char* group, const Metrics& metrics) {
    std::string line = std::string("[perf] ") + group;
    for (auto m : metrics) {
        std::string field = " " + std::string(m.name) + "=" + std::to_string(m.value);
        if (line.size() + field.size() > 3000) { LOGI("%s", line.c_str()); line = std::string("[perf] ") + group; }
        line += field;
    }
    LOGI("%s", line.c_str());
}
void report(uint64_t now, bool final) {
    auto start = now_ns();
    auto process = process_sample();
    uint64_t elapsed = last_time ? now - last_time : 0;
    auto s = frames.summary();
    LOGI("[perf] sample=%llu final=%d interval_ms=%.1f frames=%llu fps=%.1f renderer=%s resolution=%dx%d msaa=%d effective_cap=%d vsync_setting=%d focused=%d minimized=%d fast_forward=%d turbo=%d",
         (unsigned long long)++reports, final, double(elapsed)/1e6, (unsigned long long)s.frames,
         elapsed ? double(s.frames)*1e9/double(elapsed) : 0, render::enabled()?"native":"legacy",
         g_config.screen_w, g_config.screen_h, hle::msaa_samples(), effective_cap,
         settings::get_int("vsync",1), bool(window_state&SDL_WINDOW_INPUT_FOCUS), bool(window_state&SDL_WINDOW_MINIMIZED),fast_forward,turbo);
    LOGI("[perf] quality ao=%d shadows=%d lights=%d light_shadows=%d bloom=%d anisotropy=%d",
         render::ao(),render::shadows(),render::lights_level(),render::light_shadows(),render::bloom(),render::anisotropy());
    LOGI("[perf] frame_loop_ms mean=%.3f p50=%.3f p95=%.3f p99=%.3f max_retained=%.3f work=%.3f game_render=%.3f swap=%.3f limiter_sleep=%.3f limiter_spin=%.3f retained=%llu overwritten=%llu",
         s.loop_mean_ms,s.p50_ms,s.p95_ms,s.p99_ms,s.max_ms,s.work_ms,s.game_render_ms,s.swap_ms,s.sleep_ms,s.spin_ms,
         (unsigned long long)s.retained,(unsigned long long)s.overwritten);
    LOGI("[perf] process cpu_valid=%d cpu_one_core_pct=%.2f memory_valid=%d resident_bytes=%llu peak_resident_bytes=%llu private_kind=%s private_bytes=%llu",
         process.cpu_valid && previous.cpu_valid && elapsed,
         process.cpu_valid && previous.cpu_valid ? cpu_percent(previous.cpu_ns,process.cpu_ns,elapsed) : 0,
         process.memory_valid,(unsigned long long)process.resident_bytes,(unsigned long long)process.peak_resident_bytes,
         process.private_kind,(unsigned long long)process.private_bytes);
    auto threads = thread_samples();
    std::vector<std::pair<double,uint64_t>> hot;
    std::map<ThreadKey,uint64_t> next;
    for (auto t : threads) if (t.valid) {
        const ThreadKey key{t.id,t.generation};
        auto it = previous_threads.find(key);
        if (it != previous_threads.end() && elapsed) hot.push_back({cpu_percent(it->second,t.cpu_ns,elapsed),t.id});
        next[key] = t.cpu_ns;
    }
    std::sort(hot.rbegin(),hot.rend());
    std::ostringstream text;
    for(size_t i=0;i<std::min(size_t(8),hot.size());++i) text << " os_tid=" << hot[i].second << ":" << hot[i].first << "%";
    auto jit = jit_sample();
    const auto valid_queries=std::count_if(threads.begin(),threads.end(),[](auto t){return t.valid;});
    LOGI("[perf] guest_threads tracked=%zu valid_queries=%zu registration_drops=%llu jit_instances=%llu jit_configured_bytes=%llu hottest_one_core%s",
         threads.size(),size_t(valid_queries),(unsigned long long)thread_queries_dropped(),(unsigned long long)jit.instances,
         (unsigned long long)jit.configured_bytes,text.str().c_str());
    Metrics m; m.reserve(96);
    mem::performance_snapshot(m); metrics_line("guest_memory",m); m.clear();
    render::performance_snapshot(m); metrics_line("native_cache",m); m.clear();
    hle::performance_snapshot(m); metrics_line("gles_cache",m); m.clear();
    texpack::performance_snapshot(m); metrics_line("png_cache",m); m.clear();
    lights::performance_snapshot(m); metrics_line("scene_cache",m);
    metrics_line("audio",audio_sample());
    auto events = take_events();
    for (const auto& e : events.entries)
        LOGI("[perf] event type=%s label=%s age_ms=%.1f",e.type.c_str(),e.label.c_str(),e.when_ns<=now?double(now-e.when_ns)/1e6:0);
    if(events.dropped) LOGI("[perf] event_drops=%llu",(unsigned long long)events.dropped);
    previous_threads.swap(next); previous=process; last_time=now; frames.reset();
    LOGI("[perf] sampler_elapsed_ms=%.3f (includes diagnostic logging; excluded from frame_loop timings)",double(now_ns()-start)/1e6);
}
}
void safe_report(uint64_t now,bool final) {
    try { report(now,final); }
    catch(const std::exception& e) {
        failed=true;
        LOGW("[perf] sampler stopped after diagnostic failure: %s (guest execution unchanged)",e.what());
    }
}
void runtime_frame(Frame f,uint32_t flags,int cap,bool ff,bool accelerated) {
    if(!enabled() || failed)return;
    window_state=flags;
    effective_cap=cap;fast_forward=ff;turbo=accelerated;
    auto now=now_ns();
    if(!last_time) {
        LOGI("[perf] enabled version=1 interval=1s max_frames=4096 max_cache_scan=4096 max_threads=64; CPU 100%%=one logical core; bytes are not MiB; no GPU timing/VRAM claim; --fps override see startup log");
        safe_report(now,false);
        return;  // baseline excludes this first frame, which precedes the baseline sample
    }
    frames.add(f);
    if(now-last_time>=1000000000ull)safe_report(now,false);
}
void runtime_finish() { if(enabled() && !failed && last_time)safe_report(now_ns(),true); }
}  // namespace perf
