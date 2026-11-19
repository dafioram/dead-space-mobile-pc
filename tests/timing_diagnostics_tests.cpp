#include "hle/timing.h"
#include "hle/thread_timer.h"
#include "performance.h"
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
void require(bool value,const char* why) { if(!value)throw std::runtime_error(why); }
uint64_t metric(const char* name) {
    for(auto m:perf::owner_sample())if(std::string(m.name)==name)return m.value;
    throw std::runtime_error("missing timer metric");
}
struct FakeTimer {
    using Handle = unsigned;
    static inline unsigned creates=0,closes=0,waits=0;
    static inline bool create_ok=true,arm_ok=true;
    static inline int64_t requested=0;
    static Handle create() { ++creates; return create_ok ? 1 : 0; }
    static void close(Handle h) { require(h==1,"close owns exactly its created handle"); ++closes; }
    static bool wait(Handle h,int64_t ns) { require(h==1,"wait uses owned handle"); ++waits;requested=ns;return arm_ok; }
};
void injected_failures() {
    using Timer=timing::detail::ThreadTimer<FakeTimer>;
    static_assert(!std::is_copy_constructible_v<Timer> && !std::is_move_constructible_v<Timer>);
    {
        Timer timer;
        require(timer.wait(1) && timer.wait(1234567),"successful timer reused");
        require(FakeTimer::creates==1 && FakeTimer::closes==0 && FakeTimer::requested==1234567,"no premature close or duration changes");
        FakeTimer::arm_ok=false;
        require(!timer.wait(1000000),"arming failure returns to existing Sleep fallback");
    }
    require(FakeTimer::closes==1,"single owner closes once, including after arm failure");
    FakeTimer::create_ok=false;
    { Timer timer;require(!timer.wait(1000000),"creation failure returns to existing Sleep fallback"); }
    require(FakeTimer::creates==2 && FakeTimer::closes==1 && FakeTimer::waits==3,"null timer never waited or closed");
}
void workers(unsigned count) {
    for(unsigned i=0;i<count;++i) {
        std::thread worker([]{timing::sleep_ns(1);timing::sleep_ns(1000000);timing::sleep_ns(1000000);});
        worker.join();
    }
}
}
int main() {
    try {
        injected_failures();
        workers(2);  // warm CRT/thread support; cleanup must work without diagnostics
#ifdef _WIN32
        DWORD before=0,after=0;
        require(GetProcessHandleCount(GetCurrentProcess(),&before),"handle baseline query");
        workers(32);
        require(GetProcessHandleCount(GetCurrentProcess(),&after),"handle final query");
        require(after<=before+2,"disabled diagnostics must not retain one timer per exited thread");
#endif
        require(metric("wait_timer_created")==0,"disabled ownership counters stay off");
        perf::enable();
        workers(32);
#ifdef _WIN32
        auto created=metric("wait_timer_created");
        require(created+metric("wait_timer_create_failures")==32,"one creation attempt per sleeping thread, not per sleep");
        require(metric("wait_timer_closed")==created && metric("wait_timer_live")==0,"thread-exit timers balance");
        std::atomic<unsigned> ready{0};std::atomic<bool> release{false};
        std::vector<std::thread> concurrent;
        for(int i=0;i<8;++i)concurrent.emplace_back([&]{timing::sleep_ns(1000000);++ready;while(!release.load())std::this_thread::yield();});
        while(ready.load()!=8)std::this_thread::yield();
        const auto during_created=metric("wait_timer_created"),during_live=metric("wait_timer_live"),during_closed=metric("wait_timer_closed");
        release=true;for(auto& thread:concurrent)thread.join();
        require(during_live==during_created-during_closed && during_closed==created,"live threads retain their own reusable timers");
        require(metric("wait_timer_closed")==metric("wait_timer_created") && metric("wait_timer_live")==0,"concurrent thread exits close their own timers");
        for(int i=0;i<8;++i) {
            std::promise<void> done;auto exited=done.get_future();
            std::thread([done=std::move(done)]() mutable {
                timing::sleep_ns(1000000);
                done.set_value_at_thread_exit();  // ready only after TLS destructors
            }).detach();
            exited.wait();
        }
        require(metric("wait_timer_closed")==metric("wait_timer_created") && metric("wait_timer_live")==0,"detached guest-style workers also close at thread exit");
        require(metric("exited_thread_timer_retained")==0 && metric("wait_timer_close_failures")==0 && metric("wait_timer_wait_failures")==0,"no failed closes or waits");
        std::cout<<"Windows thread-local timers created/closed="<<metric("wait_timer_created")<<"; disabled/enabled and concurrent lifetimes checked\n";
#else
        require(metric("wait_timer_created")==0,"Linux must not claim Windows timers");
#endif
        std::cout<<"synthetic timer ownership/fallback tests passed; no game executed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
