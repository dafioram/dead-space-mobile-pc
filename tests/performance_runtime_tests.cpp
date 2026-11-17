// Exercises the production reporter with synthetic cache/renderer adapters.
// SDL headers/constants only: no SDL init, GL context, assets, profile or guest.
#define SDL_MAIN_HANDLED
#include "performance_runtime.h"
#include "render/native.h"
#include "hle/gles.h"
#include "texpack.h"
#include "lights.h"
#include "config.h"
#include "settings.h"
#include "log.h"
#include <SDL.h>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <stdexcept>

Config g_config;
namespace {
std::vector<std::string> logs;
bool fail_adapter=false;
void require(bool b,const char* message) { if(!b)throw std::runtime_error(message); }
bool contains(const char* text) { for(auto& line:logs)if(line.find(text)!=std::string::npos)return true;return false; }
}
namespace logging {
bool g_verbose=false;
void write(char,const char* fmt,...) {
    char buffer[4096];va_list ap;va_start(ap,fmt);std::vsnprintf(buffer,sizeof buffer,fmt,ap);va_end(ap);
    logs.push_back(buffer);
}
}
namespace render {
bool enabled(){return true;}
int ao(){return 1;} int shadows(){return 2;} int lights_level(){return 2;}
bool light_shadows(){return true;} int bloom(){return 1;} int anisotropy(){return 4;}
void performance_snapshot(perf::Metrics& out) {
    if(fail_adapter)throw std::runtime_error("synthetic sampler-only failure");
    out.push_back({"lighting_images",2});
}
}
namespace hle { int msaa_samples(){return 4;} void performance_snapshot(perf::Metrics& out){out.push_back({"cpu_buffer_shadows",1});} }
namespace mem { void performance_snapshot(perf::Metrics& out){out.push_back({"heap_live_blocks",3});} }
namespace texpack { void performance_snapshot(perf::Metrics& out){out.push_back({"replacement_bytes_scanned",1024});} }
namespace lights { void performance_snapshot(perf::Metrics& out){out.push_back({"cached_nodes",5});} }
namespace settings { int get_int(const std::string&,int fallback){return fallback;} }
int main() {
    try {
        perf::runtime_frame({},0,60,false,false);perf::runtime_finish();require(logs.empty(),"disabled reporter has no side effects");
        require(!perf::runtime_interval(0) && !perf::runtime_interval(61),"invalid diagnostic intervals rejected");
        require(perf::runtime_interval(10),"long playthrough reporting interval accepted");
        perf::enable();perf::thread_enter();
        perf::event("load-level","synthetic-map");
        perf::runtime_frame({16000000,2000000,1000000,500000,12000000,1500000},SDL_WINDOW_INPUT_FOCUS,60,false,false);
        require(contains("effective_cap=60") && contains("renderer=native"),"actual frame context logged");
        require(contains("type=load-level label=synthetic-map") && contains("guest_address_reservation")==false,"synthetic adapter isolated");
        require(contains("private_kind=") && contains("jit_configured_bytes="),"OS/JIT semantics exposed");
        require(contains("lighting_images=2") && contains("sampler_elapsed_ms="),"cache adapter and sampler cost");
        require(contains("interval_seconds=10") && !perf::runtime_interval(1),"interval logged and fixed after baseline");
        size_t baseline=logs.size();
        perf::runtime_frame({16000000,2000000,1000000,500000,12000000,1500000},SDL_WINDOW_MINIMIZED,30,false,false);
        require(logs.size()==baseline,"long interval does not report on each frame");
        perf::runtime_finish();
        require(contains("final=1") && contains("minimized=1") && contains("limiter_spin=1.500"),"final partial interval and phase measurements");
        fail_adapter=true;perf::runtime_finish();
        require(contains("sampler stopped after diagnostic failure"),"sampler failure does not escape into gameplay");
        size_t previous=logs.size();perf::runtime_finish();require(logs.size()==previous,"failed sampler stays stopped");
        perf::thread_leave();
        std::cout<<"production performance reporter adapter tests passed; no game executed\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
