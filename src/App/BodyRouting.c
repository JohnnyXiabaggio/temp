/* Body-domain LIN -> CAN routing.
 *
 *  Source spec: YG-D18-2026-1042 (J107 & J169 T-Gateway, 2026-04-22).
 *
 *  Cross-cutting rules from the spec:
 *
 *  (a) Gateway product reset has < 300 ms of LIN-CAN unavailability
 *      ("LIN转CAN无功能"). Downstream ECUs detect this via "node lost
 *      / timeout" supervision. The routing manager must not output
 *      stale CAN values during this window. We implement this by
 *      not transmitting any signal until the corresponding LIN PDU
 *      has been received at least once, and by holding the CAN
 *      "Cruise Control Enable Switch" at 0x03 "Not Available" when
 *      no LIN data has arrived in LIN_STALE_MS.
 *
 *  (b) Cruise on/off is a single-shot pulse on the rising edge of
 *      the LIN switch:   未按下 -> 0x03 Not Available
 *                        按下 ON  -> 0x01 Enabled  (one cycle)  -> 0x03
 *                        按下 OFF -> 0x00 Disabled (one cycle)  -> 0x03
 *
 *  (c) The retarder gear -> requested-torque table is per spec
 *      (page 3). No checksum -- byte 8 of TSC1_VDR is 0xFF.
 *
 *  (d) Long/short press of SET+/SET- is decided by the EMS; the
 *      gateway forwards the raw "while pressed" signal.
 *
 *  (e) Brake-pedal cruise exit is decided by the EMS (Q&A #2).
 *
 *  (f) 102A part-config drives the routing:
 *        - CruiseSrcType  = LIN     => execute cruise routing
 *        - CruiseSrcType != LIN     => skip cruise routing
 *        - AuxBrakeType   = RETARDER=> route to RCU (TSC1_VDR SA 0x27)
 *        - AuxBrakeType   = ENGINE  => route to EMS (TSC1_DR  SA 0x00)
 *        - TxType         = MT      => skip AMT routing entirely
 */

#include "BodyRouting.h"
#include "PartConfig.h"
#include "Compiler.h"
#include "Dem.h"

extern uint32 OsTime_GetMs(void);

/* Com signal updaters. On target these are Com_SendSignal calls
 * generated from the CAN matrix. */
extern void Com_Send_CCVS_VCU (const CanSig_CCVS_VCU  *s);
extern void Com_Send_TSC1_VDR (const CanSig_TSC1_VDR  *s);
extern void Com_Send_TSC1_DR  (const CanSig_TSC1_VDR  *s);   /* engine brake variant */
extern void Com_Send_TC1      (const CanSig_TC1       *s);

#define LIN_STALE_MS  300u    /* >= gateway reset window (Q&A #1) */

/* ------------------------------------------------------------------ *
 *  Module state
 * ------------------------------------------------------------------ */

static uint8 PrevCcAccMode;
static uint8 PrevOffSwitch;

static LinSig_MSWToVCU   LastMsw;
static LinSig_HandleToVCU LastHnd;
static uint32             LastMswMs;
static uint32             LastHndMs;

static BodyRouting_StatsType Stats;

/* ------------------------------------------------------------------ *
 *  Retarder gear -> torque table (YG-D18-2026-1042 page 3)
 * ------------------------------------------------------------------ */

typedef struct {
    sint8 reqTorquePct;       /* 0, -15, -25, -50, -75, -100  */
    uint8 overrideMode;       /* 0x0 disabled / 0x2 torque    */
    uint8 priority;           /* 0x3 low                       */
} RetarderEntry;

static const RetarderEntry RetMap[6] = {
    /* gear 0 - off:        */ { .reqTorquePct =    0, .overrideMode = 0x0u, .priority = 0x3u },
    /* gear 1 - 恒速档:     */ { .reqTorquePct =  -15, .overrideMode = 0x2u, .priority = 0x3u },
    /* gear 2 - 制动1挡:    */ { .reqTorquePct =  -25, .overrideMode = 0x2u, .priority = 0x3u },
    /* gear 3 - 制动2挡:    */ { .reqTorquePct =  -50, .overrideMode = 0x2u, .priority = 0x3u },
    /* gear 4 - 制动3挡:    */ { .reqTorquePct =  -75, .overrideMode = 0x2u, .priority = 0x3u },
    /* gear 5 - 制动4挡:    */ { .reqTorquePct = -100, .overrideMode = 0x2u, .priority = 0x3u }
};

/* ------------------------------------------------------------------ *
 *  Init
 * ------------------------------------------------------------------ */

Std_ReturnType BodyRouting_Init(void)
{
    PrevCcAccMode   = 0u;
    PrevOffSwitch   = 0u;
    LastMsw.valid   = 0u;
    LastHnd.valid   = 0u;
    LastMswMs       = 0u;
    LastHndMs       = 0u;
    Stats           = (BodyRouting_StatsType){0};
    return E_OK;
}

/* ------------------------------------------------------------------ *
 *  LIN RX indications (called from PduR -> LinIf)
 * ------------------------------------------------------------------ */

void BodyRouting_OnLinMSWToVCU(const LinSig_MSWToVCU *sig)
{
    if (sig == NULL) { return; }
    LastMsw    = *sig;
    LastMsw.valid = 1u;
    LastMswMs  = OsTime_GetMs();
}

void BodyRouting_OnLinHandleToVCU(const LinSig_HandleToVCU *sig)
{
    if (sig == NULL) { return; }
    LastHnd    = *sig;
    LastHnd.valid = 1u;
    LastHndMs  = OsTime_GetMs();
}

/* ------------------------------------------------------------------ *
 *  Mapping helpers
 * ------------------------------------------------------------------ */

LOCAL_INLINE boolean is_stale(uint8 valid, uint32 lastMs, uint32 nowMs)
{
    return (valid == 0u) || ((nowMs - lastMs) > LIN_STALE_MS);
}

static void MapCruise(const LinSig_MSWToVCU *lin, CanSig_CCVS_VCU *out)
{
    /* Default = idle / Not Available */
    out->cruiseControlEnableSwitch     = 0x03u;
    out->cruiseControlAccelerateSwitch = 0u;
    out->cruiseControlCoastSwitch      = 0u;
    out->cruiseControlResumeSwitch     = 0u;

    /* Single-shot pulse on rising edge: the LIN signal stays 0x1
     * while held, but the CAN signal must show Enabled / Disabled
     * for exactly one cycle (空闲 -> 0x01/0x00 -> 空闲).
     * If both edges fire in the same step (impossible from one
     * switch panel; defensive), OFF wins so the safer disable
     * command is honoured. */
    const boolean modeRising = (lin->ccAccModeSwitch == 0x1u) && (PrevCcAccMode != 0x1u);
    const boolean offRising  = (lin->offSwitch       == 0x1u) && (PrevOffSwitch  != 0x1u);

    if (offRising) {
        out->cruiseControlEnableSwitch = 0x00u;     /* Disabled */
        Stats.cruisePulsesDisable++;
    } else if (modeRising) {
        out->cruiseControlEnableSwitch = 0x01u;     /* Enabled  */
        Stats.cruisePulsesEnable++;
    }

    /* While-pressed mappings (long/short timing done by EMS). */
    out->cruiseControlAccelerateSwitch =
        ((lin->scrollUpButtonStatus   == 0x1u) || (lin->scrollUpButtonStatus   == 0x2u)) ? 1u : 0u;
    out->cruiseControlCoastSwitch =
        ((lin->scrollDownButtonStatus == 0x1u) || (lin->scrollDownButtonStatus == 0x2u)) ? 1u : 0u;
    out->cruiseControlResumeSwitch =
        (lin->cruiseControlResumeSwitch == 0x1u) ? 1u : 0u;

    PrevCcAccMode = lin->ccAccModeSwitch;
    PrevOffSwitch = lin->offSwitch;
}

static void MapRetarder(const LinSig_HandleToVCU *lin, CanSig_TSC1_VDR *out)
{
    uint8 gear = lin->auxiliaryBrakeGear;
    if (gear > 5u) { gear = 0u; }            /* spec range guard */
    const RetarderEntry *e = &RetMap[gear];

    out->overrideControlMode         = e->overrideMode;
    out->overrideControlModePriority = e->priority;
    out->requestedTorquePct          = e->reqTorquePct;
    out->checksum                    = 0xFFu;   /* "不做校验, 发 FF" */
}

static void MapAmt(const LinSig_HandleToVCU *lin, CanSig_TC1 *out)
{
    out->transmissionRequestedGear = lin->transmissionRequestedGear;
    out->transmissionMode1         = lin->transmissionMode1;
    out->amModeSwitch              = lin->amModeSwitch;
    out->mPlusSwitch               = lin->mPlusSwitch;
    out->mMinusSwitch              = lin->mMinusSwitch;
}

/* ------------------------------------------------------------------ *
 *  Main 10 ms cycle
 * ------------------------------------------------------------------ */

void BodyRouting_MainFunction(void)
{
    const PartConfig *cfg = PartConfig_Get();
    const uint32      now = OsTime_GetMs();

    /* ---- Cruise (LIN MSWToVCU -> CAN CCVS_VCU) ------------------- */
    if (cfg->cruiseSrc == CRUISE_SRC_LIN) {
        CanSig_CCVS_VCU ccvs = {
            .cruiseControlEnableSwitch     = 0x03u,
            .cruiseControlAccelerateSwitch = 0u,
            .cruiseControlCoastSwitch      = 0u,
            .cruiseControlResumeSwitch     = 0u
        };
        if (is_stale(LastMsw.valid, LastMswMs, now)) {
            Stats.staleLinDrops++;
            /* hold default = Not Available; downstream supervision
             * (timeout / E2E counter on the receiver) declares
             * gateway node loss per Q&A #1. */
        } else {
            MapCruise(&LastMsw, &ccvs);
        }
        Com_Send_CCVS_VCU(&ccvs);
    }

    /* ---- Aux brake (LIN HandleToVCU -> CAN TSC1_VDR or TSC1_DR) -- */
    if (cfg->auxBrake != AUX_BRAKE_NONE) {
        CanSig_TSC1_VDR tsc1 = {
            .overrideControlMode         = 0x0u,    /* disabled by default */
            .overrideControlModePriority = 0x3u,
            .requestedTorquePct          = 0,
            .checksum                    = 0xFFu
        };
        if (!is_stale(LastHnd.valid, LastHndMs, now)) {
            MapRetarder(&LastHnd, &tsc1);
        }

        if (cfg->auxBrake == AUX_BRAKE_RETARDER) {
            Com_Send_TSC1_VDR(&tsc1);            /* SA 0x27 -> RCU */
        } else {                                 /* AUX_BRAKE_ENGINE */
            Com_Send_TSC1_DR(&tsc1);             /* SA 0x00 -> EMS */
        }
        Stats.retarderUpdates++;
    }

    /* ---- AMT shift (LIN HandleToVCU -> CAN TC1) ------------------ */
    if (cfg->txType == TX_TYPE_AMT) {
        CanSig_TC1 tc1 = {0};
        if (!is_stale(LastHnd.valid, LastHndMs, now)) {
            MapAmt(&LastHnd, &tc1);
        }
        Com_Send_TC1(&tc1);
        Stats.amtUpdates++;
    }
    /* TxType == MT: AMT routing disabled per spec (page 4). */
}

const BodyRouting_StatsType *BodyRouting_GetStats(void) { return &Stats; }
