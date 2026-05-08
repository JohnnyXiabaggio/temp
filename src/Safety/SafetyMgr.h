#ifndef SAFETY_MGR_H
#define SAFETY_MGR_H

#include "Std_Types.h"
#include "PduR_Types.h"

typedef enum {
    SAFE_STATE_REASON_NONE = 0u,
    SAFE_STATE_REASON_ROUTE_TABLE_CRC,
    SAFE_STATE_REASON_PARTITION_FAULT,
    SAFE_STATE_REASON_WDG_TIMEOUT,
    SAFE_STATE_REASON_HSM_FAULT
} SafetyMgr_ReasonType;

extern Std_ReturnType SafetyMgr_VerifyRouteTableCrc(
        const PduR_RouteEntryType *table,
        uint32 sizeBytes,
        uint32 expectedCrc);

extern void    SafetyMgr_EnterSafeState(SafetyMgr_ReasonType reason);
extern boolean SafetyMgr_IsSafeState(void);

#endif
