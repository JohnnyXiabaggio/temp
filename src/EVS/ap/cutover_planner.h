/* Cutover sequencing decision logic — host-testable.
 *
 * The planner consumes shared-handover state and the AP's local readiness
 * signals, and tells the caller which atomic-commit step to perform next.
 * It does not call libdrm; the caller (evs-display) maps decisions to
 * drm_atomic.h calls. See Part 2 §2.6, §2.7 of the EVS reference.
 *
 * The lifecycle the planner drives:
 *   IDLE -> NEGOTIATE_REQ -> SHADOW_ARM -> SHADOW_VERIFIED ->
 *   CUTOVER_COMMIT -> POST_CUTOVER_WAIT -> OWNED
 * with a single failure path -> ABORT_REVERT.
 */
#ifndef EVS_AP_CUTOVER_PLANNER_H
#define EVS_AP_CUTOVER_PLANNER_H

#include <cstdint>

namespace evs::ap {

enum class CutoverPhase : uint32_t {
    Idle,
    NegotiateReq,        /* AP raises EVS_FLAG_AP_REQ_NEGOTIATE */
    ShadowArm,           /* AP commits AP plane off-screen / hidden */
    ShadowVerified,      /* >=2 AP frames rendered, MCU acknowledged SHADOW */
    CutoverCommit,       /* AP issues the single-vsync atomic commit */
    PostCutoverWait,     /* Wait for page flip event + out fence */
    Owned,               /* AP fully owns the EVS plane */
    AbortRevert,         /* Cutover failed; AP backs out, MCU keeps owning */
};

struct PlannerInputs {
    uint32_t mcu_state;            /* enum evs_state from shared block */
    uint64_t mcu_timestamp_ns;
    uint64_t now_ns;
    uint32_t shadow_frames_rendered;     /* AP-rendered frames in SHADOW so far */
    bool     test_only_commit_passed;    /* result of TEST_ONLY for the cutover */
    bool     real_commit_done;
    bool     page_flip_received;
    bool     out_fence_signaled;
    bool     local_fb_ready;             /* AP has a ready V4L2 dma-buf */
};

struct PlannerActions {
    bool set_flag_negotiate;       /* set EVS_FLAG_AP_REQ_NEGOTIATE */
    bool set_flag_shadow;          /* set EVS_FLAG_AP_REQ_SHADOW */
    bool set_flag_cutover;         /* set EVS_FLAG_AP_REQ_CUTOVER */
    bool clear_all_request_flags;  /* clear NEGOTIATE/SHADOW/CUTOVER */
    bool commit_shadow_plane;      /* drm: enable AP plane (still off-screen) */
    bool commit_test_only_cutover; /* drm: TEST_ONLY validation of cutover */
    bool commit_cutover_real;      /* drm: real atomic commit */
    bool wait_for_page_flip;
    bool wait_for_out_fence;
    bool revert_disable_ap_plane;  /* abort: drop the AP plane back to CRTC_ID=0 */
    bool publish_ap_state_owning;
};

/* Number of AP-rendered frames required during SHADOW before cutover. */
constexpr uint32_t kShadowMinFrames = 2;

/* Hard timeout: if the planner spends longer than this in any handover
 * state, abort. Same 500 ms budget the MCU uses for liveness. */
constexpr uint64_t kHandoverTimeoutNs = 500'000'000ULL;

/* Pure step function. */
CutoverPhase plan_step(CutoverPhase cur,
                       const PlannerInputs &in,
                       PlannerActions *out,
                       uint64_t *phase_entered_ns);

} /* namespace evs::ap */

#endif /* EVS_AP_CUTOVER_PLANNER_H */
