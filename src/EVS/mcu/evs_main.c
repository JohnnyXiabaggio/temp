/* Pure FSM implementation. The actual hardware orchestration loop is the
 * Zephyr application that reads inputs, calls evs_fsm_step, and applies the
 * outputs to disp_drv / csi_driver / handover_agent. Keeping the transition
 * logic pure makes it host-testable and certifiable in isolation.
 *
 * Transition table mirrors Part 1 §1.6 of the EVS reference. Any change to
 * this table needs a matching change to test/evs/test_evs_fsm.c.
 */

#include "evs_main.h"

#include <string.h>

static void out_clear(struct evs_outputs *o)
{
    memset(o, 0, sizeof(*o));
}

void evs_fsm_step(uint32_t cur_state,
                  const struct evs_inputs *in,
                  struct evs_outputs *out)
{
    out_clear(out);
    out->mcu_state = cur_state;

    /* Hard fault paths trump everything else: a CSI/display failure forces
     * FAULT regardless of where we are. The watchdog will eventually reset
     * us, but we publish the fault first so the AP can react. */
    if (!in->csi_ok || !in->display_ok) {
        out->mcu_state       = EVS_STATE_FAULT;
        out->do_set_mcu_fault = 1;
        out->do_disable_plane = !in->display_ok;
        out->do_stop_csi      = !in->csi_ok;
        return;
    }

    switch (cur_state) {
    case EVS_STATE_RESET:
        /* Drivers come up in INIT. */
        out->mcu_state = EVS_STATE_INIT;
        return;

    case EVS_STATE_INIT:
        /* First frame DMA'd is implied by csi_ok && display_ok. */
        out->mcu_state        = EVS_STATE_STREAMING;
        out->do_enable_plane  = 1;
        return;

    case EVS_STATE_STREAMING:
        /* Stay until the AP requests handover. */
        if (in->ap_state == EVS_STATE_NEGOTIATE ||
            (in->ap_flags & EVS_FLAG_AP_REQ_NEGOTIATE)) {
            out->mcu_state = EVS_STATE_NEGOTIATE;
        }
        return;

    case EVS_STATE_NEGOTIATE:
        /* AP timed out negotiating — revert. */
        if (in->ap_stale) {
            out->mcu_state = EVS_STATE_STREAMING;
            return;
        }
        if (in->ap_state == EVS_STATE_SHADOW ||
            (in->ap_flags & EVS_FLAG_AP_REQ_SHADOW)) {
            out->mcu_state = EVS_STATE_SHADOW;
        }
        return;

    case EVS_STATE_SHADOW:
        if (in->ap_stale) {
            /* AP died mid-handover. Reclaim. */
            out->mcu_state = EVS_STATE_STREAMING;
            return;
        }
        if (in->ap_state == EVS_STATE_CUTOVER ||
            (in->ap_flags & EVS_FLAG_AP_REQ_CUTOVER)) {
            out->mcu_state = EVS_STATE_CUTOVER;
            /* AP request consumed. */
            out->do_clear_ap_req_cutover = 1;
        }
        return;

    case EVS_STATE_CUTOVER:
        /* One final frame is rendered, then plane is disabled. */
        if (in->ap_stale) {
            out->mcu_state = EVS_STATE_STREAMING;
            return;
        }
        out->mcu_state        = EVS_STATE_RELEASED;
        out->do_disable_plane = 1;
        return;

    case EVS_STATE_RELEASED:
        /* AP owns the screen. We watch its liveness and reclaim if it
         * dies. CSI may stay running (cheap) or be stopped (lower power);
         * we keep it running so we can re-take ownership in <1 frame. */
        if (in->ap_stale || (in->ap_flags & EVS_FLAG_AP_FAULT)) {
            out->mcu_state       = EVS_STATE_STREAMING;
            out->do_enable_plane = 1;
        }
        return;

    case EVS_STATE_FAULT:
        /* Recovery is owned by the bootloader on reset. The FSM holds the
         * fault state until the watchdog fires; if csi/display recover (very
         * rare in practice — usually fault is terminal until reset) we step
         * back through INIT. */
        if (in->csi_ok && in->display_ok) {
            out->mcu_state = EVS_STATE_INIT;
        }
        return;

    default:
        out->mcu_state = EVS_STATE_RESET;
        return;
    }
}
