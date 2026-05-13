/* Smoke tests for the latency telemetry header. The header is mostly
 * constants and inline helpers; we check g2g math, the dropped/unfilled
 * sentinel, and the camera-id hash stability.
 */

#include <stdio.h>
#include <string.h>

#include "../../src/EVS/common/latency_telemetry.h"

static int n_pass, n_fail;
#define CHECK(cond) do { \
    if (cond) { n_pass++; } \
    else { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
           n_fail++; } \
} while (0)

int main(void)
{
    struct evs_frame_telemetry t;
    memset(&t, 0, sizeof(t));

    /* Unfilled fields return 0 from g2g and don't count as over budget. */
    CHECK(evs_g2g_ns(&t) == 0);
    CHECK(evs_g2g_over_budget(&t) == 0);

    /* In-budget frame: 30 ms. */
    t.t_sensor_sof_ns   = 1000000ULL;
    t.t_scanout_done_ns = 1000000ULL + 30000000ULL;
    CHECK(evs_g2g_ns(&t) == 30000000ULL);
    CHECK(evs_g2g_over_budget(&t) == 0);

    /* Over budget: 75 ms. */
    t.t_scanout_done_ns = 1000000ULL + 75000000ULL;
    CHECK(evs_g2g_over_budget(&t) == 1);

    /* Bucket array is monotonically increasing — bug catcher in case
     * someone edits the array out of order. */
    for (int i = 1; i < EVS_LATENCY_BUCKET_COUNT; i++) {
        CHECK(evs_latency_buckets_ns[i] > evs_latency_buckets_ns[i-1]);
    }

    /* Vsync constant matches 1/60 s in ns within rounding (1 ns slack). */
    CHECK(EVS_VSYNC_60HZ_NS == 16666667ULL);

    /* fnv1a hash: stable, distinguishes common camera IDs, empty -> seed. */
    CHECK(evs_camera_id_hash("") == 0x811c9dc5u);
    CHECK(evs_camera_id_hash("rear")  != evs_camera_id_hash("front"));
    CHECK(evs_camera_id_hash("rear")  == evs_camera_id_hash("rear"));

    printf("latency_telemetry: %d pass, %d fail\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
