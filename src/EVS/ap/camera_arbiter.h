/* camera-serviced — per-camera arbiter (Part 3 of EVS reference).
 *
 * The arbiter owns admission, preemption, and V4L2 spec negotiation for one
 * camera. It is intentionally pure C++ with no D-Bus or libv4l2 coupling so
 * it can be host-tested. The service binary wires it to those interfaces.
 */
#ifndef EVS_AP_CAMERA_ARBITER_H
#define EVS_AP_CAMERA_ARBITER_H

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace evs::ap {

enum class Priority : uint32_t {
    RVC          = 0,    /* regulatory */
    SURROUND     = 10,
    TRAILER      = 15,
    DVR_EVENT    = 25,
    DVR_LOOP     = 30,
    PREVIEW      = 40,
    SNAPSHOT     = 50,
};

struct ConsumerInfo {
    std::string client_name;
    int32_t     pid              = 0;
    Priority    priority         = Priority::PREVIEW;
    bool        preemptable      = true;
    bool        requires_exclusive = false;
};

struct StreamSpec {
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t fps    = 0;
    uint32_t pixfmt = 0;
    bool     needs_dmabuf_export = true;
    uint32_t buffer_count = 4;
    /* Set by latency-critical consumers (60 fps surround, mirror replacement,
     * RVC at 60 Hz). Admission becomes exclusive against any non-low-latency
     * consumer to keep the fast path uncontended; multiple low-latency
     * consumers are only permitted when the camera uses SensorVc negotiation
     * so each runs on its own hardware VC. See
     * docs/EVS_LowLatency_Design.md §8. */
    bool     low_latency = false;
};

enum class StreamState { Active, Paused, Preempted, Closing };

struct StreamHandle {
    uint64_t      id           = 0;
    std::string   camera_id;
    StreamSpec    spec {};
    ConsumerInfo  consumer {};
    StreamState   state        = StreamState::Active;
};

enum class Admission { Admitted, Rejected };

struct AdmissionResult {
    Admission             status;
    std::string           reason;
    std::vector<uint64_t> to_preempt;     /* IDs the caller must preempt first */
};

/* Callbacks the arbiter uses to talk to the rest of the service.
 *  - emit_preempted: send D-Bus StreamPreempted to a consumer with a deadline
 *  - reprogram_v4l2: blocking call invoked on a worker thread
 *  - now_ns:         monotonic clock for testability
 */
struct ArbiterDeps {
    std::function<void(const std::string &client_name, uint64_t stream_id,
                       const std::string &reason, uint32_t deadline_ms)> emit_preempted;
    std::function<bool(const StreamSpec &)> reprogram_v4l2;
    std::function<uint64_t()>               now_ns;
};

/* Spec negotiation policy from /etc/camera-service/cameras.yaml. */
enum class SpecNegotiation { SensorVc, CommonHigh, Strict };

struct CameraConfig {
    std::string     camera_id;
    SpecNegotiation negotiation = SpecNegotiation::CommonHigh;
    StreamSpec      fallback_spec {};
};

class CameraArbiter {
public:
    CameraArbiter(CameraConfig cfg, ArbiterDeps deps);

    AdmissionResult admit(const StreamSpec &spec, const ConsumerInfo &consumer);

    /* Activate an admitted stream after caller has preempted everyone in
     * AdmissionResult::to_preempt. Returns the new stream's id. */
    uint64_t activate(const StreamSpec &spec, const ConsumerInfo &consumer);

    /* Initiate cooperative preemption; the deadline is fixed at 200 ms per
     * Part 3 §3.3.2. Caller invokes onConsumerReleased when D-Bus client
     * actually closes the stream, or onPreemptionDeadline if it doesn't. */
    void preempt(uint64_t stream_id, const std::string &reason);
    void onConsumerReleased(uint64_t stream_id);
    void onPreemptionDeadline(uint64_t stream_id);

    /* Snapshot for telemetry / D-Bus QueryArbitration. */
    std::vector<StreamHandle> snapshot() const;

    /* Test hooks. */
    bool hasExclusiveHolder() const;
    StreamSpec activeV4L2Spec() const;

private:
    bool compatibleWithCurrentV4L2(const StreamSpec &s) const;
    bool canReprogramV4L2(const StreamSpec &incoming) const;

    static constexpr uint32_t kPreemptionDeadlineMs = 200;

    mutable std::mutex             mu_;
    CameraConfig                   cfg_;
    ArbiterDeps                    deps_;
    std::map<uint64_t, StreamHandle> streams_;
    std::optional<uint64_t>        exclusive_holder_;
    StreamSpec                     active_v4l2_spec_ {};
    uint64_t                       next_id_ = 1;
};

} /* namespace evs::ap */

#endif /* EVS_AP_CAMERA_ARBITER_H */
