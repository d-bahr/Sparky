#include "ConditionVariable.h"
#include "Mutex.h"
#include "Thread.h"
#include "ThreadPool.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_;

typedef struct Task_
{
    ThreadPoolTask execute;
    void * param;
    ThreadPoolCleanup cleanup;
    struct Task_ * next;
    struct Task_ * prev;
} Task;

static volatile uint16_t s_numThreads = 0;
static ThreadHandle * s_threads = NULL;
static Mutex s_mutex;
static ConditionVariable s_cv;
static ConditionVariable s_completion;
static volatile bool s_run = false;
static Task * s_queueStart = NULL;
static Task * s_queueEnd = NULL;

#ifdef __GNUC__
static void * ThreadPoolExecute(void * param);
#else
static DWORD ThreadPoolExecute(void * param);
#endif

bool ThreadPoolInitialize(uint16_t numThreads)
{
    if (s_threads != NULL)
        return false;

    if (!MutexInitialize(&s_mutex))
        goto err_mutex;

    if (!ConditionVariableInitialize(&s_cv))
        goto err_cv;

    if (!ConditionVariableInitialize(&s_completion))
        goto err_completion;

    s_threads = (ThreadHandle *) malloc(sizeof(ThreadHandle) * numThreads);
    if (s_threads == NULL)
        goto err_threads;

    s_numThreads = numThreads;
    s_run = true;

    for (uint16_t i = 0; i < s_numThreads; ++i)
        ThreadStart(&s_threads[i], &ThreadPoolExecute, NULL);
    
    return true;

err_threads:
    ConditionVariableDestroy(&s_cv);
err_completion:
    ConditionVariableDestroy(&s_completion);
err_cv:
    MutexDestroy(&s_mutex);
err_mutex:
    return false;
}

void ThreadPoolDestroy()
{
    if (s_numThreads > 0)
    {
        s_run = false;

        ConditionVariableSignalAll(&s_cv);

        for (uint16_t i = 0; i < s_numThreads; ++i)
            ThreadJoin(s_threads[i]);
        free(s_threads);
        s_threads = NULL;
        s_numThreads = 0;

        MutexLock(&s_mutex);

        while (s_queueStart != NULL)
        {
            Task * task = s_queueStart;
            s_queueStart = s_queueStart->next;
            free(task);
        }
        s_queueEnd = NULL;

        MutexUnlock(&s_mutex);

        MutexDestroy(&s_mutex);
        ConditionVariableDestroy(&s_completion);
        ConditionVariableDestroy(&s_cv);
    }
}

bool ThreadPoolQueue(ThreadPoolTask task, void * param, ThreadPoolCleanup cleanup)
{
    // TODO: Technically a race condition here with ThreadPoolDestroy, but not sure
    // I can be bothered to fix it...
    if (!s_run || task == NULL)
        return false;

    MutexLock(&s_mutex);

    Task * newTask = (Task *) malloc(sizeof(Task));
    if (newTask == NULL)
    {
        MutexUnlock(&s_mutex);
        return false;
    }
    newTask->execute = task;
    newTask->param = param;
    newTask->cleanup = cleanup;
    newTask->next = NULL;

    if (s_queueStart == NULL)
    {
        newTask->prev = NULL;
        s_queueStart = newTask;
        s_queueEnd = newTask;
    }
    else
    {
        assert(s_queueEnd != NULL);
        newTask->prev = s_queueEnd;
        s_queueEnd->next = newTask;
        s_queueEnd = newTask;
    }

    MutexUnlock(&s_mutex);

    ConditionVariableSignalOne(&s_cv);

    return true;
}

void ThreadPoolSync()
{
    MutexLock(&s_mutex);
    if (s_queueStart != NULL)
        ConditionVariableWait(&s_completion, &s_mutex);
    MutexUnlock(&s_mutex);
}

#ifdef __GNUC__
static void * ThreadPoolExecute(void * param)
#else
static DWORD ThreadPoolExecute(void * param)
#endif
{
    while (s_run)
    {
        MutexLock(&s_mutex);
        if (s_queueStart == NULL)
            ConditionVariableSignalAll(&s_completion);
        ConditionVariableWait(&s_cv, &s_mutex);
        Task * task = s_queueStart;
        if (s_queueStart != NULL)
        {
            s_queueStart = s_queueStart->next;
            if (s_queueStart == NULL)
                s_queueEnd = NULL;
        }
        MutexUnlock(&s_mutex);

        if (task != NULL)
        {
            task->execute(task->param);
            if (task->cleanup != NULL)
                task->cleanup(task->param);
            free(task);
        }
    }
    return 0;
}
