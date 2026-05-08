/* Auto-generated style: this would be emitted by the configuration tool
 * from the system description. Hand-written here for clarity. */

#include "PduR_Cfg.h"

static const PduR_DestPduType Dest_EngineTrqCmd[] = {
    { .DstPduId = 0x111u, .DstModule = PDUR_SRC_CANIF }   /* CN1 */
};

static const PduR_DestPduType Dest_BrakeStatus[] = {
    { .DstPduId = 0x210u, .DstModule = PDUR_SRC_ETHIF }   /* ETH0 (SOME/IP) */
};

static const PduR_DestPduType Dest_DoorLockReq[] = {
    { .DstPduId = 0x401u, .DstModule = PDUR_SRC_LINIF }   /* CN3 */
};

static const PduR_DestPduType Dest_OtaChunk[] = {
    { .DstPduId = 0x901u, .DstModule = PDUR_SRC_CANTP }   /* CN0 (UDS) */
};

static const PduR_DestPduType Dest_TelematicsCmd[] = {
    { .DstPduId = 0xA11u, .DstModule = PDUR_SRC_CANIF }   /* CN2 (gated)  */
};

const PduR_RouteEntryType PduR_RouteTable[PDUR_ROUTE_TABLE_SIZE] = {
    { /* 0x0011 EngineTrq_Cmd : CN0 -> CN1 ASIL-D */
        .SrcPduId        = 0x011u,
        .SrcModule       = PDUR_SRC_CANIF,
        .Class           = PDUR_CLASS_IF_TO_IF,
        .Asil            = PDUR_ASIL_D,
        .E2EProfile      = PDUR_E2E_PROFILE_02,
        .IngressGate     = PDUR_GATE_NONE,
        .LatencyBudgetUs = 1000u,
        .MaxPduLength    = 64u,
        .DestList        = Dest_EngineTrqCmd,
        .DestCount       = 1u
    },
    { /* 0x0010 Brake_Status : CN1 -> ETH0 ASIL-D */
        .SrcPduId        = 0x010u,
        .SrcModule       = PDUR_SRC_CANIF,
        .Class           = PDUR_CLASS_IF_TO_IF,
        .Asil            = PDUR_ASIL_D,
        .E2EProfile      = PDUR_E2E_PROFILE_05,
        .IngressGate     = PDUR_GATE_NONE,
        .LatencyBudgetUs = 1000u,
        .MaxPduLength    = 64u,
        .DestList        = Dest_BrakeStatus,
        .DestCount       = 1u
    },
    { /* 0x0040 DoorLock_Req : CN2 -> CN3 ASIL-A */
        .SrcPduId        = 0x040u,
        .SrcModule       = PDUR_SRC_CANIF,
        .Class           = PDUR_CLASS_IF_TO_IF,
        .Asil            = PDUR_ASIL_A,
        .E2EProfile      = PDUR_E2E_PROFILE_01,
        .IngressGate     = PDUR_GATE_NONE,
        .LatencyBudgetUs = 50000u,
        .MaxPduLength    = 8u,
        .DestList        = Dest_DoorLockReq,
        .DestCount       = 1u
    },
    { /* 0x0090 OTA_Chunk : ETH1 -> CanTp(CN0) QM->D gate */
        .SrcPduId        = 0x090u,
        .SrcModule       = PDUR_SRC_ETHIF,
        .Class           = PDUR_CLASS_IF_TO_TP,
        .Asil            = PDUR_ASIL_B,
        .E2EProfile      = PDUR_E2E_NONE,
        .IngressGate     = PDUR_GATE_SECOC,
        .LatencyBudgetUs = 0u,
        .MaxPduLength    = 4095u,
        .DestList        = Dest_OtaChunk,
        .DestCount       = 1u
    },
    { /* 0x00A0 Telematics_Cmd : WAN -> CN2 ASIL-B (gated) */
        .SrcPduId        = 0x0A0u,
        .SrcModule       = PDUR_SRC_SOAD,
        .Class           = PDUR_CLASS_IF_TO_IF,
        .Asil            = PDUR_ASIL_B,
        .E2EProfile      = PDUR_E2E_PROFILE_06,
        .IngressGate     = PDUR_GATE_SECOC_RATELIMIT_ALLOWLIST,
        .LatencyBudgetUs = 100000u,
        .MaxPduLength    = 32u,
        .DestList        = Dest_TelematicsCmd,
        .DestCount       = 1u
    }
};

/* Sparse source-PDU lookup, dense per (SrcModule,SrcPduId) keyspace.
 * In real config it would be a generated perfect hash; here a small
 * direct-mapped array is enough. */
const uint16 PduR_SrcLookup[PDUR_SRC_INDEX_SIZE] = {
    [0x011] = 0u,
    [0x010] = 1u,
    [0x040] = 2u,
    [0x090] = 3u,
    [0x0A0] = 4u,
    /* all other entries are zero-initialised; PduR_FindRoute treats 0
     * as a sentinel only when the SrcPduId/SrcModule do not match. */
};
