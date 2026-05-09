#include "camera_arbiter.h"

#include <algorithm>
#include <utility>

namespace evs::ap {

CameraArbiter::CameraArbiter(CameraConfig cfg, ArbiterDeps deps)
    : cfg_(std::move(cfg)), deps_(std::move(deps))
{
    /* Initial V4L2 program is the configured fallback until a stream is
     * actually activated. This matters because the arbiter may be queried
     * before any consumer has connected. */
    active_v4l2_spec_ = cfg_.fallback_spec;
}

bool CameraArbiter::compatibleWithCurrentV4L2(const StreamSpec &s) const
{
    /* For SensorVc, every consumer can have its own VC, so any spec fits.
     * For CommonHigh, the active spec is the highest-demand one and we
     * downscale per consumer in software/HW; admission is fine as long as
     * the active spec is >= the requested. For Strict, specs must match. */
    switch (cfg_.negotiation) {
    case SpecNegotiation::SensorVc:
        return true;
    case SpecNegotiation::CommonHigh:
        return active_v4l2_spec_.width  >= s.width  &&
               active_v4l2_spec_.height >= s.height &&
               active_v4l2_spec_.fps    >= s.fps;
    case SpecNegotiation::Strict:
        return active_v4l2_spec_.width  == s.width  &&
               active_v4l2_spec_.height == s.height &&
               active_v4l2_spec_.fps    == s.fps    &&
               active_v4l2_spec_.pixfmt == s.pixfmt;
    }
    return false;
}

bool CameraArbiter::canReprogramV4L2(const StreamSpec &incoming) const
{
    /* Reprogramming raises the active spec. We must verify all currently
     * admitted streams will still be served by the new (higher) spec. For
     * CommonHigh this is automatic since downscaling always works; for
     * Strict it would force everyone to match incoming, which is unsafe
     * without consent. */
    if (cfg_.negotiation == SpecNegotiation::Strict) {
        return streams_.empty();
    }
    /* SensorVc/CommonHigh: take the max of current and incoming. */
    (void)incoming;
    return true;
}

AdmissionResult CameraArbiter::admit(const StreamSpec &spec,
                                     const ConsumerInfo &consumer)
{
    std::lock_guard lk(mu_);

    /* Lower numeric value == higher priority (RVC=0 wins over PREVIEW=40). */
    const auto incoming_p = static_cast<uint32_t>(consumer.priority);

    /* 1. Exclusive-holder rule. */
    if (exclusive_holder_) {
        const auto it = streams_.find(*exclusive_holder_);
        if (it != streams_.end()) {
            const auto holder_p = static_cast<uint32_t>(it->second.consumer.priority);
            if (holder_p <= incoming_p) {
                return {Admission::Rejected,
                        "exclusive holder has equal/higher priority", {}};
            }
            /* Holder must be preempted, but only if it's preemptable. */
            if (!it->second.consumer.preemptable) {
                return {Admission::Rejected,
                        "exclusive holder is non-preemptable", {}};
            }
        }
    }

    /* 2. If incoming requires exclusive, find conflicts with lower priority. */
    std::vector<uint64_t> to_preempt;
    if (consumer.requires_exclusive) {
        for (const auto &[id, s] : streams_) {
            const auto p = static_cast<uint32_t>(s.consumer.priority);
            if (p > incoming_p) {
                if (s.consumer.preemptable) {
                    to_preempt.push_back(id);
                } else {
                    return {Admission::Rejected,
                            "non-preemptable conflicting consumer: " +
                                s.consumer.client_name, {}};
                }
            }
        }
    }
    /* Include the previous exclusive holder if any. */
    if (exclusive_holder_) {
        if (std::find(to_preempt.begin(), to_preempt.end(),
                      *exclusive_holder_) == to_preempt.end()) {
            to_preempt.push_back(*exclusive_holder_);
        }
    }

    /* 3. V4L2 spec compatibility. */
    if (!compatibleWithCurrentV4L2(spec)) {
        if (!canReprogramV4L2(spec)) {
            return {Admission::Rejected, "v4l2 spec mismatch", {}};
        }
    }

    return {Admission::Admitted, {}, std::move(to_preempt)};
}

uint64_t CameraArbiter::activate(const StreamSpec &spec,
                                 const ConsumerInfo &consumer)
{
    std::lock_guard lk(mu_);
    StreamHandle h;
    h.id        = next_id_++;
    h.camera_id = cfg_.camera_id;
    h.spec      = spec;
    h.consumer  = consumer;
    h.state     = StreamState::Active;

    /* Lift active spec to the max of current and incoming for CommonHigh. */
    if (cfg_.negotiation == SpecNegotiation::CommonHigh) {
        active_v4l2_spec_.width  = std::max(active_v4l2_spec_.width,  spec.width);
        active_v4l2_spec_.height = std::max(active_v4l2_spec_.height, spec.height);
        active_v4l2_spec_.fps    = std::max(active_v4l2_spec_.fps,    spec.fps);
        if (active_v4l2_spec_.pixfmt == 0) active_v4l2_spec_.pixfmt = spec.pixfmt;
    } else if (cfg_.negotiation == SpecNegotiation::Strict) {
        active_v4l2_spec_ = spec;
    }
    /* SensorVc: each VC has its own spec; no aggregate update. */

    if (consumer.requires_exclusive) {
        exclusive_holder_ = h.id;
    }
    const uint64_t id = h.id;
    streams_.emplace(id, std::move(h));

    if (deps_.reprogram_v4l2) {
        /* Synchronous in this skeleton; the service binary offloads to a
         * worker thread and posts the completion back to the main loop. */
        deps_.reprogram_v4l2(active_v4l2_spec_);
    }
    return id;
}

void CameraArbiter::preempt(uint64_t stream_id, const std::string &reason)
{
    std::lock_guard lk(mu_);
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) return;
    it->second.state = StreamState::Preempted;
    if (deps_.emit_preempted) {
        deps_.emit_preempted(it->second.consumer.client_name, stream_id,
                             reason, kPreemptionDeadlineMs);
    }
    /* The caller schedules a kPreemptionDeadlineMs timer that fires
     * onPreemptionDeadline; that path force-closes the stream. */
}

void CameraArbiter::onConsumerReleased(uint64_t stream_id)
{
    std::lock_guard lk(mu_);
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) return;
    it->second.state = StreamState::Closing;
    if (exclusive_holder_ && *exclusive_holder_ == stream_id) {
        exclusive_holder_.reset();
    }
    streams_.erase(it);
    /* If CommonHigh and we just lost the highest-spec consumer, we could
     * downgrade the V4L2 program. We deliberately don't, to avoid
     * thrashing — sensor reprogramming has its own glitch budget. The
     * spec stays at the high-water mark until the camera is fully idle.
     */
    if (streams_.empty() && cfg_.negotiation == SpecNegotiation::CommonHigh) {
        active_v4l2_spec_ = cfg_.fallback_spec;
    }
}

void CameraArbiter::onPreemptionDeadline(uint64_t stream_id)
{
    /* Force-close: identical bookkeeping as a graceful release, but the
     * caller will also tear down the V4L2 buffers and dma-buf socket. */
    onConsumerReleased(stream_id);
}

std::vector<StreamHandle> CameraArbiter::snapshot() const
{
    std::lock_guard lk(mu_);
    std::vector<StreamHandle> out;
    out.reserve(streams_.size());
    for (const auto &[_, s] : streams_) out.push_back(s);
    return out;
}

bool CameraArbiter::hasExclusiveHolder() const
{
    std::lock_guard lk(mu_);
    return exclusive_holder_.has_value();
}

StreamSpec CameraArbiter::activeV4L2Spec() const
{
    std::lock_guard lk(mu_);
    return active_v4l2_spec_;
}

} /* namespace evs::ap */
