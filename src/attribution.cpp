#include "attribution.h"
#include "performance.h"
#include "log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

namespace attribution {
thread_local bool collecting=false,hle_selected=false;
thread_local bool geometry_frame=false;
namespace {
Clock clock;
bool detailed=false,geometry=false,full=false,started=false;
uint64_t frames=0,sampled=0,cpu_ns=0,wall_ns=0,cpu_start=0,wall_start=0,cpu_valid=0;
uint64_t scope_pairs=0,bookkeeping_ns=0,geometry_ns=0,geometry_skipped=0,draw_serial=0;
uint64_t interval_frames=0,interval_sampled=0,scene=0,geometry_dumps=0;
uint64_t fixture_key=0,fixture_vertices=0,fixture_triangles=0,fixture_max_ns=0,worst_fixture_key=0;
geometry::Budget geometry_budget;
uint32_t frame_draws=0,previous_frame_draws=0;
unsigned scope_budget=0;
uint64_t scope_budget_skips=0;
std::mutex names_lock;
std::array<std::array<char,72>,hle_capacity> names{};
std::array<Draw,256> recent{},perspective_recent{};
std::array<Draw,32> suspects{};
std::array<Draw,16> red_candidates{};
uint64_t recent_total=0,perspective_total=0,suspect_total=0,red_total=0;
uint64_t legacy_draws=0,native_draws=0,array_draws=0,indexed_draws=0,capture_budget_skips=0;
void scene_check() {
    const auto current=perf::scene_serial();
    if(current!=scene){recent_total=perspective_total=suspect_total=red_total=0;scene=current;}
}
uint64_t thread_cpu() {
#ifdef _WIN32
    FILETIME created,exited,kernel,user;
    if (!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user)) return 0;
    return ((uint64_t(kernel.dwHighDateTime)<<32 | kernel.dwLowDateTime)+
            (uint64_t(user.dwHighDateTime)<<32 | user.dwLowDateTime))*100;
#else
    timespec t{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t))return 0;
    return uint64_t(t.tv_sec)*1000000000+uint64_t(t.tv_nsec);
#endif
}
void draw_line(const char* kind,const Draw& d) {
    LOGI("[detail] geometry kind=%s draw=%llu scene=%llu frame=%llu renderer=%s indexed=%d perspective=%d source=%08x vertex_buffer=%u texture=%u return=%08x mode=%x count=%u first=%u vertex_type=%x components=%u stride=%u index_type=%x index_pointer=%08x element_buffer=%u sampled_index_lo=%u sampled_index_hi=%u flags=%u vertices_checked=%u indices_checked=%u primitives_checked=%u max_eye_edge=%.5g max_local_edge=%.5g max_aspect=%.5g colors_checked=%u color_array=%d color_type=%x color_components=%u color_pointer=%08x color_buffer=%u color_mean=%.4g,%.4g,%.4g,%.4g",
        kind,(unsigned long long)d.serial,(unsigned long long)d.scene,(unsigned long long)d.frame,d.native?"native":"legacy",d.indexed,d.perspective,
        d.source,d.vertex_buffer,d.texture,d.return_address,d.mode,d.count,d.first,d.vertex_type,d.components,d.stride,d.index_type,d.index_pointer,d.element_buffer,
        d.lo,d.hi,d.flags,d.vertices_checked,d.indices_checked,d.primitives_checked,d.max_edge,d.max_local_edge,d.max_aspect,d.colors_checked,d.color_array,d.color_type,d.color_components,d.color_pointer,d.color_buffer,
        d.color_mean[0],d.color_mean[1],d.color_mean[2],d.color_mean[3]);
    LOGI("[detail] geometry_state draw=%llu local_bounds=%.5g,%.5g,%.5g:%.5g,%.5g,%.5g eye_bounds=%.5g,%.5g,%.5g:%.5g,%.5g,%.5g ndc_bounds=%.5g,%.5g,%.5g:%.5g,%.5g,%.5g clip_w=%.5g:%.5g nonpositive_clip_w=%u state_flags=%u blend=%x,%x alpha_blend=%x,%x viewport=%d,%d,%d,%d current_color=%.4g,%.4g,%.4g,%.4g modelview=%.5g,%.5g,%.5g,%.5g/%.5g,%.5g,%.5g,%.5g/%.5g,%.5g,%.5g,%.5g/%.5g,%.5g,%.5g,%.5g projection=%.5g,%.5g,%.5g,%.5g/%.5g,%.5g,%.5g,%.5g/%.5g,%.5g,%.5g,%.5g/%.5g,%.5g,%.5g,%.5g",
        (unsigned long long)d.serial,d.local_lo[0],d.local_lo[1],d.local_lo[2],d.local_hi[0],d.local_hi[1],d.local_hi[2],
        d.eye_lo[0],d.eye_lo[1],d.eye_lo[2],d.eye_hi[0],d.eye_hi[1],d.eye_hi[2],d.ndc_lo[0],d.ndc_lo[1],d.ndc_lo[2],d.ndc_hi[0],d.ndc_hi[1],d.ndc_hi[2],
        d.clip_w_lo,d.clip_w_hi,d.behind_eye,d.state.flags,d.state.blend_src,d.state.blend_dst,d.state.blend_alpha_src,d.state.blend_alpha_dst,d.state.viewport[0],d.state.viewport[1],d.state.viewport[2],d.state.viewport[3],
        d.state.color[0],d.state.color[1],d.state.color[2],d.state.color[3],
        d.state.modelview[0],d.state.modelview[1],d.state.modelview[2],d.state.modelview[3],d.state.modelview[4],d.state.modelview[5],d.state.modelview[6],d.state.modelview[7],
        d.state.modelview[8],d.state.modelview[9],d.state.modelview[10],d.state.modelview[11],d.state.modelview[12],d.state.modelview[13],d.state.modelview[14],d.state.modelview[15],
        d.state.projection[0],d.state.projection[1],d.state.projection[2],d.state.projection[3],d.state.projection[4],d.state.projection[5],d.state.projection[6],d.state.projection[7],
        d.state.projection[8],d.state.projection[9],d.state.projection[10],d.state.projection[11],d.state.projection[12],d.state.projection[13],d.state.projection[14],d.state.projection[15]);
}
}  // namespace
void Clock::charge(uint64_t now) {
    if(now<last_){++errors;last_=now;return;}
    counters[current_].ns+=now-last_;last_=now;
}
uint32_t Clock::enter(uint32_t id,uint64_t now) {
    charge(now);const auto previous=current_;
    current_=std::min(id,bucket_count-1);++counters[current_].calls;++depth_;return previous;
}
void Clock::leave(uint32_t previous,uint64_t now) {
    charge(now);if(!depth_){++errors;return;}--depth_;current_=std::min(previous,bucket_count-1);
}
void Clock::end(uint64_t now) { charge(now);if(depth_)++errors; }
bool configure(const std::string& mode,bool capture) {
    if(started || (mode!="off" && mode!="sampled" && mode!="full"))return false;
    detailed=mode!="off";full=mode=="full";geometry=capture;return true;
}
void register_hle(uint32_t id,const std::string& name) {
    if(id>=hle_capacity)return;
    std::lock_guard guard(names_lock);auto& out=names[id];
    for(size_t i=0;i<std::min(name.size(),out.size()-1);++i)out[i]=name[i]>=' ' && name[i]<='~'?name[i]:'?';
}
void frame_begin() {
    started=true;++frames;++interval_frames;
    collecting=perf::enabled() && detailed;
    hle_selected=collecting && (full || frames%16==0);
    if(hle_selected){++sampled;++interval_sampled;wall_start=perf::now_ns();cpu_start=thread_cpu();}
    if(collecting)clock.begin(perf::now_ns());
    geometry_budget={};geometry_frame=geometry && perf::enabled();
    geometry_budget.per_draw_vertices=std::clamp(8192u/std::max(previous_frame_draws,1u),3u,96u);
    geometry_budget.sample_phase=uint32_t(frames);frame_draws=0;
    scope_budget=16384;
    scene_check();
}
void frame_end() {
    if(collecting)clock.end(perf::now_ns());
    if(hle_selected){const auto cpu=thread_cpu();wall_ns+=perf::now_ns()-wall_start;
        if(cpu_start && cpu>=cpu_start){cpu_ns+=cpu-cpu_start;++cpu_valid;}}
    collecting=hle_selected=false;
    if(geometry_frame)previous_frame_draws=frame_draws;
    geometry_frame=false;
}
Scope::Scope(uint32_t bucket) : bucket_(std::min(bucket,bucket_count-1)) {
    if(!collecting || ((bucket==guest_run || bucket==jit_setup || bucket>=hle_base) && !hle_selected))return;
    if(!scope_budget){++scope_budget_skips;return;}
    --scope_budget;
    const auto start=perf::now_ns();previous_=clock.enter(bucket_,start);start_=start;live_=true;tag_=fixture_key;
    bookkeeping_ns+=perf::now_ns()-start;++scope_pairs;
}
Scope::~Scope() {
    if(!live_)return;
    const auto start=perf::now_ns();clock.leave(previous_,start);
    auto& counter=clock.counters[bucket_];
    if(start-start_>counter.max_ns){counter.max_ns=start-start_;counter.max_frame=frames;}
    if(bucket_==fixture_scan && start-start_>fixture_max_ns){fixture_max_ns=start-start_;worst_fixture_key=tag_;}
    bookkeeping_ns+=perf::now_ns()-start;
}
bool geometry_enabled() { return geometry_frame; }
void fixture_work(uint64_t key,uint32_t vertices,uint32_t triangles) {
    if(!collecting)return;
    fixture_key=key;fixture_vertices+=vertices;fixture_triangles+=triangles;
}
void geometry_submit(const geometry::Submission& input,uint64_t observation_start_ns) {
    if(!geometry_enabled())return;
    const auto start=observation_start_ns?observation_start_ns:perf::now_ns();scene_check();
    if(frame_draws<UINT32_MAX)++frame_draws;
    ++(input.native?native_draws:legacy_draws);++(input.indexed?indexed_draws:array_draws);
    Draw d=geometry::analyze(input,geometry_budget);d.serial=++draw_serial;d.scene=scene;d.frame=frames;
    if((d.flags&geometry::partial) && !d.vertices_checked)++capture_budget_skips;
    if(d.flags&geometry::partial)++geometry_skipped;
    recent[recent_total++%recent.size()]=d;
    if(d.perspective)perspective_recent[perspective_total++%perspective_recent.size()]=d;
    const auto same=[](const Draw& a,const Draw& b){return a.source==b.source && a.vertex_buffer==b.vertex_buffer && a.texture==b.texture && a.count==b.count && a.first==b.first && a.index_pointer==b.index_pointer && a.element_buffer==b.element_buffer && a.mode==b.mode && a.native==b.native && (a.flags&~uint32_t(geometry::partial))==(b.flags&~uint32_t(geometry::partial));};
    const auto score=[](const Draw& r){return double((r.flags&(geometry::nonfinite|geometry::unreadable))?1e12:0)+
        double((r.flags&geometry::thin_triangle)?1e6:0)+std::min(double(r.max_aspect),1e5)+double(r.max_edge);};
    if((d.flags&~uint32_t(geometry::partial)) && (d.perspective || (d.flags&(geometry::nonfinite|geometry::unreadable)))){
        bool duplicate=false;
        for(size_t i=0;i<std::min<uint64_t>(suspect_total,suspects.size());++i) {
            if(same(suspects[i],d)){if(score(d)>=score(suspects[i]))suspects[i]=d;duplicate=true;break;}
        }
        if(!duplicate){
            if(suspect_total<suspects.size())suspects[suspect_total++]=d;
            else {auto smallest=std::min_element(suspects.begin(),suspects.end(),[&](const auto& a,const auto& b){return score(a)<score(b);});if(score(d)>score(*smallest))*smallest=d;}
        }
    }
    if(d.perspective && d.colors_checked && d.color_mean[0]>.15f && d.color_mean[0]>d.color_mean[1]*1.4f && d.color_mean[0]>d.color_mean[2]*1.4f)
        red_candidates[red_total++%red_candidates.size()]=d;
    geometry_ns+=perf::now_ns()-start;
}
void report(bool marker) {
    const char* labels[]={"unclassified","jit_run_exclusive","fixture_scan_other","texture_lookup","texture_readback","texture_mask","mesh_sampling","fixture_merge","jit_setup","glow_analysis","fan_analysis","beacon_analysis","light_setup","open_direction","texture_copy"};
    LOGI("[detail] summary mode=%s sampled_frames=%llu interval_frames=%llu total_sampled=%llu scope_pairs=%llu bookkeeping_observed_ns=%llu sampled_main_wall_ns=%llu sampled_main_cpu_ns=%llu cpu_valid_frames=%llu scope_errors=%llu geometry=%d geometry_observed_ns=%llu geometry_partial_draws=%llu scope_budget_skips=%llu max_scopes_per_frame=16384 (bookkeeping is a lower-bound observation, NOT total overhead; CPU time is OS-resolution; JIT run includes compilation; saturated scopes remain in parent bucket; no GPU timing)",
        detailed?(full?"full":"sampled-1-in-16"):"off",(unsigned long long)interval_sampled,(unsigned long long)interval_frames,(unsigned long long)sampled,
        (unsigned long long)scope_pairs,(unsigned long long)bookkeeping_ns,(unsigned long long)wall_ns,(unsigned long long)cpu_ns,(unsigned long long)cpu_valid,
        (unsigned long long)clock.errors,geometry,(unsigned long long)geometry_ns,(unsigned long long)geometry_skipped,(unsigned long long)scope_budget_skips);
    for(uint32_t i=1;i<std::size(labels);++i){const auto& c=clock.counters[i];LOGI("[detail] bucket=%s ns=%llu calls=%llu max_inclusive_ns=%llu max_loop=%llu",labels[i],(unsigned long long)c.ns,(unsigned long long)c.calls,(unsigned long long)c.max_ns,(unsigned long long)c.max_frame);}
    LOGI("[detail] fixture_input vertices=%llu triangles=%llu worst_mesh_key=%llx worst_scan_inclusive_ns=%llu (input workload, not number of accepted lamps or sampled texels)",
        (unsigned long long)fixture_vertices,(unsigned long long)fixture_triangles,(unsigned long long)worst_fixture_key,(unsigned long long)fixture_max_ns);
    std::array<uint32_t,hle_capacity+1> order{};for(uint32_t i=0;i<order.size();++i)order[i]=hle_base+i;
    std::partial_sort(order.begin(),order.begin()+12,order.end(),[](auto a,auto b){return clock.counters[a].ns>clock.counters[b].ns;});
    {std::lock_guard guard(names_lock);for(unsigned i=0;i<12;++i){const auto id=order[i];const auto& c=clock.counters[id];if(!c.calls)continue;
        LOGI("[detail] hle_id=%u name=%s exclusive_ns=%llu calls=%llu max_inclusive_ns=%llu max_loop=%llu",id-hle_base,id-hle_base<hle_capacity?names[id-hle_base].data():"overflow",
            (unsigned long long)c.ns,(unsigned long long)c.calls,(unsigned long long)c.max_ns,(unsigned long long)c.max_frame);}
        std::array<uint64_t,5> category{};
        auto classify=[](const char* n){const std::string name(n);
            if(name.starts_with("gl"))return 0;
            if(name.starts_with("JNI") || name.starts_with("JavaVM"))return 1;
            if(name.find("pthread")!=std::string::npos || name.find("sem_")!=std::string::npos || name.find("sleep")!=std::string::npos)return 2;
            if(name.find("read")!=std::string::npos || name.find("write")!=std::string::npos || name.find("open")!=std::string::npos || name.find("seek")!=std::string::npos || name.find("fclose")!=std::string::npos)return 3;
            return 4;};
        std::array<uint32_t,hle_capacity> nongl{};size_t n=0;
        for(uint32_t id=0;id<hle_capacity;++id){const auto kind=classify(names[id].data());category[kind]+=clock.counters[hle_base+id].ns;
            if(kind && clock.counters[hle_base+id].calls)nongl[n++]=hle_base+id;}
        LOGI("[detail] hle_categories gl_ns=%llu jni_ns=%llu sync_sleep_ns=%llu io_ns=%llu other_ns=%llu overflow_ns=%llu (exclusive sampled wall; name-based categories, not proof of blocking)",
            (unsigned long long)category[0],(unsigned long long)category[1],(unsigned long long)category[2],(unsigned long long)category[3],(unsigned long long)category[4],(unsigned long long)clock.counters.back().ns);
        const size_t shown=std::min<size_t>(8,n);std::partial_sort(nongl.begin(),nongl.begin()+shown,nongl.begin()+n,[](auto a,auto b){return clock.counters[a].ns>clock.counters[b].ns;});
        for(size_t i=0;i<shown;++i){const auto id=nongl[i];const auto& c=clock.counters[id];LOGI("[detail] non_gl_hle_id=%u name=%s exclusive_ns=%llu calls=%llu max_inclusive_ns=%llu max_loop=%llu",id-hle_base,names[id-hle_base].data(),(unsigned long long)c.ns,(unsigned long long)c.calls,(unsigned long long)c.max_ns,(unsigned long long)c.max_frame);}}
    if(geometry)LOGI("[detail] geometry_coverage location=shared-GLES-input legacy_draws=%llu native_draws=%llu arrays_draws=%llu indexed_draws=%llu budget_exhausted_draws=%llu max_draws_analyzed_per_frame=2048 max_vertex_reads_per_frame=8192 max_primitives_per_draw=32 last_per_draw_vertex_budget=%u (adaptive to prior draw count; rotating samples; both renderers; safe CPU reads only; no GL queries/readback; input/eye/clip evidence, NOT final pixels)",
        (unsigned long long)legacy_draws,(unsigned long long)native_draws,(unsigned long long)array_draws,(unsigned long long)indexed_draws,(unsigned long long)capture_budget_skips,geometry_budget.per_draw_vertices);
    if(marker && geometry && geometry_dumps<8){++geometry_dumps;
        LOGI("[detail] geometry_begin dump=%llu max_dumps=8 recent_retained=%llu perspective_retained=%llu suspect_retained=%llu (flags 1=nonfinite 2=unreadable 4=eye_edge_over_20 8=unsupported 16=zero_w 32=thin_triangle 64=partial; heuristics are NOT proof; shared GLES before renderer selection)",
            (unsigned long long)geometry_dumps,(unsigned long long)std::min<uint64_t>(recent_total,recent.size()),(unsigned long long)std::min<uint64_t>(perspective_total,perspective_recent.size()),(unsigned long long)std::min<uint64_t>(suspect_total,suspects.size()));
        if(!recent_total)LOGW("[detail] geometry capture has no shared draw records; no geometry-validity conclusion is possible");
        for(size_t i=0;i<std::min<uint64_t>(suspect_total,suspects.size());++i)draw_line("suspect",suspects[i]);
        for(uint64_t i=perspective_total>64?perspective_total-64:0;i<perspective_total;++i)draw_line("perspective",perspective_recent[i%perspective_recent.size()]);
        for(uint64_t i=recent_total>16?recent_total-16:0;i<recent_total;++i)draw_line("recent",recent[i%recent.size()]);
        for(uint64_t i=red_total>16?red_total-16:0;i<red_total;++i)draw_line("red_hint",red_candidates[i%red_candidates.size()]);
        LOGI("[detail] geometry_end");}
    clock.counters={};clock.errors=0;interval_frames=interval_sampled=scope_pairs=bookkeeping_ns=wall_ns=cpu_ns=cpu_valid=geometry_ns=geometry_skipped=0;
    fixture_vertices=fixture_triangles=fixture_max_ns=worst_fixture_key=0;
    scope_budget_skips=0;
    legacy_draws=native_draws=array_draws=indexed_draws=capture_budget_skips=0;
}
}  // namespace attribution
