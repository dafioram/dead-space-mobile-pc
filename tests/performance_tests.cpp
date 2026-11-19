#include "performance.h"
#include "memory.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace {
void require(bool okay,const char* why) { if(!okay)throw std::runtime_error(why); }
uint64_t metric(const perf::Metrics& values,const char* name) {
    for(auto m:values)if(std::string(m.name)==name)return m.value;
    throw std::runtime_error("missing metric");
}
perf::Metrics memory() { perf::Metrics out; mem::performance_snapshot(out); return out; }
void frames() {
    perf::FrameWindow w;
    require(w.summary().frames==0,"empty window");
    for(uint64_t i=1;i<=100;++i)w.add({i*1000000,1000000,500000,200000,300000,100000});
    auto s=w.summary();
    require(s.p50_ms==50 && s.p95_ms==95 && s.p99_ms==99 && s.max_ms==100,"nearest-rank percentiles");
    require(s.loop_mean_ms==50.5 && s.game_render_ms==0.5 && s.spin_ms==0.1,"phase averages");
    w.reset();
    for(uint64_t i=1;i<=10000;++i)w.add({i*1000000});
    s=w.summary();
    require(s.frames==10000 && s.retained==4096 && s.overwritten==5904,"bounded rolling frame storage");
    require(s.max_ms==10000 && s.p50_ms==7952,"percentiles use retained samples");
    require(s.loop_mean_ms==5000.5,"means cover all interval frames");
    w.reset();require(w.summary().frames==0,"reset");
    require(perf::cpu_percent(10,30,10)==200,"process CPU uses one-core denominator");
    require(perf::cpu_percent(30,10,10)==0 && perf::cpu_percent(0,10,0)==0,"reset/unavailable intervals");
}
void allocator() {
    require(mem::init(),"synthetic guest reservation");
    auto a=mem::heap_alloc(1),b=mem::heap_alloc(8192);
    auto live=memory();
    require(metric(live,"heap_live_blocks")==2 && metric(live,"heap_live_block_bytes")==8208,"rounded live allocations");
    b=mem::heap_realloc(b,16384);
    require(metric(memory(),"heap_live_blocks")==2,"realloc balances old/new blocks");
    mem::heap_free(a);mem::heap_free(b);
    auto freed=memory();
    require(metric(freed,"heap_live_block_bytes")==0 && metric(freed,"heap_live_blocks")==0,"frees lower live counters");
    require(metric(freed,"heap_committed_bytes")>=16*1024*1024,"freed guest heap retains committed high water");
    require(metric(freed,"heap_counter_errors")==0,"no counter underflow");
    auto committed=metric(freed,"all_guest_mapped_bytes");
    auto map=mem::map_alloc(12345);
    require(metric(memory(),"all_guest_mapped_bytes")==committed+16384,"mapped pages tracked");
    mem::map_free(map,12345);
    require(metric(memory(),"all_guest_mapped_bytes")==committed,"map free removes committed pages");
    const auto before=metric(memory(),"heap_committed_bytes");
    for(int i=0;i<10000;++i) { auto p=mem::heap_alloc(31);mem::heap_free(p); }
    require(metric(memory(),"heap_committed_bytes")==before,"warm allocator reuses capacity");
    require(metric(memory(),"guest_address_reservation_bytes")==1ull<<32,"virtual reservation is separate");
    std::vector<std::thread> workers;
    for(int t=0;t<4;++t)workers.emplace_back([] {
        for(int n=0;n<500;++n) {auto p=mem::heap_alloc(128);mem::heap_free(p);}
    });
    for(auto& t:workers)t.join();
    require(metric(memory(),"heap_live_blocks")==0 && metric(memory(),"heap_counter_errors")==0,"concurrent allocator counters balance");
}
void stages_and_budgets() {
    perf::StageClock clock;
    clock.begin(100);
    auto host=clock.enter(perf::Stage::guest_hle_other,110);
    auto guest=clock.enter(perf::Stage::vertices,130);
    clock.leave(guest,160);
    auto same=clock.enter(perf::Stage::guest_hle_other,170);
    clock.leave(same,180);
    clock.leave(host,190);
    auto data=clock.finish(200);
    require(data.valid && data.errors==0,"valid balanced stage frame");
    require(data.ns[size_t(perf::Stage::host_other)]==20 && data.ns[size_t(perf::Stage::guest_hle_other)]==50 && data.ns[size_t(perf::Stage::vertices)]==30,"nested stages are exclusive, including same-stage recursion");
    require(std::accumulate(data.ns.begin(),data.ns.end(),uint64_t(0))==100 && data.calls[size_t(perf::Stage::guest_hle_other)]==2,"stage coverage and scope counts");
    clock.begin(100);clock.enter(perf::Stage::post,101);
    require(clock.finish(102).errors==1,"unfinished scope is explicit");
    clock.begin(100);clock.enter(perf::Stage::post,99);clock.leave(perf::Stage::host_other,101);
    require(clock.finish(102).errors==1,"backwards clock is explicit");
    perf::FrameWindow window;
    const uint64_t loops[]={25000000,25000001,33333333,50000000};
    const uint64_t work[]={16666667,16666668,25000000,33333334};
    for(size_t i=0;i<4;++i) {
        perf::Frame frame{loops[i],work[i]};
        frame.stages.valid=true;frame.stages.ns[0]=frame.loop_ns;
        frame.stages.calls[0]=1;frame.between_ns=5000000;
        frame.start_interval_ns=loops[i];frame.swap_call_interval_ns=10000000;
        window.add(frame);
    }
    auto summary=window.summary();
    require(summary.loop_gt25==3 && summary.loop_gt33==1 && summary.loop_ge50==1,"exact inclusive/exclusive loop thresholds");
    require(summary.work_gt16==3 && summary.work_gt25==1 && summary.work_gt33==1,"work thresholds exclude limiter and swap");
    require(summary.start_intervals==4 && summary.start_gt25==3 && summary.start_gt33==1 && summary.start_ge50==1,"start-to-start budgets include between-frame gaps");
    require(summary.between_mean_ms==5 && summary.start_max_ms==50 && summary.swap_call_mean_ms==10,"cadence aggregates");
    require(summary.stage_frames==4 && summary.stage_errors==0 && summary.stage_calls[0]==4 && summary.stage_max_ms[0]==50 && summary.stage_mean_ms[0]==summary.loop_mean_ms,"exclusive stage aggregation");
    perf::Frame broken{1};broken.stages.valid=true;window.add(broken);
    require(window.summary().stage_errors==1,"stage coverage mismatch is not hidden");
    window.reset();summary=window.summary();
    require(summary.stage_frames==0 && summary.stage_calls[0]==0 && summary.start_intervals==0 && summary.loop_gt25==0,"all detail aggregates reset");
    perf::stages_begin(perf::now_ns());
    try {perf::StageScope scope(perf::Stage::normals);throw std::runtime_error("synthetic");}catch(const std::runtime_error&){}
    auto scoped=perf::stages_finish(perf::now_ns());
    require(scoped.valid && scoped.errors==0 && scoped.calls[size_t(perf::Stage::normals)]==1,"exception unwinding restores stage");
    {perf::StageScope scope(perf::Stage::vertices);}
    require(!perf::stages_finish(perf::now_ns()).valid,"outside-frame stages are ignored");
    std::array<perf::StageData,2> threaded{};
    std::vector<std::thread> workers;
    for(size_t i=0;i<2;++i)workers.emplace_back([&,i] {
        perf::stages_begin(perf::now_ns());
        {perf::StageScope scope(i?perf::Stage::vertices:perf::Stage::post);}
        threaded[i]=perf::stages_finish(perf::now_ns());
    });
    for(auto& worker:workers)worker.join();
    require(threaded[0].calls[size_t(perf::Stage::vertices)]==0 && threaded[1].calls[size_t(perf::Stage::post)]==0,"stage clocks never mix thread data");
    perf::SceneCacheGauge cache;
    for(int i=0;i<6000;++i)cache.insert();
    for(int i=0;i<1500;++i)cache.retire();
    cache.hit(false);cache.hit(true);++cache.refused;
    require(cache.live==4500 && cache.dead==1500 && cache.inserted==6000 && cache.live_hits==1 && cache.dead_hits==1,"cache counts exact beyond old scan limit");
    cache.reset();require(cache.live==0 && cache.dead==0 && cache.cleared_live==4500 && cache.cleared_dead==1500 && cache.resets==1,"cache scene-reset accounting keeps lifetime totals");
    cache.retire();require(cache.errors==1 && cache.dead==0,"cache underflow explicit");
}
void detailed_observers() {
    auto before=metric(perf::owner_sample(),"asset_file_live");
    { perf::OwnedResource file(perf::Owner::asset_file);
      require(metric(perf::owner_sample(),"asset_file_live")==before+1,"owned resource acquisition"); }
    require(metric(perf::owner_sample(),"asset_file_live")==before,"owned resource lifetime balanced");
    perf::timer_error(perf::TimerError::close,6);
    require(metric(perf::owner_sample(),"wait_timer_close_failures")==1 && metric(perf::owner_sample(),"wait_timer_close_last_error")==6,"timer failure operation and Windows error remain explicit");
    perf::AudioWindow audio;
    audio.add({22528,24576,2048,10000000,2000000});
    audio.add({20000,22048,2048,120000000,0});
    const auto values=audio.metrics();
    require(metric(values,"window_writes")==2 && metric(values,"queue_before_min_bytes")==20000,"audio bounded queue range");
    require(metric(values,"gap_le10ms")==1 && metric(values,"gap_gt100ms")==1 && metric(values,"backpressure_mean_us")==1000,"audio cadence and wait buckets");
    perf::audio_configure(44100,2,2,1024);
    perf::audio_write({24576,26624,2048,11000000,2000000});
    auto sample=perf::audio_sample();
    require(metric(sample,"queue_after_max_us")==150929 && metric(sample,"device_buffer_frames")==1024,"PCM queue duration is format-derived, not end-to-end latency");
    require(metric(perf::audio_sample(),"window_writes")==0,"interval audio histogram resets");
    perf::HitchWindow history;
    for(uint64_t i=1;i<=200;++i) {
        perf::Frame frame{i==190?100000000ull:i==200?200000000ull:16000000ull};
        frame.stages.valid=true;frame.stages.ns[size_t(perf::Stage::normals)]=i;
        frame.stages.calls[size_t(perf::Stage::normals)]=1;frame.between_ns=i*1000;
        history.add({i,i*1000000,3,frame});
    }
    require(history.recent_count()==180 && history.recent(0).serial==21 && history.recent(179).serial==200,"bounded ordered frame history");
    require(history.slow_count()==2,"hitch threshold");
    require(history.recent(179).frame.stages.ns[size_t(perf::Stage::normals)]==200 && history.recent(179).frame.between_ns==200000,"history preserves same-frame stage/cadence records");
    require(history.slow(0).frame.stages.ns[size_t(perf::Stage::normals)]==190,"hitch keeps original stage attribution, not a later frame");
    for(uint64_t i=0;i<40;++i)history.add({201+i,0,3,{50000000+i*1000000}});
    require(history.slow_count()==16 && history.dropped()==26,"bounded worst-hitch retention");
    uint64_t worst=0;
    for(size_t i=0;i<history.slow_count();++i)worst=std::max(worst,history.slow(i).frame.loop_ns);
    require(worst==200000000,"overflow preserves worst observed hitch");
    history.reset_slow();require(history.slow_count()==0 && history.recent_count()==180,"draining hitches preserves rolling history");
}
}
int main() {
    try {
        require(!perf::enabled(),"disabled by default");
        perf::stages_begin(100);{perf::StageScope scope(perf::Stage::post);}
        require(!perf::stages_finish(200).valid,"disabled stage capture stays empty");
        perf::event("test","ignored");require(perf::take_events().entries.empty(),"disabled observer does not queue");
        require(perf::command("/perf").find("disabled")!=std::string::npos,"startup opt-in required");
        frames();
        perf::enable();
        stages_and_budgets();
        detailed_observers();
        perf::AllocationGauge gauge;gauge.add(32);gauge.add(64);gauge.remove(32);
        require(gauge.bytes==64 && gauge.blocks==1 && gauge.peak_bytes==96,"allocation gauge");
        gauge.remove(100);require(gauge.errors==1 && gauge.bytes==64,"underflow is diagnostic only");
        for(int i=0;i<100;++i)perf::event("unsafe\n",std::string(100,'x'));
        auto events=perf::take_events();require(events.entries.size()==32 && events.dropped==68,"bounded events");
        require(events.entries[0].label.size()==48 && events.entries[0].type=="unsafe_","sanitized labels");
        require(perf::command("/perf_mark_idle").find("idle")!=std::string::npos,"marker command");
        require(perf::take_events().entries[0].label=="idle","marker event");
        perf::jit_created(64ull<<20);perf::jit_created(16ull<<20);
        require(perf::jit_sample().instances==2 && perf::jit_sample().configured_bytes==80ull<<20,"JIT configured accounting");
        perf::jit_destroyed(16ull<<20);perf::jit_destroyed(64ull<<20);
        require(perf::jit_sample().instances==0,"JIT teardown accounting");
        perf::thread_enter();require(perf::thread_samples().size()==1,"register current thread");
        auto first=perf::process_sample();require(first.cpu_valid,"process CPU query");
        std::thread worker([] {perf::thread_enter();perf::thread_leave();});worker.join();
        require(perf::thread_samples().size()==1,"finished worker query ownership retired");
        const auto first_generation=perf::thread_samples()[0].generation;
        perf::thread_leave();require(perf::thread_samples().empty(),"current query handle released");
        perf::thread_enter();require(perf::thread_samples()[0].generation>first_generation,"thread registration generations survive OS ID reuse");
        perf::thread_leave();
        allocator();
        std::cout<<"performance diagnostics synthetic tests passed; no game executed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
