#ifndef ANOMALY_DETECTOR_H
#define ANOMALY_DETECTOR_H

#include "Std_Types.h"

/* Per-source frequency anomaly detector.
 *
 * For each monitored source PDU id we keep:
 *   - the configured nominal inter-arrival time T_nom (microseconds),
 *   - a tolerance band (low/high multipliers),
 *   - the last RX timestamp.
 *
 * On every RX the detector compares (now - lastRx) against the band:
 *   - too short  -> message-injection / replay-storm signature
 *   - too long   -> denial / spoof-of-silent-ECU signature
 *
 * Returns TRUE if the arrival is anomalous; the caller (PduR) raises
 * IDSM_SEV_FREQ_ANOMALY which is policy-mapped to BLOCK_ROUTE.
 *
 * The baseline T_nom is part of the static configuration emitted by
 * the network designer's DBC/ARXML; it is not learned online (a
 * learning detector cannot be qualified to any CAL because it admits
 * adversarial training). */

typedef struct {
    uint16 srcPduId;
    uint32 nominalUs;
    uint16 lowPct;       /* e.g. 50  -> reject if < 50% of nominal */
    uint16 highPct;      /* e.g. 200 -> reject if > 200% of nominal */
    uint32 lastRxUs;
    uint32 violationCnt;
} AnomalyEntry;

extern Std_ReturnType AnomalyDetector_Init(void);
extern boolean        AnomalyDetector_Check(uint16 srcPduId);

#endif
