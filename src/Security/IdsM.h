#ifndef IDSM_H
#define IDSM_H

#include "Std_Types.h"
#include "Ids_Types.h"

/* Intrusion Detection System Manager (AUTOSAR IdsM-style).
 *
 * Pipeline per reported security event:
 *
 *   sensor -> IdsM_ReportSecurityEvent
 *               |
 *               v
 *           filter chain  : per-id aggregation + threshold + censoring
 *               |
 *               v
 *           policy lookup : event-id -> prevention action
 *               |
 *               v
 *         security event memory (RAM ring -> NVM persisted)
 *               |
 *               v
 *           IdsR transport : DoIP/MQTT to V-SOC (off-board)
 *
 * The function is non-blocking and safe to call from RX ISR context
 * (ring is single-producer per call site, multi-consumer is the
 * background reporter task). */

extern Std_ReturnType IdsM_Init(void);

/* Report a security event from a sensor. The IdsM applies the
 * configured filter, looks up the prevention action, persists the
 * event, and returns the action so that the caller can apply it. */
extern IdsM_ActionType IdsM_ReportSecurityEvent(
        IdsM_SEvIdType            sevId,
        const IdsM_CtxDataType   *ctx);

/* Has the IdsM marked the given source module as isolated? Consulted
 * by the routing dispatcher before any other check. */
extern boolean IdsM_IsSourceIsolated(uint16 srcModule);

/* Has the IdsM marked the given route as blocked? */
extern boolean IdsM_IsRouteBlocked(uint16 srcPduId);

/* Drained periodically by the IdsR background task -- pops pending
 * events from the ring and hands them to the off-board reporter. */
extern void IdsM_MainFunction(void);

/* Counters exposed for diagnostics and unit tests. */
typedef struct {
    uint32 reportedTotal;
    uint32 filteredOut;
    uint32 dropped;
    uint32 throttled;
    uint32 blocked;
    uint32 isolated;
    uint32 escalated;
} IdsM_StatsType;

extern const IdsM_StatsType *IdsM_GetStats(void);

#endif
