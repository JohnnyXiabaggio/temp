/* CSI-2 receiver driver — MCU side. See Part 1 §1.7 of EVS reference.
 *
 * The ISR re-arms DMA and marks buffers FILLED. All business logic runs on
 * the evs_pipeline work queue; this header defines the contract only.
 */
#ifndef EVS_CSI_DRIVER_H
#define EVS_CSI_DRIVER_H

#include <stddef.h>
#include <stdint.h>

struct csi_config {
    uint8_t  num_lanes;
    uint32_t pixel_clock_hz;
    uint16_t width;
    uint16_t height;
    uint32_t pixfmt;          /* fourcc; NV12 in our path */
    uint8_t  vc;              /* MIPI virtual channel id */
};

enum csi_buf_state {
    FB_FREE = 0,
    FB_QUEUED,
    FB_FILLED,
    FB_DISPLAY,
};

struct csi_buffer {
    uintptr_t phys_addr;
    size_t    size;
    uint32_t  seq;
    uint64_t  ts_ns;
    uint32_t  state;          /* enum csi_buf_state */
};

typedef void (*csi_frame_cb_t)(struct csi_buffer *buf);

int  csi_init(const struct csi_config *cfg);
int  csi_start(struct csi_buffer pool[], size_t n);
int  csi_stop(void);
int  csi_set_frame_cb(csi_frame_cb_t cb);

#endif /* EVS_CSI_DRIVER_H */
