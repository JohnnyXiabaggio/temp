#ifndef DEM_H
#define DEM_H

#include "Std_Types.h"

typedef uint16 Dem_EventIdType;
typedef uint8  Dem_EventStatusType;

#define DEM_EVENT_STATUS_PASSED  ((Dem_EventStatusType)0u)
#define DEM_EVENT_STATUS_FAILED  ((Dem_EventStatusType)1u)

/* Routing-related events (excerpt from the project event ID space). */
#define DEM_EVT_PDUR_NULL_PTR     ((Dem_EventIdType)0xC100u)
#define DEM_EVT_PDUR_NO_ROUTE     ((Dem_EventIdType)0xC101u)
#define DEM_EVT_PDUR_LEN          ((Dem_EventIdType)0xC102u)
#define DEM_EVT_PDUR_TX_BUSY      ((Dem_EventIdType)0xC103u)
#define DEM_EVT_DEADLINE_MISS     ((Dem_EventIdType)0xC110u)
#define DEM_EVT_SECOC_AUTH_FAIL   ((Dem_EventIdType)0xC120u)
#define DEM_EVT_SECOC_FRESH_FAIL  ((Dem_EventIdType)0xC121u)
#define DEM_EVT_SECOC_RATELIMIT   ((Dem_EventIdType)0xC122u)
#define DEM_EVT_SECOC_NOT_ALLOW   ((Dem_EventIdType)0xC123u)
#define DEM_EVT_E2E_CHECK_FAIL    ((Dem_EventIdType)0xC130u)
#define DEM_EVT_ROUTE_TABLE_CRC   ((Dem_EventIdType)0xC140u)

extern Std_ReturnType Dem_ReportErrorStatus(Dem_EventIdType id, Dem_EventStatusType st);

#endif
