#pragma once

#include <cstdint>

namespace timing::detail {
// One instance belongs to one thread. Api supplies the platform operations (or
// deterministic synthetic failures in tests). Never transfer a live timer.
template <class Api> class ThreadTimer {
public:
    ThreadTimer() : handle_(Api::create()) {}
    ~ThreadTimer() { if (handle_) Api::close(handle_); }
    ThreadTimer(const ThreadTimer&) = delete;
    ThreadTimer& operator=(const ThreadTimer&) = delete;
    ThreadTimer(ThreadTimer&&) = delete;
    ThreadTimer& operator=(ThreadTimer&&) = delete;
    bool wait(int64_t ns) { return handle_ && Api::wait(handle_, ns); }
private:
    typename Api::Handle handle_;
};
}  // namespace timing::detail
