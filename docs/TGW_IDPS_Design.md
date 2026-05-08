# TGW IDPS — Cybersecurity Design

**Document:** TGW-CSEC-IDPS-001
**Standards basis:** ISO/SAE 21434:2021, UN R155 §7.3.7, AUTOSAR IdsM (R20-11+)
**Cybersecurity Assurance Level:** CAL-3
**Companion to:** TGW-SWA-RTG-001 (architecture), TGW-CODE-001 (code design)

---

## 1. Why IDPS in addition to SecOC + safety mechanisms

SecOC, the routing allow-list, and E2E close the in-band integrity
holes — they reject *individual* malformed or unauthenticated PDUs.
What they do not do is correlate events, persist evidence, or feed a
backend SOC. UN R155 §7.3.7 and ISO/SAE 21434 §RQ-09-04 require the
gateway to:

- detect cybersecurity-relevant events,
- log them with sufficient context for forensic analysis,
- report them off-board to the V-SOC,
- and react autonomously when threat thresholds are crossed.

The IDPS is the module that does all four. It sits *across* PduR,
SecOC, the diag stack, and the boot/flash drivers; every layer feeds
events into a single funnel.

---

## 2. Architecture

```
   +-----------+   +-----------+   +-----------+   +---------------+
   |  PduR     |   | SecOC_Gate|   |  AnomalyD |   |  Boot / Diag  |
   |  sensors  |   |  sensor   |   |  sensor   |   |   sensors     |
   +-----+-----+   +-----+-----+   +-----+-----+   +-------+-------+
         |               |               |                 |
         +-------+-------+-------+-------+--------+--------+
                                 |
                       IdsM_ReportSecurityEvent
                                 |
                  +--------------v--------------+
                  |  Aggregation filter         |  per-id sliding window
                  |  + censoring                |  (1 s, threshold 10/win)
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  |  Policy lookup (CRC'd)      |  SEv -> action
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  |  Prevention engine          |  drop / throttle /
                  |  (engages action)           |  block route /
                  |                             |  isolate src /
                  |                             |  safe state
                  +--------------+--------------+
                                 |
                  +--------------v--------------+
                  |  Security Event Memory      |  64-slot RAM ring
                  |  (RAM ring -> NVM-persist)  |  -> NVM (signed,
                  +--------------+--------------+     append-only)
                                 |
                  +--------------v--------------+
                  |  IdsR (off-board reporter)  |  DoIP / MQTT-SN
                  +-----------------------------+  to V-SOC
```

### 2.1 Sensor inventory

| Sensor | Lives in | Detection method | Emits |
|---|---|---|---|
| Allow-list | `PduR.c` | Specification | `PDUR_NO_ROUTE` |
| Length guard | `PduR.c` | Specification | `PDUR_LEN` |
| Deadline monitor | `DeadlineMonitor.c` | Specification | `DEADLINE_MISS` |
| SecOC verify | `SecOC_Gate.c` | Cryptographic | `SECOC_AUTH_FAIL`, `SECOC_FRESH_FAIL` |
| Rate limiter | `SecOC_Gate.c` | Anomaly | `SECOC_RATELIMIT` |
| Cmd allow-list | `SecOC_Gate.c` | Specification | `SECOC_NOT_ALLOW` |
| Frequency anomaly | `AnomalyDetector.c` | Anomaly (spec) | `FREQ_ANOMALY` |
| E2E check | `E2E_Wrapper.c` (receiver) | Integrity | `E2E_CHECK_FAIL` |
| Routing-table CRC | `SafetyMgr.c` | Integrity | `ROUTE_TABLE_CRC` |
| UDS auth | (Diag stack) | Stateful protocol | `DIAG_AUTH_FAIL` |
| Secure boot | (Boot ROM) | Cryptographic | `SECURE_BOOT_FAIL` |

The detection is **layered**: the same attack often trips several
sensors (a SecOC replay also breaks counter monotonicity, a no-route
PDU also fails the length guard if it carries an oversized frame).
This redundancy is intentional — single-point sensor evasion does not
hide an attacker.

### 2.2 Why specification-based, not learning

The frequency / inter-arrival baseline is *static* and emitted by the
network designer (CAN matrix → ARXML). A learning detector cannot be
qualified to a CAL because an attacker who controls the learning
window can drift the baseline. The static table is co-signed with
the routing table and verified at startup by `SafetyMgr`.

---

## 3. Policy table (`IdsM.c::IdsM_Policy`)

| SEv | Severity | Prevention action |
|---|---|---|
| `PDUR_NO_ROUTE` | LOW | DROP |
| `PDUR_LEN` | MEDIUM | DROP |
| `PDUR_TX_BUSY` | INFO | NONE (observe only) |
| `DEADLINE_MISS` | HIGH | NONE (downstream reaction) |
| `SECOC_AUTH_FAIL` | HIGH | DROP |
| `SECOC_FRESH_FAIL` | HIGH | DROP |
| `SECOC_RATELIMIT` | MEDIUM | THROTTLE |
| `SECOC_NOT_ALLOW` | HIGH | DROP |
| `E2E_CHECK_FAIL` | HIGH | NONE (receiver app reacts) |
| `ROUTE_TABLE_CRC` | CRITICAL | SAFE_STATE |
| `FREQ_ANOMALY` | HIGH | BLOCK_ROUTE |
| `PAYLOAD_SIGNATURE` | HIGH | DROP |
| `DIAG_AUTH_FAIL` | HIGH | ISOLATE_SRC |
| `SECURE_BOOT_FAIL` | CRITICAL | SAFE_STATE |

Properties of the policy:

- **CRITICAL bypasses the aggregation filter.** A storm of crit
  events still produces one report per occurrence, because at that
  severity we want every single one in the V-SOC trail.
- **DROP/THROTTLE are local actions** — applied on the offending PDU
  only. The dispatcher's allow-list semantics already drop the PDU,
  so the action is mostly a *label* on the SEv for the V-SOC; it
  becomes meaningful when added to a custom sensor that does not
  implicitly drop.
- **BLOCK_ROUTE / ISOLATE_SRC are sticky** — they survive across the
  attacker's next attempt, until either an authenticated diag command
  clears them or the ECU is reset.
- **SAFE_STATE is latched** — the only way out is a reset, by design
  (no MIM-attack-driven recovery).

---

## 4. Aggregation, censoring, anti-DoS

A naive design would forward every sensor hit to the V-SOC in real
time. That is itself a DoS vector — an attacker who can trigger one
sensor cheaply can flood the WAN uplink. Therefore the IdsM applies:

- **Per-id sliding window** (1 s) with a threshold of 10 events.
- Above threshold, **sample 1 event in N (N = 8)** and tag it with
  the cumulative drop count so the V-SOC sees the storm magnitude.
- CRITICAL events bypass this filter (above).
- The ring buffer is 64 deep; if full, the *oldest* event is
  evicted. Evidence freshness is preferred over completeness because
  recent attack steps are more useful than minute-old preludes.

This is the "censoring" stage in the AUTOSAR IdsM specification.

---

## 5. Persistence and reporting (IdsR)

`IdsR_Forward()` is the off-board transport boundary. On target:

- Each event is signed with an HSM-held key (Ed25519) and persisted
  to a dedicated NVM block in append-only mode. Anti-rollback is
  enforced by a monotonic counter co-stored in the HSM secure
  storage.
- Transport selection is dynamic: DoIP (Ethernet) when the workshop
  tester is connected, MQTT-SN over LTE for in-field reporting,
  CCM-encrypted with a per-vehicle session key.
- If neither transport is available, events stay in NVM and are
  drained at the next opportunity. The append-only NVM block is
  sized for ≥ 24 h of attack-storm-rate events at the censored rate.

---

## 6. Integration into the dispatcher

The dispatcher consults the IdsM **before** any other check:

```
PduR_RouteCommon():
    if (!Initialised || SafetyMgr.SafeState)        return;
    if (info == NULL)                                Dem(NULL_PTR), return;
    if (IdsM_IsSourceIsolated(srcMod))               drop, return;
    if (IdsM_IsRouteBlocked(srcId))                  drop, return;
    route = FindRoute(srcMod, srcId);
    if (route == NULL)                               Dem + IdsM(NO_ROUTE), drop;
    if (length > MaxPduLength)                       Dem + IdsM(LEN), drop;
    if (AnomalyDetector_Check(srcId))                IdsM(FREQ_ANOMALY), drop;
    if (route->IngressGate)
        if (SecOC_Gate_Check fails)                  (gate already raised IdsM), drop;
    DeadlineMon_Start();
    fan-out;
    DeadlineMon_Stop();
```

Two consequences:

1. The IDPS prevention checks (`IsSourceIsolated`, `IsRouteBlocked`)
   are at the **front** of the function, ahead of the route table
   lookup. An active attack that has already triggered one of these
   reactions is short-circuited even if a later check has a bug.
2. Every `IdsM_ReportSecurityEvent` site is paired with a `Dem`
   report. The single-fault → two-channel model keeps the FuSa
   diagnostic trail and the cybersecurity evidence trail aligned
   without duplicating sensor logic.

---

## 7. Threat model coverage (excerpt)

| STRIDE / threat | Sensor that catches it | Reaction |
|---|---|---|
| Spoof an ECU identity | SecOC verify | Drop + log |
| Replay a captured frame | SecOC freshness counter | Drop + log |
| Inject a frame from compromised body ECU | Allow-list (no route) | Drop + log |
| Frequency injection / message storm | Anomaly detector | Block route + log |
| Off-board OTA tunnel injection | SecOC + cmd allow-list + rate limit | Drop / throttle + log |
| Diagnostic auth bypass | UDS state machine + auth | Isolate source + log |
| Flash / boot tamper | Secure boot, route-table CRC | Safe state |
| Side-channel / fuzz inputs | Length guard, type checks | Drop + log |
| DoS the V-SOC via floods | IdsM aggregation + censoring | Sample + tag |

Threats deliberately *out of scope* for this module: side-channel
attacks against the HSM (covered by HSM hardening), supply-chain
attacks on the BSW (covered by build/release process), and physical
debug-port tamper (covered by JTAG fuses).

---

## 8. Verification (host, demonstrative)

`test/test_pdur_routing.c` adds five IDPS-specific tests on top of
the original eight:

```
PASS T_idsm_reports_unknown_source       -- sensor -> IdsM path
PASS T_anomaly_detector_blocks_replay_storm
                                         -- BLOCK_ROUTE policy + sticky enforcement
PASS T_isolated_source_short_circuits    -- ISOLATE_SRC policy
PASS T_idsm_drains_to_idsr               -- end-to-end pipe to off-board
PASS T_critical_event_escalates_to_safe_state
                                         -- SAFE_STATE policy
```

On-target verification adds:

- **Penetration tests** on every sensor with bench-recorded attack
  traces (SecOC replay corpus, CAN injection corpus, malformed
  DoIP).
- **Censoring measurement**: ensure the V-SOC link is never burdened
  beyond 1 % of WAN bandwidth under a full-rate attack.
- **NVM endurance**: 1 000-cycle attack-storm test against the
  signed append-only block; verify rotation policy.
- **Forensic replay**: drain SEM dumps into the V-SOC schema
  validator; ensure every event reproduces the original sensor
  context.

---

## 9. Coding-rule and CAL compliance

- All IDPS code is in the **ASIL OS-Application** (`OsApp_ASIL`); the
  QM partition cannot see or write the policy table, the SEM ring,
  or the prevention state.
- The policy table and the anomaly baseline are in `const` flash and
  are part of the `SafetyMgr` CRC scope.
- The IDPS code is qualified to **CAL-3** (matching the highest
  cybersecurity-relevant asset on the gateway). MISRA C:2012 + the
  project's secure-coding addenda apply.
- No dynamic memory; bounded loops; non-blocking from RX ISR
  context.
