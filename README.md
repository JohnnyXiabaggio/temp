# temp

TGW (Telematics / Central Gateway) software routing design based on
AUTOSAR Classic Platform R21-11, meeting ISO 26262 functional safety
(highest allocated ASIL-D via ASIL-B(D) decomposition + end-to-end
protection).

- `docs/TGW_Routing_Design.md` — architecture and safety concept
- `docs/TGW_Code_Design.md` — detailed code design (module map, control flow)
- `docs/TGW_IDPS_Design.md` — cybersecurity / IDPS design (ISO 21434, UN R155)
- `config/PduR_RoutingTable.arxml` — PduR routing table excerpt
- `src/PduR/`     — routing dispatcher
- `src/Safety/`   — SafetyMgr, SecOC gate, deadline monitor, E2E wrapper
- `src/Security/` — IdsM, anomaly detector, IdsR off-board reporter
- `src/OS/`       — OS-Application partitioning
- `test/`         — host unit tests (`cd test && make run`)

## EVS / Camera stack

Implementation of the IVI EVS/DVR/Camera stack reference (cold-boot rear-view
camera with MCU pre-Linux display, DRM atomic SHADOW→CUTOVER handover, and
camera arbitration).

- `src/EVS/common/handover_block.h` — shared MCU↔AP block layout
- `src/EVS/mcu/`  — Zephyr-style MCU firmware: `csi_driver`, `disp_drv`,
  `isp_lite`, `can_listen`, `ovl_render`, `handover_agent`, and pure FSM
  in `evs_main.{c,h}`
- `src/EVS/ap/`   — Linux AP side: `drm_atomic` (libdrm bindings, gated by
  `EVS_HAVE_LIBDRM`), `cutover_planner` (pure handover sequencer),
  `camera_arbiter` (admission, preemption, V4L2 spec negotiation),
  `ap_handover_agent` (mmap of shared block)
- `test/evs/`     — host tests (`cd test/evs && make run`): FSM transitions,
  arbiter scenarios A/B/C, cutover planner happy/abort paths, handover
  cross-side liveness
