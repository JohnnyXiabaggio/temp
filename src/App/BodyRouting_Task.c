/* BodyRouting_Task — NuttX periodic task implementation.
 * MISRA C:2012 / AUTOSAR R4.3.1 / ISO 26262 ASIL-B.                  */

#include "BodyRouting_Task.h"
#include "BodyRouting.h"
#include "BR_FuncSafety.h"
#include "NuttX_Os.h"
#include "Det.h"

/* Task module IDs (re-uses BODYROUTING module in a different instance). */
#define TASK_API_ID_INIT  ((uint8)0x10u)
#define TASK_API_ID_START ((uint8)0x11u)
#define TASK_API_ID_STOP  ((uint8)0x12u)

static VAR(NuttX_PeriodicTaskType, BODYROUTING_VAR) RoutingTask;
static VAR(NuttX_PeriodicTaskType, BODYROUTING_VAR) SupervisorTask;
static VAR(boolean, BODYROUTING_VAR)                TaskInitDone;

/* Wrappers that adapt the void(*)(void) NuttX_TaskFuncPtr signature. */
static FUNC(void, BODYROUTING_CODE) RoutingTaskEntry(void)
{
    BodyRouting_MainFunction();
}

static FUNC(void, BODYROUTING_CODE) SupervisorTaskEntry(void)
{
    BR_FuncSafety_SupervisionStep();
}

FUNC(Std_ReturnType, BODYROUTING_CODE) BodyRouting_Task_Init(void)
{
    Std_ReturnType r;

    r = BodyRouting_Init();
    if (r == E_OK) {
        TaskInitDone = TRUE;
    }

    return r;
}

FUNC(Std_ReturnType, BODYROUTING_CODE) BodyRouting_Task_Start(void)
{
    Std_ReturnType r;

    if (TaskInitDone != TRUE) {
        (void)Det_ReportError(BODYROUTING_MODULE_ID, BODYROUTING_INSTANCE_ID,
                              TASK_API_ID_START, BODYROUTING_E_UNINIT);
        r = E_NOT_OK;
    } else {
        /* Routing task: 10 ms period, lower priority. */
        r = NuttX_PeriodicTask_Start(&RoutingTask,
                                      RoutingTaskEntry,
                                      10u,
                                      BODYROUTING_TASK_PRIO_ROUTING);

        if (r == E_OK) {
            /* Supervision task: 20 ms period, higher priority so it can
             * preempt the routing task and detect a stall.             */
            r = NuttX_PeriodicTask_Start(&SupervisorTask,
                                          SupervisorTaskEntry,
                                          20u,
                                          BODYROUTING_TASK_PRIO_SUPERVISOR);

            if (r != E_OK) {
                /* Roll back routing task. */
                (void)NuttX_PeriodicTask_Stop(&RoutingTask);
            }
        }
    }

    return r;
}

FUNC(void, BODYROUTING_CODE) BodyRouting_Task_Stop(void)
{
    (void)NuttX_PeriodicTask_Stop(&SupervisorTask);
    (void)NuttX_PeriodicTask_Stop(&RoutingTask);
}

/* Single-step for unit testing: call one MainFunction cycle then one
 * supervision cycle (simulates two NuttX timer callbacks).            */
FUNC(void, BODYROUTING_CODE) BodyRouting_Task_Step(void)
{
    RoutingTaskEntry();
    SupervisorTaskEntry();
}
