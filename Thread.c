#include "Thread.h"

#ifdef __GNUC__
#define __USE_GNU
#include <errno.h>
#include <time.h>
#endif

bool ThreadStart(ThreadHandle * th, ThreadFunc func, void * param)
{
#ifdef __GNUC__
    int s = pthread_create(th, NULL, func, param);
    if (s != 0)
    {
        errno = s;
        return false;
    }
    return true;
#else
    HANDLE h = CreateThread(NULL, 0, func, param, 0, NULL);
    if (h != NULL)
    {
        *th = h;
        return true;
    }
    return false;
#endif
}

bool ThreadJoin(ThreadHandle th)
{
#ifdef __GNUC__
    int s = pthread_join(th, NULL);
    if (s != 0)
    {
        errno = s;
        return false;
    }
    return true;
#else
    DWORD s = WaitForSingleObject(th, INFINITE) == 0;
    if (s == 0)
    {
        CloseHandle(th);
        return true;
    }
    return false;
#endif
}

// TODO: Newer GCC versions support this, but disabled for now.
#ifndef __GNUC__
bool ThreadTryJoin(ThreadHandle th, size_t timeoutMs)
{
#ifdef __GNUC__
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
        return false;
    ts.tv_sec += (time_t)(timeoutMs / 1000);
    ts.tv_nsec += (long)((timeoutMs % 1000) * 1000000);
    int s = pthread_timedjoin_np(th, NULL, &ts);
    if (s != 0)
    {
        errno = s;
        return false;
    }
    return true;
#else
    DWORD s = WaitForSingleObject(th, (DWORD)timeoutMs);
    if (s == 0)
    {
        CloseHandle(th);
        return true;
    }
    return false;
#endif
}
#endif

bool ThreadDetach(ThreadHandle th)
{
#ifdef __GNUC__
    int s = pthread_detach(th);
    if (s != 0)
    {
        errno = s;
        return false;
    }
    return true;
#else
    return CloseHandle(th) != 0;
#endif
}

void ThreadSleep(size_t timeoutMs)
{
#ifdef __GNUC__
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
        return;
    ts.tv_sec += (time_t) (timeoutMs / 1000);
    ts.tv_nsec += (long) ((timeoutMs % 1000) * 1000000);
    int s = nanosleep(&ts, NULL);
    if (s != 0)
        errno = s;
#else
    Sleep((DWORD) timeoutMs);
#endif
}
