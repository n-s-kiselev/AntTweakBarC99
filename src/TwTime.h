//  ---------------------------------------------------------------------------
//
//  @file       TwTime.h
//  @brief      Monotonic timer
//  @license    This file is part of the AntTweakBar library.
//              For conditions of distribution and use, see License.txt
//
//  note:       Private header
//
//  ---------------------------------------------------------------------------


#if !defined ANT_TW_TIME_INCLUDED
#define ANT_TW_TIME_INCLUDED


//  ---------------------------------------------------------------------------

// Replaces glfwGetTime(), which the library used to call directly - on
// Windows that forced libAntTweakBarC99.dll and the consuming application
// to share a single GLFW instance (see
// docs/plans/self-contained-windows-dll.md). Same contract as
// glfwGetTime(): a monotonically increasing count of seconds with an
// arbitrary epoch - only differences between two calls are meaningful.
// Requires TwPrecomp.h to already be included (for the ANT_WINDOWS define
// and, on Windows, <windows.h> itself).

#if !defined(ANT_WINDOWS)
#include <time.h>
#endif

static inline double TwGetTimeSeconds(void)
{
#if defined(ANT_WINDOWS)
    static LARGE_INTEGER s_Freq = {0};
    LARGE_INTEGER count;
    if( s_Freq.QuadPart==0 )
        QueryPerformanceFrequency(&s_Freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)s_Freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec*1e-9;
#endif
}


//  ---------------------------------------------------------------------------


#endif // !defined ANT_TW_TIME_INCLUDED
