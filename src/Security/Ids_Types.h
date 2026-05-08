#ifndef IDS_TYPES_H
#define IDS_TYPES_H

#include "Std_Types.h"

/* Security Event identifiers (project SEv space).
 * Each id maps 1:1 to a Dem event id where applicable, so a single
 * fault produces one Dem record (FuSa) and one SEv record (CSMS)
 * with the same root cause. */
typedef uint16 IdsM_SEvIdType;

#define IDSM_SEV_PDUR_NO_ROUTE         ((IdsM_SEvIdType)0x9101u)
#define IDSM_SEV_PDUR_LEN              ((IdsM_SEvIdType)0x9102u)
#define IDSM_SEV_PDUR_TX_BUSY          ((IdsM_SEvIdType)0x9103u)
#define IDSM_SEV_DEADLINE_MISS         ((IdsM_SEvIdType)0x9110u)
#define IDSM_SEV_SECOC_AUTH_FAIL       ((IdsM_SEvIdType)0x9120u)
#define IDSM_SEV_SECOC_FRESH_FAIL      ((IdsM_SEvIdType)0x9121u)
#define IDSM_SEV_SECOC_RATELIMIT       ((IdsM_SEvIdType)0x9122u)
#define IDSM_SEV_SECOC_NOT_ALLOW       ((IdsM_SEvIdType)0x9123u)
#define IDSM_SEV_E2E_CHECK_FAIL        ((IdsM_SEvIdType)0x9130u)
#define IDSM_SEV_ROUTE_TABLE_CRC       ((IdsM_SEvIdType)0x9140u)
#define IDSM_SEV_FREQ_ANOMALY          ((IdsM_SEvIdType)0x9150u)
#define IDSM_SEV_PAYLOAD_SIGNATURE     ((IdsM_SEvIdType)0x9151u)
#define IDSM_SEV_DIAG_AUTH_FAIL        ((IdsM_SEvIdType)0x9160u)
#define IDSM_SEV_SECURE_BOOT_FAIL      ((IdsM_SEvIdType)0x9170u)

typedef enum {
    IDSM_SEVERITY_INFO    = 0u,
    IDSM_SEVERITY_LOW     = 1u,
    IDSM_SEVERITY_MEDIUM  = 2u,
    IDSM_SEVERITY_HIGH    = 3u,
    IDSM_SEVERITY_CRITICAL= 4u
} IdsM_SeverityType;

/* Prevention action selected by the IdsM policy table for an event. */
typedef enum {
    IDSM_ACTION_NONE         = 0u,  /* observe + report only            */
    IDSM_ACTION_DROP         = 1u,  /* drop the offending PDU           */
    IDSM_ACTION_THROTTLE     = 2u,  /* engage source-specific throttle  */
    IDSM_ACTION_BLOCK_ROUTE  = 3u,  /* disable the route until reset    */
    IDSM_ACTION_ISOLATE_SRC  = 4u,  /* disable all routes from source   */
    IDSM_ACTION_SAFE_STATE   = 5u   /* escalate to ECU Safe State       */
} IdsM_ActionType;

/* Context data attached to every reported SEv (32 bytes max so it
 * fits in one DoIP message). */
typedef struct {
    uint16 srcModule;
    uint16 srcPduId;
    uint16 length;
    uint16 reserved;
    uint8  payloadHead[8];   /* first 8 bytes of the offending PDU */
} IdsM_CtxDataType;

/* A single Security Event Memory record. Crosses the IdsM <-> IdsR
 * boundary; therefore declared at type-header scope. */
typedef struct {
    uint32             timestampMs;
    IdsM_SEvIdType     sevId;
    IdsM_SeverityType  severity;
    IdsM_ActionType    action;
    IdsM_CtxDataType   ctx;
} IdsM_SemEntryType;

#endif
