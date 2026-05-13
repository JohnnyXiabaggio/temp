/* Unit tests for the MCU EVS FSM. The FSM is pure (no globals, no I/O), so
 * we exhaustively walk the §1.6 transition table. Each TEST is independent
 * and free of order coupling.
 */

#include <stdio.h>
#include <string.h>

#include "../../src/EVS/mcu/evs_main.h"

static int n_pass, n_fail;
static const char *cur_test = "";

#define TEST(name) static void name(void)
#define RUN(name)  do { cur_test = #name; int p = n_pass, f = n_fail; \
                        name(); \
                        printf(" [%s] %s\n", \
                               n_fail == f ? "ok  " : "FAIL", #name); \
                        (void)p; } while (0)
#define CHECK(cond) do { \
    if (cond) { n_pass++; } \
    else { fprintf(stderr, "  FAIL [%s] %s:%d: %s\n", \
                   cur_test, __FILE__, __LINE__, #cond); n_fail++; } \
} while (0)

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

/* The FSM step must clear all do_* outputs that don't apply. Otherwise
 * stale outputs from a prior call would leak into the orchestrator. */
TEST(outputs_zeroed_when_irrelevant)
{
    struct evs_outputs o;
    memset(&o, 0xff, sizeof(o));   /* poison */
    struct evs_inputs in = healthy(EVS_STATE_INIT, 0, 1);
    evs_fsm_step(EVS_STATE_INIT, &in, &o);
    CHECK(o.do_disable_plane == 0);
    CHECK(o.do_stop_csi == 0);
    CHECK(o.do_clear_ap_req_cutover == 0);
    CHECK(o.do_set_mcu_fault == 0);
    /* enable_plane is the only true output here. */
    CHECK(o.do_enable_plane == 1);
}

TEST(reset_progresses_to_init)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_RESET, 0, 0);
    evs_fsm_step(EVS_STATE_RESET, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_INIT);
}

TEST(init_progresses_to_streaming_with_enable)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_INIT, 0, 0);
    evs_fsm_step(EVS_STATE_INIT, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_STREAMING);
    CHECK(o.do_enable_plane == 1);
}

TEST(streaming_idle_holds_state)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_INIT, 0, 1);
    evs_fsm_step(EVS_STATE_STREAMING, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_STREAMING);
    CHECK(o.do_enable_plane == 0);   /* already enabled; don't re-fire */
}

TEST(streaming_to_negotiate_via_ap_state)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_NEGOTIATE, 0, 0);
    evs_fsm_step(EVS_STATE_STREAMING, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_NEGOTIATE);
}

TEST(streaming_to_negotiate_via_flag)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_INIT, EVS_FLAG_AP_REQ_NEGOTIATE, 0);
    evs_fsm_step(EVS_STATE_STREAMING, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_NEGOTIATE);
}

TEST(negotiate_to_shadow_via_ap_state)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_SHADOW, 0, 0);
    evs_fsm_step(EVS_STATE_NEGOTIATE, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_SHADOW);
}

TEST(negotiate_to_shadow_via_flag)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_NEGOTIATE, EVS_FLAG_AP_REQ_SHADOW, 0);
    evs_fsm_step(EVS_STATE_NEGOTIATE, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_SHADOW);
}

TEST(negotiate_stale_reverts)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_NEGOTIATE, 0, 0);
    in.ap_stale = 1;
    evs_fsm_step(EVS_STATE_NEGOTIATE, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_STREAMING);
}

TEST(shadow_to_cutover_acks_request_flag)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_CUTOVER, EVS_FLAG_AP_REQ_CUTOVER, 0);
    evs_fsm_step(EVS_STATE_SHADOW, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_CUTOVER);
    CHECK(o.do_clear_ap_req_cutover == 1);
}

TEST(shadow_stale_reverts)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_SHADOW, 0, 0);
    in.ap_stale = 1;
    evs_fsm_step(EVS_STATE_SHADOW, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_STREAMING);
    /* Reverting from SHADOW does not re-disable the plane: it was never
     * disabled (that happens in CUTOVER -> RELEASED). */
    CHECK(o.do_disable_plane == 0);
}

TEST(cutover_releases_with_disable)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_CUTOVER, 0, 0);
    evs_fsm_step(EVS_STATE_CUTOVER, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_RELEASED);
    CHECK(o.do_disable_plane == 1);
}

TEST(cutover_stale_reverts_to_streaming)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_CUTOVER, 0, 0);
    in.ap_stale = 1;
    evs_fsm_step(EVS_STATE_CUTOVER, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_STREAMING);
}

TEST(released_holds_until_ap_dies)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_RELEASED, 0, 0);
    evs_fsm_step(EVS_STATE_RELEASED, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_RELEASED);
    CHECK(o.do_enable_plane == 0);
}

TEST(released_reclaims_on_stale)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_RELEASED, 0, 0);
    in.ap_stale = 1;
    evs_fsm_step(EVS_STATE_RELEASED, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_STREAMING);
    CHECK(o.do_enable_plane == 1);
}

TEST(released_reclaims_on_ap_fault_flag)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_RELEASED, EVS_FLAG_AP_FAULT, 0);
    evs_fsm_step(EVS_STATE_RELEASED, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_STREAMING);
    CHECK(o.do_enable_plane == 1);
}

TEST(csi_fault_from_streaming_stops_csi)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_STREAMING, 0, 0);
    in.csi_ok = 0;
    evs_fsm_step(EVS_STATE_STREAMING, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_FAULT);
    CHECK(o.do_set_mcu_fault == 1);
    CHECK(o.do_stop_csi == 1);
    CHECK(o.do_disable_plane == 0);    /* display is still ok */
}

TEST(display_fault_from_streaming_disables_plane)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_STREAMING, 0, 0);
    in.display_ok = 0;
    evs_fsm_step(EVS_STATE_STREAMING, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_FAULT);
    CHECK(o.do_disable_plane == 1);
    CHECK(o.do_stop_csi == 0);
}

TEST(fault_recovers_to_init_when_drivers_back)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_FAULT, 0, 0);
    evs_fsm_step(EVS_STATE_FAULT, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_INIT);
}

TEST(fault_holds_while_drivers_down)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_FAULT, 0, 0);
    in.csi_ok = 0;
    evs_fsm_step(EVS_STATE_FAULT, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_FAULT);
}

TEST(unknown_state_resets)
{
    struct evs_outputs o;
    struct evs_inputs in = healthy(EVS_STATE_RESET, 0, 0);
    evs_fsm_step(0xDEADBEEF, &in, &o);
    CHECK(o.mcu_state == EVS_STATE_RESET);
}

/* Property-style: from any non-fault state, an unhealthy CSI/display
 * forces FAULT regardless of AP inputs. */
TEST(fault_dominates_all_states)
{
    const uint32_t states[] = {
        EVS_STATE_RESET, EVS_STATE_INIT, EVS_STATE_STREAMING,
        EVS_STATE_NEGOTIATE, EVS_STATE_SHADOW,
        EVS_STATE_CUTOVER, EVS_STATE_RELEASED,
    };
    for (size_t i = 0; i < sizeof(states)/sizeof(states[0]); i++) {
        struct evs_outputs o;
        struct evs_inputs in = healthy(EVS_STATE_NEGOTIATE,
                                       EVS_FLAG_AP_REQ_CUTOVER, 1);
        in.csi_ok = 0;     /* CSI failed */
        evs_fsm_step(states[i], &in, &o);
        CHECK(o.mcu_state == EVS_STATE_FAULT);
        CHECK(o.do_set_mcu_fault == 1);
    }
}

/* End-to-end happy path: one composite test ensuring the full sequence
 * STREAMING -> NEGOTIATE -> SHADOW -> CUTOVER -> RELEASED works when each
 * input is delivered in order. Mirrors the post-handover flow in §1.6. */
TEST(full_handover_sequence)
{
    struct evs_outputs o;
    uint32_t s = EVS_STATE_STREAMING;

    struct evs_inputs in = healthy(EVS_STATE_NEGOTIATE, 0, 1);
    evs_fsm_step(s, &in, &o); s = o.mcu_state;
    CHECK(s == EVS_STATE_NEGOTIATE);

    in = healthy(EVS_STATE_SHADOW, 0, 2);
    evs_fsm_step(s, &in, &o); s = o.mcu_state;
    CHECK(s == EVS_STATE_SHADOW);

    in = healthy(EVS_STATE_CUTOVER, EVS_FLAG_AP_REQ_CUTOVER, 3);
    evs_fsm_step(s, &in, &o); s = o.mcu_state;
    CHECK(s == EVS_STATE_CUTOVER);
    CHECK(o.do_clear_ap_req_cutover == 1);

    in = healthy(EVS_STATE_CUTOVER, 0, 4);
    evs_fsm_step(s, &in, &o); s = o.mcu_state;
    CHECK(s == EVS_STATE_RELEASED);
    CHECK(o.do_disable_plane == 1);

    /* In RELEASED with AP healthy, we hold. */
    in = healthy(EVS_STATE_RESET /*unused*/, 0, 5);
    evs_fsm_step(s, &in, &o); s = o.mcu_state;
    CHECK(s == EVS_STATE_RELEASED);
}

int main(void)
{
    RUN(outputs_zeroed_when_irrelevant);
    RUN(reset_progresses_to_init);
    RUN(init_progresses_to_streaming_with_enable);
    RUN(streaming_idle_holds_state);
    RUN(streaming_to_negotiate_via_ap_state);
    RUN(streaming_to_negotiate_via_flag);
    RUN(negotiate_to_shadow_via_ap_state);
    RUN(negotiate_to_shadow_via_flag);
    RUN(negotiate_stale_reverts);
    RUN(shadow_to_cutover_acks_request_flag);
    RUN(shadow_stale_reverts);
    RUN(cutover_releases_with_disable);
    RUN(cutover_stale_reverts_to_streaming);
    RUN(released_holds_until_ap_dies);
    RUN(released_reclaims_on_stale);
    RUN(released_reclaims_on_ap_fault_flag);
    RUN(csi_fault_from_streaming_stops_csi);
    RUN(display_fault_from_streaming_disables_plane);
    RUN(fault_recovers_to_init_when_drivers_back);
    RUN(fault_holds_while_drivers_down);
    RUN(unknown_state_resets);
    RUN(fault_dominates_all_states);
    RUN(full_handover_sequence);

    printf("evs_fsm: %d pass, %d fail\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
