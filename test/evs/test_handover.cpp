/* Cross-side handover tests. Both the MCU agent (C) and the AP agent (C++)
 * are pointed at the same in-process block, then we verify state and flag
 * propagation, MCU init, both directions of liveness detection, and the
 * three-phase mcuStale state machine.
 */

#define TESTS_MAIN
#include "tests.hpp"

extern "C" {
#include <stdint.h>
struct evs_handover_block;
void     handover_init(uintptr_t shm_phys);
void     handover_publish_state(uint32_t state, uint32_t frame_seq, uint64_t now_ns);
uint32_t handover_read_ap_state(void);
uint32_t handover_read_flags(void);
void     handover_clear_flags(uint32_t mask);
void     handover_set_flags(uint32_t mask);
int      handover_ap_stale(uint64_t now_ns);
}

#include "../../src/EVS/ap/ap_handover_agent.h"
#include "../../src/EVS/common/handover_block.h"

using namespace evs::ap;

/* Fixture: AP allocates the block on the heap, then both sides bind to it.
 * Each TEST gets a fresh fixture so there is no order coupling. */
struct Fix {
    HandoverAgent ap;
    evs_handover_block *blk = nullptr;
    Fix()
    {
        ap.open(nullptr, 0, 0);
        blk = const_cast<evs_handover_block *>(ap.block());
        handover_init(reinterpret_cast<uintptr_t>(blk));
    }
    ~Fix() { ap.close(); }
};

TEST(mcu_init_writes_magic_and_version)
{
    Fix f;
    CHECK_EQ(f.blk->magic,   EVS_HANDOVER_MAGIC);
    CHECK_EQ(f.blk->version, EVS_HANDOVER_VERSION);
    CHECK_EQ(f.blk->mcu_state, (uint32_t)EVS_STATE_RESET);
    CHECK_EQ(f.blk->flags, 0u);
}

TEST(mcu_publish_visible_to_ap)
{
    Fix f;
    handover_publish_state(EVS_STATE_STREAMING, 42, 1000);
    CHECK_EQ(f.ap.readMcuState(), (uint32_t)EVS_STATE_STREAMING);
    CHECK_EQ(f.blk->frame_seq_mcu, 42u);
    CHECK_EQ(f.blk->mcu_timestamp_ns, 1000u);
}

TEST(ap_publish_visible_to_mcu)
{
    Fix f;
    f.ap.publishState(EVS_STATE_NEGOTIATE, 7, 5000);
    CHECK_EQ(handover_read_ap_state(), (uint32_t)EVS_STATE_NEGOTIATE);
    CHECK_EQ(f.blk->frame_seq_ap, 7u);
    CHECK_EQ(f.blk->ap_timestamp_ns, 5000u);
}

TEST(flag_set_and_clear_are_independent)
{
    Fix f;
    f.ap.setFlags(EVS_FLAG_AP_REQ_NEGOTIATE | EVS_FLAG_AP_REQ_SHADOW);
    CHECK((handover_read_flags() & EVS_FLAG_AP_REQ_NEGOTIATE) != 0);
    CHECK((handover_read_flags() & EVS_FLAG_AP_REQ_SHADOW)    != 0);

    /* MCU clears only one flag — the other survives. */
    handover_clear_flags(EVS_FLAG_AP_REQ_NEGOTIATE);
    CHECK((f.ap.readFlags() & EVS_FLAG_AP_REQ_NEGOTIATE) == 0);
    CHECK((f.ap.readFlags() & EVS_FLAG_AP_REQ_SHADOW)    != 0);
}

TEST(mcu_can_set_fault_flag)
{
    Fix f;
    handover_set_flags(EVS_FLAG_MCU_FAULT);
    CHECK((f.ap.readFlags() & EVS_FLAG_MCU_FAULT) != 0);
}

/* The AP staleness tracker has three phases:
 *   1. First call seeds (returns false).
 *   2. While MCU advances ts, returns false.
 *   3. After MCU stops, after timeout window, returns true. */
TEST(ap_mcu_stale_three_phases)
{
    Fix f;
    /* Phase 1: seed. */
    handover_publish_state(EVS_STATE_STREAMING, 1, 1000);
    CHECK(!f.ap.mcuStale(1000));

    /* Phase 2: MCU keeps publishing. */
    handover_publish_state(EVS_STATE_STREAMING, 2, 2000);
    CHECK(!f.ap.mcuStale(2000));

    /* Phase 3: MCU goes silent. Just under threshold -> still ok. */
    uint64_t t = 2000 + EVS_PEER_LIVENESS_TIMEOUT_NS - 1;
    CHECK(!f.ap.mcuStale(t));
    /* At/over threshold -> stale. */
    t = 2000 + EVS_PEER_LIVENESS_TIMEOUT_NS;
    CHECK(f.ap.mcuStale(t));

    /* Recovery: MCU resumes publishing. */
    handover_publish_state(EVS_STATE_STREAMING, 3, t + 100);
    CHECK(!f.ap.mcuStale(t + 100));
}

TEST(mcu_ap_stale_three_phases)
{
    Fix f;
    f.ap.publishState(EVS_STATE_NEGOTIATE, 0, 1000);
    CHECK_EQ(handover_ap_stale(1000), 0);

    f.ap.publishState(EVS_STATE_NEGOTIATE, 1, 2000);
    CHECK_EQ(handover_ap_stale(2000), 0);

    /* AP silent past timeout. */
    uint64_t t = 2000 + EVS_PEER_LIVENESS_TIMEOUT_NS;
    CHECK_EQ(handover_ap_stale(t), 1);

    f.ap.publishState(EVS_STATE_NEGOTIATE, 2, t + 50);
    CHECK_EQ(handover_ap_stale(t + 50), 0);
}

TEST(handover_block_carries_expected_fields)
{
    Fix f;
    /* Sanity check that the block was zero-initialized except for the
     * fields the MCU populates at init. The detailed ABI offset/size
     * checks live in test_handover_abi.cpp (compile-time static_asserts).
     */
    CHECK_EQ(f.blk->camera_active_mask, 0u);
    CHECK_EQ(f.blk->display_plane_id_mcu, 0u);
    CHECK_EQ(f.blk->display_plane_id_ap,  0u);
    CHECK_EQ(f.blk->frame_seq_mcu, 0u);
    CHECK_EQ(f.blk->frame_seq_ap,  0u);
    CHECK_EQ(f.blk->mcu_timestamp_ns, 0ull);
    CHECK_EQ(f.blk->ap_timestamp_ns,  0ull);
}

int main() { return tests::run_all(); }
