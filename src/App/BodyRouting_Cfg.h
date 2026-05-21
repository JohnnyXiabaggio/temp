/* BodyRouting module configuration (AUTOSAR Cfg header pattern).
 * Spec: YG-D18-2026-1042, J107 & J169 T-Gateway, 2026-04-22. */

#ifndef BODYROUTING_CFG_H
#define BODYROUTING_CFG_H

#include "Std_Types.h"

/* --- Module identification ---------------------------------------- */
/* Vendor-specific module IDs occupy range 0x8000..0xFFFF. */
#define BODYROUTING_MODULE_ID        ((uint16)0x8010u)
#define BODYROUTING_INSTANCE_ID      ((uint8) 0x00u)
#define BODYROUTING_SW_MAJOR_VERSION ((uint8) 1u)
#define BODYROUTING_SW_MINOR_VERSION ((uint8) 0u)
#define BODYROUTING_SW_PATCH_VERSION ((uint8) 0u)

/* --- Development-error detection switch --------------------------- */
#define BODYROUTING_DEV_ERROR_DETECT STD_ON

/* --- DET API identifiers ------------------------------------------ */
#define BODYROUTING_API_ID_INIT          ((uint8)0x01u)
#define BODYROUTING_API_ID_MAIN_FUNCTION ((uint8)0x02u)
#define BODYROUTING_API_ID_ON_LIN_MSW    ((uint8)0x03u)
#define BODYROUTING_API_ID_ON_LIN_HANDLE ((uint8)0x04u)
#define BODYROUTING_API_ID_GET_STATS     ((uint8)0x05u)

/* --- DET error identifiers ---------------------------------------- */
#define BODYROUTING_E_UNINIT       ((uint8)0x01u)
#define BODYROUTING_E_NULL_PTR     ((uint8)0x02u)
#define BODYROUTING_E_PARAM_CONFIG ((uint8)0x03u)

/* --- Timing -------------------------------------------------------- */
/* LIN staleness threshold.  Must be >= gateway reset window (~300 ms)
 * so that downstream ECUs detect the gap via their own node-timeout
 * supervision rather than seeing garbage CAN values (spec Q&A #1). */
#define BODYROUTING_LIN_STALE_MS ((uint32)300u)

/* --- Retarder gear range ------------------------------------------ */
#define BODYROUTING_RETARDER_GEAR_MAX ((uint8)5u)
#define BODYROUTING_RETARDER_GEAR_OFF ((uint8)0u)

/* --- CCVS_VCU cruise-enable signal constants (J1939) --------------- */
#define BODYROUTING_CC_ENABLE_DISABLED      ((uint8)0x00u)  /* Disabled        */
#define BODYROUTING_CC_ENABLE_ENABLED       ((uint8)0x01u)  /* Enabled         */
#define BODYROUTING_CC_ENABLE_NOT_AVAILABLE ((uint8)0x03u)  /* Not Available   */

/* --- TSC1_VDR signal constants (J1939, binary values) -------------- */
#define BODYROUTING_OVERRIDE_DISABLED    ((uint8)0x00u)  /* b00: off         */
#define BODYROUTING_OVERRIDE_TORQUE_CTRL ((uint8)0x02u)  /* b10: torque ctrl */
#define BODYROUTING_OVERRIDE_PRIO_LOW    ((uint8)0x03u)  /* b11: low prio    */
#define BODYROUTING_TSC1_CHECKSUM_NONE   ((uint8)0xFFu)  /* byte 8: no CRC   */

#endif
