#ifndef PDUR_CFG_H
#define PDUR_CFG_H

#include "PduR_Types.h"

#define PDUR_ROUTE_TABLE_SIZE   5u

/* Compile-time CRC of the routing table image, computed by the config
 * generator and verified at startup by the Safety Manager. */
#define PDUR_ROUTE_TABLE_CRC32  0xA3D24F71u

extern const PduR_RouteEntryType PduR_RouteTable[PDUR_ROUTE_TABLE_SIZE];

/* Source PDU ID space is dense per source module; the lookup index is
 * a flat array indexed by source-side handle id. UINT16_MAX = no route. */
#define PDUR_SRC_INDEX_SIZE     256u
extern const uint16 PduR_SrcLookup[PDUR_SRC_INDEX_SIZE];

#endif
