#ifndef BODY_ROUTING_H
#define BODY_ROUTING_H

#include "Std_Types.h"
#include "LinSignals.h"
#include "CanSignals.h"

/* ------------------------------------------------------------------ *
 *  Body-domain LIN <-> CAN routing manager.
 *
 *  Implements the three routings agreed in YG-D18-2026-1042:
 *
 *    Cruise         LIN MSWToVCU (0x02)   -> CAN CCVS_VCU  (SA 0x05) -> EMS
 *    Retarder brake LIN HandleToVCU(0x01) -> CAN TSC1_VDR  (SA 0x27) -> RCU
 *    AMT shift      LIN HandleToVCU(0x01) -> CAN TC1       (SA 0x05) -> TCU
 *
 *  This module is *signal-level* routing, performed on top of PduR
 *  by an application SW-C (not pure PduR pass-through). Reasons:
 *
 *    - cruise has a state machine (single-shot Enable/Disable pulse)
 *    - retarder gear -> J1939 torque% needs a mapping table
 *    - 102A part-config discriminates the routing at runtime
 *    - the CAN frame is shared with other sources, so we use Com to
 *      pack only the signals we own
 *
 *  Pure PDU pass-through stays in PduR; the signal transforms live
 *  here in the ASIL partition next to PduR. The module is exercised
 *  cyclically (10 ms) from a TimingEvent runnable.
 * ------------------------------------------------------------------ */

extern Std_ReturnType BodyRouting_Init(void);

/* Called when a LIN RX indication delivers the named PDU. */
extern void BodyRouting_OnLinMSWToVCU  (const LinSig_MSWToVCU   *sig);
extern void BodyRouting_OnLinHandleToVCU(const LinSig_HandleToVCU *sig);

/* Cyclic step: applies state machines, freshness checks, and
 * triggers Com signal updates. Called every 10 ms by the SchM. */
extern void BodyRouting_MainFunction(void);

/* Counters for diagnostics / unit tests. */
typedef struct {
    uint32 cruisePulsesEnable;
    uint32 cruisePulsesDisable;
    uint32 retarderUpdates;
    uint32 amtUpdates;
    uint32 staleLinDrops;       /* LIN went stale; CAN held at "Not Avail" */
} BodyRouting_StatsType;

extern const BodyRouting_StatsType *BodyRouting_GetStats(void);

#endif
