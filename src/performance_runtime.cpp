#include "performance_runtime.h"
#include "attribution.h"
#include "memory.h"
#include "render/native.h"
#include "hle/gles.h"
#include "texpack.h"
#include "lights.h"
#include "log.h"
#include "settings.h"
#include "config.h"
#include "jni/jni.h"
#include "save_io.h"
#include <SDL.h>
#include <algorithm>
#include <map>
#include <sstream>
#include <exception>
#include <iomanip>

namespace perf {
namespace {
FrameWindow frames;
HitchWindow hitches;
uint64_t frame_serial=0;
bool manual_capture=false;
uint64_t last_time = 0, reports = 0;
unsigned interval_seconds = 1;
ProcessSample previous;
using ThreadKey = std::pair<uint64_t,uint64_t>;  // OS ID + registration generation, protects ID reuse
std::map<ThreadKey, uint64_t> previous_threads;
uint32_t window_state = 0;
int effective_cap = 0;
bool fast_forward = false, turbo = false, failed = false;
SDL_Window* window = nullptr;
void presentation_context() {
    if (!window) return;
    SDL_version version{}; SDL_GetVersion(&version);
    const char* driver = SDL_GetCurrentVideoDriver();
    const int display = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode mode{};
    const bool valid = display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0;
    if (!valid) LOGW("[perf] display_mode_query_failed: %s", SDL_GetError());
    int ww=0,wh=0,dw=0,dh=0;
    SDL_GetWindowSize(window,&ww,&wh); SDL_GL_GetDrawableSize(window,&dw,&dh);
    LOGI("[perf] presentation sdl=%u.%u.%u video_driver=%s display=%d display_mode_valid=%d refresh_hz=%d refresh_known=%d actual_swap_interval=%d window=%dx%d drawable=%dx%d (configuration and call cadence, not measured display/GPU presentation)",
         unsigned(version.major),unsigned(version.minor),unsigned(version.patch),driver?driver:"unavailable",
         display,valid,valid?mode.refresh_rate:0,valid && mode.refresh_rate>0,SDL_GL_GetSwapInterval(),ww,wh,dw,dh);
}
std::string stage_fields(const StageData& stages) {
    std::ostringstream text; text << std::fixed << std::setprecision(3);
    for (size_t i=0;i<stage_count;++i) text << ' ' << stage_name(i) << "_ms=" << double(stages.ns[i])/1e6
        << ' ' << stage_name(i) << "_calls=" << stages.calls[i];
    text << " stages_valid=" << stages.valid << " stage_errors=" << stages.errors;
    return text.str();
}
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
    presentation_context();
    LOGI("[perf] frame_loop_ms mean=%.3f p50=%.3f p95=%.3f p99=%.3f max_retained=%.3f work=%.3f game_render=%.3f swap=%.3f limiter_sleep=%.3f limiter_spin=%.3f retained=%llu overwritten=%llu",
         s.loop_mean_ms,s.p50_ms,s.p95_ms,s.p99_ms,s.max_ms,s.work_ms,s.game_render_ms,s.swap_ms,s.sleep_ms,s.spin_ms,
          (unsigned long long)s.retained,(unsigned long long)s.overwritten);
    LOGI("[perf] frame_budget loop_gt25ms=%llu loop_gt33_333ms=%llu loop_ge50ms=%llu work_gt16_667ms=%llu work_gt25ms=%llu work_gt33_333ms=%llu start_intervals=%llu start_gt25ms=%llu start_gt33_333ms=%llu start_ge50ms=%llu (overlapping counts over all interval frames; cap/focus matter)",
         (unsigned long long)s.loop_gt25,(unsigned long long)s.loop_gt33,(unsigned long long)s.loop_ge50,
         (unsigned long long)s.work_gt16,(unsigned long long)s.work_gt25,(unsigned long long)s.work_gt33,
         (unsigned long long)s.start_intervals,(unsigned long long)s.start_gt25,(unsigned long long)s.start_gt33,(unsigned long long)s.start_ge50);
    LOGI("[perf] cadence_ms between_mean=%.3f between_max=%.3f start_interval_mean=%.3f start_interval_max=%.3f swap_call_interval_mean=%.3f swap_call_interval_max=%.3f (between includes preceding sampler/scheduling; swap-call intervals are NOT display intervals)",
         s.between_mean_ms,s.between_max_ms,s.start_mean_ms,s.start_max_ms,s.swap_call_mean_ms,s.swap_call_max_ms);
    std::ostringstream stage_mean,stage_max,stage_calls;
    stage_mean << std::fixed << std::setprecision(3);stage_max << std::fixed << std::setprecision(3);
    for (size_t i=0;i<stage_count;++i) {
        stage_mean << ' ' << stage_name(i) << '=' << s.stage_mean_ms[i];
        stage_max << ' ' << stage_name(i) << '=' << s.stage_max_ms[i];
        stage_calls << ' ' << stage_name(i) << '=' << s.stage_calls[i];
    }
    LOGI("[perf] stages_mean_ms%s",stage_mean.str().c_str());
    LOGI("[perf] stages_max_ms%s",stage_max.str().c_str());
    LOGI("[perf] stage_calls%s valid_frames=%llu errors=%llu (exclusive wall buckets; guest_hle_other is not pure guest/update CPU; maxima need not share a frame)",stage_calls.str().c_str(),(unsigned long long)s.stage_frames,(unsigned long long)s.stage_errors);
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
    metrics_line("owners",owner_sample());
    m.clear();jni::performance_snapshot(m);metrics_line("jni_owners",m);
    const auto registry=save_io::diagnostic_registry_counts();
    LOGI("[perf] io_registry valid=%d descriptor_records=%llu stream_records=%llu (overlapping registry records, not OS handle counts)",
         registry.valid,(unsigned long long)registry.descriptors,(unsigned long long)registry.streams);
    auto frame_line=[&](const char* kind,const FrameRecord& record) {
        const auto& f=record.frame;
        LOGI("[perf] %s frame=%llu scene=%llu age_ms=%.3f loop_ms=%.3f work_ms=%.3f game_render_ms=%.3f swap_ms=%.3f sleep_ms=%.3f spin_ms=%.3f between_ms=%.3f start_interval_ms=%.3f swap_call_interval_ms=%.3f%s",
             kind,(unsigned long long)record.serial,(unsigned long long)record.scene,
             record.when_ns<=now?double(now-record.when_ns)/1e6:0,double(f.loop_ns)/1e6,
             double(f.work_ns)/1e6,double(f.game_render_ns)/1e6,double(f.swap_ns)/1e6,double(f.sleep_ns)/1e6,double(f.spin_ns)/1e6,
             double(f.between_ns)/1e6,double(f.start_interval_ns)/1e6,double(f.swap_call_interval_ns)/1e6,stage_fields(f.stages).c_str());
    };
    for(size_t i=0;i<hitches.slow_count();++i)frame_line("hitch",hitches.slow(i));
    if(hitches.dropped())LOGI("[perf] hitch_omitted=%llu (worst 16 retained, threshold 50ms)",(unsigned long long)hitches.dropped());
    hitches.reset_slow();
    attribution::report(manual_capture);
    if(manual_capture) {
        LOGI("[perf] history_begin retained=%zu max_frames=180; manual marker, no PCM or GPU timing",hitches.recent_count());
        for(size_t i=0;i<hitches.recent_count();++i)frame_line("history",hitches.recent(i));
        LOGI("[perf] history_end");manual_capture=false;
    }
    auto events = take_events();
    for (const auto& e : events.entries)
        LOGI("[perf] event type=%s label=%s age_ms=%.1f",e.type.c_str(),e.label.c_str(),e.when_ns<=now?double(now-e.when_ns)/1e6:0);
    if(events.dropped) LOGI("[perf] event_drops=%llu",(unsigned long long)events.dropped);
    previous_threads.swap(next); previous=process; last_time=now; frames.reset();
    LOGI("[perf] sampler_elapsed_ms=%.3f (includes diagnostic logging; excluded from frame_loop timings)",double(now_ns()-start)/1e6);
}
}
void runtime_window(SDL_Window* value) { window = value; }
void safe_report(uint64_t now,bool final) {
    try { report(now,final); }
    catch(const std::exception& e) {
        failed=true;
        LOGW("[perf] sampler stopped after diagnostic failure: %s (guest execution unchanged)",e.what());
    }
}
bool runtime_interval(unsigned seconds) {
    if (last_time || seconds < 1 || seconds > 60) return false;
    interval_seconds = seconds;
    return true;
}
void runtime_frame(Frame f,uint32_t flags,int cap,bool ff,bool accelerated) {
    if(!enabled() || failed)return;
    window_state=flags;
    effective_cap=cap;fast_forward=ff;turbo=accelerated;
    auto now=now_ns();
    if(!last_time) {
        LOGI("[perf] enabled version=6 interval_seconds=%u max_frames=4096 max_cache_scan=4096 max_threads=64 history_frames=180 hitch_threshold_ms=50 max_hitches=16 F3=manual-marker; shared-GLES geometry capture supports both renderers; exclusive main-thread stages plus bounded detail attribution; exact still-scene counts; thread-exit timer close enabled even without --perf; CPU 100%%=one logical core; bytes are not MiB; no GPU timing/VRAM claim; --fps override see startup log", interval_seconds);
        safe_report(now,false);
        return;  // baseline excludes this first frame, which precedes the baseline sample
    }
    frames.add(f);
    hitches.add({++frame_serial,now,scene_serial(),f});
    manual_capture=take_capture_request() || manual_capture;
    if(now-last_time>=uint64_t(interval_seconds)*1000000000ull || (manual_capture && now-last_time>=1000000000ull))safe_report(now,false);
}
void runtime_finish() {
    if(enabled() && !failed && last_time) {
        manual_capture=take_capture_request() || manual_capture;
        safe_report(now_ns(),true);
    }
}
double runtime_worst_ms() {
    uint64_t worst=0;
    for(size_t i=0;i<hitches.recent_count();++i)worst=std::max(worst,hitches.recent(i).frame.loop_ns);
    return double(worst)/1e6;
}
}  // namespace perf
