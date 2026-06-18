/* BodyRouting Functional Safety Manager — MISRA C:2012 / AUTOSAR R4.3.1.
 * Spec: YG-D18-2026-1042 + TGW-SAF-001 (ISO 26262 ASIL-B).            */

#include "BR_FuncSafety.h"
#include "SafetyMgr.h"
#include "Dem.h"
#include "Det.h"
#include "NuttX_Os.h"

/* ------------------------------------------------------------------ *
 *  Private helpers                                                     *
 * ------------------------------------------------------------------ */

/* CRC-8/AUTOSAR (poly 0x2F, init 0xFF, XOR-out 0xFF).
 * Same polynomial used by AUTOSAR E2E Profile 01 / 02. */
static FUNC(uint8, BODYROUTING_CODE) CrcU8_Compute(
    P2CONST(uint8, AUTOMATIC, BODYROUTING_APPL_DATA) data,
    uint32 length)
{
    uint8  crc = 0xFFu;
    uint32 idx;
    uint8  bit;

    for (idx = 0u; idx < length; idx++) {
        crc ^= data[idx];
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x80u) != 0u) {
                crc = (uint8)((uint8)(crc << 1u) ^ 0x2Fu);
            } else {
                crc = (uint8)(crc << 1u);
            }
        }
    }

    return (uint8)(crc ^ 0xFFu);
}

/* ------------------------------------------------------------------ *
 *  Module state                                                        *
 * ------------------------------------------------------------------ */

/* Alive counter incremented by MainFunction, read by supervision task.
 * Declared volatile because it is written from one thread and read
 * from another (no mutex needed for a monotonic uint32 on 32-bit MCU,
 * but volatile prevents optimiser eliding the read/write pair).       */
static volatile VAR(uint32, BODYROUTING_VAR) AliveCounter;
static VAR(uint32, BODYROUTING_VAR) LastObservedAlive;

/* Execution-time stamping. */
static VAR(uint32, BODYROUTING_VAR) EntryTimeUs;

/* ROM integrity reference. */
static VAR(uint8,  BODYROUTING_VAR) RetMapRefCrc;
static P2CONST(uint8, BODYROUTING_VAR, BODYROUTING_APPL_DATA) RetMapPtr;
static VAR(uint32, BODYROUTING_VAR) RetMapSize;

/* Fault accumulator and latch. */
static VAR(uint8,   BODYROUTING_VAR) ErrorCount;
static VAR(boolean, BODYROUTING_VAR) FaultLatched;
static VAR(BR_FuncSafety_FaultType, BODYROUTING_VAR) LatchedFaultType;

/* Initialised guard (same pattern as BodyRouting). */
static VAR(boolean, BODYROUTING_VAR) Initialised;

/* ------------------------------------------------------------------ *
 *  Internal fault-accumulation logic                                   *
 * ------------------------------------------------------------------ */

static FUNC(void, BODYROUTING_CODE) AccumulateFault(BR_FuncSafety_FaultType fault)
{
    if (FaultLatched == FALSE) {
        if (ErrorCount < BR_FS_ERROR_THRESHOLD) {
            ErrorCount++;
        }
        if (ErrorCount >= BR_FS_ERROR_THRESHOLD) {
            FaultLatched      = TRUE;
            LatchedFaultType  = fault;
            /* Escalate to system safety manager to drop CAN to listen-only,
             * notify WdgM, and set the dashboard tell-tale.            */
            SafetyMgr_EnterSafeState(SAFE_STATE_REASON_WDG_TIMEOUT);
        }
    }
}

static FUNC(void, BODYROUTING_CODE) ClearTransient(void)
{
    if (FaultLatched == FALSE) {
        if (ErrorCount > 0u) {
            ErrorCount--;
        }
    }
}

/* ------------------------------------------------------------------ *
 *  Public API                                                          *
 * ------------------------------------------------------------------ */

FUNC(Std_ReturnType, BODYROUTING_CODE) BR_FuncSafety_Init(
    P2CONST(void, AUTOMATIC, BODYROUTING_APPL_DATA) retMapPtr,
    uint32                                           retMapSize)
{
    Std_ReturnType result;

    if ((retMapPtr == NULL_PTR) || (retMapSize == 0u)) {
        result = E_NOT_OK;
    } else {
        AliveCounter      = 0u;
        LastObservedAlive = 0u;
        EntryTimeUs       = 0u;
        ErrorCount        = 0u;
        FaultLatched      = FALSE;
        LatchedFaultType  = BR_FS_FAULT_NONE;

        RetMapPtr  = (P2CONST(uint8, BODYROUTING_VAR, BODYROUTING_APPL_DATA))retMapPtr;
        RetMapSize = retMapSize;
        RetMapRefCrc = CrcU8_Compute(RetMapPtr, RetMapSize);

        Initialised = TRUE;
        result = E_OK;
    }

    return result;
}

FUNC(void, BODYROUTING_CODE) BR_FuncSafety_OnMainFunctionEntry(void)
{
    if (Initialised == TRUE) {
        EntryTimeUs = OsTime_GetUs();
    }
}

FUNC(boolean, BODYROUTING_CODE) BR_FuncSafety_OnMainFunctionExit(void)
{
    boolean outputsValid;
    uint32  elapsed;
    uint8   currentCrc;

    outputsValid = TRUE;

    if (Initialised != TRUE) {
        outputsValid = FALSE;
    } else {
        /* SM-002: execution-time budget check. */
        elapsed = OsTime_GetUs() - EntryTimeUs;
        if (elapsed > BR_FS_EXEC_BUDGET_US) {
            (void)Dem_ReportErrorStatus(DEM_EVT_DEADLINE_MISS, DEM_EVENT_STATUS_FAILED);
            AccumulateFault(BR_FS_FAULT_DEADLINE);
            outputsValid = FALSE;
        } else {
            ClearTransient();
        }

        /* SM-005: RetMap CRC check (ROM integrity). */
        currentCrc = CrcU8_Compute(RetMapPtr, RetMapSize);
        if (currentCrc != RetMapRefCrc) {
            (void)Dem_ReportErrorStatus(DEM_EVT_ROUTE_TABLE_CRC, DEM_EVENT_STATUS_FAILED);
            AccumulateFault(BR_FS_FAULT_ROM);
            outputsValid = FALSE;
        }

        /* SM-001: increment alive counter (supervision reads this). */
        AliveCounter++;

        if (FaultLatched == TRUE) {
            outputsValid = FALSE;
        }
    }

    return outputsValid;
}

/* SM-001: called from an independent supervision task every 20 ms.
 * Checks that AliveCounter advanced by at least BR_FS_ALIVE_MIN_ADVANCE. */
FUNC(void, BODYROUTING_CODE) BR_FuncSafety_SupervisionStep(void)
{
    uint32 current;
    uint32 advance;

    if (Initialised == TRUE) {
        current = AliveCounter;
        advance = current - LastObservedAlive;
        LastObservedAlive = current;

        if (advance < BR_FS_ALIVE_MIN_ADVANCE) {
            (void)Dem_ReportErrorStatus(DEM_EVT_DEADLINE_MISS, DEM_EVENT_STATUS_FAILED);
            AccumulateFault(BR_FS_FAULT_ALIVE);
        }
    }
}

/* SM-003: LIN MSWToVCU plausibility (all fields are 2-bit signals). */
FUNC(boolean, BODYROUTING_CODE) BR_FuncSafety_CheckInput_MSW(
    P2CONST(LinSig_MSWToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) sig)
{
    boolean valid;

    if (sig == NULL_PTR) {
        valid = FALSE;
    } else {
        /* Each steering-wheel switch field is defined over 0x0..0x3 (2 bits). */
        valid = TRUE;
        if (sig->ccAccModeSwitch          > 0x3u) { valid = FALSE; }
        if (sig->scrollUpButtonStatus     > 0x3u) { valid = FALSE; }
        if (sig->scrollDownButtonStatus   > 0x3u) { valid = FALSE; }
        if (sig->cruiseControlResumeSwitch > 0x3u) { valid = FALSE; }
        if (sig->offSwitch                > 0x3u) { valid = FALSE; }

        if (valid == FALSE) {
            AccumulateFault(BR_FS_FAULT_INPUT);
        } else {
            ClearTransient();
        }
    }

    return valid;
}

/* SM-003: LIN HandleToVCU plausibility. */
FUNC(boolean, BODYROUTING_CODE) BR_FuncSafety_CheckInput_Handle(
    P2CONST(LinSig_HandleToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) sig)
{
    boolean valid;

    if (sig == NULL_PTR) {
        valid = FALSE;
    } else {
        valid = TRUE;
        /* Retarder gear: defined range 0..5 per spec page 2.           */
        if (sig->auxiliaryBrakeGear > BODYROUTING_RETARDER_GEAR_MAX) {
            valid = FALSE;
        }
        /* A/M and M+/M- are single-bit signals (0 or 1).              */
        if (sig->amModeSwitch > 0x1u) { valid = FALSE; }
        if (sig->mPlusSwitch  > 0x1u) { valid = FALSE; }
        if (sig->mMinusSwitch > 0x1u) { valid = FALSE; }

        if (valid == FALSE) {
            AccumulateFault(BR_FS_FAULT_INPUT);
        } else {
            ClearTransient();
        }
    }

    return valid;
}

/* SM-004: CCVS_VCU output plausibility.
 * cruiseControlEnableSwitch must be 0x00, 0x01, or 0x03 per J1939.   */
FUNC(boolean, BODYROUTING_CODE) BR_FuncSafety_CheckOutput_CCVS(
    P2CONST(CanSig_CCVS_VCU, AUTOMATIC, BODYROUTING_APPL_DATA) sig)
{
    boolean valid;

    if (sig == NULL_PTR) {
        valid = FALSE;
    } else {
        valid = TRUE;
        /* Enable switch: only 0x00, 0x01, 0x03 are defined values. */
        if ((sig->cruiseControlEnableSwitch != BODYROUTING_CC_ENABLE_DISABLED) &&
            (sig->cruiseControlEnableSwitch != BODYROUTING_CC_ENABLE_ENABLED)  &&
            (sig->cruiseControlEnableSwitch != BODYROUTING_CC_ENABLE_NOT_AVAILABLE)) {
            valid = FALSE;
        }
        /* Accelerate / coast / resume are single-bit. */
        if (sig->cruiseControlAccelerateSwitch > 1u) { valid = FALSE; }
        if (sig->cruiseControlCoastSwitch      > 1u) { valid = FALSE; }
        if (sig->cruiseControlResumeSwitch     > 1u) { valid = FALSE; }

        if (valid == FALSE) {
            AccumulateFault(BR_FS_FAULT_OUTPUT);
        }
    }

    return valid;
}

/* SM-004: TSC1_VDR output plausibility.
 * requestedTorquePct must be 0 or in [-100, -15] (multiples of the
 * spec table); overrideMode must be 0x0 or 0x2.                      */
FUNC(boolean, BODYROUTING_CODE) BR_FuncSafety_CheckOutput_TSC1(
    P2CONST(CanSig_TSC1_VDR, AUTOMATIC, BODYROUTING_APPL_DATA) sig)
{
    boolean valid;

    if (sig == NULL_PTR) {
        valid = FALSE;
    } else {
        valid = TRUE;

        if ((sig->overrideControlMode != BODYROUTING_OVERRIDE_DISABLED) &&
            (sig->overrideControlMode != BODYROUTING_OVERRIDE_TORQUE_CTRL)) {
            valid = FALSE;
        }
        /* Torque must be non-positive and not below -100 %. */
        if ((sig->requestedTorquePct > (sint8)0) ||
            (sig->requestedTorquePct < (sint8)-100)) {
            valid = FALSE;
        }

        if (valid == FALSE) {
            AccumulateFault(BR_FS_FAULT_OUTPUT);
        }
    }

    return valid;
}

FUNC(void, BODYROUTING_CODE) BR_FuncSafety_ReportFault(BR_FuncSafety_FaultType fault)
{
    AccumulateFault(fault);
}

FUNC(boolean, BODYROUTING_CODE) BR_FuncSafety_IsFaultLatched(void)
{
    return FaultLatched;
}
