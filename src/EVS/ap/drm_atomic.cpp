/* Concrete libdrm bindings for the AP-side EVS display path.
 *
 * Build with EVS_HAVE_LIBDRM=1 and link against -ldrm on target. Without
 * libdrm (host CI), this file compiles to a stub that returns -ENOSYS so
 * unit tests can link cleanly against the planner without dragging libdrm
 * into the host build.
 */

#include "drm_atomic.h"

#include <cerrno>
#include <cstring>
#include <unistd.h>

#if EVS_HAVE_LIBDRM
#  include <fcntl.h>
#  include <sys/poll.h>
#  include <xf86drm.h>
#  include <xf86drmMode.h>
#endif

namespace evs::ap {

#if EVS_HAVE_LIBDRM

static uint32_t lookup_prop(int fd, uint32_t obj_id, uint32_t obj_type,
                            const char *name)
{
    drmModeObjectProperties *props =
        drmModeObjectGetProperties(fd, obj_id, obj_type);
    if (!props) return 0;
    uint32_t id = 0;
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyRes *p = drmModeGetProperty(fd, props->props[i]);
        if (!p) continue;
        if (std::strcmp(p->name, name) == 0) {
            id = p->prop_id;
        }
        drmModeFreeProperty(p);
        if (id) break;
    }
    drmModeFreeObjectProperties(props);
    return id;
}

int drm_init(DrmCtx *ctx, const char *path)
{
    ctx->fd = open(path, O_RDWR | O_CLOEXEC);
    if (ctx->fd < 0) return -errno;
    if (drmSetClientCap(ctx->fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0)           return -errno;
    if (drmSetClientCap(ctx->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) < 0) return -errno;

    /* Resource discovery (CRTC, connector, plane) is omitted from this
     * snippet — production code resolves them from device tree config or
     * configures them in /etc/evs-display.conf and validates against
     * drmModeGetResources at boot. The caller is expected to fill ctx->{
     * crtc_id, connector_id, plane_evs_id, plane_mcu_id, display_w/h,
     * src_w/h} before drm_init returns control. */

    auto P = [&](uint32_t obj, uint32_t type, const char *n) {
        return lookup_prop(ctx->fd, obj, type, n);
    };
    ctx->props.crtc_id_on_plane     = P(ctx->plane_evs_id, DRM_MODE_OBJECT_PLANE,    "CRTC_ID");
    ctx->props.fb_id_on_plane       = P(ctx->plane_evs_id, DRM_MODE_OBJECT_PLANE,    "FB_ID");
    ctx->props.crtc_x               = P(ctx->plane_evs_id, DRM_MODE_OBJECT_PLANE,    "CRTC_X");
    ctx->props.crtc_y               = P(ctx->plane_evs_id, DRM_MODE_OBJECT_PLANE,    "CRTC_Y");
    ctx->props.crtc_w               = P(ctx->plane_evs_id, DRM_MODE_OBJECT_PLANE,    "CRTC_W");
    ctx->props.crtc_h               = P(ctx->plane_evs_id, DRM_MODE_OBJECT_PLANE,    "CRTC_H");
    ctx->props.src_x                = P(ctx->plane_evs_id, DRM_MODE_OBJECT_PLANE,    "SRC_X");
    ctx->props.src_y                = P(ctx->plane_evs_id, DRM_MODE_OBJECT_PLANE,    "SRC_Y");
    ctx->props.src_w                = P(ctx->plane_evs_id, DRM_MODE_OBJECT_PLANE,    "SRC_W");
    ctx->props.src_h                = P(ctx->plane_evs_id, DRM_MODE_OBJECT_PLANE,    "SRC_H");
    ctx->props.zpos_on_plane        = P(ctx->plane_evs_id, DRM_MODE_OBJECT_PLANE,    "zpos");
    ctx->props.in_fence_fd_on_plane = P(ctx->plane_evs_id, DRM_MODE_OBJECT_PLANE,    "IN_FENCE_FD");
    ctx->props.mode_id_on_crtc      = P(ctx->crtc_id,      DRM_MODE_OBJECT_CRTC,     "MODE_ID");
    ctx->props.active_on_crtc       = P(ctx->crtc_id,      DRM_MODE_OBJECT_CRTC,     "ACTIVE");
    ctx->props.out_fence_ptr_on_crtc= P(ctx->crtc_id,      DRM_MODE_OBJECT_CRTC,     "OUT_FENCE_PTR");
    return 0;
}

void drm_close(DrmCtx *ctx)
{
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
}

int drm_import_v4l2_buffer(const DrmCtx *ctx, int dmabuf_fd,
                           uint32_t w, uint32_t h, uint32_t fourcc,
                           DmaFb *out)
{
    out->dmabuf_fd = dmabuf_fd;
    out->w = w; out->h = h; out->fourcc = fourcc;
    if (drmPrimeFDToHandle(ctx->fd, dmabuf_fd, &out->gem_handle) < 0) {
        return -errno;
    }
    /* NV12 layout: Y plane then interleaved UV plane. */
    uint32_t handles[4] = { out->gem_handle, out->gem_handle, 0, 0 };
    uint32_t pitches[4] = { w, w, 0, 0 };
    uint32_t offsets[4] = { 0, w * h, 0, 0 };
    if (drmModeAddFB2(ctx->fd, w, h, fourcc, handles, pitches, offsets,
                      &out->fb_id, 0) < 0) {
        return -errno;
    }
    return 0;
}

void drm_release_fb(const DrmCtx *ctx, DmaFb *fb)
{
    if (fb->fb_id) {
        drmModeRmFB(ctx->fd, fb->fb_id);
        fb->fb_id = 0;
    }
    if (fb->gem_handle) {
        struct drm_gem_close gc{};
        gc.handle = fb->gem_handle;
        drmIoctl(ctx->fd, DRM_IOCTL_GEM_CLOSE, &gc);
        fb->gem_handle = 0;
    }
}

static void add_plane_geometry(drmModeAtomicReq *req, uint32_t plane_id,
                               const DrmCtx *ctx, uint32_t fb_id,
                               int in_fence_fd)
{
    drmModeAtomicAddProperty(req, plane_id, ctx->props.crtc_id_on_plane, ctx->crtc_id);
    drmModeAtomicAddProperty(req, plane_id, ctx->props.fb_id_on_plane,   fb_id);
    drmModeAtomicAddProperty(req, plane_id, ctx->props.crtc_x, 0);
    drmModeAtomicAddProperty(req, plane_id, ctx->props.crtc_y, 0);
    drmModeAtomicAddProperty(req, plane_id, ctx->props.crtc_w, ctx->display_w);
    drmModeAtomicAddProperty(req, plane_id, ctx->props.crtc_h, ctx->display_h);
    drmModeAtomicAddProperty(req, plane_id, ctx->props.src_x,  0);
    drmModeAtomicAddProperty(req, plane_id, ctx->props.src_y,  0);
    drmModeAtomicAddProperty(req, plane_id, ctx->props.src_w,  ctx->src_w << 16);
    drmModeAtomicAddProperty(req, plane_id, ctx->props.src_h,  ctx->src_h << 16);
    if (in_fence_fd >= 0 && ctx->props.in_fence_fd_on_plane) {
        drmModeAtomicAddProperty(req, plane_id,
                                 ctx->props.in_fence_fd_on_plane, in_fence_fd);
    }
}

int drm_present_frame(DrmCtx *ctx, const DmaFb *fb, int in_fence_fd)
{
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if (!req) return -ENOMEM;
    add_plane_geometry(req, ctx->plane_evs_id, ctx, fb->fb_id, in_fence_fd);
    int ret = drmModeAtomicCommit(ctx->fd, req,
        DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT,
        const_cast<DmaFb *>(fb));
    drmModeAtomicFree(req);
    return ret < 0 ? -errno : 0;
}

int drm_cutover_commit(DrmCtx *ctx, const DmaFb *ap_fb, int in_fence_fd,
                       int *out_fence_fd)
{
    *out_fence_fd = -1;
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if (!req) return -ENOMEM;

    /* AP plane: drive with current AP frame, top of Z order. */
    add_plane_geometry(req, ctx->plane_evs_id, ctx, ap_fb->fb_id, in_fence_fd);
    if (ctx->props.zpos_on_plane) {
        drmModeAtomicAddProperty(req, ctx->plane_evs_id,
                                 ctx->props.zpos_on_plane, /*above MCU plane*/2);
    }

    /* MCU plane: detach. */
    uint32_t mcu_crtc = lookup_prop(ctx->fd, ctx->plane_mcu_id,
                                    DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    uint32_t mcu_fb   = lookup_prop(ctx->fd, ctx->plane_mcu_id,
                                    DRM_MODE_OBJECT_PLANE, "FB_ID");
    if (!mcu_crtc || !mcu_fb) {
        drmModeAtomicFree(req);
        return -ENOENT;
    }
    drmModeAtomicAddProperty(req, ctx->plane_mcu_id, mcu_crtc, 0);
    drmModeAtomicAddProperty(req, ctx->plane_mcu_id, mcu_fb,   0);

    /* Out fence so caller can confirm scanout completion. */
    int32_t fence = -1;
    if (ctx->props.out_fence_ptr_on_crtc) {
        drmModeAtomicAddProperty(req, ctx->crtc_id,
            ctx->props.out_fence_ptr_on_crtc,
            reinterpret_cast<uint64_t>(&fence));
    }

    /* Validate first. */
    int ret = drmModeAtomicCommit(ctx->fd, req,
        DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
    if (ret < 0) {
        drmModeAtomicFree(req);
        return -errno;
    }

    /* Real commit. */
    ret = drmModeAtomicCommit(ctx->fd, req,
        DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT,
        const_cast<DmaFb *>(ap_fb));
    drmModeAtomicFree(req);
    if (ret < 0) return -errno;

    *out_fence_fd = fence;
    return 0;
}

int drm_create_lease(const DrmCtx *ctx, const uint32_t *objects,
                     unsigned n_objects, uint32_t *out_lessee_id)
{
    int lease_fd = drmModeCreateLease(ctx->fd, objects, n_objects,
                                      O_CLOEXEC, out_lessee_id);
    if (lease_fd < 0) return -errno;
    return lease_fd;
}

#else /* !EVS_HAVE_LIBDRM */

int drm_init(DrmCtx *, const char *)              { return -ENOSYS; }
void drm_close(DrmCtx *)                          {}
int drm_import_v4l2_buffer(const DrmCtx *, int, uint32_t, uint32_t,
                           uint32_t, DmaFb *)     { return -ENOSYS; }
void drm_release_fb(const DrmCtx *, DmaFb *)      {}
int drm_present_frame(DrmCtx *, const DmaFb *, int) { return -ENOSYS; }
int drm_cutover_commit(DrmCtx *, const DmaFb *, int, int *out)
{
    if (out) *out = -1;
    return -ENOSYS;
}
int drm_create_lease(const DrmCtx *, const uint32_t *, unsigned, uint32_t *)
{
    return -ENOSYS;
}

#endif /* EVS_HAVE_LIBDRM */

} /* namespace evs::ap */
