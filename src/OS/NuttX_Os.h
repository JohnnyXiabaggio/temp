/* NuttX OS abstraction layer.
 * Uses POSIX APIs (pthread, clock_gettime) so the same code compiles
 * on NuttX target (which is POSIX-compliant) and on the Linux host for
 * unit testing without any conditional compilation.
 *
 * NuttX configuration prerequisites (in .config / Kconfig):
 *   CONFIG_PTHREAD=y
 *   CONFIG_CLOCK_MONOTONIC=y
 *   CONFIG_SCHED_HPWORK=y   (if work-queue variant is used instead of pthread)
 *   CONFIG_STACK_COLORATION=y (stack-overflow detection) */

#ifndef NUTTX_OS_H
#define NUTTX_OS_H

#include "Std_Types.h"
#include "Compiler.h"    /* FUNC, VAR, P2VAR, BODYROUTING_CODE, etc. */
#include <pthread.h>
#include <time.h>

/* ------------------------------------------------------------------ *
 *  Mutual exclusion (replaces empty AUTOSAR SchM stubs)               *
 * ------------------------------------------------------------------ */

typedef pthread_mutex_t NuttX_MutexType;

/* Static initialiser — usable in file-scope variable initialisers. */
#define NUTTX_MUTEX_STATIC_INIT  PTHREAD_MUTEX_INITIALIZER

FUNC(Std_ReturnType, BODYROUTING_CODE) NuttX_MutexInit(
    P2VAR(NuttX_MutexType, AUTOMATIC, BODYROUTING_VAR) mutex);

FUNC(void, BODYROUTING_CODE) NuttX_MutexLock(
    P2VAR(NuttX_MutexType, AUTOMATIC, BODYROUTING_VAR) mutex);

FUNC(void, BODYROUTING_CODE) NuttX_MutexUnlock(
    P2VAR(NuttX_MutexType, AUTOMATIC, BODYROUTING_VAR) mutex);

/* ------------------------------------------------------------------ *
 *  Monotonic clock (CLOCK_MONOTONIC, not affected by NTP slew)        *
 *  Implements the OsTime_ ABI used across all modules.                *
 * ------------------------------------------------------------------ */

FUNC(uint32, BODYROUTING_CODE) OsTime_GetMs(void);
FUNC(uint32, BODYROUTING_CODE) OsTime_GetUs(void);

/* ------------------------------------------------------------------ *
 *  Periodic task                                                       *
 * ------------------------------------------------------------------ */

typedef FUNC(void, BODYROUTING_CODE) (*NuttX_TaskFuncPtr)(void);

typedef struct {
    NuttX_TaskFuncPtr               func;
    uint32                          periodMs;
    VAR(pthread_t, BODYROUTING_VAR) thread;
    VAR(boolean,   BODYROUTING_VAR) running;  /* written TRUE by Start,
                                                  FALSE by Stop/entry */
} NuttX_PeriodicTaskType;

/* Start a detached periodic task.  'priority' maps to POSIX
 * SCHED_FIFO priority (1..99); on non-real-time hosts it is silently
 * ignored if the caller lacks CAP_SYS_NICE.                          */
FUNC(Std_ReturnType, BODYROUTING_CODE) NuttX_PeriodicTask_Start(
    P2VAR(NuttX_PeriodicTaskType, AUTOMATIC, BODYROUTING_VAR) task,
    NuttX_TaskFuncPtr                                          func,
    uint32                                                     periodMs,
    int                                                        priority);

FUNC(Std_ReturnType, BODYROUTING_CODE) NuttX_PeriodicTask_Stop(
    P2VAR(NuttX_PeriodicTaskType, AUTOMATIC, BODYROUTING_VAR) task);

#endif
