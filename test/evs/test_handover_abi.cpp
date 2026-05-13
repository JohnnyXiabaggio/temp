/* ABI / layout assertions for the shared handover block.
 *
 * The block is read/written by two binaries built from independent
 * toolchains (Zephyr/arm-none-eabi on the MCU, GCC on the AP). Any drift
 * in offsets or sizes will silently corrupt the handover. These checks
 * are static_asserts so the build fails — they will also fail under any
 * cross-compiler that places the fields differently.
 *
 * If you intentionally change the block, bump EVS_HANDOVER_VERSION and
 * update both the assertions here and the consumer code on each side.
 */

#define TESTS_MAIN
#include "tests.hpp"

#include <cstddef>

#include "../../src/EVS/common/handover_block.h"

/* Field offset table from §1.10 of the EVS reference. */
static_assert(offsetof(evs_handover_block, magic)               ==   0,
              "magic must start at offset 0");
static_assert(offsetof(evs_handover_block, version)             ==   4,
              "version must follow magic");
static_assert(offsetof(evs_handover_block, mcu_state)           ==   8, "");
static_assert(offsetof(evs_handover_block, ap_state)            ==  12, "");
static_assert(offsetof(evs_handover_block, flags)               ==  16, "");
static_assert(offsetof(evs_handover_block, camera_active_mask)  ==  20, "");
static_assert(offsetof(evs_handover_block, display_plane_id_mcu)==  24, "");
static_assert(offsetof(evs_handover_block, display_plane_id_ap) ==  28, "");
static_assert(offsetof(evs_handover_block, frame_seq_mcu)       ==  32, "");
static_assert(offsetof(evs_handover_block, frame_seq_ap)        ==  36, "");
static_assert(offsetof(evs_handover_block, mcu_timestamp_ns)    ==  40,
              "u64 timestamps must be 8-byte aligned");
static_assert(offsetof(evs_handover_block, ap_timestamp_ns)     ==  48, "");
static_assert(offsetof(evs_handover_block, reserved)            ==  56, "");

/* Total size: 56 bytes of declared fields + 64 byte reserved tail. */
static_assert(sizeof(evs_handover_block) == 120,
              "handover block size changed - update MCU/AP mmap regions");

/* Reserved-tail size: checked directly because struct alignment padding
 * can hide small shrinks of the reserved field under the sizeof assert.
 * sizeof(member) is unaffected by struct trailing padding. */
static_assert(sizeof(static_cast<evs_handover_block *>(nullptr)->reserved) == 64,
              "reserved tail size changed - bump EVS_HANDOVER_VERSION");

/* The state and flag enum values are part of the ABI too. The MCU and AP
 * compare these as integers, so the values must not move. */
static_assert(EVS_STATE_RESET     == 0, "");
static_assert(EVS_STATE_INIT      == 1, "");
static_assert(EVS_STATE_STREAMING == 2, "");
static_assert(EVS_STATE_NEGOTIATE == 3, "");
static_assert(EVS_STATE_SHADOW    == 4, "");
static_assert(EVS_STATE_CUTOVER   == 5, "");
static_assert(EVS_STATE_RELEASED  == 6, "");
static_assert(EVS_STATE_FAULT     == 7, "");

static_assert(EVS_FLAG_AP_REQ_NEGOTIATE == (1u << 0), "");
static_assert(EVS_FLAG_AP_REQ_SHADOW    == (1u << 1), "");
static_assert(EVS_FLAG_AP_REQ_CUTOVER   == (1u << 2), "");
static_assert(EVS_FLAG_MCU_FAULT        == (1u << 8), "");
static_assert(EVS_FLAG_AP_FAULT         == (1u << 9), "");

static_assert(EVS_HANDOVER_MAGIC   == 0x31535645u, "");
static_assert(EVS_HANDOVER_VERSION == 1u, "");

/* The liveness timeout is part of the protocol contract. If you change it,
 * confirm the new value still fits comfortably inside the regulatory RVC
 * latency budget (per the EVS reference, ~2 s glass-to-glass). */
static_assert(EVS_PEER_LIVENESS_TIMEOUT_NS == 500'000'000ULL, "");

/* Runtime smoke test so the binary has at least one TEST and the suite
 * runner reports the file as ok. */
TEST(static_asserts_compiled)
{
    CHECK(true);
}

int main() { return tests::run_all(); }
