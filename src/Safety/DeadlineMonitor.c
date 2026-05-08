/* Per-route deadline supervision.
 *
 * Implementation: a small fixed-size ring of slots, each describing
 * one in-flight PDU. Start grabs a slot atomically; Stop releases it.
 * The MainFunction sweeps for slots whose deadline elapsed without a
 * matching Stop and raises a Dem event for each. The ring is sized
 * for the worst-case concurrency derived from the timing analysis
 * (see TGW-TIM-001).
 *
 * Concurrency: Start/Stop are called from RX ISRs and the TX
 * confirmation context. The slot acquisition uses a single atomic
 * test-and-set on the InUse flag; no spin lock is required and the
 * function is wait-free under the bound proven in the analysis.
 */

#include "DeadlineMonitor.h"
#include "Dem.h"
#include <stdatomic.h>

#define DEADLINE_SLOTS  32u

extern uint32 OsTime_GetUs(void);   /* monotonic microsecond tick */

typedef struct {
    atomic_flag inUse;
    uint16      srcPduId;
    uint32      startUs;
    uint32      budgetUs;
} DeadlineSlot;

static DeadlineSlot Slots[DEADLINE_SLOTS];

DeadlineMon_Handle DeadlineMon_Start(uint16 srcPduId, uint32 budgetUs)
{
    for (uint16 i = 0u; i < DEADLINE_SLOTS; i++) {
        if (!atomic_flag_test_and_set(&Slots[i].inUse)) {
            Slots[i].srcPduId  = srcPduId;
            Slots[i].budgetUs  = budgetUs;
            Slots[i].startUs   = OsTime_GetUs();
            return (DeadlineMon_Handle)i;
        }
    }
    /* Exhausted: deadline checking is itself best-effort and must not
     * block the routing path. */
    return DEADLINE_HANDLE_INVALID;
}

Std_ReturnType DeadlineMon_Stop(DeadlineMon_Handle h)
{
    if ((h < 0) || ((uint16)h >= DEADLINE_SLOTS)) {
        return E_NOT_OK;
    }
    DeadlineSlot * const s = &Slots[(uint16)h];
    const uint32 elapsed = OsTime_GetUs() - s->startUs;
    const uint32 budget  = s->budgetUs;
    const uint16 srcId   = s->srcPduId;
    atomic_flag_clear(&s->inUse);

    if (elapsed > budget) {
        Dem_ReportErrorStatus(DEM_EVT_DEADLINE_MISS, DEM_EVENT_STATUS_FAILED);
        (void)srcId;        /* in production: attach as Dem freeze-frame */
        return E_NOT_OK;
    }
    return E_OK;
}

void DeadlineMon_MainFunction(void)
{
    const uint32 now = OsTime_GetUs();
    for (uint16 i = 0u; i < DEADLINE_SLOTS; i++) {
        DeadlineSlot * const s = &Slots[i];
        /* Speculative read; if the slot turns out to be free, skip. */
        if (s->budgetUs == 0u) {
            continue;
        }
        if ((now - s->startUs) > s->budgetUs) {
            /* Try to reclaim. If the owner stops it concurrently we
             * lose the race -- harmless. */
            if (atomic_flag_test_and_set(&s->inUse)) {
                /* was already in use -> we own a stale slot now */
                Dem_ReportErrorStatus(DEM_EVT_DEADLINE_MISS, DEM_EVENT_STATUS_FAILED);
                s->budgetUs = 0u;
                atomic_flag_clear(&s->inUse);
            }
        }
    }
}
