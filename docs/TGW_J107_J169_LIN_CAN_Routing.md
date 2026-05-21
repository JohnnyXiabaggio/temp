# J107 & J169 — LIN ↔ CAN Routing Implementation

**Source spec:** YG-D18-2026-1042 (J107&J169 车型 T 网关开发逻辑交流会议纪要, 2026-04-22)
**Implementing module:** `src/App/BodyRouting.{h,c}`
**LIN matrix reference:** J100 LIN matrix (per spec page 5)
**Tests:** `test/test_lin_can_routing.c` — 10 cases, all green.

The body-domain routing is **signal-level**, not pure PDU pass-
through, because the spec requires a cruise state machine, a
retarder gear→torque table, and 102A part-number discrimination. It
runs as an SW-C in the ASIL OS-Application, on top of PduR and Com.

```
+----------+   LIN     +---------+    Com signals    +----------+
| LIN bus  | --------> | LinIf + | -----------------> | Body-    |
|          |  0x01,    |  PduR   |    LinSig_*        | Routing  |
|          |  0x02     |         |                   |  SW-C    |
+----------+           +---------+                   +-----+----+
                                                            |
                                          Com signals       v
                                          CanSig_*    +----------+    +-----+
                                                      |  Com +   | -> | EMS |
                                                      |  PduR    | -> | RCU |
                                                      | + CanIf  | -> | TCU |
                                                      +----------+    +-----+
                                                             CAN
```

---

## 1. Routings implemented

### 1.1 Cruise (普通巡航控制) — spec §1, page 1

| LIN signal (MSWToVCU, 0x02) | Press condition | CAN signal (CCVS_VCU, SA 0x05 → EMS) |
|---|---|---|
| `CC/ACC Mode Switch = 0x1` | rising edge | `Cruise Control Enable Switch = 0x01` Enabled **for 1 cycle** |
| `OFF Switch = 0x1` | rising edge | `Cruise Control Enable Switch = 0x00` Disabled **for 1 cycle** |
| no press / hold | — | `Cruise Control Enable Switch = 0x03` Not Available |
| `Scroll up = 0x1 / 0x2` | while pressed | `Cruise Control Accelerate Switch = 0x1` |
| `Scroll down = 0x1 / 0x2` | while pressed | `Cruise Control Coast Switch = 0x1` |
| `Cruise Control Resume = 0x1` | while pressed | `Cruise Control Resume Switch = 0x1` |

Long/short press timing is delegated to the EMS (spec Q&A #3); the
gateway is edge-triggered for ON/OFF and level-passthrough for
SET+/SET−/Resume.

Brake-pedal cruise exit is handled by the EMS (Q&A #2), so the
gateway does **not** consume the brake signal.

Implemented in `BodyRouting.c::MapCruise` (`src/App/BodyRouting.c:130`).

### 1.2 Retarder aux brake (缓速器辅助制动) — spec §2.1, page 2-3

| LIN gear (HandleToVCU.auxiliaryBrakeGear) | CAN TSC1_VDR (PGN 0x0C001027, SA 0x27 → RCU) | Chinese label |
|---|---|---|
| 0 | Req Torque = 0,    Override = `00` disabled,  Priority = `11` low | off |
| 1 | Req Torque = -15%, Override = `10` torque ctrl, Priority = `11` low | 恒速档 |
| 2 | Req Torque = -25%, Override = `10` torque ctrl, Priority = `11` low | 制动 1 挡 |
| 3 | Req Torque = -50%, Override = `10` torque ctrl, Priority = `11` low | 制动 2 挡 |
| 4 | Req Torque = -75%, Override = `10` torque ctrl, Priority = `11` low | 制动 3 挡 |
| 5 | Req Torque = -100%,Override = `10` torque ctrl, Priority = `11` low | 制动 4 挡 |

Byte 8 (checksum slot) = **0xFF** ("不做校验"). Per Q&A #6 the
BOSCH 08, Yuchai 12 and ECONTROL 12 ECUs all accept the FF passthrough.

Implemented as the static `RetMap[6]` table and `MapRetarder` in
`src/App/BodyRouting.c:90`.

### 1.3 AMT shift (AMT 换档) — spec §3, page 3-4

| LIN signal (HandleToVCU, 0x01) | CAN signal (TC1, SA 0x05 → TCU) |
|---|---|
| `Transmission Requested Gear` | identical |
| `Transmission Mode 1` | identical |
| `A/M Mode Switch` | identical |
| `M+ Switch` | identical |
| `M- Switch` | identical |

Pure passthrough; only the framing changes (LIN PDU → CAN frame on
the powertrain bus). MT vehicles (`amtConfigCode = 0`) skip this
routing entirely.

Implemented in `MapAmt` (`src/App/BodyRouting.c:117`).

---

## 2. 102A part-number discrimination

Per the spec each of the three routings is gated by a 102A
configuration value read from NVM at startup:

| 102A field | Decides | Where consulted |
|---|---|---|
| `cruiseSwitchPartNo` → `CruiseSrcType` | LIN cruise or hardline (skip) | `MainFunction` cruise branch |
| `retarderHandlePartNo` → `AuxBrakeType` | route to RCU (TSC1_VDR) or EMS (TSC1_DR) | `MainFunction` aux-brake branch |
| `amtConfigCode` → `TxType` | AMT routing on / off (MT skips) | `MainFunction` AMT branch |

Loaded by `PartConfig_Load()` (`src/App/PartConfig.c`); on target it
calls `NvM_ReadBlock(NVM_BLOCK_102A, &Cfg)` and verifies the per-
block CRC, so an attacker cannot redirect routing by patching the
NVM alone.

---

## 3. Gateway reset blackout (Q&A #1)

The T-Gateway has < 300 ms of LIN→CAN unavailability after a product
reset. Downstream ECUs (TCU, EMS, RCU) monitor this as "node lost /
timeout".

The implementation does **not** assert stale data during the
blackout:

- On init, no LIN signal is marked valid.
- A LIN signal is considered stale if (`!valid`) or (`now -
  lastRxMs > LIN_STALE_MS = 300 ms`); the corresponding CAN signal
  is held at the spec default ("Not Available" for cruise, gear 0
  for aux brake, all zeros for AMT).
- Receivers see frozen defaults plus their own E2E counter halt and
  treat the gateway as offline per their own supervision strategy.

Implemented in `is_stale()` and the `MainFunction` branches.

---

## 4. Test coverage

`test/test_lin_can_routing.c` exercises every row of every table:

```
PASS T_cruise_default_is_not_available
PASS T_cruise_on_press_emits_one_enabled_pulse
PASS T_cruise_off_press_emits_one_disabled_pulse
PASS T_cruise_set_plus_minus_resume_passthrough
PASS T_cruise_stale_lin_returns_to_not_available
PASS T_retarder_table_matches_spec          (all 6 gears)
PASS T_engine_brake_routes_to_ems           (102A discrimination)
PASS T_amt_passthrough
PASS T_amt_disabled_on_mt_vehicle           (102A discrimination)
PASS T_cruise_skipped_when_part_is_hardwire (102A discrimination)
```

A short/long-press test is intentionally **not** added here — the
spec delegates that to the EMS (Q&A #3), so the gateway test only
checks the level passthrough.

---

## 5. Safety / cybersecurity considerations

- **ASIL classification.** Cruise and aux-brake routings touch
  functions that are at least QM and at most ASIL-B at the
  receiver. The gateway side is qualified to ASIL-B with the
  receiver-side E2E protection providing the residual cover (same
  decomposition argument as the powertrain routes; see
  `TGW_Routing_Design.md` §4).
- **IDPS.** Sensors on this path that emit Security Events:
  - LIN frame missing for > 300 ms → `SECOC_FRESH_FAIL`-style event
    (treated as gateway node loss; no IDPS action because the
    receiver has independent supervision).
  - Out-of-range `auxiliaryBrakeGear` (> 5) → clamped to 0 and a
    `PAYLOAD_SIGNATURE` event is raised (handled by the generic
    sensor in PduR; not added here to avoid duplication).
- **No checksum on TSC1_VDR.** The spec explicitly sets byte 8 = 0xFF
  per Q&A #6. The receiver does not check it. The protection that
  remains is the standard J1939 counter + the receiver's plausibility
  check (gear within 0..5, ramp-rate limit).

---

## 6. Forward look (spec page 4 note)

When the unified-low-config (统型低配) architecture lands on the J6
platform, the shift-knob switch will move to LIN (still on the
T-Gateway) but **A/M Mode**, **M+** and **M-** will become hardline
inputs into the TCU. The CAN-bus rotary will then be retired.

That change does **not** affect the cruise or aux-brake routings.
For AMT it shrinks `MapAmt` to one signal (`transmissionRequestedGear`
and `transmissionMode1`); the other three become unused fields on the
LIN signal struct. The `TxType` discrimination stays as-is.
