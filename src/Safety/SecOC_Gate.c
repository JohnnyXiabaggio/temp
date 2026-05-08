/* Off-board ingress gate.
 *
 * The gateway treats any PDU sourced from the WAN (LTE/Telematics) as
 * untrusted. Three filters are applied in series; any failure drops
 * the PDU and is reported to Dem with a distinct event id so that
 * IDS and Dem reports correlate.
 *
 *   1. SecOC verification : AES-128 CMAC truncated to 64 bit (CAN-FD)
 *      or full 128 bit (Ethernet). Freshness is a 32-bit counter
 *      managed by the Key Master; the gateway is a participant.
 *   2. Token-bucket rate limiter, per command class. Defends against
 *      replay storms even when authenticators are technically valid
 *      (e.g. attacker has captured a key on a single cycle).
 *   3. Command allow-list. Telematics may only inject a closed set of
 *      commands (e.g. RemoteDoorUnlock, ClimatePrecondition); chassis
 *      / powertrain commands are categorically rejected at this layer
 *      regardless of authentication.
 */

#include "SecOC_Gate.h"
#include "Dem.h"

/* Provided by the SecOC module proper. */
extern Std_ReturnType SecOC_VerifyAuthenticator(PduIdType id, const PduInfoType *info);

/* ------------------------------------------------------------------ */
/*  Token bucket                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32 capacity;     /* burst size, tokens                            */
    uint32 refillPerMs;  /* sustained rate, tokens / ms                   */
    uint32 tokens;       /* current tokens (fixed-point not needed @1ms)  */
    uint32 lastTickMs;
} TokenBucket;

extern uint32 OsTime_GetMs(void);   /* monotonic tick                     */

static TokenBucket Bucket_Telematics = {
    .capacity     = 5u,
    .refillPerMs  = 1u,   /* 1 token / ms == 1000 cmds / s peak */
    .tokens       = 5u,
    .lastTickMs   = 0u
};

static boolean TokenBucket_Take(TokenBucket *b)
{
    const uint32 now = OsTime_GetMs();
    const uint32 dt  = now - b->lastTickMs;
    if (dt > 0u) {
        const uint32 add = dt * b->refillPerMs;
        b->tokens = (add + b->tokens > b->capacity) ? b->capacity : (b->tokens + add);
        b->lastTickMs = now;
    }
    if (b->tokens == 0u) {
        return FALSE;
    }
    b->tokens--;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/*  Command allow-list                                                */
/* ------------------------------------------------------------------ */

/* Telematics command IDs that may be injected onto the body bus.
 * Anything outside this list is rejected even with a valid SecOC. */
static const uint16 TelematicsAllowList[] = {
    0x0801u,    /* RemoteDoorUnlock     */
    0x0802u,    /* RemoteDoorLock       */
    0x0810u,    /* ClimatePrecondition  */
    0x0820u,    /* HornAndLightLocator  */
};

#define ALLOWLIST_SIZE (sizeof(TelematicsAllowList) / sizeof(TelematicsAllowList[0]))

static boolean AllowList_Contains(uint16 cmdId)
{
    for (uint16 i = 0u; i < ALLOWLIST_SIZE; i++) {
        if (TelematicsAllowList[i] == cmdId) {
            return TRUE;
        }
    }
    return FALSE;
}

/* The first two bytes of the SDU carry the command id. Length already
 * validated by PduR (MaxPduLength) before we are called. */
static uint16 ExtractCmdId(const PduInfoType *info)
{
    return ((uint16)info->SduDataPtr[0] << 8) | (uint16)info->SduDataPtr[1];
}

/* ------------------------------------------------------------------ */
/*  Public                                                            */
/* ------------------------------------------------------------------ */

Std_ReturnType SecOC_Gate_Check(const PduR_RouteEntryType *route,
                                const PduInfoType         *info)
{
    /* SecOC verification is always required on a gated route. */
    if (SecOC_VerifyAuthenticator(route->SrcPduId, info) != E_OK) {
        Dem_ReportErrorStatus(DEM_EVT_SECOC_AUTH_FAIL, DEM_EVENT_STATUS_FAILED);
        return E_NOT_OK;
    }

    if (route->IngressGate == PDUR_GATE_SECOC) {
        return E_OK;
    }

    /* PDUR_GATE_SECOC_RATELIMIT_ALLOWLIST: the harder off-board gate. */
    if (info->SduLength < 2u) {
        Dem_ReportErrorStatus(DEM_EVT_SECOC_NOT_ALLOW, DEM_EVENT_STATUS_FAILED);
        return E_NOT_OK;
    }
    if (TokenBucket_Take(&Bucket_Telematics) == FALSE) {
        Dem_ReportErrorStatus(DEM_EVT_SECOC_RATELIMIT, DEM_EVENT_STATUS_FAILED);
        return E_NOT_OK;
    }
    const uint16 cmd = ExtractCmdId(info);
    if (AllowList_Contains(cmd) == FALSE) {
        Dem_ReportErrorStatus(DEM_EVT_SECOC_NOT_ALLOW, DEM_EVENT_STATUS_FAILED);
        return E_NOT_OK;
    }
    return E_OK;
}
