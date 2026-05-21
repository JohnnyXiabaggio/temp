#ifndef LIN_SIGNALS_H
#define LIN_SIGNALS_H

#include "Std_Types.h"

/* ------------------------------------------------------------------ *
 *  LIN signal images (J107 / J169 - reference J100 LIN matrix)
 *  Source: YG-D18-2026-1042
 *  These structs are populated by LinIf RX after PduR delivers the
 *  L-PDU. Bitfields are deliberately *not* used (MISRA C:2012 Rule
 *  6.1; portability across compilers).
 * ------------------------------------------------------------------ */

/* MSWToVCU (LIN PDU id 0x02): steering-wheel cruise switches. */
typedef struct {
    uint8 ccAccModeSwitch;            /* 0..3 : 0x1 = press to toggle cruise on  */
    uint8 scrollUpButtonStatus;       /* 0..3 : 0x1/0x2 = SET+                   */
    uint8 scrollDownButtonStatus;     /* 0..3 : 0x1/0x2 = SET-                   */
    uint8 cruiseControlResumeSwitch;  /* 0..3 : 0x1 = Resume                     */
    uint8 offSwitch;                  /* 0..3 : 0x1 = press to toggle cruise off */
    uint8 valid;                      /* set by LIN RX, cleared on timeout       */
} LinSig_MSWToVCU;

/* HandleToVCU (LIN PDU id 0x01): combined handle - aux brake + AMT. */
typedef struct {
    /* Aux-brake gear (retarder or engine brake, distinguished by
     * the handle part-number identified via UDS 102A). */
    uint8 auxiliaryBrakeGear;         /* 0..5 : off/恒速/制动1..4              */

    /* AMT requested gear & mode (only meaningful on AMT vehicles --
     * see PartConfig.amtConfigCode != 0). */
    uint8 transmissionRequestedGear;  /* signed J1939 gear, raw byte           */
    uint8 transmissionMode1;          /* mode select                            */
    uint8 amModeSwitch;               /* A/M toggle                             */
    uint8 mPlusSwitch;                /* 0/1                                    */
    uint8 mMinusSwitch;               /* 0/1                                    */
    uint8 valid;                      /* set by LIN RX, cleared on timeout       */
} LinSig_HandleToVCU;

#endif
