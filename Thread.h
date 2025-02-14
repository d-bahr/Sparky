#ifndef THREAD_H_
#define THREAD_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __GNUC__
#include <pthread.h>
typedef pthread_t ThreadHandle;
typedef void * (*ThreadFunc)(void *);
#else
#include "WindowsInclude.h"
typedef HANDLE ThreadHandle;
typedef LPTHREAD_START_ROUTINE ThreadFunc;
#endif

extern bool ThreadStart(ThreadHandle * th, ThreadFunc func, void * param);
extern bool ThreadJoin(ThreadHandle th);
// TODO: Newer GCC versions support this, but disabled for now.
#ifndef __GNUC__
extern bool ThreadTryJoin(ThreadHandle th, size_t timeoutMs);
#endif
extern bool ThreadDetach(ThreadHandle th);

extern void ThreadSleep(size_t timeoutMs);

#endif // THREAD_H_
