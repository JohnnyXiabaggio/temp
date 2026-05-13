/* Unit tests for the AP-side cutover planner. We exercise the happy path
 * end-to-end and every documented failure path (Part 2 §2.10), plus the
 * gating conditions that keep the planner stationary.
 */

#define TESTS_MAIN
#include "tests.hpp"

#include "../../src/EVS/ap/cutover_planner.h"
#include "../../src/EVS/common/handover_block.h"

using namespace evs::ap;

static PlannerInputs in_at(uint32_t mcu_state, uint64_t now)
{
    PlannerInputs in {};
    in.mcu_state              = mcu_state;
    in.mcu_timestamp_ns       = now;
    in.now_ns                 = now;
    in.shadow_frames_rendered = 0;
    in.local_fb_ready         = true;
    return in;
}

/* ------------------------------------------------------------------ */
/* Idle gating                                                         */
/* ------------------------------------------------------------------ */

TEST(idle_holds_when_mcu_not_streaming)
{
    PlannerActions act {}; uint64_t entered = 0;
    auto in  = in_at(EVS_STATE_INIT, 100);
    auto cur = plan_step(CutoverPhase::Idle, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::Idle);
    CHECK(!act.set_flag_negotiate);
}

TEST(idle_advances_when_mcu_streaming)
{
    PlannerActions act {}; uint64_t entered = 0;
    auto cur = plan_step(CutoverPhase::Idle,
                         in_at(EVS_STATE_STREAMING, 100), &act, &entered);
    CHECK_EQ(cur, CutoverPhase::NegotiateReq);
    CHECK(act.set_flag_negotiate);
    CHECK_EQ(entered, 100u);            /* entered timestamp captured */
}

/* ------------------------------------------------------------------ */
/* Happy path step-by-step                                             */
/* ------------------------------------------------------------------ */

TEST(negotiate_progresses_when_mcu_acks)
{
    PlannerActions act {}; uint64_t entered = 100;
    auto cur = plan_step(CutoverPhase::NegotiateReq,
                         in_at(EVS_STATE_NEGOTIATE, 200), &act, &entered);
    CHECK_EQ(cur, CutoverPhase::ShadowArm);
    CHECK(act.set_flag_shadow);
    CHECK(act.commit_shadow_plane);
    CHECK_EQ(entered, 200u);
}

TEST(shadow_arm_waits_for_min_frames)
{
    PlannerActions act {}; uint64_t entered = 200;
    auto in = in_at(EVS_STATE_SHADOW, 220);
    in.shadow_frames_rendered = kShadowMinFrames - 1;
    auto cur = plan_step(CutoverPhase::ShadowArm, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::ShadowArm);
}

TEST(shadow_arm_waits_for_local_fb)
{
    PlannerActions act {}; uint64_t entered = 200;
    auto in = in_at(EVS_STATE_SHADOW, 220);
    in.shadow_frames_rendered = kShadowMinFrames + 2;
    in.local_fb_ready = false;
    auto cur = plan_step(CutoverPhase::ShadowArm, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::ShadowArm);
}

TEST(shadow_verified_runs_test_only_first)
{
    PlannerActions act {}; uint64_t entered = 200;
    auto in = in_at(EVS_STATE_SHADOW, 250);
    in.shadow_frames_rendered = kShadowMinFrames;
    auto cur = plan_step(CutoverPhase::ShadowArm, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::ShadowVerified);

    /* Now in ShadowVerified the planner must demand TEST_ONLY. */
    PlannerActions act2 {};
    in.test_only_commit_passed = false;
    cur = plan_step(cur, in, &act2, &entered);
    CHECK(act2.commit_test_only_cutover);
}

TEST(shadow_verified_progresses_on_test_only_pass)
{
    PlannerActions act {}; uint64_t entered = 200;
    auto in = in_at(EVS_STATE_SHADOW, 300);
    in.test_only_commit_passed = true;
    auto cur = plan_step(CutoverPhase::ShadowVerified, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::CutoverCommit);
    CHECK(act.set_flag_cutover);
    CHECK(act.commit_cutover_real);
}

TEST(cutover_commit_waits_for_real_commit_done)
{
    PlannerActions act {}; uint64_t entered = 300;
    auto in = in_at(EVS_STATE_CUTOVER, 320);
    in.real_commit_done = false;
    auto cur = plan_step(CutoverPhase::CutoverCommit, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::CutoverCommit);
}

TEST(post_cutover_wait_progresses_when_signaled)
{
    PlannerActions act {}; uint64_t entered = 300;
    auto in = in_at(EVS_STATE_CUTOVER, 350);
    in.page_flip_received = true;
    in.out_fence_signaled = true;
    auto cur = plan_step(CutoverPhase::PostCutoverWait, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::Owned);
    CHECK(act.publish_ap_state_owning);
    CHECK(act.clear_all_request_flags);
}

TEST(post_cutover_wait_holds_on_partial_signal)
{
    PlannerActions act {}; uint64_t entered = 300;
    auto in = in_at(EVS_STATE_CUTOVER, 350);
    in.page_flip_received = true;
    in.out_fence_signaled = false;     /* fence not yet signaled */
    auto cur = plan_step(CutoverPhase::PostCutoverWait, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::PostCutoverWait);
}

TEST(owned_is_terminal)
{
    PlannerActions act {}; uint64_t entered = 400;
    /* Even with weird inputs, Owned holds. */
    auto in = in_at(EVS_STATE_STREAMING, 500);
    auto cur = plan_step(CutoverPhase::Owned, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::Owned);
    CHECK(!act.set_flag_negotiate);
}

/* ------------------------------------------------------------------ */
/* Abort paths                                                         */
/* ------------------------------------------------------------------ */

TEST(negotiate_timeout_aborts_and_clears_flags)
{
    PlannerActions act {}; uint64_t entered = 100;
    auto in = in_at(EVS_STATE_STREAMING, 100 + kHandoverTimeoutNs + 1);
    auto cur = plan_step(CutoverPhase::NegotiateReq, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::AbortRevert);
    CHECK(act.clear_all_request_flags);
}

TEST(shadow_arm_timeout_disables_ap_plane)
{
    PlannerActions act {}; uint64_t entered = 100;
    auto in = in_at(EVS_STATE_NEGOTIATE, 100 + kHandoverTimeoutNs + 1);
    auto cur = plan_step(CutoverPhase::ShadowArm, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::AbortRevert);
    CHECK(act.revert_disable_ap_plane);
    CHECK(act.clear_all_request_flags);
}

TEST(test_only_failure_aborts)
{
    PlannerActions act {}; uint64_t entered = 100;
    auto in = in_at(EVS_STATE_SHADOW, 110);
    in.test_only_commit_passed = false;
    auto cur = plan_step(CutoverPhase::ShadowVerified, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::AbortRevert);
    CHECK(act.revert_disable_ap_plane);
    CHECK(act.clear_all_request_flags);
}

TEST(cutover_commit_timeout_aborts)
{
    PlannerActions act {}; uint64_t entered = 100;
    auto in = in_at(EVS_STATE_CUTOVER, 100 + kHandoverTimeoutNs + 1);
    in.real_commit_done = false;
    auto cur = plan_step(CutoverPhase::CutoverCommit, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::AbortRevert);
    CHECK(act.revert_disable_ap_plane);
}

TEST(post_cutover_timeout_aborts)
{
    PlannerActions act {}; uint64_t entered = 100;
    auto in = in_at(EVS_STATE_CUTOVER, 100 + kHandoverTimeoutNs + 1);
    auto cur = plan_step(CutoverPhase::PostCutoverWait, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::AbortRevert);
    CHECK(act.revert_disable_ap_plane);
}

TEST(abort_revert_is_terminal_until_caller_resets)
{
    PlannerActions act {}; uint64_t entered = 100;
    auto in = in_at(EVS_STATE_STREAMING, 200);
    auto cur = plan_step(CutoverPhase::AbortRevert, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::AbortRevert);
    CHECK(!act.set_flag_negotiate);
}

/* ------------------------------------------------------------------ */
/* Phase-entered tracking                                              */
/* ------------------------------------------------------------------ */

TEST(phase_entered_only_updates_on_transition)
{
    PlannerActions act {}; uint64_t entered = 0;

    auto in = in_at(EVS_STATE_STREAMING, 100);
    plan_step(CutoverPhase::Idle, in, &act, &entered);
    CHECK_EQ(entered, 100u);

    /* Stay in NegotiateReq while MCU still STREAMING (hasn't moved to
     * NEGOTIATE yet). entered must NOT advance. */
    in.now_ns = 200;
    plan_step(CutoverPhase::NegotiateReq, in, &act, &entered);
    CHECK_EQ(entered, 100u);
}

/* End-to-end happy path as a single test for regression coverage of the
 * order in which actions fire. */
TEST(end_to_end_handover)
{
    PlannerActions act; uint64_t entered = 0;
    auto cur = CutoverPhase::Idle;

    auto in = in_at(EVS_STATE_STREAMING, 100);
    cur = plan_step(cur, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::NegotiateReq);

    in = in_at(EVS_STATE_NEGOTIATE, 200);
    cur = plan_step(cur, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::ShadowArm);

    in = in_at(EVS_STATE_SHADOW, 300);
    in.shadow_frames_rendered = kShadowMinFrames;
    cur = plan_step(cur, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::ShadowVerified);

    in.now_ns = 320;
    in.test_only_commit_passed = true;
    cur = plan_step(cur, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::CutoverCommit);

    in.now_ns = 340;
    in.real_commit_done = true;
    cur = plan_step(cur, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::PostCutoverWait);

    in.now_ns = 360;
    in.page_flip_received = true;
    in.out_fence_signaled = true;
    cur = plan_step(cur, in, &act, &entered);
    CHECK_EQ(cur, CutoverPhase::Owned);
}

int main() { return tests::run_all(); }
