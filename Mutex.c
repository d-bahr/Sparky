#include "Mutex.h"

#ifdef __GNUC__
#include <errno.h>
#endif

bool MutexInitialize(Mutex * m)
{
#ifdef __GNUC__
    int s = pthread_mutex_init(m, NULL);
    if (s != 0)
    {
        errno = s;
        return false;
    }
    return true;
#else
    InitializeCriticalSection(m);
    return true;
#endif
}

void MutexDestroy(Mutex * m)
{
#ifdef __GNUC__
    int s = pthread_mutex_destroy(m);
    if (s != 0)
        errno = s;
#else
    DeleteCriticalSection(m);
#endif
}

bool MutexTryLock(Mutex * m)
{
#ifdef __GNUC__
    return pthread_mutex_trylock(m) == 0;
#else
    return TryEnterCriticalSection(m) != 0;
#endif
}

void MutexLock(Mutex * m)
{
#ifdef __GNUC__
    int s = pthread_mutex_lock(m);
    if (s != 0)
        errno = s;
#else
    EnterCriticalSection(m);
#endif
}

void MutexUnlock(Mutex * m)
{
#ifdef __GNUC__
    int s = pthread_mutex_unlock(m);
    if (s != 0)
        errno = s;
#else
    LeaveCriticalSection(m);
#endif
}
