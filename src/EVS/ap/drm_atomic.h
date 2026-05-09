/* DRM atomic helpers for the AP-side EVS display path. Part 2 of the EVS
 * reference. The design splits libdrm I/O (drm_atomic.cpp) from the cutover
 * sequencing decision (cutover_planner.h) so that the planner is testable
 * without a real DRM device.
 */
#ifndef EVS_AP_DRM_ATOMIC_H
#define EVS_AP_DRM_ATOMIC_H

#include <cstdint>

namespace evs::ap {

struct DrmCachedProps {
    uint32_t crtc_id_on_plane;
    uint32_t fb_id_on_plane;
    uint32_t crtc_x, crtc_y, crtc_w, crtc_h;
    uint32_t src_x,  src_y,  src_w,  src_h;
    uint32_t zpos_on_plane;
    uint32_t in_fence_fd_on_plane;
    uint32_t mode_id_on_crtc;
    uint32_t active_on_crtc;
    uint32_t out_fence_ptr_on_crtc;
};

struct DrmCtx {
    int      fd          = -1;     /* /dev/dri/card0 or lease */
    uint32_t crtc_id     = 0;
    uint32_t connector_id = 0;
    uint32_t plane_evs_id = 0;     /* AP overlay plane */
    uint32_t plane_mcu_id = 0;     /* MCU/early plane to disable on cutover */
    uint32_t display_w = 0, display_h = 0;
    uint32_t src_w = 0,     src_h = 0;
    DrmCachedProps props {};
};

struct DmaFb {
    int      dmabuf_fd  = -1;      /* from V4L2 */
    uint32_t fb_id      = 0;
    uint32_t gem_handle = 0;
    uint32_t w = 0, h = 0;
    uint32_t fourcc = 0;
};

int  drm_init(DrmCtx *ctx, const char *path);
void drm_close(DrmCtx *ctx);

int  drm_import_v4l2_buffer(const DrmCtx *ctx,
                            int dmabuf_fd, uint32_t w, uint32_t h,
                            uint32_t fourcc, DmaFb *out);
void drm_release_fb(const DrmCtx *ctx, DmaFb *fb);

/* Steady-state per-frame page flip on plane_evs_id. */
int  drm_present_frame(DrmCtx *ctx, const DmaFb *fb, int in_fence_fd);

/* Single atomic commit that:
 *   - keeps plane_evs_id active with `ap_fb`
 *   - sets CRTC_ID=0 on plane_mcu_id (detach)
 *   - captures an out fence in *out_fence_fd (caller waits)
 * TEST_ONLY validation runs first; on failure returns the libdrm errno
 * negated and out_fence_fd is left at -1.
 */
int  drm_cutover_commit(DrmCtx *ctx, const DmaFb *ap_fb, int in_fence_fd,
                        int *out_fence_fd);

/* Hand a Weston-targeted plane+connector+crtc set to a lease fd. The EVS
 * plane is intentionally not in the leased set — Weston cannot touch it. */
int  drm_create_lease(const DrmCtx *ctx,
                      const uint32_t *objects, unsigned n_objects,
                      uint32_t *out_lessee_id);

} /* namespace evs::ap */

#endif /* EVS_AP_DRM_ATOMIC_H */
