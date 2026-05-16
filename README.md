# temp

TGW (Telematics / Central Gateway) software routing design based on
AUTOSAR Classic Platform R21-11, meeting ISO 26262 functional safety
(highest allocated ASIL-D via ASIL-B(D) decomposition + end-to-end
protection).

- `docs/TGW_Routing_Design.md` — architecture and safety concept
- `docs/TGW_Code_Design.md` — detailed code design (module map, control flow)
- `docs/TGW_IDPS_Design.md` — cybersecurity / IDPS design (ISO 21434, UN R155)
- `docs/TGW_AUTOSAR_Methodology_ASILB.md` — AUTOSAR design methodology for ASIL-B
- `config/PduR_RoutingTable.arxml` — PduR routing table excerpt
- `src/PduR/`     — routing dispatcher
- `src/Safety/`   — SafetyMgr, SecOC gate, deadline monitor, E2E wrapper
- `src/Security/` — IdsM, anomaly detector, IdsR off-board reporter
- `src/OS/`       — OS-Application partitioning
- `test/`         — host unit tests (`cd test && make run`)
