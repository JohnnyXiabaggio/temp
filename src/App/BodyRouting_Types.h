/* BodyRouting module type definitions (AUTOSAR _Types.h pattern). */

#ifndef BODYROUTING_TYPES_H
#define BODYROUTING_TYPES_H

#include "Std_Types.h"

/* Diagnostic / observability counters exposed via BodyRouting_GetStats(). */
typedef struct {
    uint32 cruisePulsesEnable;   /* rising-edge ON  events routed to EMS */
    uint32 cruisePulsesDisable;  /* rising-edge OFF events routed to EMS */
    uint32 retarderUpdates;      /* TSC1_VDR frames sent per MainFunction */
    uint32 amtUpdates;           /* TC1 frames sent per MainFunction      */
    uint32 staleLinDrops;        /* cycles where LIN was stale (>300 ms)  */
} BodyRouting_StatsType;

/* Module init-state tag: prevents API use before BodyRouting_Init(). */
typedef enum {
    BODYROUTING_STATE_UNINIT = 0u,
    BODYROUTING_STATE_INIT   = 1u
} BodyRouting_StateType;

#endif
