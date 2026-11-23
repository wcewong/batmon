#pragma once
// Hands the memory pages the app is not using back to Windows, so the idle footprint stays small.
// The pages that are touched again (the next poll, the next menu) come back on demand from RAM;
// nothing is freed or reallocated, and the app keeps working the same way.
#include <windows.h>

namespace memory_trim {

inline void Now() { SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); }

inline void CALLBACK TimerProc(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    Now();
}

// Trims a moment later, once the window or menu that just closed has finished tearing down.
// Must be called from a thread with a message loop (the UI thread); calls in quick succession share one trim.
inline void Soon(UINT ms = 2000) {
    static UINT_PTR timer = 0;
    if (timer) KillTimer(nullptr, timer);
    timer = SetTimer(nullptr, 0, ms, TimerProc);
}

}  // namespace memory_trim
