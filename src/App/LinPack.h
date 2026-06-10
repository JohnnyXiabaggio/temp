#ifndef LIN_PACK_H
#define LIN_PACK_H

#include "Std_Types.h"
#include "LinSignals.h"

/* ------------------------------------------------------------------ *
 *  LIN PDU byte <-> signal-struct converters.
 *
 *  Reference: J100 LIN matrix (per YG-D18-2026-1042 page 5).
 *  All LIN frames in this project are 8 bytes (LIN 2.x classic).
 *  Bit positions below are LSB-first within byte (LIN/CAN convention)
 *  and are taken from the J100 matrix import; the matrix is the
 *  master, this file is the generated artefact.
 *
 *  Layout of MSWToVCU (LIN PDU id 0x02), 8 bytes:
 *
 *    byte 0  bits 1..0   CC/ACC Mode Switch              (2)
 *            bits 3..2   Scroll Up Button Status         (2)
 *            bits 5..4   Scroll Down Button Status       (2)
 *            bits 7..6   Cruise Control Resume Switch    (2)
 *    byte 1  bits 1..0   OFF Switch                      (2)
 *            bits 7..2   reserved                        (6)
 *    bytes 2..7          reserved
 *
 *  Layout of HandleToVCU (LIN PDU id 0x01), 8 bytes:
 *
 *    byte 0  bits 3..0   Auxiliary brake gear            (4)
 *            bits 7..4   reserved                        (4)
 *    byte 1  bits 7..0   Transmission Requested Gear     (8)
 *    byte 2  bits 3..0   Transmission Mode 1             (4)
 *            bits 5..4   A/M Mode Switch                 (2)
 *            bits 7..6   reserved                        (2)
 *    byte 3  bits 1..0   M+ Switch                       (2)
 *            bits 3..2   M- Switch                       (2)
 *            bits 7..4   reserved                        (4)
 *    bytes 4..7          reserved
 *
 *  All unpack functions return E_NOT_OK if the PDU length is wrong
 *  or the byte pointer is NULL -- the caller (PduR Rx indication)
 *  treats this as a malformed frame and drops it.
 * ------------------------------------------------------------------ */

#define LIN_FRAME_SIZE      8u

#define LIN_PDU_HANDLE_TO_VCU   0x01u
#define LIN_PDU_MSW_TO_VCU      0x02u

extern Std_ReturnType LinPack_UnpackMSWToVCU  (const uint8 *bytes, uint16 len,
                                               LinSig_MSWToVCU   *out);
extern Std_ReturnType LinPack_UnpackHandleToVCU(const uint8 *bytes, uint16 len,
                                               LinSig_HandleToVCU *out);

/* LIN classic checksum (enhanced, including PID). Reference:
 * LIN Specification Package 2.2A §2.3.1.5. Returned 0 if the frame
 * is too short. */
extern uint8 LinPack_Checksum(uint8 pid, const uint8 *bytes, uint16 len);

#endif
