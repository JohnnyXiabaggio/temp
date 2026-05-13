/* CAN signal listener — MCU reads gear/ignition directly so it can react
 * without depending on the AP. Part 1 §1.11.
 */
#ifndef EVS_CAN_LISTEN_H
#define EVS_CAN_LISTEN_H

#include <stdint.h>

typedef enum { GEAR_P, GEAR_R, GEAR_N, GEAR_D, GEAR_UNKNOWN } gear_t;
typedef enum { IGN_OFF, IGN_ACC, IGN_RUN, IGN_CRANK }         ignition_t;

struct can_signal_map {
    uint32_t gear_msg_id;
    uint8_t  gear_start_bit;
    uint8_t  gear_length;
    uint32_t ignition_msg_id;
    uint8_t  ignition_start_bit;
    uint8_t  ignition_length;
};

typedef void (*gear_cb_t)(gear_t prev, gear_t now, uint64_t ts_ns);
typedef void (*ign_cb_t) (ignition_t prev, ignition_t now, uint64_t ts_ns);

int can_listen_init(uint32_t bitrate);
int can_listen_register(const struct can_signal_map *map);
int can_listen_set_gear_cb(gear_cb_t cb);
int can_listen_set_ign_cb (ign_cb_t  cb);

#endif /* EVS_CAN_LISTEN_H */
