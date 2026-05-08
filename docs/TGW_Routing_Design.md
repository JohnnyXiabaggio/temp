# TGW Software Routing Design — AUTOSAR Classic + ISO 26262

**Document:** TGW-SWA-RTG-001
**Revision:** 1.0
**Standard basis:** AUTOSAR Classic Platform R21-11, ISO 26262:2018
**Target ECU:** Telematics / Central Gateway (TGW)
**Highest ASIL allocated to routing path:** ASIL-B (with ASIL-D pass-through for chassis frames)

---

## 1. Scope

The TGW routes signals/PDUs between heterogeneous in-vehicle networks:

| Channel | Bus type | Domain | Typical ASIL of payload |
|---|---|---|---|
| CN0 | CAN-FD 2 Mbit/s | Powertrain | ASIL-D |
| CN1 | CAN-FD 2 Mbit/s | Chassis (brake/EPS) | ASIL-D |
| CN2 | CAN 500 kbit/s | Body | QM / ASIL-A |
| CN3 | LIN | Comfort | QM |
| ETH0 | 100BASE-T1 | ADAS / Backbone | ASIL-B |
| ETH1 | 1000BASE-T1 | Infotainment / Diag | QM |
| WAN | LTE (Telematics) | Off-board | QM (with ASIL-B gate) |

The routing function must be free from interference (FFI) per ISO 26262-9 §6 between mixed-criticality streams sharing the gateway.

---

## 2. AUTOSAR Architecture Allocation

```
+--------------------------------------------------------------+
|                     Application Layer (SW-Cs)                |
|   RoutingMgr_SWC | DiagRouter_SWC | SecOC_KeyMgr_SWC         |
+--------------------------------------------------------------+
|                              RTE                             |
+--------------------------------------------------------------+
| Services         | ECU Abstraction       | MCAL              |
|  - Com           |  - CanIf / LinIf      |  - Can / Lin      |
|  - PduR  <===    |  - EthIf / FrIf       |  - Eth / Fr       |
|  - CanTp/DoIP    |  - SoAd               |  - SPI/DIO        |
|  - SecOC         |                       |                   |
|  - IpduM         |                       |                   |
|  - CanNm/Nm      |                       |                   |
|  - Dcm/Dem       |                       |                   |
|  - WdgM, SBSW    |                       |                   |
+--------------------------------------------------------------+
```

**Routing core:** PDU Router (PduR) is the single point of routing for I-PDUs. Signal routing is intentionally avoided — it forces a Com unpack/pack cycle that breaks latency budgets and inflates the safety case.

---

## 3. Routing Topology

Three routing classes are defined:

### 3.1 Gateway routing (PduR_RoutingPath, no transformation)
1:1 or 1:N forwarding of an I-PDU from a source `If`-module to one or more destination `If`-modules. Used for all real-time control traffic (powertrain ↔ chassis, ADAS ↔ chassis).

### 3.2 Gateway routing with on-the-fly modification
Used when the destination network has different framing (e.g. CAN → CAN-FD payload split, or CAN → SOME/IP via SoAd). PduR holds a per-route transformer chain.

### 3.3 TP routing (CanTp ↔ DoIP)
Diagnostic flows (UDS, OTA chunks) are routed at the TP layer. PduR multiplexes on N_SA / N_TA addresses.

### 3.4 Routing path table (excerpt)

| RouteId | Src module | Src PDU | Dst module(s) | Class | ASIL | E2E profile | SecOC |
|---|---|---|---|---|---|---|---|
| 0x0010 | CanIf(CN1) | Brake_Status | EthIf(ETH0)/SoAd | If→TCP | D | P05 | — |
| 0x0011 | CanIf(CN0) | EngineTrq_Cmd | CanIf(CN1) | If→If | D | P02 | SecOC |
| 0x0040 | CanIf(CN2) | DoorLock_Req | LinIf(CN3) | If→If | A | P01 | — |
| 0x0090 | EthIf(ETH1) | OTA_Chunk | CanTp→CanIf(CN0) | TP | QM→D gate | — | SecOC |
| 0x00A0 | SoAd(WAN) | Telematics_Cmd | PduR-Filter→CanIf(CN2) | If→If (gated) | QM→B | P06 | SecOC |

---

## 4. Functional Safety Concept (ISO 26262)

### 4.1 Safety goals derived for routing
- **SG-RTG-01 (ASIL-D):** No corruption of a forwarded ASIL-D PDU shall remain undetected by the receiver.
- **SG-RTG-02 (ASIL-D):** No insertion, deletion, masquerade, re-ordering, or unintended repetition of ASIL-D PDUs.
- **SG-RTG-03 (ASIL-B):** Off-board (WAN) traffic shall not influence on-board safety-relevant behaviour without explicit authentication and rate-limiting.
- **SG-RTG-04 (ASIL-B):** A failure of a QM partition shall not block, delay beyond budget, or alter routing of ASIL ≥ B PDUs (FFI).

### 4.2 ASIL decomposition
PduR core dispatch is **developed to ASIL-B(D)**. The end-to-end protection that closes the residual risk to ASIL-D is implemented in the application SW-Cs at sender and receiver via the **E2E Library** (developed to ASIL-D). This is a textbook ASIL-D = ASIL-B(D) + ASIL-B(D) decomposition with independent E2E counters/CRCs.

```
   Sender SW-C (ASIL-D)                   Receiver SW-C (ASIL-D)
        |  E2E_Protect (CRC + Counter)         ^  E2E_Check
        v                                      |
   --[ RTE | Com | PduR (ASIL-B(D)) | CanIf | Can | Bus ]--> ... -->
```
Because E2E protection is end-to-end, any bit flip, drop, repetition, mis-route, or mis-ordering inside the gateway is detected by the receiver and treated as a safe-state trigger.

### 4.3 Freedom From Interference mechanisms

| Interference type | Mechanism |
|---|---|
| Memory (spatial) | OS-Application partitioning (per ISO 26262-6 Annex D); MPU-enforced; QM partition has no write access to ASIL stack RAM/ROM. |
| Timing/execution | Fixed-priority preemptive OS, ASIL routing tasks at higher priority than QM; Program Flow Monitoring via WdgM supervised entities; deadline monitoring per route. |
| Exchange of information | E2E on every ASIL ≥ B route; PduR rejects PDUs whose routing path is not configured (no implicit any-to-any). |
| Common-cause (HW) | Lock-step core or core self-test (SBST) on safety MCU; ECC on RAM; CRC on flash containing PduR config. |

### 4.4 Per-route safety mechanisms

1. **Static config integrity** — PduR routing table is in flash, CRC-32 protected, verified at startup by the Safety Manager. Mismatch ⇒ enter Safe State (only diagnostic comms permitted).
2. **E2E supervision** — `E2EXf` transformer chained in PduR for routes where the receiver cannot verify natively.
3. **SecOC** — All cross-domain ASIL routes carry an authenticator (CMAC AES-128 + freshness counter). Off-board ingress (WAN) is mandatorily SecOC-gated.
4. **Deadline monitoring** — Each ASIL route has `LatencyBudget_us`. PduR/EcuM time-stamps on RX, checks on TX. Violation ⇒ DTC + degrade.
5. **Buffer overflow handling** — Each route has a bounded queue. Overflow on a QM route never blocks an ASIL route (separate buffer pools).
6. **Gateway-only filter** — Allow-list only. The routing table is closed by default; an unknown CAN-ID is dropped and counted (Dem event).

### 4.5 Off-board gating (SG-RTG-03)
WAN ingress flows through a **gateway firewall SW-C** running in an ASIL-B partition that:
- verifies SecOC authenticator and freshness;
- enforces a token-bucket rate limiter per command class;
- maps only an explicit allow-list of telematics commands to internal PDUs;
- writes to a **diode buffer** (single-writer, single-reader) — internal-to-external direction only enabled for vehicle data on a separate path.

---

## 5. Module-Level Design

### 5.1 PduR configuration policy
- `PduRDevErrorDetect = TRUE` (development), `FALSE` for production with safety reporting via Dem.
- `PduRZeroCostOperation = FALSE` — we need the routing table indirection for the safety case.
- `PduRMetaDataSupport = TRUE` — required to carry SecOC freshness and source identifier across routing.
- One destination PDU per `PduRDestPdu` element; fan-out is multiple `PduRDestPdu` entries on the same source.
- Trigger transmit (TT) is forbidden on ASIL routes (no implicit retransmission).

### 5.2 CanIf
- Hardware acceptance filtering programmed from the routing table — PDUs without a route are rejected by the CAN controller, removing them from the CPU load budget.
- Separate HRH/HTH per ASIL class to avoid head-of-line blocking by QM traffic.
- Wakeup validation per ISO 11898-2; spurious wakeups on WAN are rate-limited.

### 5.3 SoAd / DoIP
- Static SoAd socket connections; no dynamic DNS/DHCP on safety paths.
- DoIP entity ID, source/target address checked against an allow-list before being handed to PduR.

### 5.4 Com
- Used **only** for the gateway's own application signals (NM, diag triggers). Pure routing does not traverse Com.

### 5.5 SecOC
- Profile: Authenticator-only mode (AES-128 CMAC truncated to 64 bit) on CAN-FD; full 128-bit on Ethernet where MTU allows.
- Freshness: 32-bit truncated counter, master held by Key Master ECU; gateway is a participant, not master.
- Verification failures are counted; threshold ⇒ Dem event ⇒ degraded routing for that source.

### 5.6 OS / Partitioning
- AUTOSAR OS, SC4 (scalability class 4) with memory protection.
- Two OS-Applications: `OsApp_ASIL` (trusted) and `OsApp_QM` (non-trusted).
- IOC (Inter-OS-Application Communication) for any data crossing the partition boundary; size and direction statically configured.

### 5.7 WdgM / Watchdog stack
- Supervised Entities for each routing task; alive, deadline, and logical (program flow) supervision.
- Hardware watchdog driven by an independent oscillator; window watchdog mode.

---

## 6. Timing Budget (per ASIL-D route, worst case)

| Stage | Budget |
|---|---|
| Bus → CanIf RX ISR | 50 µs |
| CanIf → PduR dispatch | 30 µs |
| PduR → CanIf TX request | 30 µs |
| TX queue + arbitration (CAN-FD 2 Mbit/s, 64-byte frame) | ≤ 400 µs |
| **End-to-end gateway latency** | **≤ 1 ms** |

Verified by hardware trace (Lauterbach) on the integration bench; documented in TGW-TIM-001.

---

## 7. Failure Mode & Diagnostics

| Failure | Detection | Reaction |
|---|---|---|
| Routing table CRC mismatch | Startup self-test | Stay in Safe State, only UDS reachable |
| E2E counter/CRC failure on RX | E2E_Check at consumer | DTC, degrade, sender-side limp home |
| SecOC verification fail (sustained) | SecOC counter > threshold | Dem event 0xC101, route disabled |
| Deadline miss on ASIL route | Time-stamp delta in PduR hook | Dem event 0xC110, NM degraded |
| QM partition crash | OS protection hook | Kill QM partition, ASIL routes continue |
| Bus-off on a CAN channel | CanSM | Standardised recovery, NM coordinated |

All failures are reported to Dem; safety-relevant DTCs are stored in a dedicated, ECC-protected NVM block.

---

## 8. Verification & Validation

- **Unit:** PduR routing dispatch, transformer chain, partition IOC — MC/DC coverage ≥ 95% on ASIL-B units (ISO 26262-6 §9.4.5).
- **Integration:** HIL with bus restbus simulation; fault-injection (bit flips, frame drops, repetitions, mis-orderings) on every ASIL route — receiver E2E must catch 100%.
- **Safety:** FMEDA achieving SPFM ≥ 90%, LFM ≥ 60% on the routing path (ASIL-B target), PMHF ≤ 100 FIT.
- **Penetration:** Off-board ingress fuzzing (WAN/DoIP) — no command shall reach an ASIL bus without valid SecOC.

---

## 9. Open Items

- Final selection of HSM (CMAC throughput must sustain WC SecOC load + 30% headroom).
- Decision on Ethernet TSN (802.1Qbv) usage for ADAS routes — reduces jitter but adds config complexity.
- Confirmation of ASIL decomposition independence argument with the safety assessor.
