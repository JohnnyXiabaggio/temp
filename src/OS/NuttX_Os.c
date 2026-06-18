/* NuttX OS abstraction — POSIX implementation.
 * Compiles on NuttX target and on Linux host (unit-test builds).
 *
 * _POSIX_C_SOURCE 200809L enables: clock_gettime, clock_nanosleep,
 * CLOCK_MONOTONIC, TIMER_ABSTIME, SCHED_FIFO, struct sched_param. */

#define _POSIX_C_SOURCE 200809L

#include "NuttX_Os.h"
#include "Compiler.h"

#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <errno.h>

/* ------------------------------------------------------------------ *
 *  Mutex                                                               *
 * ------------------------------------------------------------------ */

FUNC(Std_ReturnType, BODYROUTING_CODE) NuttX_MutexInit(
    P2VAR(NuttX_MutexType, AUTOMATIC, BODYROUTING_VAR) mutex)
{
    Std_ReturnType result;

    if (mutex == NULL_PTR) {
        result = E_NOT_OK;
    } else if (pthread_mutex_init(mutex, NULL_PTR) != 0) {
        result = E_NOT_OK;
    } else {
        result = E_OK;
    }

    return result;
}

FUNC(void, BODYROUTING_CODE) NuttX_MutexLock(
    P2VAR(NuttX_MutexType, AUTOMATIC, BODYROUTING_VAR) mutex)
{
    if (mutex != NULL_PTR) {
        /* pthread_mutex_lock is only non-zero on programming error
         * (uninitialized mutex, deadlock).  Treat as fatal on target;
         * on host the assert in the unit test framework catches it.   */
        (void)pthread_mutex_lock(mutex);
    }
}

FUNC(void, BODYROUTING_CODE) NuttX_MutexUnlock(
    P2VAR(NuttX_MutexType, AUTOMATIC, BODYROUTING_VAR) mutex)
{
    if (mutex != NULL_PTR) {
        (void)pthread_mutex_unlock(mutex);
    }
}

/* ------------------------------------------------------------------ *
 *  Monotonic clock                                                     *
 * ------------------------------------------------------------------ */

/* Weak symbols allow unit-test builds to override with a fake clock
 * without #ifdef.  On target the linker uses these real definitions. */

__attribute__((weak))
FUNC(uint32, BODYROUTING_CODE) OsTime_GetMs(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    /* Truncate to uint32; wraps after ~49.7 days — acceptable for
     * staleness windows measured in milliseconds.                     */
    return (uint32)((uint32)(ts.tv_sec * 1000u) + (uint32)(ts.tv_nsec / 1000000u));
}

__attribute__((weak))
FUNC(uint32, BODYROUTING_CODE) OsTime_GetUs(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32)((uint32)(ts.tv_sec * 1000000u) + (uint32)(ts.tv_nsec / 1000u));
}

/* ------------------------------------------------------------------ *
 *  Periodic task                                                       *
 * ------------------------------------------------------------------ */

/* Internal pthread entry: loops calling the user function with a
 * fixed sleep between iterations.  clock_nanosleep is used so that
 * drift from the sleep syscall overhead does not accumulate.          */
static void *PeriodicTask_Entry(void *arg)
{
    P2VAR(NuttX_PeriodicTaskType, AUTOMATIC, BODYROUTING_VAR) task =
        (NuttX_PeriodicTaskType *)arg;

    struct timespec nextWake;
    const long periodNs = (long)task->periodMs * 1000000L;

    (void)clock_gettime(CLOCK_MONOTONIC, &nextWake);

    while (task->running == TRUE) {
        task->func();

        /* Advance wake time by one period (no drift accumulation). */
        nextWake.tv_nsec += periodNs;
        if (nextWake.tv_nsec >= 1000000000L) {
            nextWake.tv_sec  += (time_t)(nextWake.tv_nsec / 1000000000L);
            nextWake.tv_nsec %= 1000000000L;
        }

        /* TIMER_ABSTIME: wakes at the absolute time, not relative.
         * EINTR is benign (signal delivery); loop retries.           */
        int rc;
        do {
            rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &nextWake, NULL_PTR);
        } while ((rc == EINTR) && (task->running == TRUE));
    }

    return NULL_PTR;
}

FUNC(Std_ReturnType, BODYROUTING_CODE) NuttX_PeriodicTask_Start(
    P2VAR(NuttX_PeriodicTaskType, AUTOMATIC, BODYROUTING_VAR) task,
    NuttX_TaskFuncPtr                                          func,
    uint32                                                     periodMs,
    int                                                        priority)
{
    Std_ReturnType result;

    if ((task == NULL_PTR) || (func == NULL_PTR) || (periodMs == 0u)) {
        result = E_NOT_OK;
    } else {
        pthread_attr_t   attr;
        struct sched_param sp;

        task->func     = func;
        task->periodMs = periodMs;
        task->running  = TRUE;

        (void)pthread_attr_init(&attr);

        /* Request real-time scheduling; silently falls back to
         * time-sharing if the process lacks the privilege.           */
        if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO) == 0) {
            sp.sched_priority = priority;
            (void)pthread_attr_setschedparam(&attr, &sp);
            (void)pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        }

        if (pthread_create(&task->thread, &attr, PeriodicTask_Entry, task) != 0) {
            task->running = FALSE;
            result = E_NOT_OK;
        } else {
            result = E_OK;
        }

        (void)pthread_attr_destroy(&attr);
    }

    return result;
}

FUNC(Std_ReturnType, BODYROUTING_CODE) NuttX_PeriodicTask_Stop(
    P2VAR(NuttX_PeriodicTaskType, AUTOMATIC, BODYROUTING_VAR) task)
{
    Std_ReturnType result;

    if (task == NULL_PTR) {
        result = E_NOT_OK;
    } else {
        task->running = FALSE;
        (void)pthread_join(task->thread, NULL_PTR);
        result = E_OK;
    }

    return result;
}
