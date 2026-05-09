/* Cross-side handover test: drive the MCU agent and the AP agent against a
 * shared block (in-process, since both modules accept a host-allocated
 * buffer) and verify state propagation + liveness detection.
 */

#include <cstdio>
#include <cstring>

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

static int n_pass, n_fail;
#define CHECK(cond) do { \
    if (cond) { n_pass++; } \
    else { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); n_fail++; } \
} while (0)

int main()
{
    /* Use the AP-side host build path to allocate the block, then point the
     * MCU agent at the same memory. */
    evs::ap::HandoverAgent ap;
    int rc = ap.open(nullptr, 0, 0);
    CHECK(rc == 0);

    auto *blk = const_cast<evs_handover_block *>(ap.block());
    CHECK(blk != nullptr);
    /* MCU init clobbers magic; do AP read after MCU init. */
    handover_init(reinterpret_cast<uintptr_t>(blk));
    CHECK(blk->magic == EVS_HANDOVER_MAGIC);

    /* MCU publishes STREAMING. */
    handover_publish_state(EVS_STATE_STREAMING, 1, /*now*/1'000'000);
    CHECK(ap.readMcuState() == EVS_STATE_STREAMING);
    CHECK(!ap.mcuStale(1'000'000));

    /* AP publishes NEGOTIATE + sets the flag. */
    ap.publishState(EVS_STATE_NEGOTIATE, 0, 1'500'000);
    ap.setFlags(EVS_FLAG_AP_REQ_NEGOTIATE);
    CHECK(handover_read_ap_state() == EVS_STATE_NEGOTIATE);
    CHECK((handover_read_flags() & EVS_FLAG_AP_REQ_NEGOTIATE) != 0);

    /* MCU acks by clearing the flag and publishing NEGOTIATE state. */
    handover_clear_flags(EVS_FLAG_AP_REQ_NEGOTIATE);
    handover_publish_state(EVS_STATE_NEGOTIATE, 2, 2'000'000);
    CHECK(ap.readMcuState() == EVS_STATE_NEGOTIATE);
    CHECK((ap.readFlags() & EVS_FLAG_AP_REQ_NEGOTIATE) == 0);

    /* AP goes silent: MCU should detect staleness after the timeout. */
    /* First call seeds the tracker. */
    uint64_t now = 3'000'000;
    CHECK(handover_ap_stale(now) == 0);
    /* Advance MCU clock past the threshold without AP updating its ts. */
    now += EVS_PEER_LIVENESS_TIMEOUT_NS + 1;
    CHECK(handover_ap_stale(now) == 1);

    /* AP heartbeat resumes -> staleness clears. */
    ap.publishState(EVS_STATE_NEGOTIATE, 1, now);
    /* Need one more call so the tracker observes the new ts. */
    CHECK(handover_ap_stale(now) == 0);

    ap.close();
    std::printf("handover: %d pass, %d fail\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
