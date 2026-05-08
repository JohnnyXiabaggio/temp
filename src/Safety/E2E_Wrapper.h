#ifndef E2E_WRAPPER_H
#define E2E_WRAPPER_H

#include "Std_Types.h"
#include "PduR_Types.h"

/* End-to-end protect/check wrappers used by the application SW-Cs at
 * the sender and receiver of every ASIL >= B route.
 *
 * The wrappers exist solely to centralise the profile dispatch; the
 * underlying CRC + counter implementation is the qualified AUTOSAR
 * E2E Library (developed to ASIL-D). Per the safety concept, this is
 * the second leg of the ASIL-D = ASIL-B(D) + ASIL-B(D) decomposition:
 * any corruption / drop / replay / mis-route inside PduR is detected
 * here and translated into a route-specific application reaction.
 *
 * Profile mapping:
 *   PROFILE_01 : 8-bit CRC,  4-bit counter   (legacy CAN, body)
 *   PROFILE_02 : 8-bit CRC,  4-bit counter   (CAN-FD, supports gw)
 *   PROFILE_05 : 16-bit CRC, 8-bit counter   (Ethernet PDU)
 *   PROFILE_06 : 16-bit CRC, 8-bit counter   (SOME/IP, big-endian)
 */

typedef struct {
    uint8  counter;     /* monotonic per (sender,DataId)             */
    uint16 crc;         /* last computed CRC                         */
    uint8  errorCount;  /* sustained error counter, for Dem trigger  */
    uint8  okCount;
} E2E_RuntimeStateType;

/* Sender side: write counter + CRC into the SDU. */
extern Std_ReturnType E2E_Protect(PduR_E2EProfileType   profile,
                                  E2E_RuntimeStateType *state,
                                  uint16                dataId,
                                  uint8                *sdu,
                                  uint16                length);

/* Receiver side: validate counter monotonicity + CRC. */
extern Std_ReturnType E2E_Check  (PduR_E2EProfileType   profile,
                                  E2E_RuntimeStateType *state,
                                  uint16                dataId,
                                  const uint8          *sdu,
                                  uint16                length);

#endif
