#ifndef DEADLINE_MONITOR_H
#define DEADLINE_MONITOR_H

#include "Std_Types.h"

typedef sint16 DeadlineMon_Handle;
#define DEADLINE_HANDLE_INVALID ((DeadlineMon_Handle)-1)

/* Start a deadline supervision window for one in-flight PDU.
 *  - srcPduId  : route key, used for Dem freeze-frame context
 *  - budgetUs  : maximum permissible end-to-end gateway latency
 * Returns an opaque handle to be passed to DeadlineMon_Stop. The
 * implementation is wait-free (no critical section) by using a
 * per-CPU ring of slots; if the ring is exhausted the call returns
 * DEADLINE_HANDLE_INVALID and the caller treats this as a soft
 * failure (PDU still forwarded; the deadline simply not policed). */
extern DeadlineMon_Handle DeadlineMon_Start(uint16 srcPduId, uint32 budgetUs);

/* Stop supervision. Returns E_OK if the deadline was met, E_NOT_OK
 * if the budget was exceeded (Dem already reported by this fn). */
extern Std_ReturnType DeadlineMon_Stop(DeadlineMon_Handle h);

/* Cyclically called from the safety task to scan for slots that were
 * started but never stopped within their budget (e.g. lost in TX). */
extern void DeadlineMon_MainFunction(void);

#endif
