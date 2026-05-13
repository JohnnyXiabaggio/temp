#include "ap_handover_agent.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#ifndef EVS_HOST_BUILD
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <unistd.h>
#endif

namespace evs::ap {

int HandoverAgent::open(const char *device_path, uint64_t phys_addr,
                        size_t length)
{
    if (!device_path) {
        /* Host build / unit test: allocate an in-process block. */
        blk_ = static_cast<evs_handover_block *>(
            std::calloc(1, sizeof(evs_handover_block)));
        if (!blk_) return -ENOMEM;
        host_owned_ = true;
        return 0;
    }
#ifdef EVS_HOST_BUILD
    (void)phys_addr; (void)length;
    return -ENOSYS;
#else
    int fd = ::open(device_path, O_RDWR | O_SYNC);
    if (fd < 0) return -errno;
    void *m = ::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED,
                     fd, static_cast<off_t>(phys_addr));
    ::close(fd);
    if (m == MAP_FAILED) return -errno;
    mapping_ = m;
    map_len_ = length;
    blk_     = static_cast<evs_handover_block *>(m);
    /* Wait for MCU to populate magic. The caller is expected to retry if
     * magic is not yet visible — we don't busy-wait here. */
    return 0;
#endif
}

void HandoverAgent::close()
{
    if (host_owned_) {
        std::free(blk_);
        blk_ = nullptr;
        host_owned_ = false;
        return;
    }
#ifndef EVS_HOST_BUILD
    if (mapping_) {
        ::munmap(mapping_, map_len_);
        mapping_ = nullptr;
        blk_     = nullptr;
        map_len_ = 0;
    }
#endif
}

void HandoverAgent::publishState(uint32_t state, uint32_t frame_seq,
                                 uint64_t now_ns)
{
    if (!blk_) return;
    blk_->ap_state        = state;
    blk_->frame_seq_ap    = frame_seq;
    blk_->ap_timestamp_ns = now_ns;
}

uint32_t HandoverAgent::readMcuState() const
{
    return blk_ ? blk_->mcu_state : static_cast<uint32_t>(EVS_STATE_RESET);
}

uint32_t HandoverAgent::readFlags() const
{
    return blk_ ? blk_->flags : 0u;
}

void HandoverAgent::setFlags(uint32_t mask)
{
    if (!blk_) return;
    blk_->flags |= mask;
}

void HandoverAgent::clearFlags(uint32_t mask)
{
    if (!blk_) return;
    blk_->flags &= ~mask;
}

bool HandoverAgent::mcuStale(uint64_t now_ns)
{
    if (!blk_) return true;
    uint64_t ts = blk_->mcu_timestamp_ns;
    if (ts != last_mcu_ts_) {
        last_mcu_ts_       = ts;
        last_mcu_progress_ = now_ns;
        return false;
    }
    if (last_mcu_progress_ == 0) {
        last_mcu_progress_ = now_ns;
        return false;
    }
    return (now_ns - last_mcu_progress_) >= EVS_PEER_LIVENESS_TIMEOUT_NS;
}

} /* namespace evs::ap */
