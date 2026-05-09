# TGW Routing — Detailed Code Design

**Companion to:** TGW-SWA-RTG-001 (architecture & safety concept)
**Tree:**
```
src/
  Common/   Std_Types.h   Compiler.h
  PduR/     PduR.{h,c}    PduR_Types.h   PduR_Cfg.{h,c}
  Safety/   SafetyMgr.{h,c}   SecOC_Gate.{h,c}
            DeadlineMonitor.{h,c}   E2E_Wrapper.{h,c}   Dem.h
  OS/       Partition.h
test/       test_pdur_routing.c   Makefile
```
The host build (`cd test && make run`) compiles the dispatcher, safety
manager, SecOC gate and deadline monitor against a mock lower-layer and
runs eight property tests. On the target the same C is built into the
ASIL OS-Application by the project's BSW build (no source change).

---

## 1. Module map

| Module | Files | Safety class | Role |
|---|---|---|---|
| **PduR** | `src/PduR/*` | ASIL-B(D) | Single point of routing. Static, table-driven. Allow-list only. |
| **SafetyMgr** | `src/Safety/SafetyMgr.*` | ASIL-B(D) | Routing-table CRC self-test, latched Safe State flag. |
| **SecOC_Gate** | `src/Safety/SecOC_Gate.*` | ASIL-B | Off-board ingress filter: SecOC verify → token bucket → cmd allow-list. |
| **DeadlineMonitor** | `src/Safety/DeadlineMonitor.*` | ASIL-B | Per-route latency budget supervision (wait-free slot ring). |
| **E2E_Wrapper** | `src/Safety/E2E_Wrapper.*` | ASIL-D | Sender/receiver CRC + counter (P02/P05/P06). The independent leg of the ASIL decomposition. |
| **Dem** | `src/Safety/Dem.h` | (header only) | Event-id space; reports failures with one event per fault class so that IDS and FuSa diagnostics share evidence. |
| **Partition** | `src/OS/Partition.h` | (config) | OS-Application split (`OsApp_ASIL` / `OsApp_QM`) and IOC channels. |

---

## 2. Routing table (`PduR_Cfg.c`)

Statically initialised array of `PduR_RouteEntryType`. Each entry binds
a source `(Module, PduId)` to N destinations and carries the per-route
safety attributes that drive the runtime checks:

```
SrcPduId | SrcModule | Class      | ASIL | E2E   | IngressGate                       | Budget
0x011    | CanIf     | IF_TO_IF   | D    | P02   | NONE                              | 1 ms
0x010    | CanIf     | IF_TO_IF   | D    | P05   | NONE                              | 1 ms
0x040    | CanIf     | IF_TO_IF   | A    | P01   | NONE                              | 50 ms
0x090    | EthIf     | IF_TO_TP   | B    | NONE  | SECOC                             | (n/a)
0x0A0    | SoAd      | IF_TO_IF   | B    | P06   | SECOC + RATELIMIT + ALLOWLIST     | 100 ms
```

`PduR_SrcLookup[]` is a flat O(1) source-id → table-index map. The
production config tool emits a perfect hash; the design retains the
allow-list semantics: an unmatched key returns `NULL` and is dropped.

---

## 3. Dispatcher control flow (`PduR.c::PduR_RouteCommon`)

Exactly one path; every guard precedes the next, no early shortcuts:

1. **Module ready & not in Safe State** — otherwise return silently.
2. **Null pointer guard** — Dem `0xC100`.
3. **Route lookup** — unmatched key ⇒ Dem `0xC101`, `droppedNoRoute++`.
4. **Length guard** — `SduLength > MaxPduLength` ⇒ Dem `0xC102`,
   `droppedLength++`. Defends destination RAM and downstream framing.
5. **Ingress gate** — only invoked if the route requests it. The gate
   reports its own precise Dem reason (`0xC120..0xC123`).
6. **Deadline window opens** — non-blocking slot acquisition.
7. **Fan-out transmit** — independent per destination; one TX failure
   does not abort the others (FFI between routes).
8. **Deadline window closes** — overshoot ⇒ Dem `0xC110`.
9. **Stats update** — exactly once per RX indication.

The function is non-recursive, has bounded loops (`DestCount` is
`uint8`), and performs no heap allocation. These are explicit
preconditions for the ASIL-B(D) coding standard (MISRA C:2012 +
project rules).

---

## 4. SecOC ingress gate (`SecOC_Gate.c`)

Three filters in series for off-board (WAN) PDUs:

```
SecOC_VerifyAuthenticator()      AES-128 CMAC + 32-bit freshness counter
        │ E_NOT_OK -> Dem 0xC120 (SECOC_AUTH_FAIL)
        ▼
TokenBucket_Take(&Bucket_Telematics)   capacity=5, refill=1/ms
        │ exhausted -> Dem 0xC122 (SECOC_RATELIMIT)
        ▼
AllowList_Contains(cmdId)        4-entry static list
        │ unlisted -> Dem 0xC123 (SECOC_NOT_ALLOW)
        ▼
        E_OK -> hand back to PduR for fan-out
```

Properties:

- The gate **never** modifies the SDU; it is a pure decision function.
- The token bucket uses `OsTime_GetMs()` only — no critical section,
  no shared state across routes (each route owns its bucket).
- The allow-list lives in `const` flash; it is part of the routing
  configuration image and therefore covered by `SafetyMgr` CRC.
- Failures are reported with **distinct** Dem ids so IDS and FuSa
  diagnostics can correlate on the same evidence trail without losing
  the precise reason.

---

## 5. Deadline monitor (`DeadlineMonitor.c`)

A small ring of 32 slots, each guarded by an `atomic_flag`. `Start`
performs a wait-free `test_and_set` to claim a slot; `Stop` clears it.
A cyclic `MainFunction` reaps slots whose budget elapsed without a
matching `Stop` (e.g. a TX that was lost in confirmation). The ring
size is derived from the worst-case in-flight count in TGW-TIM-001
plus 30 % headroom; exhaustion downgrades to *best effort* — the PDU
is still forwarded, only deadline policing is skipped, with a stat
counted. This keeps the routing path itself wait-free.

---

## 6. E2E wrapper (`E2E_Wrapper.c`)

Thin profile dispatcher in front of the qualified E2E Library
(developed to ASIL-D). The implementation here reproduces:

- Profile P01/P02 — 8-bit CRC (poly 0x2F, H2F variant) + 4-bit
  counter, allowing at most 1 lost frame.
- Profile P05/P06 — CRC-16/CCITT + 8-bit counter, allowing at most 3
  lost frames.

The protect/check functions are called by **application SW-Cs** at the
sender and receiver — not by the gateway. This is precisely how the
ASIL-D residual risk left by the ASIL-B(D) PduR is closed: any
corruption, drop, replay, or mis-route introduced inside the gateway
is caught at the receiver as a CRC or counter failure and translates
into a route-specific application reaction (degrade, hold last value,
…). After `E2E_DEM_THRESHOLD = 5` consecutive failures the wrapper
raises Dem `0xC130` to feed the diagnostic & cybersecurity pipeline.

---

## 7. Safety Manager (`SafetyMgr.c`)

- `SafetyMgr_VerifyRouteTableCrc` — CRC-32 (poly 0xEDB88320, refl) over
  the routing-table image. The expected value `PDUR_ROUTE_TABLE_CRC32`
  is emitted by the configuration tool; mismatch ⇒ Safe State.
- `SafetyMgr_EnterSafeState` — **latched**; the only way out is a hard
  reset. In the real ECU this transitions WdgM to `SAFE` mode, drops
  CAN controllers to listen-only, disables SoAd ingress, and lights a
  dashboard tell-tale.
- `SafetyMgr_IsSafeState` — consulted by `PduR_RouteCommon` on every
  call; in Safe State only the diagnostic path remains active.

---

## 8. OS partitioning (`OS/Partition.h`)

Two OS-Applications on the safety MCU:

- `OsApp_ASIL` (trusted): PduR, SecOC, E2E, SafetyMgr, WdgM, NM.
  Read/write access to safety RAM and CAN/Ethernet driver registers.
- `OsApp_QM` (non-trusted): UDS app layer, DoIP, telematics command
  parser. Reads from the safety RAM only via IOC; cannot touch driver
  registers.

The MPU is configured from this header. A QM partition fault is
caught by the OS `ProtectionHook`, which kills the QM partition only;
the ASIL partition continues routing — this is the structural FFI
guarantee for SG-RTG-04.

`TelematicsCmd_IocMsg` is the only data structure that crosses the
boundary; size and direction are statically defined so the IOC channel
itself becomes part of the safety-relevant boundary specification.

---

## 9. Dem event-id space (excerpt)

| Event | Class |
|---|---|
| `0xC100` | PduR null pointer (defensive) |
| `0xC101` | PduR no route (allow-list rejection) |
| `0xC102` | PduR length |
| `0xC103` | PduR TX busy / lower-layer reject |
| `0xC110` | Deadline miss |
| `0xC120` | SecOC authenticator fail |
| `0xC121` | SecOC freshness fail |
| `0xC122` | SecOC rate-limit |
| `0xC123` | SecOC not-allow-listed |
| `0xC130` | E2E check fail (sustained) |
| `0xC140` | Routing-table CRC fail |

Each event maps 1:1 to a UDS DTC and to a CSMS log code; this is what
makes a single failure traceable across cybersecurity monitoring and
functional-safety diagnostics without duplicate logging logic.

---

## 10. Verification (host, demonstrative)

`test/test_pdur_routing.c` runs eight property tests:

```
PASS T_init_succeeds_with_valid_table
PASS T_unknown_source_pdu_is_dropped_and_counted
PASS T_oversize_pdu_is_rejected
PASS T_normal_route_forwards_to_destination
PASS T_off_board_route_blocks_invalid_secoc
PASS T_off_board_route_blocks_unlisted_cmd
PASS T_off_board_route_accepts_listed_cmd
PASS T_rate_limiter_drops_burst (accepted=3, gated=19)
```

These cover, respectively: startup integrity; allow-list semantics
(`SG-RTG-01`/`-04` partial); RAM-safety length guard; happy-path fan-
out; SecOC-only gate (`SG-RTG-03`); allow-list rejection; allow-list
acceptance; rate-limit (`SG-RTG-03`).

The on-target verification adds: MC/DC ≥ 95 %, fault-injection on
every ASIL route (bit flip / drop / replay / reorder), FMEDA per
TGW-FME-001.

---

## 11. Coding-rule compliance summary

- MISRA C:2012 mandatory + required, project deviations registered.
- No dynamic memory; no recursion; bounded loops only.
- Single `return` policy is **not** applied — guard-clause style is
  the project rule for routing code (improves readability of the
  decision tree above; reviewed and accepted by the safety assessor).
- `volatile` is used only on the Safe-State flag; everything else
  relies on the AUTOSAR scheduler model.
- Every cross-module symbol is declared in a `.h` and given an
  AUTOSAR memory class via the `FUNC()` macro; on host builds the
  classes resolve to nothing (see `Compiler.h`).
