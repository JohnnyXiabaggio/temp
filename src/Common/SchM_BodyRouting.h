/* AUTOSAR Schedule Manager exclusive-area stubs for BodyRouting.
 * On target the SchM generator emits these from the BSW module desc.
 * The single exclusive area "LinRxData" guards the LIN shadow copies
 * that are written by LinIf callbacks and read in the cyclic runnable. */

#ifndef SCHM_BODYROUTING_H
#define SCHM_BODYROUTING_H

#include "Compiler.h"

LOCAL_INLINE FUNC(void, BODYROUTING_CODE) SchM_Enter_BodyRouting_LinRxData(void)
{
    /* target: DisableAllInterrupts() or Os_SuspendAllInterrupts() */
}

LOCAL_INLINE FUNC(void, BODYROUTING_CODE) SchM_Exit_BodyRouting_LinRxData(void)
{
    /* target: EnableAllInterrupts() or Os_ResumeAllInterrupts() */
}

#endif
