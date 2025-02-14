#ifndef CONDITION_VARIABLE_H_
#define CONDITION_VARIABLE_H_

#include <Mutex.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __GNUC__
#include <pthread.h>
typedef pthread_cond_t ConditionVariable;
#else
#include "WindowsInclude.h"
typedef CONDITION_VARIABLE ConditionVariable;
#endif

extern bool ConditionVariableInitialize(ConditionVariable * cv);
extern void ConditionVariableDestroy(ConditionVariable * cv);
extern void ConditionVariableSignalOne(ConditionVariable * cv);
extern void ConditionVariableSignalAll(ConditionVariable * cv);
extern void ConditionVariableWait(ConditionVariable * cv, Mutex * mutex);
extern bool ConditionVariableWaitTimeout(ConditionVariable * cv, Mutex * mutex, size_t timeoutMs);

#endif // CONDITION_VARIABLE_H_
