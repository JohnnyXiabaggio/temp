/* Host tests for the AP-side cutover planner. Walks the lifecycle and
 * verifies the abort paths described in Part 2 §2.10.
 */

#include <cstdio>

#include "../../src/EVS/ap/cutover_planner.h"
#include "../../src/EVS/common/handover_block.h"

using namespace evs::ap;

static int n_pass, n_fail;
#define CHECK(cond) do { \
    if (cond) { n_pass++; } \
    else { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); n_fail++; } \
} while (0)

static PlannerInputs make_in(uint32_t mcu_state, uint64_t now)
{
    PlannerInputs in {};
    in.mcu_state              = mcu_state;
    in.mcu_timestamp_ns       = now;
    in.now_ns                 = now;
    in.shadow_frames_rendered = 0;
    in.local_fb_ready         = true;
    return in;
}

static void test_happy_path()
{
    PlannerActions act {};
    uint64_t entered = 0;
    auto cur = CutoverPhase::Idle;

    /* MCU streaming -> request negotiate. */
    auto in = make_in(EVS_STATE_STREAMING, 100);
    cur = plan_step(cur, in, &act, &entered);
    CHECK(cur == CutoverPhase::NegotiateReq);
    CHECK(act.set_flag_negotiate);

    /* MCU acks negotiate -> arm shadow. */
    in = make_in(EVS_STATE_NEGOTIATE, 200);
    cur = plan_step(cur, in, &act, &entered);
    CHECK(cur == CutoverPhase::ShadowArm);
    CHECK(act.commit_shadow_plane);
    CHECK(act.set_flag_shadow);

    /* MCU enters SHADOW but we haven't rendered enough frames yet. */
    in = make_in(EVS_STATE_SHADOW, 300);
    in.shadow_frames_rendered = 1;
    cur = plan_step(cur, in, &act, &entered);
    CHECK(cur == CutoverPhase::ShadowArm);

    /* Two frames rendered -> ShadowVerified. */
    in.shadow_frames_rendered = 2;
    in.now_ns = 320;
    cur = plan_step(cur, in, &act, &entered);
    CHECK(cur == CutoverPhase::ShadowVerified);

    /* TEST_ONLY passes -> CutoverCommit. */
    in.now_ns = 340;
    in.test_only_commit_passed = true;
    cur = plan_step(cur, in, &act, &entered);
    CHECK(cur == CutoverPhase::CutoverCommit);
    CHECK(act.set_flag_cutover);
    CHECK(act.commit_cutover_real);

    /* Real commit reported done -> wait for flip. */
    in.now_ns = 360;
    in.real_commit_done = true;
    cur = plan_step(cur, in, &act, &entered);
    CHECK(cur == CutoverPhase::PostCutoverWait);
    CHECK(act.wait_for_page_flip);
    CHECK(act.wait_for_out_fence);

    /* Page flip + out fence -> Owned. */
    in.now_ns = 380;
    in.page_flip_received = true;
    in.out_fence_signaled = true;
    cur = plan_step(cur, in, &act, &entered);
    CHECK(cur == CutoverPhase::Owned);
    CHECK(act.publish_ap_state_owning);
    CHECK(act.clear_all_request_flags);
}

static void test_test_only_failure_aborts()
{
    PlannerActions act {};
    uint64_t entered = 0;
    auto cur = CutoverPhase::ShadowVerified;
    entered = 100;

    auto in = make_in(EVS_STATE_SHADOW, 110);
    in.test_only_commit_passed = false;
    cur = plan_step(cur, in, &act, &entered);
    CHECK(cur == CutoverPhase::AbortRevert);
    CHECK(act.revert_disable_ap_plane);
    CHECK(act.clear_all_request_flags);
}

static void test_negotiate_timeout_aborts()
{
    PlannerActions act {};
    uint64_t entered = 100;
    auto cur = CutoverPhase::NegotiateReq;

    /* MCU never moves. now_ns - entered > kHandoverTimeoutNs. */
    auto in = make_in(EVS_STATE_STREAMING,
                      100 + kHandoverTimeoutNs + 1);
    cur = plan_step(cur, in, &act, &entered);
    CHECK(cur == CutoverPhase::AbortRevert);
    CHECK(act.clear_all_request_flags);
}

static void test_post_cutover_timeout_aborts()
{
    PlannerActions act {};
    uint64_t entered = 100;
    auto cur = CutoverPhase::PostCutoverWait;

    auto in = make_in(EVS_STATE_CUTOVER, 100 + kHandoverTimeoutNs + 1);
    /* Page flip never came back. */
    cur = plan_step(cur, in, &act, &entered);
    CHECK(cur == CutoverPhase::AbortRevert);
    CHECK(act.revert_disable_ap_plane);
}

int main()
{
    test_happy_path();
    test_test_only_failure_aborts();
    test_negotiate_timeout_aborts();
    test_post_cutover_timeout_aborts();
    std::printf("cutover_planner: %d pass, %d fail\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
