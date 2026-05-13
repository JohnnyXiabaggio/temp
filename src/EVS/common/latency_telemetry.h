/* Per-frame latency telemetry. Each producer fills the fields it owns;
 * unfilled fields stay at 0 and are interpreted as "not reached." Times
 * are CLOCK_MONOTONIC nanoseconds — the same clock V4L2 buffers and DRM
 * out-fences report, so cross-process subtraction is well-defined.
 *
 * The struct is forwarded alongside the dma-buf FD; on Linux this rides
 * in a V4L2 metadata-output buffer or a side-channel control struct
 * delivered over the same Unix socket as the FD. Producers must NEVER
 * stash this in the pixel data — that would force a copy.
 *
 * Acceptance budgets are defined in docs/EVS_LowLatency_Design.md and
 * codified here as constants so CI gates and runtime alarms share one
 * source of truth.
 */
#ifndef EVS_LATENCY_TELEMETRY_H
#define EVS_LATENCY_TELEMETRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct evs_frame_telemetry {
    uint32_t frame_seq;            /* monotonic, set by capture producer  */
    uint32_t camera_id_hash;       /* fnv1a32 of camera_id string         */
    uint64_t t_sensor_sof_ns;      /* sensor start-of-frame               */
    uint64_t t_csi_done_ns;        /* CSI DMA writeback complete          */
    uint64_t t_dqbuf_ns;           /* userspace returned from VIDIOC_DQBUF*/
    uint64_t t_handed_to_ap_ns;    /* fd delivered to evs-display         */
    uint64_t t_commit_done_ns;     /* drmModeAtomicCommit returned        */
    uint64_t t_page_flip_ns;       /* DRM PAGE_FLIP_EVENT received        */
    uint64_t t_scanout_done_ns;    /* OUT_FENCE_PTR signaled              */
    uint32_t flags;                /* see EVS_TELEM_FLAG_*                */
    uint32_t reserved;
};

/* Set by the producer when this frame was dropped before reaching the
 * display. The struct is still emitted so latency analytics can see
 * where the drop happened. */
#define EVS_TELEM_FLAG_DROPPED        (1u << 0)
/* Set when the frame missed its vsync deadline (page flip arrived on
 * vsync N+1 rather than N). */
#define EVS_TELEM_FLAG_LATE_FLIP      (1u << 1)
/* Set when the in-fence was unsignaled at commit time and forced a wait. */
#define EVS_TELEM_FLAG_FENCE_WAIT     (1u << 2)
/* Set when the consumer was a low_latency stream — used to gate alarm
 * thresholds; non-low-latency consumers tolerate more jitter. */
#define EVS_TELEM_FLAG_LOW_LATENCY    (1u << 3)

/* Budget constants from docs/EVS_LowLatency_Design.md §2.
 * One vsync at 60 fps = 16_666_667 ns. */
#define EVS_VSYNC_60HZ_NS             16666667ULL
#define EVS_GLASS_TO_GLASS_BUDGET_NS  50000000ULL   /* 50 ms p99 */
#define EVS_FRAME_JITTER_BUDGET_NS    1000000ULL    /* 1 ms p99  */

/* Histogram bucket edges (ns), shared between exporter and analyzer so
 * percentile math is consistent. Powers-of-two-ish around the budget. */
#define EVS_LATENCY_BUCKET_COUNT      12
static const uint64_t evs_latency_buckets_ns[EVS_LATENCY_BUCKET_COUNT] = {
    5000000ULL,    10000000ULL,   16666667ULL,   25000000ULL,
    33333333ULL,   40000000ULL,   50000000ULL,   66666667ULL,
    83333333ULL,   100000000ULL,  150000000ULL,  250000000ULL,
};

/* Glass-to-glass = t_scanout_done − t_sensor_sof. Returns 0 if either
 * end of the measurement was not reached (caller should ignore). */
static inline uint64_t evs_g2g_ns(const struct evs_frame_telemetry *t)
{
    if (!t->t_scanout_done_ns || !t->t_sensor_sof_ns) return 0;
    return t->t_scanout_done_ns - t->t_sensor_sof_ns;
}

/* Returns 1 if the frame breached the glass-to-glass budget. */
static inline int evs_g2g_over_budget(const struct evs_frame_telemetry *t)
{
    uint64_t d = evs_g2g_ns(t);
    return d != 0 && d > EVS_GLASS_TO_GLASS_BUDGET_NS;
}

/* fnv1a-32 of a NUL-terminated camera_id string — small and stable
 * across processes. Used so telemetry consumers can group by camera
 * without round-tripping the string. */
static inline uint32_t evs_camera_id_hash(const char *s)
{
    uint32_t h = 0x811c9dc5u;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 0x01000193u;
    }
    return h;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EVS_LATENCY_TELEMETRY_H */
