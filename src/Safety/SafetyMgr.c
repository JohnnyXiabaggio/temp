/* Safety Manager.
 *
 * Responsibilities:
 *   - Verifies the integrity of the static routing configuration at
 *     startup (CRC-32 over the routing table image stored in flash).
 *   - Owns the Safe-State flag consulted by PduR on every dispatch.
 *   - Notifies the WdgM / EcuM that a transition has occurred so that
 *     the wider system can react (degrade ASIL functions, switch CAN
 *     channels to listen-only, etc.).
 *
 * The CRC algorithm and seed are shared with the configuration tool;
 * a mismatch implies either flash corruption or a configuration that
 * was not signed off, both of which preclude safe operation.
 */

#include "SafetyMgr.h"
#include "Dem.h"

static volatile boolean              SafeStateActive = FALSE;
static volatile SafetyMgr_ReasonType SafeStateReason = SAFE_STATE_REASON_NONE;

/* Bit-by-bit CRC-32 (polynomial 0xEDB88320, reflected). The production
 * build switches to the hardware CRC unit; the algorithm is identical. */
static uint32 SafetyMgr_Crc32(const uint8 *data, uint32 length)
{
    uint32 crc = 0xFFFFFFFFu;
    for (uint32 i = 0u; i < length; i++) {
        crc ^= (uint32)data[i];
        for (uint8 b = 0u; b < 8u; b++) {
            uint32 mask = (uint32)0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

__attribute__((weak))
Std_ReturnType SafetyMgr_VerifyRouteTableCrc(const PduR_RouteEntryType *table,
                                             uint32 sizeBytes,
                                             uint32 expectedCrc)
{
    /* Compute over the table image. NB: the image includes pointers to
     * the per-route DestList; in production the linker pins these to a
     * fixed segment so the CRC is deterministic across builds. */
    const uint32 actual = SafetyMgr_Crc32((const uint8 *)table, sizeBytes);
    if (actual != expectedCrc) {
        Dem_ReportErrorStatus(DEM_EVT_ROUTE_TABLE_CRC, DEM_EVENT_STATUS_FAILED);
        return E_NOT_OK;
    }
    return E_OK;
}

void SafetyMgr_EnterSafeState(SafetyMgr_ReasonType reason)
{
    /* Latched: once entered, never leaves without a reset. */
    SafeStateReason = reason;
    SafeStateActive = TRUE;
    /* In the real ECU this triggers WdgM_SetMode(SAFE), drops all CAN
     * controllers to listen-only, disables SoAd ingress, and lights a
     * dashboard tell-tale via Dem. */
}

boolean SafetyMgr_IsSafeState(void)
{
    return SafeStateActive;
}
