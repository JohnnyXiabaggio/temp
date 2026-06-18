/* BodyRouting Functional Safety Manager (BR_FuncSafety).
 *
 * Implements the ISO 26262 ASIL-B software safety mechanisms for the
 * LIN->CAN gateway routing function defined in YG-D18-2026-1042.
 *
 * Safety Requirements addressed (mapped to TSR in TGW-SAF-001):
 *
 *   SM-001  Alive counter supervision
 *           MainFunction increments an atomic counter on every call.
 *           An independent supervision task checks that the counter
 *           advances at the expected rate (10 ms ± 20 %).
 *
 *   SM-002  Execution-time budget check
 *           Each MainFunction call must complete within 8 000 µs
 *           (80 % of the 10 ms period).  Overflow triggers a DEM event
 *           and increments the fault accumulator.
 *
 *   SM-003  LIN input signal plausibility
 *           Every received LIN signal is checked against its defined
 *           valid range before being passed to the routing maps.
 *           Out-of-range values are rejected; the last valid snapshot
 *           is retained unchanged.
 *
 *   SM-004  CAN output signal plausibility
 *           Computed CAN signal values are verified against valid
 *           ranges before Com_Send_xxx is called.  Out-of-range results
 *           are replaced with the safe-state default.
 *
 *   SM-005  ROM integrity (RetMap CRC-8)
 *           The retarder gear-to-torque mapping table is hashed at
 *           Init.  The hash is recomputed on every MainFunction call;
 *           a mismatch indicates flash/RAM corruption and triggers an
 *           immediate safe-state transition.
 *
 *   SM-006  Safe-state CAN output
 *           On any latched safety fault all CAN outputs are set to
 *           their safe defaults (CCVS Not Available, TSC1 override
 *           disabled, TC1 zeros) until an ECU reset clears the fault. */

#ifndef BR_FUNCSAFETY_H
#define BR_FUNCSAFETY_H

#include "Std_Types.h"
#include "Compiler.h"
#include "LinSignals.h"
#include "CanSignals.h"
#include "BodyRouting_Cfg.h"

/* ------------------------------------------------------------------ *
 *  Fault type enumeration                                              *
 * ------------------------------------------------------------------ */

typedef enum {
    BR_FS_FAULT_NONE     = 0u,
    BR_FS_FAULT_ALIVE    = 1u,   /* SM-001: alive counter stagnated   */
    BR_FS_FAULT_DEADLINE = 2u,   /* SM-002: execution time exceeded   */
    BR_FS_FAULT_ROM      = 3u,   /* SM-005: RetMap CRC mismatch       */
    BR_FS_FAULT_INPUT    = 4u,   /* SM-003: LIN signal out of range   */
    BR_FS_FAULT_OUTPUT   = 5u    /* SM-004: CAN signal out of range   */
} BR_FuncSafety_FaultType;

/* Consecutive-error threshold before a fault becomes latched. */
#define BR_FS_ERROR_THRESHOLD ((uint8)3u)

/* Execution-time budget (µs): 80 % of the 10 ms period. */
#define BR_FS_EXEC_BUDGET_US  ((uint32)8000u)

/* Maximum expected alive-counter advance between two supervision
 * calls (period = 10 ms, supervision at 20 ms → expect 1-2 ticks). */
#define BR_FS_ALIVE_MIN_ADVANCE ((uint32)1u)

/* ------------------------------------------------------------------ *
 *  Public API                                                          *
 * ------------------------------------------------------------------ */

/* Initialise safety state.  Must be called after BodyRouting_Init().
 * 'retMapPtr' and 'retMapSize' are the address and byte-size of the
 * constant RetMap array so that the reference CRC can be established. */
FUNC(Std_ReturnType, BODYROUTING_CODE) BR_FuncSafety_Init(
    P2CONST(void, AUTOMATIC, BODYROUTING_APPL_DATA) retMapPtr,
    uint32                                           retMapSize);

/* Called at the start of every BodyRouting_MainFunction invocation. */
FUNC(void, BODYROUTING_CODE) BR_FuncSafety_OnMainFunctionEntry(void);

/* Called at the end of every BodyRouting_MainFunction invocation.
 * Returns TRUE when all safety checks pass (outputs are valid),
 * FALSE when the outputs must be replaced with safe-state defaults. */
FUNC(boolean, BODYROUTING_CODE) BR_FuncSafety_OnMainFunctionExit(void);

/* LIN input range checks (SM-003).
 * Return TRUE = signal is within valid range.
 * Return FALSE = signal is out-of-range; caller must discard it.     */
FUNC(boolean, BODYROUTING_CODE) BR_FuncSafety_CheckInput_MSW(
    P2CONST(LinSig_MSWToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) sig);

FUNC(boolean, BODYROUTING_CODE) BR_FuncSafety_CheckInput_Handle(
    P2CONST(LinSig_HandleToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) sig);

/* CAN output range checks (SM-004).
 * Return TRUE = signal is within valid range.
 * Return FALSE = caller should substitute safe defaults.             */
FUNC(boolean, BODYROUTING_CODE) BR_FuncSafety_CheckOutput_CCVS(
    P2CONST(CanSig_CCVS_VCU, AUTOMATIC, BODYROUTING_APPL_DATA) sig);

FUNC(boolean, BODYROUTING_CODE) BR_FuncSafety_CheckOutput_TSC1(
    P2CONST(CanSig_TSC1_VDR, AUTOMATIC, BODYROUTING_APPL_DATA) sig);

/* Supervision step — called from an independent higher-priority task
 * at twice the MainFunction period (every 20 ms) per SM-001.         */
FUNC(void, BODYROUTING_CODE) BR_FuncSafety_SupervisionStep(void);

/* Report an external fault.  Used by BodyRouting.c when ROM CRC
 * mismatches or plausibility checks fail.                            */
FUNC(void, BODYROUTING_CODE) BR_FuncSafety_ReportFault(
    BR_FuncSafety_FaultType fault);

/* Returns TRUE when a latched safety fault is active (SM-006).       */
FUNC(boolean, BODYROUTING_CODE) BR_FuncSafety_IsFaultLatched(void);

#endif
