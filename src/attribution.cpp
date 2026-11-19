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
namespace {
Clock clock;
bool detailed=false,geometry=false,full=false,started=false;
uint64_t frames=0,sampled=0,cpu_ns=0,wall_ns=0,cpu_start=0,wall_start=0,cpu_valid=0;
uint64_t scope_pairs=0,bookkeeping_ns=0,geometry_ns=0,geometry_skipped=0,draw_serial=0;
uint64_t interval_frames=0,interval_sampled=0,scene=0,geometry_dumps=0;
uint64_t fixture_key=0,fixture_vertices=0,fixture_triangles=0,fixture_max_ns=0,worst_fixture_key=0;
unsigned vertex_budget=0;
std::mutex names_lock;
std::array<std::array<char,72>,hle_capacity> names{};
std::array<Draw,256> recent{};
std::array<Draw,32> suspects{};
uint64_t recent_total=0,suspect_total=0;
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
    LOGI("[detail] geometry kind=%s draw=%llu scene=%llu source=%08x mode=%x count=%u index_lo=%u index_hi=%u flags=%u vertices_checked=%u triangles_checked=%u max_edge=%.3f local_bounds=%.3f,%.3f,%.3f:%.3f,%.3f,%.3f world_bounds=%.3f,%.3f,%.3f:%.3f,%.3f,%.3f transform=%.4g,%.4g,%.4g,%.4g/%.4g,%.4g,%.4g,%.4g/%.4g,%.4g,%.4g,%.4g/%.4g,%.4g,%.4g,%.4g",
        kind,(unsigned long long)d.serial,(unsigned long long)d.scene,d.source,d.mode,d.count,d.lo,d.hi,d.flags,d.vertices_checked,d.triangles_checked,d.max_edge,
        d.local_lo[0],d.local_lo[1],d.local_lo[2],d.local_hi[0],d.local_hi[1],d.local_hi[2],
        d.world_lo[0],d.world_lo[1],d.world_lo[2],d.world_hi[0],d.world_hi[1],d.world_hi[2],
        d.transform[0],d.transform[1],d.transform[2],d.transform[3],d.transform[4],d.transform[5],d.transform[6],d.transform[7],
        d.transform[8],d.transform[9],d.transform[10],d.transform[11],d.transform[12],d.transform[13],d.transform[14],d.transform[15]);
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
    vertex_budget=4096;
    const auto current=perf::scene_serial();
    if(current!=scene){recent_total=suspect_total=0;scene=current;}
}
void frame_end() {
    if(collecting)clock.end(perf::now_ns());
    if(hle_selected){const auto cpu=thread_cpu();wall_ns+=perf::now_ns()-wall_start;
        if(cpu_start && cpu>=cpu_start){cpu_ns+=cpu-cpu_start;++cpu_valid;}}
    collecting=hle_selected=false;
}
Scope::Scope(uint32_t bucket) : bucket_(std::min(bucket,bucket_count-1)) {
    if(!collecting || ((bucket==guest_run || bucket==jit_setup || bucket>=hle_base) && !hle_selected))return;
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
bool geometry_enabled() { return geometry && perf::enabled(); }
void fixture_work(uint64_t key,uint32_t vertices,uint32_t triangles) {
    if(!collecting)return;fixture_key=key;fixture_vertices+=vertices;fixture_triangles+=triangles;
}
void geometry_draw(Draw d,const float* vertices,uint32_t vertex_count,unsigned stride,const uint16_t* indices,size_t count) {
    if(!geometry_enabled())return;
    const auto current=perf::scene_serial();if(current!=scene){recent_total=suspect_total=0;scene=current;}
    const auto start=perf::now_ns();d.serial=++draw_serial;d.scene=scene;
    const auto n=std::min({vertex_count,64u,vertex_budget});vertex_budget-=n;
    if(n<vertex_count)++geometry_skipped;
    d.local_lo.fill(std::numeric_limits<float>::infinity());d.world_lo=d.local_lo;
    d.local_hi.fill(-std::numeric_limits<float>::infinity());d.world_hi=d.local_hi;
    auto world=[&](const float* p,unsigned axis){return d.transform[axis]*p[0]+d.transform[4+axis]*p[1]+d.transform[8+axis]*p[2]+d.transform[12+axis];};
    for(unsigned k=0;k<n;++k){const unsigned index=n==vertex_count?k:unsigned(uint64_t(k)*vertex_count/n);const float* p=vertices+size_t(index)*stride;
        ++d.vertices_checked;for(unsigned a=0;a<3;++a){const float w=world(p,a);
            if(!std::isfinite(p[a]) || !std::isfinite(w))d.flags|=1;
            else {d.local_lo[a]=std::min(d.local_lo[a],p[a]);d.local_hi[a]=std::max(d.local_hi[a],p[a]);d.world_lo[a]=std::min(d.world_lo[a],w);d.world_hi[a]=std::max(d.world_hi[a],w);}}}
    if(n && d.mode==4) for(size_t k=0;k+2<count && k<96;k+=3){
        const auto a=indices[k],b=indices[k+1],c=indices[k+2];
        if(a>=vertex_count || b>=vertex_count || c>=vertex_count){d.flags|=2;continue;}
        ++d.triangles_checked;
        for(auto edge:std::array<std::array<uint16_t,2>,3>{{{a,b},{b,c},{c,a}}}){
            const float* p=vertices+size_t(edge[0])*stride;const float* q=vertices+size_t(edge[1])*stride;
            float squared=0;for(unsigned axis=0;axis<3;++axis){const float delta=world(p,axis)-world(q,axis);squared+=delta*delta;}
            if(!std::isfinite(squared))d.flags|=1;else d.max_edge=std::max(d.max_edge,std::sqrt(squared));}}
    if(d.max_edge>20)d.flags|=4; // heuristic, not a verdict; never suppress/modify a draw
    recent[recent_total++%recent.size()]=d;
    if(d.flags){bool duplicate=false;for(size_t i=0;i<std::min<uint64_t>(suspect_total,suspects.size());++i)
        if(suspects[i].source==d.source && suspects[i].count==d.count && suspects[i].flags==d.flags){suspects[i]=d;duplicate=true;break;}
        if(!duplicate)suspects[suspect_total++%suspects.size()]=d;}
    geometry_ns+=perf::now_ns()-start;
}
void report(bool marker) {
    const char* labels[]={"unclassified","jit_run_exclusive","fixture_scan_other","texture_lookup","texture_readback","texture_mask","mesh_sampling","fixture_merge","jit_setup","glow_analysis","fan_analysis","beacon_analysis","light_setup","open_direction","texture_copy"};
    LOGI("[detail] summary mode=%s sampled_frames=%llu interval_frames=%llu total_sampled=%llu scope_pairs=%llu bookkeeping_observed_ns=%llu sampled_main_wall_ns=%llu sampled_main_cpu_ns=%llu cpu_valid_frames=%llu scope_errors=%llu geometry=%d geometry_observed_ns=%llu geometry_partial_draws=%llu (bookkeeping is a lower-bound observation, NOT total overhead; CPU time is OS-resolution; JIT run includes compilation; no GPU timing)",
        detailed?(full?"full":"sampled-1-in-16"):"off",(unsigned long long)interval_sampled,(unsigned long long)interval_frames,(unsigned long long)sampled,
        (unsigned long long)scope_pairs,(unsigned long long)bookkeeping_ns,(unsigned long long)wall_ns,(unsigned long long)cpu_ns,(unsigned long long)cpu_valid,
        (unsigned long long)clock.errors,geometry,(unsigned long long)geometry_ns,(unsigned long long)geometry_skipped);
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
    if(marker && geometry && geometry_dumps<8){++geometry_dumps;
        LOGI("[detail] geometry_begin dump=%llu max_dumps=8 recent_retained=%llu suspect_retained=%llu vertex_budget_per_frame=4096 sampled_vertices_per_draw=64 sampled_triangles_per_draw=32 (sampled bounds; flags 1=nonfinite 2=index 4=edge_over_20m; heuristics may flag legitimate geometry)",
            (unsigned long long)geometry_dumps,(unsigned long long)std::min<uint64_t>(recent_total,recent.size()),(unsigned long long)std::min<uint64_t>(suspect_total,suspects.size()));
        for(size_t i=0;i<std::min<uint64_t>(suspect_total,suspects.size());++i)draw_line("suspect",suspects[i]);
        // Keep marker I/O bounded: newest 32 metadata records plus up to 32 scene suspects.
        for(uint64_t i=recent_total>32?recent_total-32:0;i<recent_total;++i)draw_line("recent",recent[i%recent.size()]);
        LOGI("[detail] geometry_end");}
    clock.counters={};clock.errors=0;interval_frames=interval_sampled=scope_pairs=bookkeeping_ns=wall_ns=cpu_ns=cpu_valid=geometry_ns=geometry_skipped=0;
    fixture_vertices=fixture_triangles=fixture_max_ns=worst_fixture_key=0;
}
}  // namespace attribution
