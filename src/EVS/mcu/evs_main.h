/* Top-level MCU EVS application — owns the FSM (Part 1 §1.6).
 *
 * The FSM is exposed as a pure step function that takes the current AP
 * state/flags + a clock + a few inputs and returns the new MCU state. This
 * lets us host-test the entire transition matrix without Zephyr.
 */
#ifndef EVS_MAIN_H
#define EVS_MAIN_H

#include <stdint.h>
#include "../common/handover_block.h"

struct evs_inputs {
    uint32_t ap_state;            /* enum evs_state */
    uint32_t ap_flags;            /* EVS_FLAG_* */
    uint64_t now_ns;
    uint8_t  ap_stale;            /* result of handover_ap_stale */
    uint8_t  csi_ok;              /* sensor + CSI healthy */
    uint8_t  display_ok;          /* DCU + plane healthy */
};

struct evs_outputs {
    uint32_t mcu_state;           /* enum evs_state, next */
    uint8_t  do_enable_plane;
    uint8_t  do_disable_plane;
    uint8_t  do_stop_csi;
    uint8_t  do_clear_ap_req_cutover;  /* ack of AP request */
    uint8_t  do_set_mcu_fault;
};

/* Pure FSM step. No globals, no I/O. */
void evs_fsm_step(uint32_t cur_state,
                  const struct evs_inputs *in,
                  struct evs_outputs *out);

#endif /* EVS_MAIN_H */
