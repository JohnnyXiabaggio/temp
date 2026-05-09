/* Parking-line overlay renderer. Part 1 §1.12.
 * Composited above the camera plane by the display controller.
 */
#ifndef EVS_OVL_RENDER_H
#define EVS_OVL_RENDER_H

#include <stdint.h>

typedef struct {
    int16_t steering_angle_deg_x10;  /* Q?.1, e.g. 1234 = 123.4 deg */
    uint8_t draw_static;             /* outer guides */
    uint8_t draw_dynamic;            /* curving lines based on steering */
    uint8_t warn_object_close;       /* colored bar from ultrasonic input */
} overlay_state_t;

void ovl_render_init(uintptr_t fb_phys, uint16_t w, uint16_t h);
void ovl_render_update(const overlay_state_t *state);
void ovl_render_disable(void);

#endif /* EVS_OVL_RENDER_H */
