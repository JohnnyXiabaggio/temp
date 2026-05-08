#ifndef SECOC_GATE_H
#define SECOC_GATE_H

#include "Std_Types.h"
#include "PduR_Types.h"

/* Verify a PDU presented at the gateway ingress against the policy
 * implied by the route's IngressGate field:
 *   - SECOC                           : authenticator + freshness only
 *   - SECOC_RATELIMIT_ALLOWLIST       : as above + token-bucket + cmd
 *                                       allow-list (used for off-board
 *                                       ingress on the WAN channel).
 * Returns E_OK if the PDU is permitted, E_NOT_OK otherwise.
 *
 * Failures are reported to Dem with a precise reason code so that
 * cybersecurity monitoring (IDS) and FuSa diagnostics share the same
 * evidence trail.
 */
extern Std_ReturnType SecOC_Gate_Check(const PduR_RouteEntryType *route,
                                       const PduInfoType         *info);

#endif
