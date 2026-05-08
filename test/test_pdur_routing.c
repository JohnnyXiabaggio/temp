/* Lightweight host-side unit tests for the PduR dispatcher and the
 * SecOC ingress gate. Compiles with any C11 host compiler; the
 * production tests run under the project framework (Cantata / VTT)
 * with MC/DC instrumentation -- see TGW-VER-001. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "Std_Types.h"
#include "PduR.h"
#include "PduR_Cfg.h"
#include "SafetyMgr.h"
#include "Dem.h"

/* --- mock lower layers --------------------------------------------- */

typedef struct { PduIdType id; PduLengthType len; uint8 data[64]; uint32 calls; } TxLog;
static TxLog Log_Can, Log_Lin, Log_Eth, Log_Soad, Log_CanTp, Log_DoIP;

static Std_ReturnType MockTx(TxLog *l, PduIdType id, const PduInfoType *info)
{
    l->id   = id;
    l->len  = info->SduLength;
    if (info->SduLength <= sizeof(l->data)) {
        memcpy(l->data, info->SduDataPtr, info->SduLength);
    }
    l->calls++;
    return E_OK;
}

Std_ReturnType CanIf_Transmit (PduIdType id, const PduInfoType *i){return MockTx(&Log_Can,  id,i);}
Std_ReturnType LinIf_Transmit (PduIdType id, const PduInfoType *i){return MockTx(&Log_Lin,  id,i);}
Std_ReturnType EthIf_Transmit (PduIdType id, const PduInfoType *i){return MockTx(&Log_Eth,  id,i);}
Std_ReturnType SoAd_IfTransmit(PduIdType id, const PduInfoType *i){return MockTx(&Log_Soad, id,i);}
Std_ReturnType CanTp_Transmit (PduIdType id, const PduInfoType *i){return MockTx(&Log_CanTp,id,i);}
Std_ReturnType DoIP_TpTransmit(PduIdType id, const PduInfoType *i){return MockTx(&Log_DoIP, id,i);}

/* --- mock services -------------------------------------------------- */

static int Dem_Reports;
static Dem_EventIdType Dem_LastEvent;
Std_ReturnType Dem_ReportErrorStatus(Dem_EventIdType id, Dem_EventStatusType st)
{
    if (st == DEM_EVENT_STATUS_FAILED) { Dem_Reports++; Dem_LastEvent = id; }
    return E_OK;
}

static uint32 FakeMs, FakeUs;
uint32 OsTime_GetMs(void) { return FakeMs; }
uint32 OsTime_GetUs(void) { return FakeUs; }

/* The SecOC verifier: in real SecOC the authenticator is appended at
 * the end of the SDU. The mock treats the last byte as a valid flag
 * so tests stay decoupled from the cmd-id bytes. */
Std_ReturnType SecOC_VerifyAuthenticator(PduIdType id, const PduInfoType *info)
{
    (void)id;
    if (info->SduLength == 0u) { return E_NOT_OK; }
    return (info->SduDataPtr[info->SduLength - 1u] == 0xA5u) ? E_OK : E_NOT_OK;
}

/* Override the CRC verifier in tests: the production routing table
 * holds pointers (DestList) whose values are not byte-stable across
 * builds, so the byte-CRC of the in-memory image is not portable.
 * The production build uses a layout-stable image emitted by the
 * config tool with pointers resolved at link time, and the real
 * SafetyMgr_VerifyRouteTableCrc applies. */
Std_ReturnType SafetyMgr_VerifyRouteTableCrc(const PduR_RouteEntryType *t,
                                             uint32 n, uint32 e)
{ (void)t; (void)n; (void)e; return E_OK; }

/* --- helpers -------------------------------------------------------- */

static void reset(void)
{
    memset(&Log_Can,  0, sizeof Log_Can);
    memset(&Log_Lin,  0, sizeof Log_Lin);
    memset(&Log_Eth,  0, sizeof Log_Eth);
    memset(&Log_Soad, 0, sizeof Log_Soad);
    memset(&Log_CanTp,0, sizeof Log_CanTp);
    memset(&Log_DoIP, 0, sizeof Log_DoIP);
    Dem_Reports = 0; Dem_LastEvent = 0;
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

static void T_init_succeeds_with_valid_table(void)
{
    Std_ReturnType r = PduR_Init();
    assert(r == E_OK);
    assert(SafetyMgr_IsSafeState() == FALSE);
    printf("PASS T_init_succeeds_with_valid_table\n");
}

static void T_unknown_source_pdu_is_dropped_and_counted(void)
{
    reset();
    PduInfoType pi = { .SduDataPtr = (uint8[]){0,0,0,0}, .SduLength = 4u };
    PduR_CanIfRxIndication(0x7FFu, &pi);

    const PduR_StatsType *s = PduR_GetStats();
    assert(s->droppedNoRoute >= 1u);
    assert(Log_Can.calls == 0);
    printf("PASS T_unknown_source_pdu_is_dropped_and_counted\n");
}

static void T_oversize_pdu_is_rejected(void)
{
    reset();
    uint8 big[200] = {0};
    PduInfoType pi = { .SduDataPtr = big, .SduLength = 200u };
    /* Route 0x040 (DoorLock_Req) has MaxPduLength = 8. */
    PduR_CanIfRxIndication(0x040u, &pi);

    const PduR_StatsType *s = PduR_GetStats();
    assert(s->droppedLength >= 1u);
    assert(Log_Lin.calls == 0);
    printf("PASS T_oversize_pdu_is_rejected\n");
}

static void T_normal_route_forwards_to_destination(void)
{
    reset();
    uint8 frame[8] = { 0xCA, 0xFE, 0xBA, 0xBE, 0,0,0,0 };
    PduInfoType pi = { .SduDataPtr = frame, .SduLength = 8u };
    /* Route 0x040 (DoorLock_Req) -> LinIf 0x401. */
    PduR_CanIfRxIndication(0x040u, &pi);

    assert(Log_Lin.calls == 1u);
    assert(Log_Lin.id    == 0x401u);
    assert(Log_Lin.len   == 8u);
    assert(Log_Lin.data[0] == 0xCAu);
    printf("PASS T_normal_route_forwards_to_destination\n");
}

static void T_off_board_route_blocks_invalid_secoc(void)
{
    reset();
    /* trailing byte != 0xA5 -> SecOC verify fails */
    uint8 frame[16] = { 0x08, 0x01, 0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0x00 };
    PduInfoType pi = { .SduDataPtr = frame, .SduLength = sizeof frame };
    PduR_SoAdIfRxIndication(0x0A0u, &pi);

    const PduR_StatsType *s = PduR_GetStats();
    assert(s->droppedGate >= 1u);
    assert(Log_Can.calls == 0);
    printf("PASS T_off_board_route_blocks_invalid_secoc\n");
}

static void T_off_board_route_blocks_unlisted_cmd(void)
{
    reset();
    /* cmd 0xDEAD not in allow-list, trailing 0xA5 = valid SecOC */
    uint8 frame[16] = { 0xDE, 0xAD, 0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0xA5 };
    PduInfoType pi = { .SduDataPtr = frame, .SduLength = sizeof frame };
    PduR_SoAdIfRxIndication(0x0A0u, &pi);

    const PduR_StatsType *s = PduR_GetStats();
    assert(s->droppedGate >= 1u);
    assert(Log_Can.calls == 0);
    printf("PASS T_off_board_route_blocks_unlisted_cmd\n");
}

static void T_off_board_route_accepts_listed_cmd(void)
{
    reset();
    /* cmd 0x0801 RemoteDoorUnlock (allow-listed); trailing 0xA5 SecOC OK. */
    uint8 frame[16] = { 0x08, 0x01, 0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0xA5 };
    PduInfoType pi = { .SduDataPtr = frame, .SduLength = sizeof frame };
    PduR_SoAdIfRxIndication(0x0A0u, &pi);

    assert(Log_Can.calls == 1u);
    assert(Log_Can.id    == 0xA11u);
    printf("PASS T_off_board_route_accepts_listed_cmd\n");
}

static void T_rate_limiter_drops_burst(void)
{
    reset();
    uint8 frame[16] = { 0x08, 0x01, 0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0xA5 };
    PduInfoType pi = { .SduDataPtr = frame, .SduLength = sizeof frame };
    /* Bucket capacity 5; drive 20 calls without time advancing. */
    for (int i = 0; i < 20; i++) {
        PduR_SoAdIfRxIndication(0x0A0u, &pi);
    }
    /* Some are accepted (initial tokens), the rest gated. */
    assert(Log_Can.calls < 20u);
    const PduR_StatsType *s = PduR_GetStats();
    assert(s->droppedGate > 0u);
    printf("PASS T_rate_limiter_drops_burst (accepted=%u, gated=%u)\n",
           Log_Can.calls, s->droppedGate);
}

int main(void)
{
    T_init_succeeds_with_valid_table();
    T_unknown_source_pdu_is_dropped_and_counted();
    T_oversize_pdu_is_rejected();
    T_normal_route_forwards_to_destination();
    T_off_board_route_blocks_invalid_secoc();
    T_off_board_route_blocks_unlisted_cmd();
    T_off_board_route_accepts_listed_cmd();
    T_rate_limiter_drops_burst();
    printf("All routing tests passed.\n");
    return 0;
}
