/* Frequency anomaly detector.
 *
 * Specification-based: the nominal inter-arrival time per source PDU
 * is derived from the network design (CAN matrix). Any arrival that
 * falls outside [low%..high%] of the nominal is flagged.
 *
 * Why specification-based and not learning:
 *   The vehicle V-SOC requires that detectors be deterministic and
 *   re-qualifiable. A model that learns at runtime cannot be signed
 *   off against a CAL because an attacker who controls the learning
 *   set can shift the baseline. The detector therefore reads its
 *   table from CRC-protected flash; the table is co-signed with the
 *   routing table.
 */

#include "AnomalyDetector.h"
#include "Compiler.h"

extern uint32 OsTime_GetUs(void);

/* Static baseline. Same indexing convention as the routing table
 * (sparse, by SrcPduId). In production this is generated. */
static AnomalyEntry Baseline[] = {
    /* EngineTrq_Cmd : nominal 10 ms, accept 5..20 ms              */
    { .srcPduId = 0x011u, .nominalUs = 10000u, .lowPct = 50u,  .highPct = 200u },
    /* Brake_Status  : nominal 10 ms, accept 5..20 ms              */
    { .srcPduId = 0x010u, .nominalUs = 10000u, .lowPct = 50u,  .highPct = 200u },
    /* DoorLock_Req  : nominal 100 ms, accept 50..500 ms (event-d) */
    { .srcPduId = 0x040u, .nominalUs = 100000u,.lowPct = 50u,  .highPct = 500u },
    /* Telematics_Cmd: nominal 1 s,    accept 100 ms..10 s         */
    { .srcPduId = 0x0A0u, .nominalUs = 1000000u,.lowPct = 10u, .highPct = 1000u}
};

#define BASELINE_SIZE (sizeof(Baseline) / sizeof(Baseline[0]))

LOCAL_INLINE AnomalyEntry *find(uint16 id)
{
    for (uint16 i = 0u; i < BASELINE_SIZE; i++) {
        if (Baseline[i].srcPduId == id) { return &Baseline[i]; }
    }
    return NULL;
}

Std_ReturnType AnomalyDetector_Init(void)
{
    for (uint16 i = 0u; i < BASELINE_SIZE; i++) {
        Baseline[i].lastRxUs     = 0u;
        Baseline[i].violationCnt = 0u;
    }
    return E_OK;
}

boolean AnomalyDetector_Check(uint16 srcPduId)
{
    AnomalyEntry *e = find(srcPduId);
    if (e == NULL) {
        return FALSE;        /* not monitored -> not anomalous */
    }
    const uint32 now = OsTime_GetUs();
    if (e->lastRxUs == 0u) {
        e->lastRxUs = now;
        return FALSE;        /* first arrival establishes phase */
    }
    const uint32 dt   = now - e->lastRxUs;
    const uint32 lo   = (e->nominalUs / 100u) * e->lowPct;
    const uint32 hi   = (e->nominalUs / 100u) * e->highPct;
    e->lastRxUs       = now;

    if ((dt < lo) || (dt > hi)) {
        e->violationCnt++;
        return TRUE;
    }
    return FALSE;
}
