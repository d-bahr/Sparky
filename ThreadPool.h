#ifndef THREAD_POOL_H_
#define THREAD_POOL_H_

#include <stdbool.h>
#include <stdint.h>

typedef void (*ThreadPoolTask)(void *);
typedef void (*ThreadPoolCleanup)(void *);

extern bool ThreadPoolInitialize(uint16_t numThreads);
extern void ThreadPoolDestroy();
extern bool ThreadPoolQueue(ThreadPoolTask task, void * param, ThreadPoolCleanup cleanup);
extern void ThreadPoolSync();

#endif // THREAD_POOL_H_
