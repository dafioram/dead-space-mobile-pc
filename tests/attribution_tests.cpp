#include "attribution.h"
#include "performance.h"
#include "geometry_source.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
std::vector<std::string> logs;
void require(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
bool contains(const char* text){for(auto& line:logs)if(line.find(text)!=std::string::npos)return true;return false;}
uint64_t compute(uint64_t x){for(unsigned j=0;j<64;++j)x=(x*1664525+1013904223)^(x>>13);return x;}
void capture(const float* vertices,size_t n,const uint16_t* indices,size_t count,bool native=false) {
    const geometry::Source v{reinterpret_cast<const unsigned char*>(vertices),n*3*sizeof(float)};
    const geometry::Source ix{reinterpret_cast<const unsigned char*>(indices),count*sizeof(uint16_t)};
    geometry::Submission input;input.indexed=true;input.native=native;input.count=uint32_t(count);
    input.vertices={v.reader(),42,0,0x1406,3,0,true};input.indices=ix.reader();
    input.state.projection[11]=-1;input.state.projection[15]=0;input.state.modelview[14]=-5;
    input.state.color={1,0,0,1};
    attribution::geometry_submit(input);
}
uint64_t workload(bool instrument,unsigned frames){
    uint64_t result=0;
    for(unsigned f=0;f<frames;++f){
        if(instrument)attribution::frame_begin();
        for(unsigned i=0;i<128;++i){
            if(instrument){attribution::Scope guest(attribution::guest_run);result^=compute(i+f);
                attribution::Scope hle(attribution::hle_base+1);result^=compute(i+f+1);}
            else {result^=compute(i+f);result^=compute(i+f+1);}
        }
        if(instrument){attribution::fixture_work(123,64,32);attribution::Scope fixture(attribution::fixture_scan);result^=compute(f+17);}
        else result^=compute(f+17);
        if(instrument && attribution::geometry_enabled()) {
            const float vertices[]={0,0,0,1,0,0,0,1,0};const uint16_t indices[]={0,1,2};
            capture(vertices,3,indices,3);
        }
        if(instrument)attribution::frame_end();
    }
    return result;
}
}
namespace logging {
bool g_verbose=false;
void write(char,const char* fmt,...){char b[4096];va_list ap;va_start(ap,fmt);std::vsnprintf(b,sizeof b,fmt,ap);va_end(ap);
    require(logs.size()<5000 && std::strlen(b)<3000,"bounded output");logs.emplace_back(b);}
}
int main(int argc,char** argv){try{
    attribution::Clock c;c.begin(100);
    auto root=c.enter(attribution::guest_run,110);
    auto hle=c.enter(attribution::hle_base+1,150);
    auto nested=c.enter(attribution::guest_run,170);
    c.leave(nested,190);c.leave(hle,210);c.leave(root,250);c.end(260);
    require(c.counters[attribution::other].ns==20 && c.counters[attribution::guest_run].ns==100 && c.counters[attribution::hle_base+1].ns==40 && !c.errors,"exclusive nested guest/HLE partition");
    c.begin(300);c.leave(0,310);require(c.errors==1,"scope underflow diagnosed");
    const std::string mode=argc>1?argv[1]:"sampled";const bool geometry=argc>2;
    require(!attribution::configure("invalid",false),"invalid mode rejected");
    require(attribution::configure(mode,geometry),"startup mode accepted");
    perf::enable();attribution::register_hle(1,"pthread_cond_wait\nINJECTED");
    attribution::register_hle(attribution::hle_capacity+99,"overflow");
    std::thread background([]{attribution::Scope s(attribution::hle_base+1);require(!attribution::collecting,"background threads not counted as main");});background.join();
    workload(true,32);
    attribution::frame_begin();
    for(unsigned i=0;i<20000;++i){attribution::Scope bounded(attribution::fixture_scan);}
    attribution::frame_end();
    require(!attribution::configure("off",false),"no unrequested runtime switches");
    if(geometry){
        attribution::frame_begin();
        float vertices[]={0,0,0,100,0,0,0,1,0};uint16_t indices[]={0,1,2};
        for(int i=0;i<600;++i)capture(vertices,3,indices,3,i%2==0);
        indices[2]=9;capture(vertices,3,indices,3);
        indices[2]=2;vertices[3]=std::numeric_limits<float>::infinity();
        capture(vertices,3,indices,3);vertices[3]=100;
        for(int i=0;i<2000;++i)capture(vertices,3,indices,3);
        attribution::frame_end();
        for(int i=0;i<10;++i)attribution::report(true);
        require(contains("flags=4") && contains("flags=2") && contains("geometry_end"),"long-edge/index suspects reported, not suppressed");
        require(contains("flags=1") && contains("vertices_checked=0"),"nonfinite evidence retained and exhausted scan budget explicit");
        unsigned dumps=0;for(auto& l:logs)if(l.find("geometry_begin")!=std::string::npos)++dumps;
        require(dumps==8,"geometry dump count bounded");
        require(contains("renderer=legacy") && contains("renderer=native") && contains("location=shared-GLES-input"),"both renderer inputs retained and coverage explicit");
    }else attribution::report(false);
    require(contains("scope_errors=0") && contains("bookkeeping_observed_ns="),"self-cost and accounting validity exposed");
    if(mode=="off")require(contains("scope_pairs=0"),"detail off has no timed scopes");
    else {
        require(contains("name=pthread_cond_wait?INJECTED") && contains("non_gl_hle_id="),"names sanitized, non-GL attribution retained");
        require(contains("scope_budget_skips=3616"),"clock work per frame capped without unbalanced scopes");
    }
    // Synthetic A/B only: identical deterministic work, no assets/GL/guest/JIT.
    using clock=std::chrono::steady_clock;std::vector<double> off,on;uint64_t checksum=0;
    for(int trial=0;trial<7;++trial){auto a=clock::now();const auto x=workload(false,128);auto b=clock::now();const auto y=workload(true,128);auto end=clock::now();
        require(x==y,"instrumentation preserves synthetic workload");checksum^=x;
        off.push_back(std::chrono::duration<double,std::nano>(b-a).count()/128);on.push_back(std::chrono::duration<double,std::nano>(end-b).count()/128);}
    std::sort(off.begin(),off.end());std::sort(on.begin(),on.end());
    std::cout<<"synthetic_detail_benchmark mode="<<mode<<" geometry="<<geometry<<" baseline_ns_per_frame="<<off[3]<<" instrumented_ns_per_frame="<<on[3]
             <<" incremental_ns_per_frame="<<on[3]-off[3]<<" checksum="<<checksum<<" (synthetic scope workload, NOT gameplay overhead)\n";
    attribution::report(false);
    std::cout<<"Attribution nesting, modes, bounded metadata and self-cost tests passed; no game executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
