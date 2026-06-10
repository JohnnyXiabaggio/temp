#ifndef CAN_PACK_H
#define CAN_PACK_H

#include "Std_Types.h"
#include "CanSignals.h"

/* ------------------------------------------------------------------ *
 *  J1939 CAN frame byte <-> signal-struct converters.
 *
 *  References:
 *    - SAE J1939-71 for CCVS (PGN 65265) and TSC1 (PGN 0)
 *    - YG-D18-2026-1042 for the J107/J169 usage of each signal
 *
 *  CCVS_VCU  (PGN 65265, SA 0x05, 8 bytes) -- only the four cruise
 *  switch signals are owned by the T-Gateway; all other CCVS signals
 *  are set to "not available" (all-ones) per J1939-71:
 *
 *    byte 3  bits 7..6   Cruise Control Enable Switch     (2)
 *    byte 4  bits 1..0   Cruise Control Set/Accel Switch  (2)  [SET+]
 *    byte 4  bits 3..2   Cruise Control Coast Switch      (2)  [SET-]
 *    byte 4  bits 5..4   Cruise Control Resume Switch     (2)
 *
 *  TSC1 (PGN 0, here addressed to RCU: 0x0C001027 = priority 3,
 *  PGN 0, DA 0x10? -- per the meeting minutes the full ID is
 *  0x0C001027 with SA 0x27), 8 bytes:
 *
 *    byte 0  bits 1..0   Override Control Mode            (2)
 *            bits 3..2   Requested Speed Control Cond.    (2) -> 0x3 NA
 *            bits 5..4   Override Control Mode Priority   (2)
 *            bits 7..6   reserved                         (2) -> 0x3
 *    byte 1..2           Requested Speed/Speed Limit      (16) -> 0xFFFF NA
 *    byte 3              Requested Torque/Torque Limit    (8)
 *                        raw = pct + 125  (J1939 -125..+125 offset)
 *    byte 4..6           reserved -> 0xFF
 *    byte 7              checksum slot -> 0xFF per Q&A #6 (不做校验)
 *
 *  TC1 (PGN 256, SA 0x05, 8 bytes) -- gateway-owned signals:
 *
 *    byte 0  bits 5..0   Transmission Requested Gear      (6)
 *    byte 1  bits 3..0   Transmission Mode 1              (4)
 *            bits 5..4   A/M Mode Switch                  (2)
 *    byte 2  bits 1..0   M+ Switch                        (2)
 *            bits 3..2   M- Switch                        (2)
 *    remaining bits/bytes -> not available (all-ones)
 * ------------------------------------------------------------------ */

#define CAN_FRAME_SIZE   8u

/* 29-bit extended IDs as agreed in the minutes. */
#define CANID_CCVS_VCU   0x18FEF105u    /* PGN 65265, prio 6, SA 0x05 */
#define CANID_TSC1_VDR   0x0C001027u    /* per spec page 3            */
#define CANID_TC1_VCU    0x0C010005u    /* PGN 256,  prio 3, SA 0x05  */

extern Std_ReturnType CanPack_PackCCVS_VCU(const CanSig_CCVS_VCU *in,
                                           uint8 *bytes, uint16 len);
extern Std_ReturnType CanPack_PackTSC1   (const CanSig_TSC1_VDR *in,
                                           uint8 *bytes, uint16 len);
extern Std_ReturnType CanPack_PackTC1    (const CanSig_TC1 *in,
                                           uint8 *bytes, uint16 len);

/* Unpackers for the test bench / receiver simulation. */
extern Std_ReturnType CanPack_UnpackCCVS_VCU(const uint8 *bytes, uint16 len,
                                             CanSig_CCVS_VCU *out);
extern Std_ReturnType CanPack_UnpackTSC1   (const uint8 *bytes, uint16 len,
                                             CanSig_TSC1_VDR *out);
extern Std_ReturnType CanPack_UnpackTC1    (const uint8 *bytes, uint16 len,
                                             CanSig_TC1 *out);

#endif
