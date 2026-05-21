#ifndef CAN_SIGNALS_H
#define CAN_SIGNALS_H

#include "Std_Types.h"

/* ------------------------------------------------------------------ *
 *  CAN signal images (powertrain CAN)
 *  Source: YG-D18-2026-1042
 *  These structs are *signal-view* of the I-PDU; the Com module
 *  packs them into bytes per the CAN matrix.
 * ------------------------------------------------------------------ */

/* CCVS_VCU (source address 0x05) -> EMS. Cruise control switches. */
typedef struct {
    uint8 cruiseControlEnableSwitch;    /* 0x00 Disabled, 0x01 Enabled,
                                           0x02 Reserved, 0x03 Not Available */
    uint8 cruiseControlAccelerateSwitch;/* 0/1                                */
    uint8 cruiseControlCoastSwitch;     /* 0/1                                */
    uint8 cruiseControlResumeSwitch;    /* 0/1                                */
} CanSig_CCVS_VCU;

/* TSC1_VDR (PGN 0x0C001027, source address 0x27) -> RCU.
 * Per spec: byte 1 carries torque control + low priority bits;
 *           byte 8 = 0xFF (no checksum). */
typedef struct {
    uint8 overrideControlMode;          /* 0x0=disabled, 0x2=torque control */
    uint8 overrideControlModePriority;  /* 0x3 = low priority               */
    sint8 requestedTorquePct;           /* 0, -15, -25, -50, -75, -100      */
    uint8 checksum;                     /* 0xFF (not checked)               */
} CanSig_TSC1_VDR;

/* TC1 (source address 0x05) -> TCU. AMT requested gear / mode. */
typedef struct {
    uint8 transmissionRequestedGear;
    uint8 transmissionMode1;
    uint8 amModeSwitch;
    uint8 mPlusSwitch;
    uint8 mMinusSwitch;
} CanSig_TC1;

#endif
