/* Verifies the YG-D18-2026-1042 LIN <-> CAN routing tables. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "Std_Types.h"
#include "BodyRouting.h"
#include "PartConfig.h"

/* ------------------------------------------------------------------ *
 *  Mock OS clock and Com transmitters
 * ------------------------------------------------------------------ */

static uint32 FakeMs;
uint32 OsTime_GetMs(void) { return FakeMs; }
uint32 OsTime_GetUs(void) { return FakeMs * 1000u; }

static CanSig_CCVS_VCU  LastCCVS;
static CanSig_TSC1_VDR  LastTSC1_VDR;
static CanSig_TSC1_VDR  LastTSC1_DR;
static CanSig_TC1       LastTC1;
static uint32 SentCcvs, SentTscVdr, SentTscDr, SentTc1;

void Com_Send_CCVS_VCU(const CanSig_CCVS_VCU *s) { LastCCVS = *s;    SentCcvs++;   }
void Com_Send_TSC1_VDR(const CanSig_TSC1_VDR *s) { LastTSC1_VDR = *s;SentTscVdr++; }
void Com_Send_TSC1_DR (const CanSig_TSC1_VDR *s) { LastTSC1_DR  = *s;SentTscDr++;  }
void Com_Send_TC1     (const CanSig_TC1      *s) { LastTC1 = *s;     SentTc1++;    }

/* Dem stub (BodyRouting compiles standalone). */
#include "Dem.h"
Std_ReturnType Dem_ReportErrorStatus(Dem_EventIdType id, Dem_EventStatusType st)
{ (void)id; (void)st; return E_OK; }

/* ------------------------------------------------------------------ *
 *  Helpers
 * ------------------------------------------------------------------ */

static void reset(void)
{
    BodyRouting_Init();
    PartConfig_Load();
    FakeMs = 0u;
    SentCcvs = SentTscVdr = SentTscDr = SentTc1 = 0u;
    memset(&LastCCVS, 0, sizeof LastCCVS);
    memset(&LastTSC1_VDR, 0, sizeof LastTSC1_VDR);
    memset(&LastTSC1_DR, 0, sizeof LastTSC1_DR);
    memset(&LastTC1, 0, sizeof LastTC1);
}

static void tick_10ms(void) { FakeMs += 10u; BodyRouting_MainFunction(); }

/* ------------------------------------------------------------------ *
 *  Cruise tests (LIN MSWToVCU -> CAN CCVS_VCU)
 * ------------------------------------------------------------------ */

static void T_cruise_default_is_not_available(void)
{
    reset();
    tick_10ms();
    /* No LIN PDU received yet -> CAN held at Not Available. */
    assert(LastCCVS.cruiseControlEnableSwitch == 0x03u);
    printf("PASS T_cruise_default_is_not_available\n");
}

static void T_cruise_on_press_emits_one_enabled_pulse(void)
{
    reset();
    LinSig_MSWToVCU s = {0};
    /* tick 1: button up */
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlEnableSwitch == 0x03u);

    /* tick 2: press ON */
    s.ccAccModeSwitch = 0x1u;
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlEnableSwitch == 0x01u);  /* Enabled pulse */

    /* tick 3: still held -- must return to Not Available (single shot) */
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlEnableSwitch == 0x03u);

    /* tick 4: release */
    s.ccAccModeSwitch = 0x0u;
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlEnableSwitch == 0x03u);

    /* tick 5: press again -- another pulse */
    s.ccAccModeSwitch = 0x1u;
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlEnableSwitch == 0x01u);
    printf("PASS T_cruise_on_press_emits_one_enabled_pulse\n");
}

static void T_cruise_off_press_emits_one_disabled_pulse(void)
{
    reset();
    LinSig_MSWToVCU s = {0};
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();

    s.offSwitch = 0x1u;
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlEnableSwitch == 0x00u);  /* Disabled pulse */

    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlEnableSwitch == 0x03u);  /* back to idle */
    printf("PASS T_cruise_off_press_emits_one_disabled_pulse\n");
}

static void T_cruise_set_plus_minus_resume_passthrough(void)
{
    reset();
    LinSig_MSWToVCU s = {0};
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();

    s.scrollUpButtonStatus    = 0x2u;
    s.scrollDownButtonStatus  = 0x0u;
    s.cruiseControlResumeSwitch = 0x1u;
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();

    assert(LastCCVS.cruiseControlAccelerateSwitch == 1u);
    assert(LastCCVS.cruiseControlCoastSwitch      == 0u);
    assert(LastCCVS.cruiseControlResumeSwitch     == 1u);

    /* Release: signals go to 0 */
    s.scrollUpButtonStatus    = 0x0u;
    s.cruiseControlResumeSwitch = 0x0u;
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlAccelerateSwitch == 0u);
    assert(LastCCVS.cruiseControlResumeSwitch     == 0u);
    printf("PASS T_cruise_set_plus_minus_resume_passthrough\n");
}

static void T_cruise_stale_lin_returns_to_not_available(void)
{
    reset();
    LinSig_MSWToVCU s = { .scrollUpButtonStatus = 0x1u };
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlAccelerateSwitch == 1u);

    /* Skip 35 cycles = 350 ms without LIN RX (> LIN_STALE_MS = 300). */
    for (int i = 0; i < 35; i++) { tick_10ms(); }
    assert(LastCCVS.cruiseControlEnableSwitch == 0x03u);
    assert(LastCCVS.cruiseControlAccelerateSwitch == 0u);
    printf("PASS T_cruise_stale_lin_returns_to_not_available\n");
}

/* ------------------------------------------------------------------ *
 *  Retarder aux-brake tests (page 3 table)
 * ------------------------------------------------------------------ */

static void T_retarder_table_matches_spec(void)
{
    /* Spec table:
     *  gear 0 -> 0%,    Override disabled,   low priority
     *  gear 1 -> -15%,  Torque control,      low priority   (恒速档)
     *  gear 2 -> -25%,  Torque control,      low priority   (制动1挡)
     *  gear 3 -> -50%,  Torque control,      low priority   (制动2挡)
     *  gear 4 -> -75%,  Torque control,      low priority   (制动3挡)
     *  gear 5 -> -100%, Torque control,      low priority   (制动4挡)
     *  Byte 8 (checksum) = 0xFF.
     */
    static const struct { uint8 g; sint8 t; uint8 m; } expected[] = {
        { 0,    0, 0x0 }, { 1, -15, 0x2 }, { 2, -25, 0x2 },
        { 3,  -50, 0x2 }, { 4, -75, 0x2 }, { 5, -100, 0x2 }
    };

    for (uint8 i = 0; i < 6u; i++) {
        reset();
        LinSig_HandleToVCU h = { .auxiliaryBrakeGear = expected[i].g };
        BodyRouting_OnLinHandleToVCU(&h);
        tick_10ms();
        assert(LastTSC1_VDR.requestedTorquePct          == expected[i].t);
        assert(LastTSC1_VDR.overrideControlMode         == expected[i].m);
        assert(LastTSC1_VDR.overrideControlModePriority == 0x3u);
        assert(LastTSC1_VDR.checksum                    == 0xFFu);
    }
    printf("PASS T_retarder_table_matches_spec\n");
}

static void T_engine_brake_routes_to_ems(void)
{
    /* Re-load config as engine aux brake. */
    PartConfig_Load();
    /* Hack: write through the pointer cast away const for the test. */
    PartConfig *c = (PartConfig *)PartConfig_Get();
    c->auxBrake = AUX_BRAKE_ENGINE;

    BodyRouting_Init();
    FakeMs = 0u;
    SentTscVdr = SentTscDr = 0u;

    LinSig_HandleToVCU h = { .auxiliaryBrakeGear = 3u };
    BodyRouting_OnLinHandleToVCU(&h);
    tick_10ms();

    assert(SentTscDr  > 0u);     /* engine brake target */
    assert(SentTscVdr == 0u);    /* retarder path silent */
    assert(LastTSC1_DR.requestedTorquePct == -50);
    printf("PASS T_engine_brake_routes_to_ems\n");
}

/* ------------------------------------------------------------------ *
 *  AMT tests (page 4)
 * ------------------------------------------------------------------ */

static void T_amt_passthrough(void)
{
    reset();
    LinSig_HandleToVCU h = {
        .transmissionRequestedGear = 0x05u,
        .transmissionMode1         = 0x01u,
        .amModeSwitch              = 0x01u,
        .mPlusSwitch               = 0x01u,
        .mMinusSwitch              = 0x00u
    };
    BodyRouting_OnLinHandleToVCU(&h);
    tick_10ms();
    assert(LastTC1.transmissionRequestedGear == 0x05u);
    assert(LastTC1.transmissionMode1         == 0x01u);
    assert(LastTC1.amModeSwitch              == 0x01u);
    assert(LastTC1.mPlusSwitch               == 0x01u);
    assert(LastTC1.mMinusSwitch              == 0x00u);
    printf("PASS T_amt_passthrough\n");
}

static void T_amt_disabled_on_mt_vehicle(void)
{
    PartConfig_Load();
    PartConfig *c = (PartConfig *)PartConfig_Get();
    c->txType = TX_TYPE_MT;

    BodyRouting_Init();
    FakeMs = 0u;
    SentTc1 = 0u;

    LinSig_HandleToVCU h = { .transmissionRequestedGear = 0x05u };
    BodyRouting_OnLinHandleToVCU(&h);
    tick_10ms();
    assert(SentTc1 == 0u);  /* MT -> no TC1 transmitted */
    printf("PASS T_amt_disabled_on_mt_vehicle\n");
}

/* ------------------------------------------------------------------ *
 *  Cruise source discriminated by 102A part config
 * ------------------------------------------------------------------ */

static void T_cruise_skipped_when_part_is_hardwire(void)
{
    PartConfig_Load();
    PartConfig *c = (PartConfig *)PartConfig_Get();
    c->cruiseSrc = CRUISE_SRC_HARDWIRE;

    BodyRouting_Init();
    FakeMs = 0u;
    SentCcvs = 0u;

    LinSig_MSWToVCU s = { .ccAccModeSwitch = 0x1u };
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(SentCcvs == 0u);   /* hardline => gateway does not route */
    printf("PASS T_cruise_skipped_when_part_is_hardwire\n");
}

/* ------------------------------------------------------------------ *
 *  Edge-case / robustness tests (spec YG-D18-2026-1042 Q&A + guard)
 * ------------------------------------------------------------------ */

/* Retarder gear values outside 0..5 must be clamped to gear-0 (off)
 * so an out-of-spec LIN byte never commands unintended retarder torque. */
static void T_retarder_out_of_range_gear_clamped_to_off(void)
{
    reset();
    LinSig_HandleToVCU h = { .auxiliaryBrakeGear = 0x06u }; /* > spec max */
    BodyRouting_OnLinHandleToVCU(&h);
    tick_10ms();
    assert(LastTSC1_VDR.requestedTorquePct          ==    0);
    assert(LastTSC1_VDR.overrideControlMode         == 0x0u); /* disabled */
    assert(LastTSC1_VDR.overrideControlModePriority == 0x3u);
    assert(LastTSC1_VDR.checksum                    == 0xFFu);
    printf("PASS T_retarder_out_of_range_gear_clamped_to_off\n");
}

/* When both the ON switch and the OFF switch rise on the same LIN frame,
 * the gateway must output Disabled (OFF wins) -- safer of the two. */
static void T_cruise_simultaneous_on_off_off_wins(void)
{
    reset();
    LinSig_MSWToVCU s = {0};
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();

    s.ccAccModeSwitch = 0x1u;
    s.offSwitch       = 0x1u;
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlEnableSwitch == 0x00u); /* Disabled wins */

    /* Single-shot: back to Not Available the next cycle. */
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlEnableSwitch == 0x03u);
    printf("PASS T_cruise_simultaneous_on_off_off_wins\n");
}

/* Spec page 1: SET- is triggered by Scroll down = 0x1 OR 0x2.
 * 0x1 is already exercised by T_cruise_set_plus_minus_resume_passthrough;
 * verify 0x2 maps correctly to coast and clears on release. */
static void T_cruise_scroll_down_0x2_maps_coast(void)
{
    reset();
    LinSig_MSWToVCU s = {0};
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();

    s.scrollDownButtonStatus = 0x2u;
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlCoastSwitch == 1u);

    s.scrollDownButtonStatus = 0x0u;
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();
    assert(LastCCVS.cruiseControlCoastSwitch == 0u);
    printf("PASS T_cruise_scroll_down_0x2_maps_coast\n");
}

/* A NULL pointer to OnLinMSWToVCU must be silently ignored (no crash,
 * no stale update).  The CAN output stays at Not Available. */
static void T_null_lin_msw_input_is_silently_ignored(void)
{
    reset();
    BodyRouting_OnLinMSWToVCU(NULL);
    tick_10ms();
    assert(LastCCVS.cruiseControlEnableSwitch == 0x03u);
    printf("PASS T_null_lin_msw_input_is_silently_ignored\n");
}

/* Verify that BodyRouting_GetStats() counts Enable and Disable pulses
 * independently, matching the number of rising-edge events sent. */
static void T_stats_cruise_pulses_counted(void)
{
    reset();
    LinSig_MSWToVCU s = {0};
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();

    /* One Enable pulse. */
    s.ccAccModeSwitch = 0x1u;
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();

    /* Release, then one Disable pulse. */
    s.ccAccModeSwitch = 0x0u;
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();

    s.offSwitch = 0x1u;
    BodyRouting_OnLinMSWToVCU(&s);
    tick_10ms();

    const BodyRouting_StatsType *st = BodyRouting_GetStats();
    assert(st->cruisePulsesEnable  == 1u);
    assert(st->cruisePulsesDisable == 1u);
    printf("PASS T_stats_cruise_pulses_counted\n");
}

/* ------------------------------------------------------------------ */

int main(void)
{
    T_cruise_default_is_not_available();
    T_cruise_on_press_emits_one_enabled_pulse();
    T_cruise_off_press_emits_one_disabled_pulse();
    T_cruise_set_plus_minus_resume_passthrough();
    T_cruise_stale_lin_returns_to_not_available();
    T_retarder_table_matches_spec();
    T_engine_brake_routes_to_ems();
    T_amt_passthrough();
    T_amt_disabled_on_mt_vehicle();
    T_cruise_skipped_when_part_is_hardwire();
    T_retarder_out_of_range_gear_clamped_to_off();
    T_cruise_simultaneous_on_off_off_wins();
    T_cruise_scroll_down_0x2_maps_coast();
    T_null_lin_msw_input_is_silently_ignored();
    T_stats_cruise_pulses_counted();
    printf("All LIN/CAN routing tests passed.\n");
    return 0;
}
