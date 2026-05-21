/* BodyRouting — body-domain LIN -> CAN routing (AUTOSAR + MISRA C:2012).
 * Spec: YG-D18-2026-1042, J107 & J169 T-Gateway, 2026-04-22.
 *
 * MISRA C:2012 deviations (all justified):
 *   Dir 4.8  [Adv] — LinSig / CanSig types defined in shared headers that
 *                    are also used by test, LinIf, and Com modules.
 *   Rule 8.7 [Adv] — File-scope statics are shared across Init, callbacks
 *                    and MainFunction within this single translation unit.
 *   Rule 15.5[Adv] — DET error-check paths use else-if chains that provide
 *                    a single logical exit; the body of each branch is a
 *                    complete statement block with no fall-through.         */

/* Own header first (AUTOSAR R4.3.1 coding guideline). */
#include "BodyRouting.h"
#include "BodyRouting_Cfg.h"
#include "BodyRouting_Types.h"
#include "PartConfig.h"
#include "Compiler.h"
#include "Det.h"
#include "Dem.h"
#include "SchM_BodyRouting.h"

/* ------------------------------------------------------------------ *
 *  External interface declarations (fulfilled by generated stubs on   *
 *  target; declared here to avoid pulling in generated headers).      *
 * ------------------------------------------------------------------ */

extern FUNC(void, BODYROUTING_CODE) Com_Send_CCVS_VCU(
    P2CONST(CanSig_CCVS_VCU, AUTOMATIC, BODYROUTING_APPL_DATA) s);

extern FUNC(void, BODYROUTING_CODE) Com_Send_TSC1_VDR(
    P2CONST(CanSig_TSC1_VDR, AUTOMATIC, BODYROUTING_APPL_DATA) s);

extern FUNC(void, BODYROUTING_CODE) Com_Send_TSC1_DR(
    P2CONST(CanSig_TSC1_VDR, AUTOMATIC, BODYROUTING_APPL_DATA) s);

extern FUNC(void, BODYROUTING_CODE) Com_Send_TC1(
    P2CONST(CanSig_TC1, AUTOMATIC, BODYROUTING_APPL_DATA) s);

extern FUNC(uint32, BODYROUTING_CODE) OsTime_GetMs(void);

/* ------------------------------------------------------------------ *
 *  Retarder gear -> J1939 torque map (YG-D18-2026-1042 page 3)        *
 *  Priority is always BODYROUTING_OVERRIDE_PRIO_LOW for all gears.    *
 * ------------------------------------------------------------------ */

typedef struct {
    sint8 reqTorquePct;   /* Requested Torque/Torque Limit: 0, -15..-100  */
    uint8 overrideMode;   /* Override Control Mode: b00=off, b10=torque   */
} RetarderEntryType;

static CONST(RetarderEntryType, BODYROUTING_CONST)
RetMap[(uint8)(BODYROUTING_RETARDER_GEAR_MAX + 1u)] = {
    /* [0] gear 0 — off (缓速器0)   */ {   0, BODYROUTING_OVERRIDE_DISABLED    },
    /* [1] gear 1 — 恒速挡 (-15%)   */ { -15, BODYROUTING_OVERRIDE_TORQUE_CTRL },
    /* [2] gear 2 — 制动1挡 (-25%)  */ { -25, BODYROUTING_OVERRIDE_TORQUE_CTRL },
    /* [3] gear 3 — 制动2挡 (-50%)  */ { -50, BODYROUTING_OVERRIDE_TORQUE_CTRL },
    /* [4] gear 4 — 制动3挡 (-75%)  */ { -75, BODYROUTING_OVERRIDE_TORQUE_CTRL },
    /* [5] gear 5 — 制动4挡 (-100%) */ { -100, BODYROUTING_OVERRIDE_TORQUE_CTRL }
};

/* ------------------------------------------------------------------ *
 *  Module variables (AUTOSAR VAR macro; section assigned via MemMap)  *
 * ------------------------------------------------------------------ */

static VAR(BodyRouting_StateType,  BODYROUTING_VAR) ModuleState;
static VAR(uint8,                  BODYROUTING_VAR) PrevCcAccMode;
static VAR(uint8,                  BODYROUTING_VAR) PrevOffSwitch;
static VAR(LinSig_MSWToVCU,        BODYROUTING_VAR) LastMsw;
static VAR(LinSig_HandleToVCU,     BODYROUTING_VAR) LastHnd;
static VAR(uint32,                 BODYROUTING_VAR) LastMswMs;
static VAR(uint32,                 BODYROUTING_VAR) LastHndMs;
static VAR(BodyRouting_StatsType,  BODYROUTING_VAR) Stats;

/* ------------------------------------------------------------------ *
 *  Private helpers (static; used across Init / callbacks / Main)      *
 * ------------------------------------------------------------------ */

/* Returns TRUE when the LIN shadow copy is absent or older than the
 * staleness threshold (BODYROUTING_LIN_STALE_MS).
 * MISRA Rule 15.5 [Adv]: single exit via return of local result. */
static FUNC(boolean, BODYROUTING_CODE) IsLinStale(
    uint8  valid,
    uint32 lastMs,
    uint32 nowMs)
{
    boolean result;

    if ((valid == 0u) || ((nowMs - lastMs) > BODYROUTING_LIN_STALE_MS)) {
        result = TRUE;
    } else {
        result = FALSE;
    }

    return result;
}

/* Map LIN MSWToVCU signals to CAN CCVS_VCU fields.
 *
 * Cruise enable/disable uses a self-resetting pulse (自复位型):
 *   Idle -> 0x01 Enabled  (one MainFunction cycle) -> Idle   on CC-ON  rising edge
 *   Idle -> 0x00 Disabled (one MainFunction cycle) -> Idle   on OFF    rising edge
 * When both edges occur simultaneously, OFF wins (safety-first).
 * SET+/SET-/RES are while-held: 0x1 while pressed, 0x0 on release.
 * Long/short press distinction is delegated to EMS (spec Q&A #3).   */
static FUNC(void, BODYROUTING_CODE) MapCruise(
    P2CONST(LinSig_MSWToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) lin,
    P2VAR(CanSig_CCVS_VCU,   AUTOMATIC, BODYROUTING_APPL_DATA) out)
{
    boolean modeRising;
    boolean offRising;

    /* Defaults: idle / Not Available. */
    out->cruiseControlEnableSwitch     = BODYROUTING_CC_ENABLE_NOT_AVAILABLE;
    out->cruiseControlAccelerateSwitch = 0u;
    out->cruiseControlCoastSwitch      = 0u;
    out->cruiseControlResumeSwitch     = 0u;

    /* Detect rising edges on CC-ON and CC-OFF switches. */
    if ((lin->ccAccModeSwitch == 0x1u) && (PrevCcAccMode != 0x1u)) {
        modeRising = TRUE;
    } else {
        modeRising = FALSE;
    }

    if ((lin->offSwitch == 0x1u) && (PrevOffSwitch != 0x1u)) {
        offRising = TRUE;
    } else {
        offRising = FALSE;
    }

    /* OFF wins when both edges occur simultaneously (MISRA 14.4: boolean ctrl expr). */
    if (offRising == TRUE) {
        out->cruiseControlEnableSwitch = BODYROUTING_CC_ENABLE_DISABLED;
        Stats.cruisePulsesDisable++;
    } else if (modeRising == TRUE) {
        out->cruiseControlEnableSwitch = BODYROUTING_CC_ENABLE_ENABLED;
        Stats.cruisePulsesEnable++;
    } else {
        /* Idle state retained; no action required. */
    }

    /* While-pressed passthroughs: spec says 0x1 or 0x2 both assert SET+/SET-. */
    if ((lin->scrollUpButtonStatus == 0x1u) || (lin->scrollUpButtonStatus == 0x2u)) {
        out->cruiseControlAccelerateSwitch = 1u;
    }

    if ((lin->scrollDownButtonStatus == 0x1u) || (lin->scrollDownButtonStatus == 0x2u)) {
        out->cruiseControlCoastSwitch = 1u;
    }

    if (lin->cruiseControlResumeSwitch == 0x1u) {
        out->cruiseControlResumeSwitch = 1u;
    }

    /* Save current state for next-cycle edge detection. */
    PrevCcAccMode = lin->ccAccModeSwitch;
    PrevOffSwitch = lin->offSwitch;
}

/* Map LIN HandleToVCU auxiliary-brake gear to J1939 TSC1_VDR fields.
 * Gear values outside 0..BODYROUTING_RETARDER_GEAR_MAX are clamped to
 * gear-0 (off) so an out-of-spec LIN byte never commands unintended torque. */
static FUNC(void, BODYROUTING_CODE) MapRetarder(
    P2CONST(LinSig_HandleToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) lin,
    P2VAR(CanSig_TSC1_VDR,      AUTOMATIC, BODYROUTING_APPL_DATA) out)
{
    uint8 gear;

    gear = lin->auxiliaryBrakeGear;
    if (gear > BODYROUTING_RETARDER_GEAR_MAX) {
        gear = BODYROUTING_RETARDER_GEAR_OFF;
    }

    out->overrideControlMode         = RetMap[gear].overrideMode;
    out->overrideControlModePriority = BODYROUTING_OVERRIDE_PRIO_LOW;
    out->requestedTorquePct          = RetMap[gear].reqTorquePct;
    out->checksum                    = BODYROUTING_TSC1_CHECKSUM_NONE; /* "不做校验，发 FF" */
}

/* Pass AMT gear and mode signals straight through (no transform needed). */
static FUNC(void, BODYROUTING_CODE) MapAmt(
    P2CONST(LinSig_HandleToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) lin,
    P2VAR(CanSig_TC1,           AUTOMATIC, BODYROUTING_APPL_DATA) out)
{
    out->transmissionRequestedGear = lin->transmissionRequestedGear;
    out->transmissionMode1         = lin->transmissionMode1;
    out->amModeSwitch              = lin->amModeSwitch;
    out->mPlusSwitch               = lin->mPlusSwitch;
    out->mMinusSwitch              = lin->mMinusSwitch;
}

/* ------------------------------------------------------------------ *
 *  Public API                                                          *
 * ------------------------------------------------------------------ */

FUNC(Std_ReturnType, BODYROUTING_CODE) BodyRouting_Init(void)
{
    /* Reset edge-detection history. */
    PrevCcAccMode = 0u;
    PrevOffSwitch = 0u;

    /* Zero LIN shadow copies field-by-field (no compound literal: MISRA
     * C:2012 Rule 9.1 / toolchain portability). */
    LastMsw.ccAccModeSwitch           = 0u;
    LastMsw.scrollUpButtonStatus      = 0u;
    LastMsw.scrollDownButtonStatus    = 0u;
    LastMsw.cruiseControlResumeSwitch = 0u;
    LastMsw.offSwitch                 = 0u;
    LastMsw.valid                     = 0u;

    LastHnd.auxiliaryBrakeGear        = 0u;
    LastHnd.transmissionRequestedGear = 0u;
    LastHnd.transmissionMode1         = 0u;
    LastHnd.amModeSwitch              = 0u;
    LastHnd.mPlusSwitch               = 0u;
    LastHnd.mMinusSwitch              = 0u;
    LastHnd.valid                     = 0u;

    LastMswMs = 0u;
    LastHndMs = 0u;

    /* Zero diagnostic counters field-by-field. */
    Stats.cruisePulsesEnable  = 0u;
    Stats.cruisePulsesDisable = 0u;
    Stats.retarderUpdates     = 0u;
    Stats.amtUpdates          = 0u;
    Stats.staleLinDrops       = 0u;

    ModuleState = BODYROUTING_STATE_INIT;

    return E_OK;
}

/* LinIf RX-indication — called on receipt of LIN PDU MSWToVCU (0x02).
 * Pre-condition: sig != NULL_PTR, module initialised.
 * On DET error the function returns without modifying module state. */
FUNC(void, BODYROUTING_CODE) BodyRouting_OnLinMSWToVCU(
    P2CONST(LinSig_MSWToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) sig)
{
    if (ModuleState != BODYROUTING_STATE_INIT) {
        (void)Det_ReportError(BODYROUTING_MODULE_ID, BODYROUTING_INSTANCE_ID,
                              BODYROUTING_API_ID_ON_LIN_MSW, BODYROUTING_E_UNINIT);
    } else if (sig == NULL_PTR) {
        (void)Det_ReportError(BODYROUTING_MODULE_ID, BODYROUTING_INSTANCE_ID,
                              BODYROUTING_API_ID_ON_LIN_MSW, BODYROUTING_E_NULL_PTR);
    } else {
        SchM_Enter_BodyRouting_LinRxData();
        LastMsw       = *sig;
        LastMsw.valid = 1u;
        LastMswMs     = OsTime_GetMs();
        SchM_Exit_BodyRouting_LinRxData();
    }
}

/* LinIf RX-indication — called on receipt of LIN PDU HandleToVCU (0x01).
 * Shared by both auxiliary-brake and AMT-shift routing paths.           */
FUNC(void, BODYROUTING_CODE) BodyRouting_OnLinHandleToVCU(
    P2CONST(LinSig_HandleToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) sig)
{
    if (ModuleState != BODYROUTING_STATE_INIT) {
        (void)Det_ReportError(BODYROUTING_MODULE_ID, BODYROUTING_INSTANCE_ID,
                              BODYROUTING_API_ID_ON_LIN_HANDLE, BODYROUTING_E_UNINIT);
    } else if (sig == NULL_PTR) {
        (void)Det_ReportError(BODYROUTING_MODULE_ID, BODYROUTING_INSTANCE_ID,
                              BODYROUTING_API_ID_ON_LIN_HANDLE, BODYROUTING_E_NULL_PTR);
    } else {
        SchM_Enter_BodyRouting_LinRxData();
        LastHnd       = *sig;
        LastHnd.valid = 1u;
        LastHndMs     = OsTime_GetMs();
        SchM_Exit_BodyRouting_LinRxData();
    }
}

/* Cyclic 10 ms runnable registered with SchM_Act_BodyRouting.
 * Reads atomic snapshots of LIN shadow copies, applies the three
 * routing transforms, and posts the results via Com_SendSignal. */
FUNC(void, BODYROUTING_CODE) BodyRouting_MainFunction(void)
{
    P2CONST(PartConfig, AUTOMATIC, BODYROUTING_APPL_DATA) cfg;
    uint32             now;
    LinSig_MSWToVCU    mswSnap;
    LinSig_HandleToVCU hndSnap;
    uint32             mswSnapMs;
    uint32             hndSnapMs;

    if (ModuleState != BODYROUTING_STATE_INIT) {
        (void)Det_ReportError(BODYROUTING_MODULE_ID, BODYROUTING_INSTANCE_ID,
                              BODYROUTING_API_ID_MAIN_FUNCTION, BODYROUTING_E_UNINIT);
    } else {
        cfg = PartConfig_Get();
        now = OsTime_GetMs();

        /* Snapshot LIN shadow copies atomically (Rule 8.9: vars shared
         * with ISR-context callbacks must be read inside SchM gate).  */
        SchM_Enter_BodyRouting_LinRxData();
        mswSnap   = LastMsw;
        hndSnap   = LastHnd;
        mswSnapMs = LastMswMs;
        hndSnapMs = LastHndMs;
        SchM_Exit_BodyRouting_LinRxData();

        /* ---- 1. Cruise: LIN MSWToVCU -> CAN CCVS_VCU (SA 0x05) -> EMS ---- */
        if (cfg->cruiseSrc == CRUISE_SRC_LIN) {
            CanSig_CCVS_VCU ccvs;
            ccvs.cruiseControlEnableSwitch     = BODYROUTING_CC_ENABLE_NOT_AVAILABLE;
            ccvs.cruiseControlAccelerateSwitch = 0u;
            ccvs.cruiseControlCoastSwitch      = 0u;
            ccvs.cruiseControlResumeSwitch     = 0u;

            if (IsLinStale(mswSnap.valid, mswSnapMs, now) == TRUE) {
                Stats.staleLinDrops++;
                /* Hold Not Available; receiver node-timeout supervision
                 * detects the gateway reset gap (spec Q&A #1).        */
            } else {
                MapCruise(&mswSnap, &ccvs);
            }
            Com_Send_CCVS_VCU(&ccvs);
        }

        /* ---- 2. Aux brake: LIN HandleToVCU -> CAN TSC1_VDR -> RCU --------- */
        if (cfg->auxBrake != AUX_BRAKE_NONE) {
            CanSig_TSC1_VDR tsc1;
            tsc1.overrideControlMode         = BODYROUTING_OVERRIDE_DISABLED;
            tsc1.overrideControlModePriority = BODYROUTING_OVERRIDE_PRIO_LOW;
            tsc1.requestedTorquePct          = (sint8)0;
            tsc1.checksum                    = BODYROUTING_TSC1_CHECKSUM_NONE;

            if (IsLinStale(hndSnap.valid, hndSnapMs, now) == FALSE) {
                MapRetarder(&hndSnap, &tsc1);
            }

            if (cfg->auxBrake == AUX_BRAKE_RETARDER) {
                Com_Send_TSC1_VDR(&tsc1);      /* SA 0x27 -> RCU */
            } else {
                Com_Send_TSC1_DR(&tsc1);       /* engine brake   -> EMS */
            }
            Stats.retarderUpdates++;
        }

        /* ---- 3. AMT shift: LIN HandleToVCU -> CAN TC1 (SA 0x05) -> TCU ---- */
        if (cfg->txType == TX_TYPE_AMT) {
            CanSig_TC1 tc1;
            tc1.transmissionRequestedGear = 0u;
            tc1.transmissionMode1         = 0u;
            tc1.amModeSwitch              = 0u;
            tc1.mPlusSwitch               = 0u;
            tc1.mMinusSwitch              = 0u;

            if (IsLinStale(hndSnap.valid, hndSnapMs, now) == FALSE) {
                MapAmt(&hndSnap, &tc1);
            }
            Com_Send_TC1(&tc1);
            Stats.amtUpdates++;
        }
        /* TX_TYPE_MT: no TC1 sent — routing disabled per spec page 4. */
    }
}

/* Read-only pointer into the module's diagnostic counter block.
 * Returns NULL_PTR when called before BodyRouting_Init().           */
FUNC(P2CONST(BodyRouting_StatsType, AUTOMATIC, BODYROUTING_APPL_DATA),
     BODYROUTING_CODE)
BodyRouting_GetStats(void)
{
    P2CONST(BodyRouting_StatsType, AUTOMATIC, BODYROUTING_APPL_DATA) result;

    if (ModuleState != BODYROUTING_STATE_INIT) {
        (void)Det_ReportError(BODYROUTING_MODULE_ID, BODYROUTING_INSTANCE_ID,
                              BODYROUTING_API_ID_GET_STATS, BODYROUTING_E_UNINIT);
        result = NULL_PTR;
    } else {
        result = &Stats;
    }

    return result;
}
