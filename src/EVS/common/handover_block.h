/*
 * Shared handover block — mapped at MCU SRAM 0x200F_0000 and into AP physical
 * address space via the SoC interconnect. Both sides use this exact layout.
 *
 * Field semantics: Part 1 §1.10 of the EVS reference. Producers write their
 * own state/timestamp/seq fields; consumers only read the peer's fields.
 * Volatile is required because the shared block lives in non-cached memory
 * on both sides and the compiler must not elide loads/stores.
 */
#ifndef EVS_HANDOVER_BLOCK_H
#define EVS_HANDOVER_BLOCK_H

#include <stdint.h>

#define EVS_HANDOVER_MAGIC   0x31535645u  /* 'EVS1' little-endian */
#define EVS_HANDOVER_VERSION 1u

/* States are shared across MCU and AP. The two sides own different fields,
 * but the enum is the same so log decoding is symmetric. */
enum evs_state {
    EVS_STATE_RESET     = 0,
    EVS_STATE_INIT      = 1,
    EVS_STATE_STREAMING = 2,
    EVS_STATE_NEGOTIATE = 3,
    EVS_STATE_SHADOW    = 4,
    EVS_STATE_CUTOVER   = 5,
    EVS_STATE_RELEASED  = 6,
    EVS_STATE_FAULT     = 7,
};

/* Flag bits in evs_handover_block.flags. Set by either side; clearer is the
 * side that "consumed" the request (see field comments). */
#define EVS_FLAG_AP_REQ_NEGOTIATE  (1u << 0)  /* AP -> MCU: please prepare to hand over */
#define EVS_FLAG_AP_REQ_SHADOW     (1u << 1)  /* AP -> MCU: AP rendering off-screen plane */
#define EVS_FLAG_AP_REQ_CUTOVER    (1u << 2)  /* AP -> MCU: drop your plane next vsync */
#define EVS_FLAG_MCU_FAULT         (1u << 8)  /* MCU -> AP: fault, stay/reclaim */
#define EVS_FLAG_AP_FAULT          (1u << 9)  /* AP -> MCU: AP fault, MCU should reclaim */

struct evs_handover_block {
    uint32_t magic;                       /* EVS_HANDOVER_MAGIC */
    uint32_t version;                     /* EVS_HANDOVER_VERSION */

    volatile uint32_t mcu_state;          /* enum evs_state, MCU-owned */
    volatile uint32_t ap_state;           /* enum evs_state, AP-owned  */
    volatile uint32_t flags;              /* EVS_FLAG_*, either side   */

    uint32_t camera_active_mask;          /* bitmask of active cameras, MCU writes */
    uint32_t display_plane_id_mcu;        /* DRM plane id used by MCU early path  */
    uint32_t display_plane_id_ap;         /* DRM plane id claimed by AP evs-display */

    volatile uint32_t frame_seq_mcu;      /* monotonic, MCU-owned */
    volatile uint32_t frame_seq_ap;       /* monotonic, AP-owned  */

    volatile uint64_t mcu_timestamp_ns;   /* liveness clock, MCU-owned */
    volatile uint64_t ap_timestamp_ns;    /* liveness clock, AP-owned  */

    uint8_t  reserved[64];
};

/* Liveness threshold: if peer timestamp does not advance within this window
 * during SHADOW or CUTOVER, the watching side reverts to STREAMING/owns. */
#define EVS_PEER_LIVENESS_TIMEOUT_NS  500000000ull   /* 500 ms */

#endif /* EVS_HANDOVER_BLOCK_H */
