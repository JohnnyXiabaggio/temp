#ifndef PDUR_TYPES_H
#define PDUR_TYPES_H

#include "Std_Types.h"

typedef enum {
    PDUR_SRC_CANIF = 0u,
    PDUR_SRC_LINIF,
    PDUR_SRC_ETHIF,
    PDUR_SRC_SOAD,
    PDUR_SRC_CANTP,
    PDUR_SRC_DOIP,
    PDUR_SRC_INVALID = 0xFFu
} PduR_SrcModuleType;

typedef PduR_SrcModuleType PduR_DstModuleType;

typedef enum {
    PDUR_ASIL_QM = 0u,
    PDUR_ASIL_A,
    PDUR_ASIL_B,
    PDUR_ASIL_C,
    PDUR_ASIL_D
} PduR_AsilType;

typedef enum {
    PDUR_CLASS_IF_TO_IF = 0u,   /* gateway 1:1 / 1:N                       */
    PDUR_CLASS_IF_TO_TP,        /* if -> tp (e.g. eth -> can-tp)           */
    PDUR_CLASS_TP_TO_IF,
    PDUR_CLASS_TP_TO_TP
} PduR_RouteClassType;

typedef enum {
    PDUR_GATE_NONE = 0u,
    PDUR_GATE_SECOC,
    PDUR_GATE_SECOC_RATELIMIT_ALLOWLIST   /* used for off-board (WAN) ingress */
} PduR_IngressGateType;

typedef enum {
    PDUR_E2E_NONE = 0u,
    PDUR_E2E_PROFILE_01,
    PDUR_E2E_PROFILE_02,
    PDUR_E2E_PROFILE_05,
    PDUR_E2E_PROFILE_06
} PduR_E2EProfileType;

typedef struct {
    PduIdType            DstPduId;
    PduR_DstModuleType   DstModule;
} PduR_DestPduType;

typedef struct {
    PduIdType                 SrcPduId;          /* lookup key                     */
    PduR_SrcModuleType        SrcModule;
    PduR_RouteClassType       Class;
    PduR_AsilType             Asil;
    PduR_E2EProfileType       E2EProfile;
    PduR_IngressGateType      IngressGate;
    uint32                    LatencyBudgetUs;   /* 0 = no deadline check          */
    PduLengthType             MaxPduLength;      /* hard upper bound (RAM safety)  */
    const PduR_DestPduType   *DestList;          /* fan-out targets                */
    uint8                     DestCount;
} PduR_RouteEntryType;

#endif
