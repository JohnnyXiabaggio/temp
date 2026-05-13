/* AP-side companion to MCU handover_agent.
 *
 * mmaps the shared block via /dev/mem (or the SoC's reserved-memory dt-node
 * exported as a uio device) and provides the symmetric API: AP publishes
 * its state and reads the MCU's. Host-buildable; the mmap is replaced with
 * a heap allocation when EVS_HOST_BUILD is set so unit tests can run.
 */
#ifndef EVS_AP_HANDOVER_AGENT_H
#define EVS_AP_HANDOVER_AGENT_H

#include <cstddef>
#include <cstdint>
#include "../common/handover_block.h"

namespace evs::ap {

class HandoverAgent {
public:
    /* Open the shared block. On target, `device_path` is e.g.
     * "/dev/uio0" or "/dev/mem" with `phys_addr` set. On host build,
     * pass nullptr/0 to allocate an in-process block. Returns 0 on success. */
    int  open(const char *device_path, uint64_t phys_addr, size_t length);
    void close();

    void     publishState(uint32_t state, uint32_t frame_seq, uint64_t now_ns);
    uint32_t readMcuState() const;
    uint32_t readFlags() const;
    void     setFlags(uint32_t mask);
    void     clearFlags(uint32_t mask);

    /* Returns true when MCU timestamp has not advanced past the liveness
     * threshold. Tracks state across calls; pass a monotonic clock. */
    bool     mcuStale(uint64_t now_ns);

    /* Direct access for the cutover planner to read mcu_state cheaply. */
    const evs_handover_block *block() const { return blk_; }

private:
    evs_handover_block *blk_       = nullptr;
    void               *mapping_   = nullptr;
    size_t              map_len_   = 0;
    bool                host_owned_ = false;

    uint64_t last_mcu_ts_       = 0;
    uint64_t last_mcu_progress_ = 0;
};

} /* namespace evs::ap */

#endif /* EVS_AP_HANDOVER_AGENT_H */
