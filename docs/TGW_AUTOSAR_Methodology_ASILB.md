# AUTOSAR Design Methodology for ASIL-B

**Document:** TGW-MET-ASILB-001
**Standards basis:** AUTOSAR Classic Platform R21-11 Methodology (TR_Methodology),
ISO 26262:2018 Parts 4, 6, 8, 9
**Scope:** systematic design approach to produce an AUTOSAR ECU that
meets ASIL-B. Specific examples taken from the TGW (this repo).

This is a *methodology* document — it tells you what to do at each
phase, in what order, with which artefacts, and why each step is
required by ISO 26262 for ASIL-B. It is not a coding standard
(see TGW-CR-001) and not a safety case (see TGW-SC-001).

---

## 1. The methodology in one page

```
 ISO 26262 phase            AUTOSAR phase                       Key artefact
 ----------------------     -----------------------------       -----------------------
 Part 3  HARA               (out of AUTOSAR scope)              Safety Goal + ASIL
 Part 4  Functional         Functional safety concept           FSR (Functional Safety
         safety concept                                          Requirements)
 Part 4  Technical safety   System design                       TSR + System Description
         concept            (ARXML: SystemDescription)           (Atomic SWCs, Compositions,
                                                                  Signals, Bus)
 Part 6  SW safety req.     SW Component design                 SWC Internal Behavior,
                            (ARXML: SwComponentType)             Runnables, port interfaces
         SW architectural   ECU configuration                   ECU Extract,
         design             (ARXML: EcucModuleConfiguration)     BSW config (Com, PduR,
                                                                  Os, WdgM, E2E, SecOC, ...)
         SW unit design     Generation + integration            Generated RTE,
         + implementation                                        contract phase + integration
         Verification       Test + analysis                     Reviews, static analysis,
                                                                 unit tests (MC/DC for ASIL-B),
                                                                 integration tests
 Part 9  ASIL decomposition Mapped onto SW partitioning         OS-Application split,
                                                                 IOC, MPU config
 Part 8  Tool qualification Tool confidence assessment          TCL/TI report per tool
```

The right column is what the configuration tool (DaVinci CFG, EB
tresos, ...) emits and what is reviewed.

---

## 2. ASIL-B-specific obligations (cheat sheet)

| Topic | ISO 26262 ref | What ASIL-B demands |
|---|---|---|
| Requirements traceability | 6 §6 | Bi-directional: SG → FSR → TSR → SWSR → unit |
| SW architecture | 6 §7.4.3 | Hierarchical, encapsulated, restricted use of interrupts, pointers, recursion |
| Coding guidelines | 6 §5.4.7 | MISRA C:2012 (mandatory + required), no dynamic memory, no recursion |
| Unit testing | 6 §9.4.5 | Coverage: **statement + branch** (MC/DC required only for ASIL-D, recommended for B) |
| Integration testing | 6 §10 | Functional + interface + resource usage |
| Tool qualification | 8 §11 | TCL classification; if TCL3 then qualification per ISO 26262-8 §11.4.6 |
| HW/SW interface | 5 §6 | Documented HSI; defensive checks for HW failure modes |
| Freedom From Interference | 9 §6 | Required if mixed-ASIL on one ECU |
| Dependent failures analysis | 9 §7 | Required |
| Safety mechanisms | 6 §7.4.14 | Per element: detection + reaction with fault tolerant time interval |

"Recommended" in ISO 26262 means *"the method shall be applied unless
a documented rationale justifies its omission."* In practice MC/DC is
applied for ASIL-B on all safety-relevant units in this project.

---

## 3. Phase-by-phase

### 3.1 Item definition + HARA → Safety Goals

*Pre-AUTOSAR.* Output: a numbered list of safety goals, each tagged
with an ASIL and a Safe State. Example for the TGW:

```
SG-RTG-01 (ASIL-D) No corruption of a forwarded ASIL-D PDU shall
                   remain undetected by the receiver.
SG-RTG-03 (ASIL-B) Off-board (WAN) traffic shall not influence on-
                   board safety-relevant behaviour without explicit
                   authentication and rate-limiting.
SG-RTG-04 (ASIL-B) A failure of a QM partition shall not block, delay
                   beyond budget, or alter routing of ASIL ≥ B PDUs.
```

For an ASIL-B-only ECU the highest tag in this list is B.

### 3.2 Functional safety concept (FSC)

Decompose each SG into one or more **Functional Safety Requirements**.
Each FSR is allocated to a vehicle-level function and given:

- ASIL inherited from the SG
- Fault Tolerant Time Interval (FTTI)
- Safe State to enter on failure
- Detection mechanism (intent only, no implementation yet)

For ASIL-B the FTTI for a routing path is typically 100 ms–1 s
depending on the controlled function. Document this — the integrator
will measure against it.

### 3.3 Technical safety concept (TSC)

Each FSR → one or more **Technical Safety Requirements**. The TSR
binds the function to the ECU and to the AUTOSAR architecture. For
the TGW, the TSC contains things like:

```
TSR-RTG-03.1 (ASIL-B)
  The gateway shall verify a SecOC authenticator on every PDU
  received from the WAN channel before forwarding.
TSR-RTG-03.2 (ASIL-B)
  The gateway shall apply a token-bucket rate limit of capacity
  5 / refill 1 per ms to WAN ingress.
TSR-RTG-03.3 (ASIL-B)
  The gateway shall reject WAN commands that are not in a static
  allow-list. The allow-list shall be CRC-protected.
```

These are still requirements, not design. The traceability tool
(Polarion, DOORS) records FSR ↔ TSR ↔ test links.

### 3.4 System design — AUTOSAR System Description

Modelled in ARXML. Outputs:

- **Atomic SW-Cs** with port-based interfaces (Sender/Receiver,
  Client/Server, Mode, NV).
- **Composition** showing the wiring.
- **System signals** mapped to **I-PDUs** mapped to bus frames.
- **Mapping** of SW-Cs to ECUs.
- **Timing extensions** giving end-to-end deadlines per signal.

For ASIL-B the ARXML elements that carry safety-relevant data must
carry the `<DATA-FILTER>` and `<E2E-PROTECTION-SET>` extensions; the
configurator uses these to generate the E2E protect/check code in
the sender/receiver runnables.

### 3.5 ECU design — AUTOSAR ECU Extract + BSW configuration

The ECU Extract is the slice of the System Description that lives on
one ECU. From it the **BSW configurator** generates the EcuC values
for every BSW module. For an ASIL-B ECU the critical knobs:

| Module | ASIL-B-relevant configuration |
|---|---|
| **Os** | SC4 (memory protection on), `OsScalabilityClass=SC4`, OS-Apps per ASIL class, ProtectionHook = reaction "kill faulty app" |
| **WdgM** | Supervised Entities for every safety-relevant runnable; alive + deadline + logical supervision |
| **PduR** | `PduRZeroCostOperation=FALSE`, `PduRMetaDataSupport=TRUE`, allow-list-only routing table |
| **Com** | E2E transformer in the transformer chain for ASIL signals |
| **E2E** | Profile per signal (P02 / P05 / P06), DataId set unique, max delta counter set per latency tolerance |
| **SecOC** | Authenticator length, freshness window, key slot id from HSM |
| **CanIf / EthIf** | Separate hardware filter handles per ASIL class — no head-of-line blocking |
| **Dem** | Event configuration with operation cycle, FDC tracking, NVM storage for ASIL events |
| **NvM** | ECC + CRC on every safety-relevant NV block; redundant copies |
| **Fls / Fee** | Read-back verification on safety-relevant writes |
| **EcuM** | Init-list order: safety BSW before any application |

The configurator must be a TCL-classified tool (see §3.10).

### 3.6 SW-C internal behaviour

For each safety-relevant SW-C, in ARXML:

- **Runnables** with explicit event triggers (TimingEvent,
  DataReceivedEvent, OperationInvokedEvent).
- **Inter-Runnable Variables** for data shared between runnables of
  the same SW-C (avoid file-scope statics).
- **Per-runnable** memory section (so the linker can group code/data
  for MPU configuration).
- **Exclusive Areas** where needed; prefer cooperative scheduling
  over disabling interrupts.

The SW-C design step ends when the **contract phase header** can be
generated; this is the moment when implementation can begin against
a fixed interface.

### 3.7 Coding and implementation

For ASIL-B units:

- **Coding standard**: MISRA C:2012 mandatory + required, project
  deviations registered. Static analysis tool runs in CI; deviations
  reviewed per release.
- **Style**: no dynamic memory, no recursion, bounded loops, single
  exit *not* enforced (guard-clause style allowed if reviewed).
- **Defensive programming** at module boundaries only; do not bury
  re-checks in the middle of trusted code (they obscure the safety
  case).
- **HW failure modes** explicitly addressed: ECC double-bit on RAM,
  CRC on flash sections, watchdog refresh in a dedicated runnable.
- **Determinism**: AUTOSAR OS tasks have fixed priorities; no
  priority inheritance gymnastics; resource locks held for bounded
  time and measured.

### 3.8 Verification

Three layers, all required for ASIL-B:

1. **Static** — MISRA + Cantata/QAC + project-specific rules. Zero
   *required* deviations; *advisory* deviations reviewed.
2. **Unit** — every safety-relevant unit reaches **statement and
   branch coverage** (ISO 26262 6 Table 12 column ASIL-B). MC/DC is
   recommended for B and applied here for routing/safety/security
   units (TGW project rule).
3. **Integration** — functional, interface, resource (CPU load,
   stack, RAM, NVM endurance), fault-injection (the FuSa
   verification team injects bit-flips on routes, sensor frames,
   memory; the IdsM must report and the dispatcher must drop).

Coverage gaps are not closed by writing tests-for-coverage — they
are flagged for review; usually the gap indicates dead code that
should be removed, or a missing requirement.

### 3.9 Freedom From Interference (FFI)

Required as soon as the ECU hosts code of mixed ASILs (the TGW does
— it carries QM telematics code next to ASIL-B/D routing). The FFI
argument is built on four pillars:

| Interference type | Mechanism on the TGW |
|---|---|
| Memory (spatial) | OS-Application partitioning + MPU |
| Timing/execution | Fixed-priority preemption + deadline supervision (WdgM) |
| Information exchange | E2E protection on every cross-ASIL data; IOC with static channel definitions |
| Common-cause HW | ECC RAM, flash CRC, dual-channel watchdog clock |

Each mechanism is documented with its diagnostic coverage and the
fault model it covers. ISO 26262-9 §6.4 requires that the analysis
shows *each* identified interference type is covered by *at least
one* mechanism.

### 3.10 Tool qualification

Every tool that produces an artefact in the safety chain has a
**Tool Confidence Level**:

- TCL1 — no qualification needed.
- TCL2 — increased confidence from use; qualify by validation,
  proven in use, or process compliance.
- TCL3 — full qualification per ISO 26262-8 §11.4.6 (development
  according to a safety standard).

For the AUTOSAR toolchain on this project:

| Tool | Classification | Justification |
|---|---|---|
| Configurator (DaVinci CFG) | TCL3 | Emits production-code config; error → safety violation. Vendor qualified. |
| Generator (RTE/BSW) | TCL3 | Same. Vendor qualified. |
| Compiler | TCL3 | Vendor-supplied compiler qualification kit. |
| Static analyser | TCL1 | Off-line tool; absence of finding ≠ correctness; classified TI1. |
| Test framework | TCL1 | Same. |

The TCL assessment is reviewed at every tool version bump.

### 3.11 Safety case

A live document that argues, with evidence, that the SG is met.
Structure (GSN-style):

```
Goal:  SG-RTG-03 is met (off-board traffic cannot influence safety)
  Strategy: argue over the TSR chain TSR-RTG-03.1..3
    Sub-goal: TSR-RTG-03.1 met (SecOC verified)
      Evidence: SecOC qualification, CanIf cfg review,
                fault-injection report FUI-RTG-03.1-2025-03
    ...
```

Each sub-goal links to a specific test report, a review record, or
a tool-qualification artefact. Reviewers walk the tree top-down.

---

## 4. Activity / artefact matrix (ASIL-B subset)

| Activity | Performed by | Reviewed by | Artefact | ISO 26262 ref |
|---|---|---|---|---|
| HARA | Safety mgr + system eng | Safety assessor | HARA report | 3 §7 |
| FSC | System safety | FuSa lead | FSC document | 4 §6 |
| TSC | SW safety | FuSa lead | TSC document | 4 §7 |
| ARXML system design | System architect | SW architect | SystemDescription.arxml | 6 §7 |
| BSW configuration | Integrator | FuSa lead | ECUC values (ARXML) | 6 §7 |
| SW-C design | Component owner | Tech lead | SwComponentType.arxml + design note | 6 §7 |
| Implementation | Component owner | Peer | Source + unit tests | 6 §9 |
| Unit verification | Component owner | Tech lead | Coverage report | 6 §9 |
| Integration verification | Integrator | FuSa lead | Integration test report | 6 §10 |
| FFI analysis | FuSa lead | Safety assessor | FFI report | 9 §6 |
| Dependent failure analysis | FuSa lead | Safety assessor | DFA report | 9 §7 |
| Tool qualification | Tool owner | Safety assessor | TCL/TI report | 8 §11 |
| Safety case | FuSa lead | Safety assessor | Safety case (GSN) | 4 §6.4.6 |

The right-hand column points to the clause that demands the
artefact — every row exists because of an ISO 26262 obligation, not
because of process inertia.

---

## 5. ASIL-B specific design rules (TGW conventions)

These are the project-level rules that translate the standard into
day-to-day engineering decisions for ASIL-B work:

1. **One safety mechanism per safety requirement**, named in the
   code with a comment block. Tracing tool indexes the comment.
2. **Allow-list semantics by default** at every external interface.
   "Drop and count" is the project's standard reaction for unknown
   inputs.
3. **No assumption of caller correctness** at module boundaries —
   validate inputs once, deeply, at the entry. *Inside* a module the
   caller is trusted, so no re-validation (avoids hiding the safety
   case).
4. **Latch on serious faults** — Safe State, isolation, route block
   are never auto-cleared. The only way back is a controlled reset.
5. **Static everything** that goes into the safety case: routing
   table, policy table, anomaly baseline, allow-list, partition map.
   Verified at startup by CRC.
6. **No silent best-effort** in safety code — every fallback path
   counts a stat and reports a Dem event.
7. **Deadline-bound every routing path**; if the budget is not in the
   table, the path is not safety-relevant by construction and lives
   in the QM partition.
8. **Cross-partition data only via IOC**, with static channel
   definitions. No `extern` access to safety RAM from the QM
   partition.

---

## 6. Anti-patterns to refuse

Common patterns that look harmless but break the ASIL-B argument:

- **Learning detectors** (drift-trained baselines) — cannot be
  qualified; an attacker shifts the training set.
- **Recovery from latched safe state without reset** — the very
  failure that triggered safe state may have corrupted the recovery
  decision logic.
- **Defensive checks deep inside trusted modules** — they hide the
  real entry-point check from the safety case and create the illusion
  of robustness.
- **Dynamic memory in safety code** — even one `malloc` invalidates
  the resource-usage argument for the entire ECU.
- **"Should not happen" assertions that proceed on failure** — either
  it cannot happen (delete the assertion) or it can (handle it).
- **Mixing QM and ASIL code in one OS-Application** — destroys FFI;
  the partition boundary is the FFI boundary.
- **Ad-hoc Dem events** added without a safety requirement linking
  to them — every Dem event must trace to an FSR/TSR.

---

## 7. Where this project's artefacts live

| Artefact | Location |
|---|---|
| FSC / TSC | (external, project PLM) |
| System ARXML | (external, network design tool) |
| ECU Extract + BSW config | `config/PduR_RoutingTable.arxml` (excerpt in this repo) |
| Architecture & safety concept | `docs/TGW_Routing_Design.md` |
| Code design | `docs/TGW_Code_Design.md` |
| Cybersecurity / IDPS | `docs/TGW_IDPS_Design.md` |
| Reference implementation | `src/PduR/`, `src/Safety/`, `src/Security/`, `src/OS/` |
| Unit tests | `test/` |
| Safety case | (external, GSN tool) |

For onboarding: read TGW-SWA-RTG-001 first (architecture), then this
document (methodology), then TGW-CODE-001 (code design), then the
source. Reviews go in the opposite order — source upwards.
