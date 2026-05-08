/* PduR routing dispatcher.
 *
 * Safety classification: ASIL-B(D).
 * The dispatch is qualified to ASIL-B(D); the residual ASIL-D risk is
 * covered by end-to-end protection (E2E library) which runs in the
 * sender and receiver SW-Cs and is independent of this module.
 *
 * Design constraints enforced here:
 *   - Static, table-driven routing only. No dynamic registration.
 *   - Allow-list semantics: unknown source PDU -> dropped + counted.
 *   - Length is bounded per route; oversized PDUs are rejected before
 *     any further processing (defends RAM safety properties).
 *   - Off-board ingress is gated; non-gated routes never accept PDUs
 *     from off-board sources (enforced by the routing table itself).
 *   - Per-destination failure is independent: a TX failure on one
 *     destination must not prevent fan-out to others.
 *   - No malloc, no recursion, bounded loops (DestCount is uint8).
 */

#include "PduR.h"
#include "PduR_Cfg.h"
#include "Compiler.h"
#include "SafetyMgr.h"
#include "DeadlineMonitor.h"
#include "SecOC_Gate.h"
#include "Dem.h"

/* External lower-layer transmit primitives. Real AUTOSAR uses
 * <Module>_Transmit; declared here to keep the file self-contained. */
extern Std_ReturnType CanIf_Transmit (PduIdType id, const PduInfoType *info);
extern Std_ReturnType LinIf_Transmit (PduIdType id, const PduInfoType *info);
extern Std_ReturnType EthIf_Transmit (PduIdType id, const PduInfoType *info);
extern Std_ReturnType SoAd_IfTransmit(PduIdType id, const PduInfoType *info);
extern Std_ReturnType CanTp_Transmit (PduIdType id, const PduInfoType *info);
extern Std_ReturnType DoIP_TpTransmit(PduIdType id, const PduInfoType *info);

static PduR_StatsType PduR_Stats;
static boolean        PduR_Initialised = FALSE;

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

LOCAL_INLINE const PduR_RouteEntryType *
PduR_FindRoute(PduR_SrcModuleType srcMod, PduIdType srcId)
{
    if (srcId >= PDUR_SRC_INDEX_SIZE) {
        return NULL;
    }
    const uint16 idx = PduR_SrcLookup[srcId];
    if (idx >= PDUR_ROUTE_TABLE_SIZE) {
        return NULL;
    }
    const PduR_RouteEntryType *r = &PduR_RouteTable[idx];
    /* Guard against the zero-initialised "no entry" case: the slot is
     * only valid if both the source module and id match. */
    if ((r->SrcModule != srcMod) || (r->SrcPduId != srcId)) {
        return NULL;
    }
    return r;
}

LOCAL_INLINE Std_ReturnType
PduR_DispatchOne(const PduR_DestPduType *dst, const PduInfoType *info)
{
    switch (dst->DstModule) {
        case PDUR_SRC_CANIF: return CanIf_Transmit (dst->DstPduId, info);
        case PDUR_SRC_LINIF: return LinIf_Transmit (dst->DstPduId, info);
        case PDUR_SRC_ETHIF: return EthIf_Transmit (dst->DstPduId, info);
        case PDUR_SRC_SOAD:  return SoAd_IfTransmit(dst->DstPduId, info);
        case PDUR_SRC_CANTP: return CanTp_Transmit (dst->DstPduId, info);
        case PDUR_SRC_DOIP:  return DoIP_TpTransmit(dst->DstPduId, info);
        default:             return E_NOT_OK;
    }
}

/* ------------------------------------------------------------------ */
/*  Core dispatch                                                     */
/* ------------------------------------------------------------------ */

static void
PduR_RouteCommon(PduR_SrcModuleType srcMod, PduIdType srcId, const PduInfoType *info)
{
    /* Pre-conditions: the module must be initialised and the safety
     * manager must not have ordered Safe State. In Safe State the
     * gateway only forwards UDS/diag, which lives on a separate path
     * and bypasses this dispatcher. */
    if ((PduR_Initialised == FALSE) || (SafetyMgr_IsSafeState() == TRUE)) {
        return;
    }
    if ((info == NULL) || (info->SduDataPtr == NULL)) {
        Dem_ReportErrorStatus(DEM_EVT_PDUR_NULL_PTR, DEM_EVENT_STATUS_FAILED);
        return;
    }

    const PduR_RouteEntryType *route = PduR_FindRoute(srcMod, srcId);
    if (route == NULL) {
        PduR_Stats.droppedNoRoute++;
        Dem_ReportErrorStatus(DEM_EVT_PDUR_NO_ROUTE, DEM_EVENT_STATUS_FAILED);
        return;
    }

    /* Length guard: protect destination RAM and downstream framing. */
    if (info->SduLength > route->MaxPduLength) {
        PduR_Stats.droppedLength++;
        Dem_ReportErrorStatus(DEM_EVT_PDUR_LEN, DEM_EVENT_STATUS_FAILED);
        return;
    }

    /* Ingress gate (SecOC + rate-limit + allow-list for off-board). */
    if (route->IngressGate != PDUR_GATE_NONE) {
        if (SecOC_Gate_Check(route, info) != E_OK) {
            PduR_Stats.droppedGate++;
            /* Dem already reported by the gate with the precise reason. */
            return;
        }
    }

    /* Start-of-route timestamp for deadline monitoring. */
    DeadlineMon_Handle dlm = DEADLINE_HANDLE_INVALID;
    if (route->LatencyBudgetUs != 0u) {
        dlm = DeadlineMon_Start(route->SrcPduId, route->LatencyBudgetUs);
    }

    /* Fan-out. Each destination is independent; one TX failure does
     * not abort the others (FFI between routes). */
    uint8 i;
    boolean anyOk = FALSE;
    for (i = 0u; i < route->DestCount; i++) {
        if (PduR_DispatchOne(&route->DestList[i], info) == E_OK) {
            anyOk = TRUE;
        } else {
            PduR_Stats.droppedTxBusy++;
            Dem_ReportErrorStatus(DEM_EVT_PDUR_TX_BUSY, DEM_EVENT_STATUS_FAILED);
        }
    }

    if (dlm != DEADLINE_HANDLE_INVALID) {
        if (DeadlineMon_Stop(dlm) != E_OK) {
            PduR_Stats.droppedDeadline++;
        }
    }
    if (anyOk == TRUE) {
        PduR_Stats.routedOk++;
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

FUNC(Std_ReturnType, PDUR_CODE) PduR_Init(void)
{
    if (SafetyMgr_VerifyRouteTableCrc(PduR_RouteTable,
                                      sizeof(PduR_RouteTable),
                                      PDUR_ROUTE_TABLE_CRC32) != E_OK) {
        SafetyMgr_EnterSafeState(SAFE_STATE_REASON_ROUTE_TABLE_CRC);
        return E_NOT_OK;
    }
    /* Memset-equivalent without <string.h>. */
    PduR_Stats.routedOk         = 0u;
    PduR_Stats.droppedNoRoute   = 0u;
    PduR_Stats.droppedGate      = 0u;
    PduR_Stats.droppedLength    = 0u;
    PduR_Stats.droppedDeadline  = 0u;
    PduR_Stats.droppedTxBusy    = 0u;
    PduR_Initialised = TRUE;
    return E_OK;
}

FUNC(void, PDUR_CODE) PduR_CanIfRxIndication (PduIdType id, const PduInfoType *i) { PduR_RouteCommon(PDUR_SRC_CANIF, id, i); }
FUNC(void, PDUR_CODE) PduR_LinIfRxIndication (PduIdType id, const PduInfoType *i) { PduR_RouteCommon(PDUR_SRC_LINIF, id, i); }
FUNC(void, PDUR_CODE) PduR_EthIfRxIndication (PduIdType id, const PduInfoType *i) { PduR_RouteCommon(PDUR_SRC_ETHIF, id, i); }
FUNC(void, PDUR_CODE) PduR_SoAdIfRxIndication(PduIdType id, const PduInfoType *i) { PduR_RouteCommon(PDUR_SRC_SOAD,  id, i); }

FUNC(const PduR_StatsType *, PDUR_CODE) PduR_GetStats(void) { return &PduR_Stats; }
