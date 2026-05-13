/* Software ISP (debayer + CCM + gamma + lens shading + overlay stamp).
 * Part 1 §1.8. ~12 ms per 1280x720 frame on M7@300 MHz with SIMD intrinsics.
 * If the sensor outputs YUV directly, isp_lite_process becomes a passthrough.
 */
#ifndef EVS_ISP_LITE_H
#define EVS_ISP_LITE_H

#include <stdint.h>
#include "csi_driver.h"

typedef struct {
    int16_t ccm[3][3];               /* Q4.12 */
    int16_t gamma_lut[256];
    int16_t lens_shading[33][33];    /* sparse correction grid */
    uint8_t debug_overlay;           /* 0/1 — stamps "EVS-MCU" */
} isp_params_t;

int isp_lite_init(const isp_params_t *params);
int isp_lite_process(const struct csi_buffer *src, struct csi_buffer *dst);

#endif /* EVS_ISP_LITE_H */
