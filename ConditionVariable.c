#include "ConditionVariable.h"

#ifdef __GNUC__
#include <errno.h>
#endif

bool ConditionVariableInitialize(ConditionVariable * cv)
{
#ifdef __GNUC__
    int s = pthread_cond_init(cv, NULL);
    if (s != 0)
    {
        errno = s;
        return false;
    }
    return true;
#else
    InitializeConditionVariable(cv);
    return true;
#endif
}

void ConditionVariableDestroy(ConditionVariable * cv)
{
#ifdef __GNUC__
    int s = pthread_cond_destroy(cv);
    if (s != 0)
        errno = s;
#else
    // Nothing to do; condition variables in Windows are tied to a global kernel object and do not need to be deleted.
#endif
}

void ConditionVariableSignalOne(ConditionVariable * cv)
{
#ifdef __GNUC__
    int s = pthread_cond_signal(cv);
    if (s != 0)
        errno = s;
#else
    WakeConditionVariable(cv);
#endif
}

void ConditionVariableSignalAll(ConditionVariable * cv)
{
#ifdef __GNUC__
    int s = pthread_cond_broadcast(cv);
    if (s != 0)
        errno = s;
#else
    WakeAllConditionVariable(cv);
#endif
}

void ConditionVariableWait(ConditionVariable * cv, Mutex * mutex)
{
#ifdef __GNUC__
    int s = pthread_cond_wait(cv, mutex);
    if (s != 0)
        errno = s;
#else
    SleepConditionVariableCS(cv, mutex, INFINITE);
#endif
}

bool ConditionVariableWaitTimeout(ConditionVariable * cv, Mutex * mutex, size_t timeoutMs)
{
#ifdef __GNUC__
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
        return false;
    ts.tv_sec += (time_t) (timeoutMs / 1000);
    ts.tv_nsec += (long) ((timeoutMs % 1000) * 1000000);
    int s = pthread_cond_timedwait(cv, mutex, &ts);
    if (s != 0)
    {
        errno = s;
        return false;
    }
    return true;
#else
    return SleepConditionVariableCS(cv, mutex, (DWORD)timeoutMs) != 0;
#endif
}
