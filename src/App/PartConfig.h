#ifndef PART_CONFIG_H
#define PART_CONFIG_H

#include "Std_Types.h"

/* ------------------------------------------------------------------ *
 *  102A part-number / OEM configuration.
 *
 *  Per YG-D18-2026-1042 the T-Gateway must discriminate at runtime:
 *    - Cruise switch form  : hardline or LIN (by part number)
 *    - Aux brake handle    : engine aux brake or retarder aux brake
 *                            (by handle part number)
 *    - Transmission type   : 0x00 = MT (no AMT routing); !=0 = AMT
 *
 *  The values are written into NVM by the UDS 102A diagnostic
 *  service; the routing manager reads them at startup and on every
 *  config-change indication. The NVM block is CRC + ECC protected.
 * ------------------------------------------------------------------ */

typedef enum {
    CRUISE_SRC_NONE   = 0u,
    CRUISE_SRC_LIN    = 1u,    /* T-Gateway routes LIN -> CAN          */
    CRUISE_SRC_HARDWIRE = 2u   /* hardline read elsewhere -- skip route*/
} CruiseSrcType;

typedef enum {
    AUX_BRAKE_NONE      = 0u,
    AUX_BRAKE_RETARDER  = 1u,  /* route to RCU via TSC1_VDR (SA 0x27) */
    AUX_BRAKE_ENGINE    = 2u   /* route to EMS via TSC1_DR (SA 0x00)  */
} AuxBrakeType;

typedef enum {
    TX_TYPE_MT  = 0u,          /* no AMT routing                       */
    TX_TYPE_AMT = 1u
} TxType;

typedef struct {
    CruiseSrcType  cruiseSrc;
    AuxBrakeType   auxBrake;
    TxType         txType;
    uint16         cruiseSwitchPartNo;
    uint16         retarderHandlePartNo;
    uint16         amtConfigCode;
} PartConfig;

extern Std_ReturnType PartConfig_Load(void);
extern const PartConfig *PartConfig_Get(void);

#endif
