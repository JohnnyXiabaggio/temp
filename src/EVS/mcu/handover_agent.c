/* MCU-side handover agent — see header. */

#include "handover_agent.h"

#include <stddef.h>

static struct evs_handover_block *g_blk;
static uint64_t g_last_ap_ts_ns;
static uint64_t g_last_ap_progress_ns;
static volatile uint32_t g_mailbox_pending;

void handover_init(uintptr_t shm_phys)
{
    g_blk = (struct evs_handover_block *)shm_phys;

    /* Initialize the block on cold boot. The AP sees magic=0 until we write
     * it, so the AP-side mmap loop knows to wait. */
    g_blk->magic   = EVS_HANDOVER_MAGIC;
    g_blk->version = EVS_HANDOVER_VERSION;

    g_blk->mcu_state         = EVS_STATE_RESET;
    g_blk->ap_state          = EVS_STATE_RESET;
    g_blk->flags             = 0;
    g_blk->camera_active_mask = 0;
    g_blk->frame_seq_mcu     = 0;
    g_blk->mcu_timestamp_ns  = 0;

    g_last_ap_ts_ns        = 0;
    g_last_ap_progress_ns  = 0;
    g_mailbox_pending      = 0;
}

void handover_publish_state(uint32_t state, uint32_t frame_seq, uint64_t now_ns)
{
    if (!g_blk) {
        return;
    }
    g_blk->mcu_state        = state;
    g_blk->frame_seq_mcu    = frame_seq;
    g_blk->mcu_timestamp_ns = now_ns;
}

uint32_t handover_read_ap_state(void)
{
    return g_blk ? g_blk->ap_state : (uint32_t)EVS_STATE_RESET;
}

uint32_t handover_read_flags(void)
{
    return g_blk ? g_blk->flags : 0u;
}

void handover_clear_flags(uint32_t mask)
{
    if (!g_blk) {
        return;
    }
    /* Best-effort clear. Real hardware should use exclusive load/store; on
     * single-writer flag bits this RMW is fine because the AP only writes
     * AP_REQ_* and the MCU only writes MCU_FAULT/AP_FAULT-clear. */
    g_blk->flags &= ~mask;
}

void handover_set_flags(uint32_t mask)
{
    if (!g_blk) {
        return;
    }
    g_blk->flags |= mask;
}

int handover_ap_stale(uint64_t now_ns)
{
    if (!g_blk) {
        return 1;
    }
    uint64_t ap_ts = g_blk->ap_timestamp_ns;
    if (ap_ts != g_last_ap_ts_ns) {
        g_last_ap_ts_ns       = ap_ts;
        g_last_ap_progress_ns = now_ns;
        return 0;
    }
    /* AP has not advanced its timestamp since last check. */
    if (g_last_ap_progress_ns == 0) {
        /* First call after init — give AP one window to come up. */
        g_last_ap_progress_ns = now_ns;
        return 0;
    }
    return (now_ns - g_last_ap_progress_ns) >= EVS_PEER_LIVENESS_TIMEOUT_NS;
}

void handover_on_mailbox_irq(void)
{
    g_mailbox_pending = 1;
}

struct evs_handover_block *handover_block_for_test(void)
{
    return g_blk;
}
