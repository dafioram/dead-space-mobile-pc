#include "frame_wait.h"
#include <limits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace frame_wait {

Timer::Timer(bool enabled) {
#ifdef _WIN32
    if (enabled) {
        // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Windows 10 1803+). Using the
        // flag, not a process-wide timer-resolution or thread-priority change.
        handle_ = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002,
                                        TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!handle_) error_ = GetLastError();
    }
#else
    (void)enabled;
#endif
}

Timer::~Timer() { close(); }

void Timer::close() {
#ifdef _WIN32
    if (handle_) CloseHandle(handle_);
#endif
    handle_ = nullptr;
}

bool Timer::wait(uint64_t duration_100ns) {
#ifdef _WIN32
    if (!handle_ || !duration_100ns || duration_100ns > uint64_t(std::numeric_limits<int64_t>::max())) return false;
    LARGE_INTEGER due;
    due.QuadPart = -int64_t(duration_100ns);  // relative time, not wall clock
    if (!SetWaitableTimer(handle_, &due, 0, nullptr, nullptr, FALSE) ||
        WaitForSingleObject(handle_, INFINITE) != WAIT_OBJECT_0) {
        error_ = GetLastError();
        close();  // disable for subsequent frames; the caller logs once
        return false;
    }
    return true;
#else
    (void)duration_100ns;
    return false;
#endif
}

}  // namespace frame_wait
