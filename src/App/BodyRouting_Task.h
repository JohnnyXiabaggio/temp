/* BodyRouting_Task — NuttX periodic task wrapper.
 *
 * Runs BodyRouting_MainFunction every 10 ms on a real-time POSIX thread
 * (SCHED_FIFO, priority 40 by default).  An independent supervision
 * thread runs BR_FuncSafety_SupervisionStep every 20 ms at higher
 * priority (60) so it can preempt the routing task and detect stalls. */

#ifndef BODYROUTING_TASK_H
#define BODYROUTING_TASK_H

#include "Std_Types.h"
#include "Compiler.h"

/* NuttX thread priorities used (SCHED_FIFO, 1 = lowest, 255 = highest).
 * Keep supervision above routing so it can detect a stalled routing task. */
#define BODYROUTING_TASK_PRIO_ROUTING    (40)
#define BODYROUTING_TASK_PRIO_SUPERVISOR (60)

/* Initialise BodyRouting, BR_FuncSafety, and the NuttX mutex.
 * Must be called once from the application main() before Start().     */
FUNC(Std_ReturnType, BODYROUTING_CODE) BodyRouting_Task_Init(void);

/* Spawn the routing thread and the supervision thread.               */
FUNC(Std_ReturnType, BODYROUTING_CODE) BodyRouting_Task_Start(void);

/* Signal both threads to stop and join them (blocking).              */
FUNC(void, BODYROUTING_CODE) BodyRouting_Task_Stop(void);

/* Single-step: call MainFunction once, then SupervisionStep once.
 * Used by unit tests instead of spawning real threads.               */
FUNC(void, BODYROUTING_CODE) BodyRouting_Task_Step(void);

#endif
