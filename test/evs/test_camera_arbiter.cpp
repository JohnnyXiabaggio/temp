/* Host tests for the camera arbiter, covering the three worked scenarios
 * from Part 3 §3.3.3:
 *  A. DVR + preview coexist.
 *  B. RVC engaged while DVR recording — preempts DVR and preview.
 *  C. Surround + RVC on the rear camera.
 */

#include <cassert>
#include <cstdio>
#include <vector>

#include "../../src/EVS/ap/camera_arbiter.h"

using namespace evs::ap;

static int n_pass, n_fail;
#define CHECK(cond) do { \
    if (cond) { n_pass++; } \
    else { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); n_fail++; } \
} while (0)

static CameraConfig rear_cfg()
{
    CameraConfig c;
    c.camera_id  = "rear";
    c.negotiation = SpecNegotiation::CommonHigh;
    c.fallback_spec = StreamSpec{1280, 720, 30, /*pixfmt*/0x3231564eu, true, 4};
    return c;
}

static StreamSpec spec_720p30()
{
    return StreamSpec{1280, 720, 30, 0x3231564eu, true, 4};
}

static StreamSpec spec_1080p30()
{
    return StreamSpec{1920, 1080, 30, 0x3231564eu, true, 4};
}

static ConsumerInfo dvr_loop()
{
    return ConsumerInfo{"dvr-managerd", 1001, Priority::DVR_LOOP, true, false};
}
static ConsumerInfo preview()
{
    return ConsumerInfo{"hmi", 1002, Priority::PREVIEW, true, false};
}
static ConsumerInfo rvc()
{
    return ConsumerInfo{"evs-managerd", 1003, Priority::RVC, false, true};
}
static ConsumerInfo surround()
{
    return ConsumerInfo{"surround-viewd", 1004, Priority::SURROUND, true, false};
}

static ArbiterDeps simple_deps(std::vector<std::string> *preempt_log,
                               std::vector<StreamSpec>  *reprog_log)
{
    ArbiterDeps d;
    d.emit_preempted = [preempt_log](const std::string &name, uint64_t,
                                     const std::string &, uint32_t) {
        preempt_log->push_back(name);
    };
    d.reprogram_v4l2 = [reprog_log](const StreamSpec &s) {
        reprog_log->push_back(s); return true;
    };
    d.now_ns = [] { return 0ULL; };
    return d;
}

static void scenario_a_concurrent_dvr_preview()
{
    std::vector<std::string> p; std::vector<StreamSpec> r;
    CameraArbiter arb(rear_cfg(), simple_deps(&p, &r));

    auto a1 = arb.admit(spec_1080p30(), dvr_loop());
    CHECK(a1.status == Admission::Admitted);
    CHECK(a1.to_preempt.empty());
    arb.activate(spec_1080p30(), dvr_loop());

    auto a2 = arb.admit(spec_720p30(), preview());
    CHECK(a2.status == Admission::Admitted);
    CHECK(a2.to_preempt.empty());
    arb.activate(spec_720p30(), preview());

    /* CommonHigh keeps the V4L2 spec at 1080p30. */
    CHECK(arb.activeV4L2Spec().width == 1920);
    CHECK(arb.activeV4L2Spec().height == 1080);
    CHECK(arb.snapshot().size() == 2);
    CHECK(!arb.hasExclusiveHolder());
}

static void scenario_b_rvc_preempts_dvr_and_preview()
{
    std::vector<std::string> p; std::vector<StreamSpec> r;
    CameraArbiter arb(rear_cfg(), simple_deps(&p, &r));

    arb.activate(spec_1080p30(), dvr_loop());
    arb.activate(spec_720p30(),  preview());

    auto a = arb.admit(spec_720p30(), rvc());
    CHECK(a.status == Admission::Admitted);
    CHECK(a.to_preempt.size() == 2);

    /* Caller preempts in turn. */
    for (uint64_t id : a.to_preempt) arb.preempt(id, "rvc engaged");
    CHECK(p.size() == 2);

    /* Consumers release. */
    for (uint64_t id : a.to_preempt) arb.onConsumerReleased(id);
    CHECK(arb.snapshot().empty());

    arb.activate(spec_720p30(), rvc());
    CHECK(arb.hasExclusiveHolder());

    /* Now no second preview can take it. */
    auto a2 = arb.admit(spec_720p30(), preview());
    CHECK(a2.status == Admission::Rejected);
}

static void scenario_c_rvc_after_rvc_releases()
{
    std::vector<std::string> p; std::vector<StreamSpec> r;
    CameraArbiter arb(rear_cfg(), simple_deps(&p, &r));

    arb.activate(spec_720p30(), rvc());
    CHECK(arb.hasExclusiveHolder());

    /* Surround tries to attach to rear cam — rejected (RVC holds exclusive
     * with higher priority). */
    auto a = arb.admit(spec_720p30(), surround());
    CHECK(a.status == Admission::Rejected);

    /* RVC releases. */
    auto streams = arb.snapshot();
    CHECK(streams.size() == 1);
    arb.onConsumerReleased(streams[0].id);
    CHECK(!arb.hasExclusiveHolder());

    /* Now surround can attach. */
    auto a2 = arb.admit(spec_720p30(), surround());
    CHECK(a2.status == Admission::Admitted);
}

static void test_non_preemptable_rejection()
{
    std::vector<std::string> p; std::vector<StreamSpec> r;
    CameraArbiter arb(rear_cfg(), simple_deps(&p, &r));

    /* A non-preemptable, non-exclusive consumer at SURROUND priority. */
    ConsumerInfo stubborn{"safety-tracker", 9, Priority::SURROUND, false, false};
    arb.activate(spec_720p30(), stubborn);

    auto a = arb.admit(spec_720p30(), rvc());
    CHECK(a.status == Admission::Rejected);
}

int main()
{
    scenario_a_concurrent_dvr_preview();
    scenario_b_rvc_preempts_dvr_and_preview();
    scenario_c_rvc_after_rvc_releases();
    test_non_preemptable_rejection();
    std::printf("camera_arbiter: %d pass, %d fail\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
