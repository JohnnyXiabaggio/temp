/* J1939 CAN frame codec.
 *
 * J1939 conventions honoured here:
 *   - unused bits/bytes transmit as all-ones ("not available")
 *   - torque percent is offset-coded: raw = pct + 125, range
 *     -125..+125; 0xFF stays "not available"
 *   - 2-bit switch states: 00 off, 01 on, 10 error, 11 not available
 */

#include "CanPack.h"

#define NA_BYTE 0xFFu

/* -- CCVS_VCU -------------------------------------------------------- */

Std_ReturnType CanPack_PackCCVS_VCU(const CanSig_CCVS_VCU *in,
                                    uint8 *bytes, uint16 len)
{
    if ((in == NULL) || (bytes == NULL) || (len != CAN_FRAME_SIZE)) {
        return E_NOT_OK;
    }
    for (uint16 i = 0u; i < CAN_FRAME_SIZE; i++) { bytes[i] = NA_BYTE; }

    /* byte 3 bits 7..6 : Enable Switch */
    bytes[3] = (uint8)((bytes[3] & 0x3Fu) |
                       ((in->cruiseControlEnableSwitch & 0x03u) << 6u));
    /* byte 4 : SET+ / SET- / Resume (2 bits each, rest stays NA) */
    bytes[4] = (uint8)(0xC0u |
                       ( in->cruiseControlAccelerateSwitch & 0x03u)        |
                       ((in->cruiseControlCoastSwitch      & 0x03u) << 2u) |
                       ((in->cruiseControlResumeSwitch     & 0x03u) << 4u));
    return E_OK;
}

Std_ReturnType CanPack_UnpackCCVS_VCU(const uint8 *bytes, uint16 len,
                                      CanSig_CCVS_VCU *out)
{
    if ((bytes == NULL) || (out == NULL) || (len != CAN_FRAME_SIZE)) {
        return E_NOT_OK;
    }
    out->cruiseControlEnableSwitch     = (uint8)((bytes[3] >> 6u) & 0x03u);
    out->cruiseControlAccelerateSwitch = (uint8)( bytes[4]        & 0x03u);
    out->cruiseControlCoastSwitch      = (uint8)((bytes[4] >> 2u) & 0x03u);
    out->cruiseControlResumeSwitch     = (uint8)((bytes[4] >> 4u) & 0x03u);
    return E_OK;
}

/* -- TSC1 ------------------------------------------------------------ */

Std_ReturnType CanPack_PackTSC1(const CanSig_TSC1_VDR *in,
                                uint8 *bytes, uint16 len)
{
    if ((in == NULL) || (bytes == NULL) || (len != CAN_FRAME_SIZE)) {
        return E_NOT_OK;
    }
    /* Range guard before offset coding: J1939 torque is -125..+125. */
    if ((in->requestedTorquePct < -125) || (in->requestedTorquePct > 125)) {
        return E_NOT_OK;
    }

    /* byte 0: override mode (1..0), speed cond NA (3..2),
     *         priority (5..4), reserved 11 (7..6) */
    bytes[0] = (uint8)(( in->overrideControlMode         & 0x03u)        |
                       (0x03u                                     << 2u) |
                       ((in->overrideControlModePriority & 0x03u) << 4u) |
                       (0x03u                                     << 6u));
    /* bytes 1..2: requested speed -> not available */
    bytes[1] = NA_BYTE;
    bytes[2] = NA_BYTE;
    /* byte 3: torque, offset +125 */
    bytes[3] = (uint8)((sint16)in->requestedTorquePct + 125);
    /* bytes 4..6 reserved, byte 7 = checksum slot = FF (Q&A #6) */
    bytes[4] = NA_BYTE;
    bytes[5] = NA_BYTE;
    bytes[6] = NA_BYTE;
    bytes[7] = in->checksum;    /* always 0xFF from BodyRouting */
    return E_OK;
}

Std_ReturnType CanPack_UnpackTSC1(const uint8 *bytes, uint16 len,
                                  CanSig_TSC1_VDR *out)
{
    if ((bytes == NULL) || (out == NULL) || (len != CAN_FRAME_SIZE)) {
        return E_NOT_OK;
    }
    out->overrideControlMode         = (uint8)( bytes[0]        & 0x03u);
    out->overrideControlModePriority = (uint8)((bytes[0] >> 4u) & 0x03u);
    out->requestedTorquePct          = (sint8)((sint16)bytes[3] - 125);
    out->checksum                    = bytes[7];
    return E_OK;
}

/* -- TC1 ------------------------------------------------------------- */

Std_ReturnType CanPack_PackTC1(const CanSig_TC1 *in,
                               uint8 *bytes, uint16 len)
{
    if ((in == NULL) || (bytes == NULL) || (len != CAN_FRAME_SIZE)) {
        return E_NOT_OK;
    }
    for (uint16 i = 0u; i < CAN_FRAME_SIZE; i++) { bytes[i] = NA_BYTE; }

    /* byte 0 bits 5..0: requested gear (bits 7..6 stay NA) */
    bytes[0] = (uint8)(0xC0u | (in->transmissionRequestedGear & 0x3Fu));
    /* byte 1: mode (3..0) + A/M switch (5..4), 7..6 NA */
    bytes[1] = (uint8)(0xC0u |
                       ( in->transmissionMode1 & 0x0Fu)        |
                       ((in->amModeSwitch      & 0x03u) << 4u));
    /* byte 2: M+ (1..0), M- (3..2), 7..4 NA */
    bytes[2] = (uint8)(0xF0u |
                       ( in->mPlusSwitch  & 0x03u)        |
                       ((in->mMinusSwitch & 0x03u) << 2u));
    return E_OK;
}

Std_ReturnType CanPack_UnpackTC1(const uint8 *bytes, uint16 len,
                                 CanSig_TC1 *out)
{
    if ((bytes == NULL) || (out == NULL) || (len != CAN_FRAME_SIZE)) {
        return E_NOT_OK;
    }
    out->transmissionRequestedGear = (uint8)( bytes[0]        & 0x3Fu);
    out->transmissionMode1         = (uint8)( bytes[1]        & 0x0Fu);
    out->amModeSwitch              = (uint8)((bytes[1] >> 4u) & 0x03u);
    out->mPlusSwitch               = (uint8)( bytes[2]        & 0x03u);
    out->mMinusSwitch              = (uint8)((bytes[2] >> 2u) & 0x03u);
    return E_OK;
}
