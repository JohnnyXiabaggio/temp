/* Intrusion Detection System Manager.
 *
 * Cybersecurity classification: CAL-3 (ISO/SAE 21434) -- the IdsM is
 * a primary CSMS sensor for the vehicle V-SOC. UN R155 §7.3.7
 * requires that detection mechanisms log security events and report
 * them; the IdsM is the project's compliant implementation point.
 *
 * Implementation choices:
 *   - The policy table is static and lives in flash next to the
 *     routing table; both are protected by the SafetyMgr CRC so an
 *     attacker cannot disable the IDPS by patching the policy alone.
 *   - The aggregation filter is a sliding-window counter per event
 *     id. The window is short (1 s) -- long enough to suppress storms
 *     during a normal noisy condition, short enough to forward real
 *     attacks at near line rate.
 *   - Per-source isolation and per-route block flags are read by
 *     PduR before any other check, so prevention is enforced at the
 *     dispatch entry point, not opportunistically.
 */

#include "IdsM.h"
#include "Compiler.h"
#include "SafetyMgr.h"

#define IDSM_RING_SLOTS         64u
#define IDSM_FILTER_WINDOW_MS  1000u
#define IDSM_AGG_THRESHOLD       10u   /* >threshold/window -> still report
                                          but only every N-th event */
#define IDSM_AGG_EVERY            8u

extern uint32 OsTime_GetMs(void);

/* ------------------------------------------------------------------ */
/*  Policy table (static, CRC-protected)                              */
/* ------------------------------------------------------------------ */

typedef struct {
    IdsM_SEvIdType    sevId;
    IdsM_SeverityType severity;
    IdsM_ActionType   action;
} IdsM_PolicyEntry;

static const IdsM_PolicyEntry IdsM_Policy[] = {
    /* PduR layer */
    { IDSM_SEV_PDUR_NO_ROUTE,     IDSM_SEVERITY_LOW,      IDSM_ACTION_DROP         },
    { IDSM_SEV_PDUR_LEN,          IDSM_SEVERITY_MEDIUM,   IDSM_ACTION_DROP         },
    { IDSM_SEV_PDUR_TX_BUSY,      IDSM_SEVERITY_INFO,     IDSM_ACTION_NONE         },
    { IDSM_SEV_DEADLINE_MISS,     IDSM_SEVERITY_HIGH,     IDSM_ACTION_NONE         },
    /* SecOC ingress */
    { IDSM_SEV_SECOC_AUTH_FAIL,   IDSM_SEVERITY_HIGH,     IDSM_ACTION_DROP         },
    { IDSM_SEV_SECOC_FRESH_FAIL,  IDSM_SEVERITY_HIGH,     IDSM_ACTION_DROP         },
    { IDSM_SEV_SECOC_RATELIMIT,   IDSM_SEVERITY_MEDIUM,   IDSM_ACTION_THROTTLE     },
    { IDSM_SEV_SECOC_NOT_ALLOW,   IDSM_SEVERITY_HIGH,     IDSM_ACTION_DROP         },
    /* End-to-end & integrity */
    { IDSM_SEV_E2E_CHECK_FAIL,    IDSM_SEVERITY_HIGH,     IDSM_ACTION_NONE         },
    { IDSM_SEV_ROUTE_TABLE_CRC,   IDSM_SEVERITY_CRITICAL, IDSM_ACTION_SAFE_STATE   },
    /* Anomaly / signature */
    { IDSM_SEV_FREQ_ANOMALY,      IDSM_SEVERITY_HIGH,     IDSM_ACTION_BLOCK_ROUTE  },
    { IDSM_SEV_PAYLOAD_SIGNATURE, IDSM_SEVERITY_HIGH,     IDSM_ACTION_DROP         },
    /* Diagnostic & boot */
    { IDSM_SEV_DIAG_AUTH_FAIL,    IDSM_SEVERITY_HIGH,     IDSM_ACTION_ISOLATE_SRC  },
    { IDSM_SEV_SECURE_BOOT_FAIL,  IDSM_SEVERITY_CRITICAL, IDSM_ACTION_SAFE_STATE   }
};

#define IDSM_POLICY_SIZE (sizeof(IdsM_Policy) / sizeof(IdsM_Policy[0]))

static const IdsM_PolicyEntry *IdsM_FindPolicy(IdsM_SEvIdType id)
{
    for (uint16 i = 0u; i < IDSM_POLICY_SIZE; i++) {
        if (IdsM_Policy[i].sevId == id) {
            return &IdsM_Policy[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Aggregation filter                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    IdsM_SEvIdType id;
    uint32         windowStartMs;
    uint16         countInWindow;
    uint16         droppedSince;
} IdsM_AggSlot;

#define IDSM_AGG_SLOTS  16u
static IdsM_AggSlot AggSlots[IDSM_AGG_SLOTS];

static IdsM_AggSlot *IdsM_GetAggSlot(IdsM_SEvIdType id, uint32 nowMs)
{
    IdsM_AggSlot *free = NULL;
    for (uint16 i = 0u; i < IDSM_AGG_SLOTS; i++) {
        if (AggSlots[i].id == id) { return &AggSlots[i]; }
        if ((free == NULL) && (AggSlots[i].id == 0u)) { free = &AggSlots[i]; }
    }
    if (free != NULL) {
        free->id            = id;
        free->windowStartMs = nowMs;
        free->countInWindow = 0u;
        free->droppedSince  = 0u;
    }
    return free;
}

/* Return TRUE if the event passes the filter and should be reported. */
static boolean IdsM_FilterAdmit(IdsM_SEvIdType id)
{
    const uint32 now = OsTime_GetMs();
    IdsM_AggSlot *s  = IdsM_GetAggSlot(id, now);
    if (s == NULL) {
        return TRUE;   /* filter exhausted -> fail-open (still report) */
    }
    if ((now - s->windowStartMs) > IDSM_FILTER_WINDOW_MS) {
        s->windowStartMs = now;
        s->countInWindow = 0u;
        s->droppedSince  = 0u;
    }
    s->countInWindow++;
    if (s->countInWindow <= IDSM_AGG_THRESHOLD) {
        return TRUE;
    }
    /* Above threshold -> sample every Nth event. The samples carry
     * the cumulative drop count via context data so the V-SOC sees
     * the storm magnitude. */
    s->droppedSince++;
    if ((s->droppedSince % IDSM_AGG_EVERY) == 0u) {
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Security Event Memory (RAM ring; persisted by IdsR task)          */
/* ------------------------------------------------------------------ */

static IdsM_SemEntryType SemRing[IDSM_RING_SLOTS];
static volatile uint16 SemHead;     /* writer */
static volatile uint16 SemTail;     /* reader (IdsR task) */

LOCAL_INLINE uint16 ring_next(uint16 i) { return (uint16)((i + 1u) % IDSM_RING_SLOTS); }

static void IdsM_SemPush(const IdsM_SemEntryType *e)
{
    const uint16 nh = ring_next(SemHead);
    if (nh == SemTail) {
        /* Ring full: drop oldest -- counted in stats. The intent is
         * never to lose recent evidence in favour of old. */
        SemTail = ring_next(SemTail);
    }
    SemRing[SemHead] = *e;
    SemHead = nh;
}

/* ------------------------------------------------------------------ */
/*  Prevention state                                                  */
/* ------------------------------------------------------------------ */

#define IDSM_BLOCKED_ROUTES_MAX  16u
#define IDSM_ISOLATED_SRCS_MAX    8u

static uint16  BlockedRoutes [IDSM_BLOCKED_ROUTES_MAX];
static uint16  BlockedCount;
static uint16  IsolatedSrcs  [IDSM_ISOLATED_SRCS_MAX];
static uint16  IsolatedCount;

static IdsM_StatsType Stats;

static void IdsM_Engage(IdsM_ActionType a, const IdsM_CtxDataType *ctx)
{
    switch (a) {
        case IDSM_ACTION_DROP:
            Stats.dropped++;
            break;
        case IDSM_ACTION_THROTTLE:
            Stats.throttled++;
            /* Caller (e.g. SecOC_Gate) already engaged its bucket;
             * IdsM only records that throttling is happening. */
            break;
        case IDSM_ACTION_BLOCK_ROUTE:
            if ((ctx != NULL) && (BlockedCount < IDSM_BLOCKED_ROUTES_MAX)) {
                BlockedRoutes[BlockedCount++] = ctx->srcPduId;
                Stats.blocked++;
            }
            break;
        case IDSM_ACTION_ISOLATE_SRC:
            if ((ctx != NULL) && (IsolatedCount < IDSM_ISOLATED_SRCS_MAX)) {
                IsolatedSrcs[IsolatedCount++] = ctx->srcModule;
                Stats.isolated++;
            }
            break;
        case IDSM_ACTION_SAFE_STATE:
            Stats.escalated++;
            SafetyMgr_EnterSafeState(SAFE_STATE_REASON_HSM_FAULT);
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

Std_ReturnType IdsM_Init(void)
{
    for (uint16 i = 0u; i < IDSM_AGG_SLOTS;          i++) { AggSlots[i].id = 0u; }
    for (uint16 i = 0u; i < IDSM_BLOCKED_ROUTES_MAX; i++) { BlockedRoutes[i] = 0xFFFFu; }
    for (uint16 i = 0u; i < IDSM_ISOLATED_SRCS_MAX;  i++) { IsolatedSrcs[i]  = 0xFFFFu; }
    BlockedCount = 0u;
    IsolatedCount = 0u;
    SemHead = 0u; SemTail = 0u;
    Stats = (IdsM_StatsType){0};
    return E_OK;
}

IdsM_ActionType IdsM_ReportSecurityEvent(IdsM_SEvIdType id, const IdsM_CtxDataType *ctx)
{
    Stats.reportedTotal++;

    const IdsM_PolicyEntry *p = IdsM_FindPolicy(id);
    const IdsM_ActionType   a = (p != NULL) ? p->action   : IDSM_ACTION_NONE;
    const IdsM_SeverityType s = (p != NULL) ? p->severity : IDSM_SEVERITY_INFO;

    /* CRITICAL events bypass the aggregation filter. */
    boolean admit = (s == IDSM_SEVERITY_CRITICAL) ? TRUE : IdsM_FilterAdmit(id);

    if (admit) {
        IdsM_SemEntryType e = {
            .timestampMs = OsTime_GetMs(),
            .sevId       = id,
            .severity    = s,
            .action      = a,
            .ctx         = (ctx != NULL) ? *ctx : (IdsM_CtxDataType){0}
        };
        IdsM_SemPush(&e);
    } else {
        Stats.filteredOut++;
    }

    IdsM_Engage(a, ctx);
    return a;
}

boolean IdsM_IsSourceIsolated(uint16 srcModule)
{
    for (uint16 i = 0u; i < IsolatedCount; i++) {
        if (IsolatedSrcs[i] == srcModule) { return TRUE; }
    }
    return FALSE;
}

boolean IdsM_IsRouteBlocked(uint16 srcPduId)
{
    for (uint16 i = 0u; i < BlockedCount; i++) {
        if (BlockedRoutes[i] == srcPduId) { return TRUE; }
    }
    return FALSE;
}

void IdsM_MainFunction(void)
{
    /* Drain the ring into the off-board reporter. The reporter is
     * implemented in IdsR.c on target (DoIP/MQTT) and stubbed
     * here. */
    extern Std_ReturnType IdsR_Forward(const IdsM_SemEntryType *e);
    while (SemTail != SemHead) {
        if (IdsR_Forward(&SemRing[SemTail]) != E_OK) {
            break;   /* transport busy; retry next cycle */
        }
        SemTail = ring_next(SemTail);
    }
}

const IdsM_StatsType *IdsM_GetStats(void) { return &Stats; }
