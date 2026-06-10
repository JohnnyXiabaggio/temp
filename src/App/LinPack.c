/* LIN PDU byte-level codec.
 *
 * Generated-style code: the bit positions are the J100 LIN matrix
 * import (see LinPack.h header comment for the layout tables).
 * Style rules for codec code:
 *   - no multi-bit reads across byte boundaries (matrix guarantees
 *     byte-aligned or sub-byte signals on LIN)
 *   - mask first, then shift, so out-of-range raw bytes can never
 *     overflow the destination field
 *   - length is checked once at entry; below that the indices are
 *     constants so no runtime bound check is needed
 */

#include "LinPack.h"

Std_ReturnType LinPack_UnpackMSWToVCU(const uint8 *bytes, uint16 len,
                                      LinSig_MSWToVCU *out)
{
    if ((bytes == NULL) || (out == NULL) || (len != LIN_FRAME_SIZE)) {
        return E_NOT_OK;
    }

    out->ccAccModeSwitch           = (uint8)( bytes[0]        & 0x03u);
    out->scrollUpButtonStatus      = (uint8)((bytes[0] >> 2u) & 0x03u);
    out->scrollDownButtonStatus    = (uint8)((bytes[0] >> 4u) & 0x03u);
    out->cruiseControlResumeSwitch = (uint8)((bytes[0] >> 6u) & 0x03u);
    out->offSwitch                 = (uint8)( bytes[1]        & 0x03u);
    out->valid                     = 1u;
    return E_OK;
}

Std_ReturnType LinPack_UnpackHandleToVCU(const uint8 *bytes, uint16 len,
                                         LinSig_HandleToVCU *out)
{
    if ((bytes == NULL) || (out == NULL) || (len != LIN_FRAME_SIZE)) {
        return E_NOT_OK;
    }

    out->auxiliaryBrakeGear        = (uint8)( bytes[0]        & 0x0Fu);
    out->transmissionRequestedGear =          bytes[1];
    out->transmissionMode1         = (uint8)( bytes[2]        & 0x0Fu);
    out->amModeSwitch              = (uint8)((bytes[2] >> 4u) & 0x03u);
    out->mPlusSwitch               = (uint8)( bytes[3]        & 0x03u);
    out->mMinusSwitch              = (uint8)((bytes[3] >> 2u) & 0x03u);
    out->valid                     = 1u;
    return E_OK;
}

uint8 LinPack_Checksum(uint8 pid, const uint8 *bytes, uint16 len)
{
    if ((bytes == NULL) || (len == 0u)) {
        return 0u;
    }
    /* Enhanced checksum: PID + data, carry added back (LIN 2.2A). */
    uint16 sum = pid;
    for (uint16 i = 0u; i < len; i++) {
        sum += bytes[i];
        if (sum > 0xFFu) {
            sum = (sum & 0xFFu) + 1u;
        }
    }
    return (uint8)(~sum & 0xFFu);
}
