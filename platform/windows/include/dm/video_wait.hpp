#pragma once
#include <windows.h>
#include <chrono>
#include <stdexcept>

namespace dm::video {
// Per-thread timer: no process/system timer-resolution changes or busy polling.
inline void precise_wait(std::chrono::steady_clock::duration duration) {
    if(duration<=duration.zero())return;
    struct Timer {
        HANDLE handle=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        ~Timer(){if(handle)CloseHandle(handle);}
    };
    thread_local Timer timer;
    if(!timer.handle)throw std::runtime_error("Cannot create video pacing timer");
    LARGE_INTEGER due{};
    due.QuadPart=-(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count()/100+1);
    if(!SetWaitableTimer(timer.handle,&due,0,nullptr,nullptr,FALSE) || WaitForSingleObject(timer.handle,5000)!=WAIT_OBJECT_0)
        throw std::runtime_error("Video pacing timer failed");
}
}
