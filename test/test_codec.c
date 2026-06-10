/* Byte-level codec tests for the J107/J169 LIN and CAN frames. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "Std_Types.h"
#include "LinPack.h"
#include "CanPack.h"

/* ------------------------------------------------------------------ *
 *  LIN unpack
 * ------------------------------------------------------------------ */

static void T_lin_msw_unpacks_each_field(void)
{
    /* byte 0:
     *   bits 1..0 = CC/ACC Mode = 0x1
     *   bits 3..2 = Scroll Up   = 0x2
     *   bits 5..4 = Scroll Down = 0x0
     *   bits 7..6 = Resume      = 0x1
     *  -> 0b01_00_10_01 = 0x49
     * byte 1:
     *   bits 1..0 = OFF Switch  = 0x1
     *  -> 0x01
     */
    uint8 frame[8] = { 0x49u, 0x01u, 0,0, 0,0, 0,0 };
    LinSig_MSWToVCU s;
    assert(LinPack_UnpackMSWToVCU(frame, 8u, &s) == E_OK);
    assert(s.ccAccModeSwitch           == 0x1u);
    assert(s.scrollUpButtonStatus      == 0x2u);
    assert(s.scrollDownButtonStatus    == 0x0u);
    assert(s.cruiseControlResumeSwitch == 0x1u);
    assert(s.offSwitch                 == 0x1u);
    assert(s.valid == 1u);
    printf("PASS T_lin_msw_unpacks_each_field\n");
}

static void T_lin_msw_rejects_wrong_length(void)
{
    uint8 frame[4] = {0};
    LinSig_MSWToVCU s;
    assert(LinPack_UnpackMSWToVCU(frame, 4u, &s) == E_NOT_OK);
    printf("PASS T_lin_msw_rejects_wrong_length\n");
}

static void T_lin_handle_unpacks_each_field(void)
{
    /* byte 0 bits 3..0 = aux brake gear = 0x5 -> 0x05
     * byte 1 = req gear = 0x12
     * byte 2 bits 3..0 = mode1 = 0x3, bits 5..4 = A/M = 0x1 -> 0x13
     * byte 3 bits 1..0 = M+ = 0x1, bits 3..2 = M- = 0x2 -> 0x09
     */
    uint8 frame[8] = { 0x05u, 0x12u, 0x13u, 0x09u, 0,0,0,0 };
    LinSig_HandleToVCU s;
    assert(LinPack_UnpackHandleToVCU(frame, 8u, &s) == E_OK);
    assert(s.auxiliaryBrakeGear        == 0x5u);
    assert(s.transmissionRequestedGear == 0x12u);
    assert(s.transmissionMode1         == 0x3u);
    assert(s.amModeSwitch              == 0x1u);
    assert(s.mPlusSwitch               == 0x1u);
    assert(s.mMinusSwitch              == 0x2u);
    assert(s.valid == 1u);
    printf("PASS T_lin_handle_unpacks_each_field\n");
}

static void T_lin_checksum_matches_spec_example(void)
{
    /* LIN 2.2A enhanced checksum example from the spec:
     *   PID = 0x4A, data = {0x55, 0x93, 0xE5}
     *   sum = 0x4A + 0x55 + 0x93 + 0xE5 = 0x21D
     *     carry-fold: 0x1D + 2 = 0x1F (well, 0x21D -> 0x1D+0x02 = 0x1F)
     *   actually 0x4A+0x55=0x9F, +0x93 = 0x132 -> 0x32+0x01 = 0x33;
     *           0x33+0xE5 = 0x118 -> 0x18+0x01 = 0x19; ~0x19 = 0xE6.
     * Verified by hand. */
    uint8 data[3] = { 0x55u, 0x93u, 0xE5u };
    assert(LinPack_Checksum(0x4Au, data, 3u) == 0xE6u);
    printf("PASS T_lin_checksum_matches_spec_example\n");
}

/* ------------------------------------------------------------------ *
 *  CAN pack -- exact byte patterns
 * ------------------------------------------------------------------ */

static void T_ccvs_pack_enabled_pulse(void)
{
    CanSig_CCVS_VCU s = {
        .cruiseControlEnableSwitch     = 0x01u,  /* Enabled         */
        .cruiseControlAccelerateSwitch = 0x01u,
        .cruiseControlCoastSwitch      = 0x00u,
        .cruiseControlResumeSwitch     = 0x00u
    };
    uint8 buf[8];
    assert(CanPack_PackCCVS_VCU(&s, buf, 8u) == E_OK);

    /* byte 3 bits 7..6 = 01 -> 0x40 | lower NA 0x3F -> 0x7F.
     *   our pack writes lower bits as NA (1) so byte3 = 0x40 | 0x3F = 0x7F.
     * Actually our impl: bytes[3] starts at 0xFF (NA), then (bytes[3] & 0x3F)
     * | (01 << 6) = 0x3F | 0x40 = 0x7F. Correct.
     */
    assert(buf[3] == 0x7Fu);
    /* byte 4 = 0xC0 (reserved) | accel 01 (b1..0) | coast 00 (b3..2)
     *                          | resume 00 (b5..4) = 0xC1. */
    assert(buf[4] == 0xC1u);
    /* Untouched bytes stay 0xFF (J1939 not-available). */
    assert(buf[0] == 0xFFu && buf[1] == 0xFFu && buf[2] == 0xFFu);
    assert(buf[5] == 0xFFu && buf[6] == 0xFFu && buf[7] == 0xFFu);
    printf("PASS T_ccvs_pack_enabled_pulse\n");
}

static void T_ccvs_roundtrip(void)
{
    CanSig_CCVS_VCU in = { .cruiseControlEnableSwitch     = 0x02u,
                           .cruiseControlAccelerateSwitch = 0x01u,
                           .cruiseControlCoastSwitch      = 0x02u,
                           .cruiseControlResumeSwitch     = 0x01u };
    CanSig_CCVS_VCU out;
    uint8 buf[8];
    assert(CanPack_PackCCVS_VCU(&in, buf, 8u) == E_OK);
    assert(CanPack_UnpackCCVS_VCU(buf, 8u, &out) == E_OK);
    assert(out.cruiseControlEnableSwitch     == in.cruiseControlEnableSwitch);
    assert(out.cruiseControlAccelerateSwitch == in.cruiseControlAccelerateSwitch);
    assert(out.cruiseControlCoastSwitch      == in.cruiseControlCoastSwitch);
    assert(out.cruiseControlResumeSwitch     == in.cruiseControlResumeSwitch);
    printf("PASS T_ccvs_roundtrip\n");
}

static void T_tsc1_pack_brake_gear_3(void)
{
    /* Retarder gear 3 -> requested torque = -50%, override = torque
     * control (0x2), priority = low (0x3). */
    CanSig_TSC1_VDR s = {
        .overrideControlMode         = 0x2u,
        .overrideControlModePriority = 0x3u,
        .requestedTorquePct          = -50,
        .checksum                    = 0xFFu
    };
    uint8 buf[8];
    assert(CanPack_PackTSC1(&s, buf, 8u) == E_OK);

    /* byte 0 = (0x2) | (0x3 << 2) | (0x3 << 4) | (0x3 << 6)
     *        =  0x2  |   0x0C     |   0x30     |   0xC0     = 0xFE */
    assert(buf[0] == 0xFEu);
    /* bytes 1..2 = NA */
    assert(buf[1] == 0xFFu);
    assert(buf[2] == 0xFFu);
    /* byte 3 = pct + 125 = -50 + 125 = 75 = 0x4B */
    assert(buf[3] == 75u);
    /* checksum slot */
    assert(buf[7] == 0xFFu);
    printf("PASS T_tsc1_pack_brake_gear_3\n");
}

static void T_tsc1_roundtrip_all_gears(void)
{
    static const sint8 t[6] = { 0, -15, -25, -50, -75, -100 };
    for (int i = 0; i < 6; i++) {
        CanSig_TSC1_VDR in = {
            .overrideControlMode         = (i == 0) ? 0x0u : 0x2u,
            .overrideControlModePriority = 0x3u,
            .requestedTorquePct          = t[i],
            .checksum                    = 0xFFu
        };
        uint8 buf[8];
        CanSig_TSC1_VDR out;
        assert(CanPack_PackTSC1(&in, buf, 8u) == E_OK);
        assert(CanPack_UnpackTSC1(buf, 8u, &out) == E_OK);
        assert(out.requestedTorquePct == in.requestedTorquePct);
        assert(out.overrideControlMode == in.overrideControlMode);
        assert(out.overrideControlModePriority == in.overrideControlModePriority);
    }
    printf("PASS T_tsc1_roundtrip_all_gears\n");
}

static void T_tsc1_rejects_out_of_range_torque(void)
{
    CanSig_TSC1_VDR s = { .requestedTorquePct = -126 };  /* J1939 min = -125 */
    uint8 buf[8];
    assert(CanPack_PackTSC1(&s, buf, 8u) == E_NOT_OK);
    printf("PASS T_tsc1_rejects_out_of_range_torque\n");
}

static void T_tc1_roundtrip(void)
{
    CanSig_TC1 in = {
        .transmissionRequestedGear = 0x12u,
        .transmissionMode1         = 0x03u,
        .amModeSwitch              = 0x01u,
        .mPlusSwitch               = 0x01u,
        .mMinusSwitch              = 0x02u
    };
    uint8 buf[8];
    CanSig_TC1 out;
    assert(CanPack_PackTC1(&in, buf, 8u) == E_OK);
    assert(CanPack_UnpackTC1(buf, 8u, &out) == E_OK);
    assert(out.transmissionRequestedGear == in.transmissionRequestedGear);
    assert(out.transmissionMode1         == in.transmissionMode1);
    assert(out.amModeSwitch              == in.amModeSwitch);
    assert(out.mPlusSwitch               == in.mPlusSwitch);
    assert(out.mMinusSwitch              == in.mMinusSwitch);
    printf("PASS T_tc1_roundtrip\n");
}

/* ------------------------------------------------------------------ *
 *  End-to-end byte path: LIN frame -> Com routing -> CAN frame
 * ------------------------------------------------------------------ */

#include "BodyRouting.h"
#include "PartConfig.h"

extern void BodyRoutingCom_LinRxIndication(uint8 linPduId,
                                           const uint8 *bytes, uint16 len);

static uint32 FakeMs;
uint32 OsTime_GetMs(void) { return FakeMs; }
uint32 OsTime_GetUs(void) { return FakeMs * 1000u; }

#include "Dem.h"
Std_ReturnType Dem_ReportErrorStatus(Dem_EventIdType id, Dem_EventStatusType st)
{ (void)id; (void)st; return E_OK; }

static uint32 LastCanId;
static uint8  LastCanBuf[8];
static uint16 LastCanLen;
static uint32 CanTxCount;

Std_ReturnType CanIf_GwTransmit(uint32 id, const uint8 *bytes, uint16 len)
{
    LastCanId = id; LastCanLen = len;
    for (uint16 i = 0u; i < len; i++) { LastCanBuf[i] = bytes[i]; }
    CanTxCount++;
    return E_OK;
}

static void T_end_to_end_retarder_gear_4(void)
{
    BodyRouting_Init();
    PartConfig_Load();
    FakeMs = 0u;
    CanTxCount = 0u;

    /* LIN HandleToVCU frame, gear 4 in low nibble of byte 0. */
    uint8 linFrame[8] = { 0x04u, 0,0,0, 0,0,0,0 };
    BodyRoutingCom_LinRxIndication(LIN_PDU_HANDLE_TO_VCU, linFrame, 8u);

    FakeMs = 10u;
    BodyRouting_MainFunction();

    /* Expect a TSC1_VDR transmit with torque = -75 (raw = 50 = 0x32). */
    /* CanIf saw at least one TX. Find the TSC1_VDR one. */
    assert(CanTxCount > 0u);
    /* The last TX is TC1 (because AMT routing also fires). Drive
     * an MT vehicle so only the TSC1 path is exercised, then re-run. */

    PartConfig *c = (PartConfig *)PartConfig_Get();
    c->txType = TX_TYPE_MT;        /* skip TC1            */
    c->cruiseSrc = CRUISE_SRC_HARDWIRE;  /* skip CCVS          */
    BodyRouting_Init();
    FakeMs = 0u;
    CanTxCount = 0u;
    BodyRoutingCom_LinRxIndication(LIN_PDU_HANDLE_TO_VCU, linFrame, 8u);
    FakeMs = 10u;
    BodyRouting_MainFunction();
    assert(CanTxCount == 1u);
    assert(LastCanId  == CANID_TSC1_VDR);
    /* byte 0 = override(2) | spd-cond(3<<2) | prio(3<<4) | resv(3<<6)
     *        = 0x02 | 0x0C | 0x30 | 0xC0 = 0xFE                       */
    assert(LastCanBuf[0] == 0xFEu);
    /* byte 3 = -75 + 125 = 50 = 0x32 */
    assert(LastCanBuf[3] == 50u);
    /* byte 7 = checksum slot = 0xFF (no checksum per Q&A #6) */
    assert(LastCanBuf[7] == 0xFFu);
    printf("PASS T_end_to_end_retarder_gear_4\n");
}

int main(void)
{
    T_lin_msw_unpacks_each_field();
    T_lin_msw_rejects_wrong_length();
    T_lin_handle_unpacks_each_field();
    T_lin_checksum_matches_spec_example();
    T_ccvs_pack_enabled_pulse();
    T_ccvs_roundtrip();
    T_tsc1_pack_brake_gear_3();
    T_tsc1_roundtrip_all_gears();
    T_tsc1_rejects_out_of_range_torque();
    T_tc1_roundtrip();
    T_end_to_end_retarder_gear_4();
    printf("All codec tests passed.\n");
    return 0;
}
