/* Display controller driver — MCU side. See Part 1 §1.9.
 *
 * disp_set_buffer must double-buffer with vsync wait; tearing on the rear
 * camera image is unacceptable per ISO 16505.
 */
#ifndef EVS_DISP_DRV_H
#define EVS_DISP_DRV_H

#include <stdint.h>
#include "csi_driver.h"

#define PIX_FMT_NV12 0x3231564eu  /* fourcc 'NV12' */

struct disp_init_params {
    uint16_t hactive, vactive;
    uint16_t hfp, hsync, hbp;
    uint16_t vfp, vsync, vbp;
    uint32_t pixel_clock_hz;
    uint8_t  format;          /* PIX_FMT_NV12 */
    uint8_t  plane_id;        /* pre-claimed overlay plane */
};

int disp_init(const struct disp_init_params *params);
int disp_set_buffer(struct csi_buffer *buf);
int disp_disable_plane(void);
int disp_enable_plane(void);

#endif /* EVS_DISP_DRV_H */
