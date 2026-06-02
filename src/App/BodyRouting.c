/* BodyRouting — body-domain LIN -> CAN routing.
 * AUTOSAR Classic R4.3.1 + NuttX OS + ISO 26262 ASIL-B.
 * Spec: YG-D18-2026-1042, J107 & J169 T-Gateway, 2026-04-22.
 *
 * MISRA C:2012 deviations (all justified):
 *   Dir 4.8  [Adv] — LinSig / CanSig types shared across LinIf, Com, test.
 *   Rule 8.7 [Adv] — File-scope statics shared across Init, callbacks,
 *                    and MainFunction within this single TU.
 *   Rule 15.5[Adv] — DET error-check paths use else-if chains with a
 *                    single logical exit per branch.                   */

#include "BodyRouting.h"
#include "BodyRouting_Cfg.h"
#include "BodyRouting_Types.h"
#include "PartConfig.h"
#include "Compiler.h"
#include "Det.h"
#include "Dem.h"
#include "SchM_BodyRouting.h"
#include "BR_FuncSafety.h"
#include "NuttX_Os.h"

/* ------------------------------------------------------------------ *
 *  External Com signal API (fulfilled by generated stubs on target)    *
 * ------------------------------------------------------------------ */

extern FUNC(void, BODYROUTING_CODE) Com_Send_CCVS_VCU(
    P2CONST(CanSig_CCVS_VCU, AUTOMATIC, BODYROUTING_APPL_DATA) s);
extern FUNC(void, BODYROUTING_CODE) Com_Send_TSC1_VDR(
    P2CONST(CanSig_TSC1_VDR, AUTOMATIC, BODYROUTING_APPL_DATA) s);
extern FUNC(void, BODYROUTING_CODE) Com_Send_TSC1_DR(
    P2CONST(CanSig_TSC1_VDR, AUTOMATIC, BODYROUTING_APPL_DATA) s);
extern FUNC(void, BODYROUTING_CODE) Com_Send_TC1(
    P2CONST(CanSig_TC1, AUTOMATIC, BODYROUTING_APPL_DATA) s);

/* ------------------------------------------------------------------ *
 *  Retarder gear -> J1939 torque map (YG-D18-2026-1042 page 3)
 *  Protected by CRC-8 (SM-005 in BR_FuncSafety).                      *
 * ------------------------------------------------------------------ */

typedef struct {
    sint8 reqTorquePct;   /* 0, -15, -25, -50, -75, -100             */
    uint8 overrideMode;   /* b00=Override disabled, b10=Torque ctrl  */
} RetarderEntryType;

static CONST(RetarderEntryType, BODYROUTING_CONST)
RetMap[(uint8)(BODYROUTING_RETARDER_GEAR_MAX + 1u)] = {
    /* [0] off (缓速器0)    */ {   0, BODYROUTING_OVERRIDE_DISABLED    },
    /* [1] 恒速挡  (-15 %)  */ { -15, BODYROUTING_OVERRIDE_TORQUE_CTRL },
    /* [2] 制动1挡 (-25 %)  */ { -25, BODYROUTING_OVERRIDE_TORQUE_CTRL },
    /* [3] 制动2挡 (-50 %)  */ { -50, BODYROUTING_OVERRIDE_TORQUE_CTRL },
    /* [4] 制动3挡 (-75 %)  */ { -75, BODYROUTING_OVERRIDE_TORQUE_CTRL },
    /* [5] 制动4挡 (-100 %) */ { -100, BODYROUTING_OVERRIDE_TORQUE_CTRL }
};

/* ------------------------------------------------------------------ *
 *  NuttX exclusive-area mutex (owns the LinRx shadow copies)           *
 * ------------------------------------------------------------------ */

static VAR(NuttX_MutexType, BODYROUTING_VAR) LinRxMutex;

/* Called by SchM_BodyRouting.h inline wrappers. */
FUNC(void, BODYROUTING_CODE) BodyRouting_EnterLinRxMutex(void)
{
    NuttX_MutexLock(&LinRxMutex);
}

FUNC(void, BODYROUTING_CODE) BodyRouting_ExitLinRxMutex(void)
{
    NuttX_MutexUnlock(&LinRxMutex);
}

/* ------------------------------------------------------------------ *
 *  Module variables                                                    *
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
 *  Safe-state CAN output constants (SM-006)                            *
 * ------------------------------------------------------------------ */

static CONST(CanSig_CCVS_VCU, BODYROUTING_CONST) SafeCcvs = {
    BODYROUTING_CC_ENABLE_NOT_AVAILABLE, 0u, 0u, 0u
};

static CONST(CanSig_TSC1_VDR, BODYROUTING_CONST) SafeTsc1 = {
    BODYROUTING_OVERRIDE_DISABLED, BODYROUTING_OVERRIDE_PRIO_LOW,
    (sint8)0, BODYROUTING_TSC1_CHECKSUM_NONE
};

static CONST(CanSig_TC1, BODYROUTING_CONST) SafeTc1 = {
    0u, 0u, 0u, 0u, 0u
};

/* ------------------------------------------------------------------ *
 *  Private helpers                                                     *
 * ------------------------------------------------------------------ */

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

static FUNC(void, BODYROUTING_CODE) MapCruise(
    P2CONST(LinSig_MSWToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) lin,
    P2VAR(CanSig_CCVS_VCU,   AUTOMATIC, BODYROUTING_APPL_DATA) out)
{
    boolean modeRising;
    boolean offRising;

    out->cruiseControlEnableSwitch     = BODYROUTING_CC_ENABLE_NOT_AVAILABLE;
    out->cruiseControlAccelerateSwitch = 0u;
    out->cruiseControlCoastSwitch      = 0u;
    out->cruiseControlResumeSwitch     = 0u;

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

    /* OFF wins when both edges occur simultaneously (safety-first). */
    if (offRising == TRUE) {
        out->cruiseControlEnableSwitch = BODYROUTING_CC_ENABLE_DISABLED;
        Stats.cruisePulsesDisable++;
    } else if (modeRising == TRUE) {
        out->cruiseControlEnableSwitch = BODYROUTING_CC_ENABLE_ENABLED;
        Stats.cruisePulsesEnable++;
    } else {
        /* Idle state retained. */
    }

    if ((lin->scrollUpButtonStatus == 0x1u) || (lin->scrollUpButtonStatus == 0x2u)) {
        out->cruiseControlAccelerateSwitch = 1u;
    }
    if ((lin->scrollDownButtonStatus == 0x1u) || (lin->scrollDownButtonStatus == 0x2u)) {
        out->cruiseControlCoastSwitch = 1u;
    }
    if (lin->cruiseControlResumeSwitch == 0x1u) {
        out->cruiseControlResumeSwitch = 1u;
    }

    PrevCcAccMode = lin->ccAccModeSwitch;
    PrevOffSwitch = lin->offSwitch;
}

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
    out->checksum                    = BODYROUTING_TSC1_CHECKSUM_NONE;
}

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

/* SM-006: broadcast safe-state values on all active routing paths. */
static FUNC(void, BODYROUTING_CODE) SendSafeStateOutputs(
    P2CONST(PartConfig, AUTOMATIC, BODYROUTING_APPL_DATA) cfg)
{
    if (cfg->cruiseSrc == CRUISE_SRC_LIN) {
        Com_Send_CCVS_VCU(&SafeCcvs);
    }
    if (cfg->auxBrake != AUX_BRAKE_NONE) {
        if (cfg->auxBrake == AUX_BRAKE_RETARDER) {
            Com_Send_TSC1_VDR(&SafeTsc1);
        } else {
            Com_Send_TSC1_DR(&SafeTsc1);
        }
    }
    if (cfg->txType == TX_TYPE_AMT) {
        Com_Send_TC1(&SafeTc1);
    }
}

/* ------------------------------------------------------------------ *
 *  Public API                                                          *
 * ------------------------------------------------------------------ */

FUNC(Std_ReturnType, BODYROUTING_CODE) BodyRouting_Init(void)
{
    Std_ReturnType r;

    PrevCcAccMode = 0u;
    PrevOffSwitch = 0u;

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

    Stats.cruisePulsesEnable  = 0u;
    Stats.cruisePulsesDisable = 0u;
    Stats.retarderUpdates     = 0u;
    Stats.amtUpdates          = 0u;
    Stats.staleLinDrops       = 0u;

    /* Initialise the NuttX mutex that backs the SchM exclusive area. */
    r = NuttX_MutexInit(&LinRxMutex);

    if (r == E_OK) {
        /* Initialise functional safety manager with RetMap address and
         * size so it can establish the ROM-integrity reference CRC.   */
        r = BR_FuncSafety_Init(
                (P2CONST(void, AUTOMATIC, BODYROUTING_APPL_DATA))RetMap,
                (uint32)sizeof(RetMap));
    }

    if (r == E_OK) {
        ModuleState = BODYROUTING_STATE_INIT;
    }

    return r;
}

FUNC(void, BODYROUTING_CODE) BodyRouting_OnLinMSWToVCU(
    P2CONST(LinSig_MSWToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) sig)
{
    if (ModuleState != BODYROUTING_STATE_INIT) {
        (void)Det_ReportError(BODYROUTING_MODULE_ID, BODYROUTING_INSTANCE_ID,
                              BODYROUTING_API_ID_ON_LIN_MSW, BODYROUTING_E_UNINIT);
    } else if (sig == NULL_PTR) {
        (void)Det_ReportError(BODYROUTING_MODULE_ID, BODYROUTING_INSTANCE_ID,
                              BODYROUTING_API_ID_ON_LIN_MSW, BODYROUTING_E_NULL_PTR);
    } else if (BR_FuncSafety_CheckInput_MSW(sig) == FALSE) {
        /* SM-003: out-of-range LIN frame — discard, retain last valid. */
        (void)Dem_ReportErrorStatus(DEM_EVT_PDUR_LEN, DEM_EVENT_STATUS_FAILED);
    } else {
        SchM_Enter_BodyRouting_LinRxData();
        LastMsw       = *sig;
        LastMsw.valid = 1u;
        LastMswMs     = OsTime_GetMs();
        SchM_Exit_BodyRouting_LinRxData();
    }
}

FUNC(void, BODYROUTING_CODE) BodyRouting_OnLinHandleToVCU(
    P2CONST(LinSig_HandleToVCU, AUTOMATIC, BODYROUTING_APPL_DATA) sig)
{
    if (ModuleState != BODYROUTING_STATE_INIT) {
        (void)Det_ReportError(BODYROUTING_MODULE_ID, BODYROUTING_INSTANCE_ID,
                              BODYROUTING_API_ID_ON_LIN_HANDLE, BODYROUTING_E_UNINIT);
    } else if (sig == NULL_PTR) {
        (void)Det_ReportError(BODYROUTING_MODULE_ID, BODYROUTING_INSTANCE_ID,
                              BODYROUTING_API_ID_ON_LIN_HANDLE, BODYROUTING_E_NULL_PTR);
    } else if (BR_FuncSafety_CheckInput_Handle(sig) == FALSE) {
        /* SM-003: out-of-range LIN frame — discard. */
        (void)Dem_ReportErrorStatus(DEM_EVT_PDUR_LEN, DEM_EVENT_STATUS_FAILED);
    } else {
        SchM_Enter_BodyRouting_LinRxData();
        LastHnd       = *sig;
        LastHnd.valid = 1u;
        LastHndMs     = OsTime_GetMs();
        SchM_Exit_BodyRouting_LinRxData();
    }
}

FUNC(void, BODYROUTING_CODE) BodyRouting_MainFunction(void)
{
    P2CONST(PartConfig, AUTOMATIC, BODYROUTING_APPL_DATA) cfg;
    uint32             now;
    LinSig_MSWToVCU    mswSnap;
    LinSig_HandleToVCU hndSnap;
    uint32             mswSnapMs;
    uint32             hndSnapMs;
    boolean            outputsOk;

    if (ModuleState != BODYROUTING_STATE_INIT) {
        (void)Det_ReportError(BODYROUTING_MODULE_ID, BODYROUTING_INSTANCE_ID,
                              BODYROUTING_API_ID_MAIN_FUNCTION, BODYROUTING_E_UNINIT);
    } else {
        BR_FuncSafety_OnMainFunctionEntry();  /* SM-002: record entry timestamp */

        cfg = PartConfig_Get();
        now = OsTime_GetMs();

        SchM_Enter_BodyRouting_LinRxData();
        mswSnap   = LastMsw;
        hndSnap   = LastHnd;
        mswSnapMs = LastMswMs;
        hndSnapMs = LastHndMs;
        SchM_Exit_BodyRouting_LinRxData();

        /* SM-006: if a latched safety fault is active, send safe-state
         * outputs and skip all routing computations.                  */
        if (BR_FuncSafety_IsFaultLatched() == TRUE) {
            SendSafeStateOutputs(cfg);
        } else {
            /* ---- 1. Cruise ---------------------------------------- */
            if (cfg->cruiseSrc == CRUISE_SRC_LIN) {
                CanSig_CCVS_VCU ccvs;
                ccvs.cruiseControlEnableSwitch     = BODYROUTING_CC_ENABLE_NOT_AVAILABLE;
                ccvs.cruiseControlAccelerateSwitch = 0u;
                ccvs.cruiseControlCoastSwitch      = 0u;
                ccvs.cruiseControlResumeSwitch     = 0u;

                if (IsLinStale(mswSnap.valid, mswSnapMs, now) == TRUE) {
                    Stats.staleLinDrops++;
                } else {
                    MapCruise(&mswSnap, &ccvs);
                }

                /* SM-004: output plausibility before sending. */
                if (BR_FuncSafety_CheckOutput_CCVS(&ccvs) == TRUE) {
                    Com_Send_CCVS_VCU(&ccvs);
                } else {
                    Com_Send_CCVS_VCU(&SafeCcvs);
                }
            }

            /* ---- 2. Aux brake ------------------------------------- */
            if (cfg->auxBrake != AUX_BRAKE_NONE) {
                CanSig_TSC1_VDR tsc1;
                tsc1.overrideControlMode         = BODYROUTING_OVERRIDE_DISABLED;
                tsc1.overrideControlModePriority = BODYROUTING_OVERRIDE_PRIO_LOW;
                tsc1.requestedTorquePct          = (sint8)0;
                tsc1.checksum                    = BODYROUTING_TSC1_CHECKSUM_NONE;

                if (IsLinStale(hndSnap.valid, hndSnapMs, now) == FALSE) {
                    MapRetarder(&hndSnap, &tsc1);
                }

                /* SM-004: output plausibility. */
                if (BR_FuncSafety_CheckOutput_TSC1(&tsc1) == FALSE) {
                    tsc1 = SafeTsc1;
                }

                if (cfg->auxBrake == AUX_BRAKE_RETARDER) {
                    Com_Send_TSC1_VDR(&tsc1);
                } else {
                    Com_Send_TSC1_DR(&tsc1);
                }
                Stats.retarderUpdates++;
            }

            /* ---- 3. AMT shift ------------------------------------- */
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
        }

        /* SM-002 + SM-005: check deadline and ROM integrity.
         * Returns FALSE when a fault is detected; outputs have already
         * been sent so no rollback needed — next cycle enters safe state. */
        outputsOk = BR_FuncSafety_OnMainFunctionExit();
        if (outputsOk == FALSE) {
            Stats.staleLinDrops++;   /* reuse counter as general fault log */
        }
    }
}

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
