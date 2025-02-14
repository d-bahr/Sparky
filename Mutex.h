#ifndef MUTEX_H_
#define MUTEX_H_

#include <stdbool.h>

#ifdef __GNUC__
#include <pthread.h>
typedef pthread_mutex_t Mutex;
#else
#include "WindowsInclude.h"
typedef CRITICAL_SECTION Mutex;
#endif

extern bool MutexInitialize(Mutex * m);
extern void MutexDestroy(Mutex * m);
extern bool MutexTryLock(Mutex * m);
extern void MutexLock(Mutex * m);
extern void MutexUnlock(Mutex * m);

#endif // MUTEX_H_
