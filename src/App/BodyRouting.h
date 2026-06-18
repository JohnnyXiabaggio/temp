/* BodyRouting — body-domain LIN <-> CAN routing SW-C.
 * AUTOSAR Classic R4.3.1 BSW module header.
 * Spec: YG-D18-2026-1042, J107 & J169 T-Gateway, 2026-04-22.
 *
 * Routing paths:
 *   Cruise   : LIN MSWToVCU  (0x02) -> CAN CCVS_VCU (SA 0x05) -> EMS
 *   Retarder : LIN HandleToVCU(0x01) -> CAN TSC1_VDR (SA 0x27) -> RCU
 *   AMT shift: LIN HandleToVCU(0x01) -> CAN TC1      (SA 0x05) -> TCU */

#ifndef BODYROUTING_H
#define BODYROUTING_H

#include "Std_Types.h"
#include "Compiler.h"
#include "LinSignals.h"
#include "CanSignals.h"
#include "BodyRouting_Types.h"
#include "BodyRouting_Cfg.h"

/* ------------------------------------------------------------------ *
 *  Public API                                                          *
 * ------------------------------------------------------------------ */

/* Initialise module state.  Must be called before any other API.
 * Returns E_OK unconditionally; retained for AUTOSAR Init pattern. */
FUNC(Std_ReturnType, BODYROUTING_CODE)
BodyRouting_Init(void);

/* LinIf RX-indication callbacks — called by PduR/LinIf on frame receipt. */
FUNC(void, BODYROUTING_CODE)
BodyRouting_OnLinMSWToVCU(
    P2CONST(LinSig_MSWToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) sig);

FUNC(void, BODYROUTING_CODE)
BodyRouting_OnLinHandleToVCU(
    P2CONST(LinSig_HandleToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) sig);

/* Cyclic runnable — registered with SchM at 10 ms period.
 * Applies state machines, staleness checks, and triggers Com signal
 * updates for all three routing paths. */
FUNC(void, BODYROUTING_CODE)
BodyRouting_MainFunction(void);

/* Diagnostic counter access (read-only pointer into module RAM).
 * Returns NULL_PTR and reports DET if module is not initialised. */
FUNC(P2CONST(BodyRouting_StatsType, AUTOMATIC, BODYROUTING_APPL_DATA),
     BODYROUTING_CODE)
BodyRouting_GetStats(void);

#endif
