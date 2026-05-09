#include "cutover_planner.h"
#include "../common/handover_block.h"

#include <cstring>

namespace evs::ap {

static void zero(PlannerActions *a) { std::memset(a, 0, sizeof(*a)); }

static bool timed_out(uint64_t now, uint64_t entered)
{
    if (entered == 0) return false;
    return (now - entered) >= kHandoverTimeoutNs;
}

CutoverPhase plan_step(CutoverPhase cur,
                       const PlannerInputs &in,
                       PlannerActions *out,
                       uint64_t *phase_entered_ns)
{
    zero(out);
    CutoverPhase next = cur;

    auto enter = [&](CutoverPhase p) {
        if (p != cur) {
            *phase_entered_ns = in.now_ns;
        }
        next = p;
    };

    switch (cur) {
    case CutoverPhase::Idle:
        if (in.mcu_state == EVS_STATE_STREAMING) {
            out->set_flag_negotiate = true;
            enter(CutoverPhase::NegotiateReq);
        }
        break;

    case CutoverPhase::NegotiateReq:
        if (timed_out(in.now_ns, *phase_entered_ns)) {
            out->clear_all_request_flags = true;
            enter(CutoverPhase::AbortRevert);
            break;
        }
        if (in.mcu_state == EVS_STATE_NEGOTIATE) {
            out->set_flag_shadow      = true;
            out->commit_shadow_plane  = true;     /* AP plane on, AP-owned */
            enter(CutoverPhase::ShadowArm);
        }
        break;

    case CutoverPhase::ShadowArm:
        if (timed_out(in.now_ns, *phase_entered_ns)) {
            out->clear_all_request_flags = true;
            out->revert_disable_ap_plane = true;
            enter(CutoverPhase::AbortRevert);
            break;
        }
        if (in.mcu_state == EVS_STATE_SHADOW &&
            in.shadow_frames_rendered >= kShadowMinFrames &&
            in.local_fb_ready) {
            enter(CutoverPhase::ShadowVerified);
        }
        break;

    case CutoverPhase::ShadowVerified:
        out->commit_test_only_cutover = true;
        if (in.test_only_commit_passed) {
            out->set_flag_cutover    = true;
            out->commit_cutover_real = true;
            enter(CutoverPhase::CutoverCommit);
        } else {
            /* Hardware refused this plane combination; back out cleanly. */
            out->clear_all_request_flags = true;
            out->revert_disable_ap_plane = true;
            enter(CutoverPhase::AbortRevert);
        }
        break;

    case CutoverPhase::CutoverCommit:
        if (timed_out(in.now_ns, *phase_entered_ns)) {
            out->clear_all_request_flags = true;
            out->revert_disable_ap_plane = true;
            enter(CutoverPhase::AbortRevert);
            break;
        }
        if (in.real_commit_done) {
            out->wait_for_page_flip = true;
            out->wait_for_out_fence = true;
            enter(CutoverPhase::PostCutoverWait);
        }
        break;

    case CutoverPhase::PostCutoverWait:
        if (timed_out(in.now_ns, *phase_entered_ns)) {
            /* No flip event in 500 ms — display state is uncertain. */
            out->revert_disable_ap_plane = true;
            enter(CutoverPhase::AbortRevert);
            break;
        }
        if (in.page_flip_received && in.out_fence_signaled) {
            out->publish_ap_state_owning = true;
            out->clear_all_request_flags = true;
            enter(CutoverPhase::Owned);
        }
        break;

    case CutoverPhase::Owned:
        /* Terminal until display ownership is yielded by external policy. */
        break;

    case CutoverPhase::AbortRevert:
        /* Caller drives recovery; planner waits to be reset to Idle. */
        break;
    }

    return next;
}

} /* namespace evs::ap */
