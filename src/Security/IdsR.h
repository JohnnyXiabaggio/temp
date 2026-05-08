#ifndef IDSR_H
#define IDSR_H

#include "Std_Types.h"
#include "Ids_Types.h"

/* Off-board reporter. On target, IdsR.c implements:
 *   - persistence to NVM (anti-rollback: append-only, signed)
 *   - serialisation to a CSMS V-SOC schema
 *   - transport via DoIP (over Ethernet) or MQTT-SN (over LTE),
 *     whichever the connectivity manager selects.
 * Returns E_OK if the entry was queued for transmission, E_NOT_OK if
 * the transport is busy -- the caller (IdsM_MainFunction) retries
 * next cycle, so events are never silently lost. */
extern Std_ReturnType IdsR_Forward(const IdsM_SemEntryType *e);

#endif
