/* Host tests for the MCU EVS FSM.
 *
 * The FSM is pure (no globals, no I/O) so we drive it through every
 * documented transition in Part 1 §1.6 and assert outputs.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/EVS/mcu/evs_main.h"

static struct evs_inputs healthy(uint32_t ap_state, uint32_t flags, uint64_t now)
{
    struct evs_inputs in;
    memset(&in, 0, sizeof(in));
    in.ap_state   = ap_state;
    in.ap_flags   = flags;
    in.now_ns     = now;
    in.csi_ok     = 1;
    in.display_ok = 1;
    return in;
}

static int n_pass, n_fail;
#define CHECK(cond) do { \
    if (cond) { n_pass++; } \
    else { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); n_fail++; } \
} while (0)

static void test_cold_path_to_streaming(void)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_RESET, 0, 1000);

    evs_fsm_step(EVS_STATE_RESET, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_INIT);

    evs_fsm_step(EVS_STATE_INIT, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_STREAMING);
    CHECK(o.do_enable_plane == 1);
}

static void test_negotiate_to_released(void)
{
    struct evs_outputs o;

    /* AP requests negotiate via flag bit. */
    struct evs_inputs in = healthy(EVS_STATE_INIT, EVS_FLAG_AP_REQ_NEGOTIATE, 1);
    evs_fsm_step(EVS_STATE_STREAMING, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_NEGOTIATE);

    /* AP advances to SHADOW. */
    in = healthy(EVS_STATE_SHADOW, 0, 2);
    evs_fsm_step(EVS_STATE_NEGOTIATE, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_SHADOW);

    /* AP commits cutover. */
    in = healthy(EVS_STATE_CUTOVER, EVS_FLAG_AP_REQ_CUTOVER, 3);
    evs_fsm_step(EVS_STATE_SHADOW, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_CUTOVER);
    CHECK(o.do_clear_ap_req_cutover == 1);

    /* CUTOVER renders one final frame, then RELEASED. */
    in = healthy(EVS_STATE_CUTOVER, 0, 4);
    evs_fsm_step(EVS_STATE_CUTOVER, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_RELEASED);
    CHECK(o.do_disable_plane == 1);
}

static void test_ap_dies_in_shadow(void)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_SHADOW, 0, 100);
    in.ap_stale = 1;

    evs_fsm_step(EVS_STATE_SHADOW, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_STREAMING);
}

static void test_ap_dies_when_released(void)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_RELEASED, 0, 100);
    in.ap_stale = 1;

    evs_fsm_step(EVS_STATE_RELEASED, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_STREAMING);
    CHECK(o.do_enable_plane == 1);
}

static void test_csi_fault_anywhere(void)
{
    struct evs_outputs o;
    /* From STREAMING. */
    struct evs_inputs in = healthy(EVS_STATE_STREAMING, 0, 10);
    in.csi_ok = 0;
    evs_fsm_step(EVS_STATE_STREAMING, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_FAULT);
    CHECK(o.do_set_mcu_fault == 1);
    CHECK(o.do_stop_csi == 1);

    /* From RELEASED — also faults. */
    in = healthy(EVS_STATE_RELEASED, 0, 11);
    in.display_ok = 0;
    evs_fsm_step(EVS_STATE_RELEASED, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_FAULT);
    CHECK(o.do_disable_plane == 1);
}

static void test_negotiate_timeout_reverts(void)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_NEGOTIATE, 0, 100);
    in.ap_stale = 1;

    evs_fsm_step(EVS_STATE_NEGOTIATE, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_STREAMING);
}

int main(void)
{
    test_cold_path_to_streaming();
    test_negotiate_to_released();
    test_ap_dies_in_shadow();
    test_ap_dies_when_released();
    test_csi_fault_anywhere();
    test_negotiate_timeout_reverts();

    printf("evs_fsm: %d pass, %d fail\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
