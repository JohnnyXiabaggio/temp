#ifndef PDUR_H
#define PDUR_H

#include "PduR_Types.h"
#include "Compiler.h"

/* Initialise PduR. Verifies routing table integrity (CRC) and clears
 * runtime statistics. Must be called from the trusted ASIL partition. */
FUNC(Std_ReturnType, PDUR_CODE) PduR_Init(void);

/* Lower-layer indication entry points: invoked by the source If/TP
 * module when a PDU has been received. */
FUNC(void, PDUR_CODE) PduR_CanIfRxIndication (PduIdType srcPduId, const PduInfoType *info);
FUNC(void, PDUR_CODE) PduR_LinIfRxIndication (PduIdType srcPduId, const PduInfoType *info);
FUNC(void, PDUR_CODE) PduR_EthIfRxIndication (PduIdType srcPduId, const PduInfoType *info);
FUNC(void, PDUR_CODE) PduR_SoAdIfRxIndication(PduIdType srcPduId, const PduInfoType *info);

/* Statistics for diagnostics / FuSa argumentation. */
typedef struct {
    uint32 routedOk;
    uint32 droppedNoRoute;
    uint32 droppedGate;
    uint32 droppedLength;
    uint32 droppedDeadline;
    uint32 droppedTxBusy;
} PduR_StatsType;

FUNC(const PduR_StatsType *, PDUR_CODE) PduR_GetStats(void);

#endif
