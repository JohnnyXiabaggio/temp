#ifndef PARTITION_H
#define PARTITION_H

#include "Std_Types.h"

/* AUTOSAR OS-Application partitioning.
 *
 * The TGW runs two partitions on the safety MCU:
 *   - OsApp_ASIL : trusted, hosts PduR, SecOC, E2E, SafetyMgr,
 *                  WdgM, NM. Has full access to safety RAM and to
 *                  CAN/Ethernet driver registers.
 *   - OsApp_QM   : non-trusted, hosts diag (UDS-app layer), DoIP,
 *                  telematics command parser. Read-only view of the
 *                  safety RAM through the IOC; cannot touch driver
 *                  registers directly.
 *
 * The MPU configuration is generated from this header by the OS
 * configuration tool. A QM partition fault is caught by the
 * ProtectionHook and handled by killing the offending partition; the
 * ASIL partition continues to route. */

typedef enum {
    OS_APP_ASIL = 0,
    OS_APP_QM   = 1
} OsAppId;

/* Inter-OS-Application Communication (IOC) channels.
 * One channel per data direction; size is statically defined. */

typedef struct {
    uint16 cmdId;
    uint16 length;
    uint8  data[32];
} TelematicsCmd_IocMsg;

extern Std_ReturnType IocSend_TelematicsCmd_QM_to_ASIL(const TelematicsCmd_IocMsg *m);
extern Std_ReturnType IocReceive_TelematicsCmd_ASIL   (TelematicsCmd_IocMsg *m);

/* ProtectionHook reaction: kill QM, keep ASIL. */
extern void Os_ProtectionHook_KillQmPartition(void);

#endif
