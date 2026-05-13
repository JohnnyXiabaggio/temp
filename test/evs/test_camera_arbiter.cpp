/* Unit tests for camera_arbiter. Cover all admission outcomes, every spec
 * negotiation policy (Part 3 §3.3.5), preemption deadline handling, and
 * the fallback-spec restoration on arbiter empty.
 */

#define TESTS_MAIN
#include "tests.hpp"

#include "../../src/EVS/ap/camera_arbiter.h"

using namespace evs::ap;

static StreamSpec spec_720p30()  { return {1280,  720, 30, 0x3231564eu, true, 4}; }
static StreamSpec spec_1080p30() { return {1920, 1080, 30, 0x3231564eu, true, 4}; }
static StreamSpec spec_720p60()  { return {1280,  720, 60, 0x3231564eu, true, 4}; }

static ConsumerInfo dvr_loop()
    { return {"dvr-managerd", 1001, Priority::DVR_LOOP,  true,  false}; }
static ConsumerInfo dvr_event()
    { return {"dvr-managerd", 1001, Priority::DVR_EVENT, true,  false}; }
static ConsumerInfo preview()
    { return {"hmi",          1002, Priority::PREVIEW,   true,  false}; }
static ConsumerInfo rvc()
    { return {"evs-managerd", 1003, Priority::RVC,       false, true};  }
static ConsumerInfo surround()
    { return {"surround-viewd", 1004, Priority::SURROUND, true, false}; }
static ConsumerInfo non_preempt_safety()
    { return {"safety-tracker", 9, Priority::SURROUND,   false, false}; }

struct Mocks {
    std::vector<std::string> preempts;
    std::vector<StreamSpec>  reprograms;
    uint64_t                 fake_now = 0;
    ArbiterDeps deps()
    {
        return {
            [this](const std::string &n, uint64_t, const std::string &, uint32_t) {
                preempts.push_back(n);
            },
            [this](const StreamSpec &s) { reprograms.push_back(s); return true; },
            [this] { return fake_now; },
        };
    }
};

static CameraConfig rear(SpecNegotiation neg = SpecNegotiation::CommonHigh)
{
    return {"rear", neg, spec_720p30()};
}

/* ------------------------------------------------------------------ */
/* Admission                                                           */
/* ------------------------------------------------------------------ */

TEST(empty_arbiter_admits_anything)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    auto r = a.admit(spec_720p30(), preview());
    CHECK_EQ(r.status, Admission::Admitted);
    CHECK(r.to_preempt.empty());
}

TEST(two_non_exclusive_consumers_coexist)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    a.activate(spec_1080p30(), dvr_loop());
    auto r = a.admit(spec_720p30(), preview());
    CHECK_EQ(r.status, Admission::Admitted);
    CHECK(r.to_preempt.empty());
}

TEST(exclusive_admission_collects_all_lower_priority_streams)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    a.activate(spec_1080p30(), dvr_loop());
    a.activate(spec_720p30(),  preview());
    auto r = a.admit(spec_720p30(), rvc());
    CHECK_EQ(r.status, Admission::Admitted);
    CHECK_EQ(r.to_preempt.size(), 2u);
}

TEST(exclusive_holder_repels_lower_priority_request)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    a.activate(spec_720p30(), rvc());
    auto r = a.admit(spec_720p30(), preview());
    CHECK_EQ(r.status, Admission::Rejected);
}

TEST(equal_priority_against_exclusive_holder_rejected)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    a.activate(spec_720p30(), rvc());
    auto r = a.admit(spec_720p30(), rvc());
    CHECK_EQ(r.status, Admission::Rejected);
}

TEST(non_preemptable_blocks_exclusive_request)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    a.activate(spec_720p30(), non_preempt_safety());
    auto r = a.admit(spec_720p30(), rvc());
    CHECK_EQ(r.status, Admission::Rejected);
}

/* DVR_EVENT (priority 25) outranks DVR_LOOP (30) but neither is exclusive.
 * The arbiter should admit both side-by-side without preempting DVR_LOOP. */
TEST(higher_non_exclusive_does_not_preempt_lower)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    a.activate(spec_1080p30(), dvr_loop());
    auto r = a.admit(spec_1080p30(), dvr_event());
    CHECK_EQ(r.status, Admission::Admitted);
    CHECK(r.to_preempt.empty());
}

/* ------------------------------------------------------------------ */
/* Preemption                                                          */
/* ------------------------------------------------------------------ */

TEST(preempt_emits_signal_and_marks_state)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    auto id = a.activate(spec_720p30(), dvr_loop());
    a.preempt(id, "rvc engaged");
    CHECK_EQ(m.preempts.size(), 1u);
    CHECK_EQ(m.preempts[0], std::string("dvr-managerd"));
}

TEST(consumer_release_clears_stream_and_holder)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    auto id = a.activate(spec_720p30(), rvc());
    CHECK(a.hasExclusiveHolder());
    a.onConsumerReleased(id);
    CHECK(!a.hasExclusiveHolder());
    CHECK(a.snapshot().empty());
}

TEST(deadline_force_close_matches_graceful_release)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    auto id = a.activate(spec_720p30(), dvr_loop());
    a.preempt(id, "rvc engaged");
    a.onPreemptionDeadline(id);    /* consumer didn't yield in time */
    CHECK(a.snapshot().empty());
}

TEST(release_unknown_id_is_a_noop)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    a.onConsumerReleased(0xdead);  /* must not crash */
    CHECK(a.snapshot().empty());
}

/* ------------------------------------------------------------------ */
/* Spec negotiation                                                    */
/* ------------------------------------------------------------------ */

TEST(commonhigh_lifts_active_spec_to_max)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    a.activate(spec_720p30(),  preview());
    a.activate(spec_1080p30(), dvr_loop());
    CHECK_EQ(a.activeV4L2Spec().width,  1920u);
    CHECK_EQ(a.activeV4L2Spec().height, 1080u);
}

TEST(commonhigh_active_spec_falls_back_when_empty)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    auto id = a.activate(spec_1080p30(), dvr_loop());
    a.onConsumerReleased(id);
    /* Empty arbiter must reset to the configured fallback so the next
     * admission isn't biased by the previous high-water mark. */
    CHECK_EQ(a.activeV4L2Spec().width,  1280u);
    CHECK_EQ(a.activeV4L2Spec().height,  720u);
}

TEST(commonhigh_does_not_thrash_on_intermediate_release)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    a.activate(spec_1080p30(), dvr_loop());
    auto preview_id = a.activate(spec_720p30(), preview());
    /* Release preview, dvr still at 1080p. */
    a.onConsumerReleased(preview_id);
    CHECK_EQ(a.activeV4L2Spec().width, 1920u);  /* unchanged */
}

TEST(strict_rejects_mismatched_spec)
{
    Mocks m; CameraArbiter a(rear(SpecNegotiation::Strict), m.deps());
    a.activate(spec_720p30(), preview());
    auto r = a.admit(spec_1080p30(), dvr_loop());
    CHECK_EQ(r.status, Admission::Rejected);
}

TEST(strict_admits_matching_spec)
{
    Mocks m; CameraArbiter a(rear(SpecNegotiation::Strict), m.deps());
    a.activate(spec_720p30(), preview());
    auto r = a.admit(spec_720p30(), dvr_loop());
    CHECK_EQ(r.status, Admission::Admitted);
}

TEST(sensorvc_admits_disjoint_specs)
{
    Mocks m; CameraArbiter a(rear(SpecNegotiation::SensorVc), m.deps());
    a.activate(spec_720p30(),  preview());
    a.activate(spec_1080p30(), dvr_loop());
    auto r = a.admit(spec_720p60(), surround());
    CHECK_EQ(r.status, Admission::Admitted);
}

/* ------------------------------------------------------------------ */
/* Plumbing                                                            */
/* ------------------------------------------------------------------ */

TEST(reprogram_v4l2_invoked_with_active_spec)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    a.activate(spec_1080p30(), dvr_loop());
    CHECK_EQ(m.reprograms.size(), 1u);
    CHECK_EQ(m.reprograms[0].width,  1920u);
    CHECK_EQ(m.reprograms[0].height, 1080u);
}

TEST(snapshot_returns_all_active_streams)
{
    Mocks m; CameraArbiter a(rear(), m.deps());
    a.activate(spec_720p30(),  preview());
    a.activate(spec_1080p30(), dvr_loop());
    auto v = a.snapshot();
    CHECK_EQ(v.size(), 2u);
    /* Each entry has consumer + spec populated. */
    bool saw_dvr = false, saw_preview = false;
    for (const auto &h : v) {
        if (h.consumer.client_name == "dvr-managerd") saw_dvr = true;
        if (h.consumer.client_name == "hmi")          saw_preview = true;
    }
    CHECK(saw_dvr);
    CHECK(saw_preview);
}

/* End-to-end Scenario B from the reference doc, reproduced as a single
 * integration check that exercises admission, preempt, release, and the
 * exclusive transition. */
TEST(scenario_b_rvc_engages_then_releases)
{
    Mocks m; CameraArbiter a(rear(), m.deps());

    a.activate(spec_1080p30(), dvr_loop());
    a.activate(spec_720p30(),  preview());

    auto adm = a.admit(spec_720p30(), rvc());
    CHECK_EQ(adm.status, Admission::Admitted);
    CHECK_EQ(adm.to_preempt.size(), 2u);
    for (auto id : adm.to_preempt) a.preempt(id, "rvc");
    for (auto id : adm.to_preempt) a.onConsumerReleased(id);

    a.activate(spec_720p30(), rvc());
    CHECK(a.hasExclusiveHolder());

    /* Gear back to D — release. */
    auto streams = a.snapshot();
    CHECK_EQ(streams.size(), 1u);
    a.onConsumerReleased(streams[0].id);
    CHECK(!a.hasExclusiveHolder());

    /* Active V4L2 spec is now back to fallback (the camera went idle). */
    CHECK_EQ(a.activeV4L2Spec().width, 1280u);
}

int main() { return tests::run_all(); }
