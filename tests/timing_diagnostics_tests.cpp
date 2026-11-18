#include "hle/timing.h"
#include "performance.h"
#include <iostream>
#include <stdexcept>
#include <thread>

uint64_t metric(const char* name) {
    for(auto m:perf::owner_sample())if(std::string(m.name)==name)return m.value;
    throw std::runtime_error("missing timer metric");
}
int main() {
    try {
        perf::enable();
        for(int i=0;i<3;++i) {
            std::thread worker([]{timing::sleep_ns(1000000);timing::sleep_ns(1000000);});
            worker.join();
        }
#ifdef _WIN32
        const auto created=metric("wait_timer_created");
        if(created && (created!=3 || metric("wait_timer_closed")!=0 || metric("exited_thread_timer_retained")!=3))
            throw std::runtime_error("timer creation/thread-exit observations unbalanced");
        std::cout<<"existing raw timer handles observed="<<created<<"; no cleanup policy changed\n";
#else
        if(metric("wait_timer_created")!=0)throw std::runtime_error("Linux must not claim Windows timer handles");
#endif
        std::cout<<"synthetic timer lifecycle observer tests passed; no game executed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
