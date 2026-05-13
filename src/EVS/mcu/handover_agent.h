/* MCU-side handover agent. See Part 1 §1.10.
 *
 * Publishes MCU state/heartbeat into the shared block at 100 Hz, decodes AP
 * requests via flag bits, and tracks AP liveness. The actual FSM transitions
 * live in evs_main; this module is purely the shared-block I/O surface.
 */
#ifndef EVS_MCU_HANDOVER_AGENT_H
#define EVS_MCU_HANDOVER_AGENT_H

#include <stdint.h>
#include "../common/handover_block.h"

void     handover_init(uintptr_t shm_phys);
void     handover_publish_state(uint32_t state, uint32_t frame_seq, uint64_t now_ns);
uint32_t handover_read_ap_state(void);
uint32_t handover_read_flags(void);
void     handover_clear_flags(uint32_t mask);
void     handover_set_flags(uint32_t mask);

/* Returns 1 if AP timestamp has not advanced for >= EVS_PEER_LIVENESS_TIMEOUT_NS. */
int      handover_ap_stale(uint64_t now_ns);

/* Mailbox IRQ entry — AP rang the doorbell. Just latches; pipeline polls. */
void     handover_on_mailbox_irq(void);

/* Test-only direct accessor. Not for production code paths. */
struct evs_handover_block *handover_block_for_test(void);

#endif /* EVS_MCU_HANDOVER_AGENT_H */
