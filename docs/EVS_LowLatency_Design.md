# EVS Camera Stack — 60 fps Low-Latency Design

Companion to the EVS/Camera technical reference. This document specifies the
hardware, kernel, and software configuration required for the EVS camera
path to sustain **60 fps** with a **glass-to-glass latency ≤ 50 ms**
under worst-case load.

## 1. Target metrics

| Metric | Target | Why |
|---|---|---|
| Frame rate | 60 fps continuous | 16.67 ms vsync period |
| Glass-to-glass latency (sensor SOF → photons) | ≤ 50 ms p99 | ISO 16505 mirror replacement; smooth surround view |
| Frame jitter (vsync to displayed) | ≤ 1 ms p99 | No visible cadence stutter |
| Dropped frame rate | 0 in nominal, ≤ 1 / 10 min under load | RVC regulatory + UX |
| Latency variance after cutover | ≤ 5 ms step | Cutover (Part 2) must not bump latency |

## 2. Latency budget

Worst-case allocation for one frame at 60 fps. The pipeline must end-to-end
fit in **3 × vsync = 50 ms**, of which:

```
  Sensor integration        16.67 ms   (full 1/60 s exposure)
  CSI receive + DMA writeback  0.5 ms   (~3 Gbps × 1280×720 NV12)
  ISP (HW, inline)             1.5 ms   (3A, debayer, CCM, LSC)
  V4L2 DQBUF + dma-buf import  0.2 ms   (cached metadata)
  Display scanout + TCON      16.67 ms  (1 vsync to scan + ~5 ms TCON)
  Panel response             ≤ 10 ms    (LCD; OLED faster)
  ─────────────────────────────────
  Total worst-case            ≈ 45 ms   (≤ 50 ms p99 target met)
```

The middle row (ISP + DQBUF + queue/commit) **must fit inside one vsync
(16.67 ms)** with margin. If any stage exceeds this, the pipeline slips
one frame and latency jumps by 16.67 ms — the next budget alarm.

## 3. Pipeline at a glance

```
  ┌─────────┐  MIPI-CSI2   ┌──────────────┐         ┌────────────┐
  │ Sensor  │ ───────────→ │ CSI receiver │  DMA →  │  V4L2 vb   │
  │ 60 fps  │ 4-lane 2.5 G │  + HW ISP    │ NV12 FB │  dma-buf   │
  └─────────┘              └──────────────┘         └─────┬──────┘
                                                          │ dup() FD
                                                          ▼
                                                ┌─────────────────┐
                                                │ camera-serviced │
                                                │  (fast path)    │
                                                └─────────┬───────┘
                                                          │ SCM_RIGHTS
                                                          ▼
                                                ┌─────────────────┐
                                                │  evs-display    │
                                                │  (DRM atomic    │
                                                │   NONBLOCK)     │
                                                └─────────┬───────┘
                                                          │ PAGE_FLIP_EVENT
                                                          ▼
                                                  ┌──────────────┐
                                                  │  Display HW  │
                                                  └──────────────┘

  Zero pixel copies anywhere. Frame travels as a dma-buf FD; only
  metadata (timestamps, sequence numbers) is copied between processes.
```

## 4. Buffer strategy

The choice is **3-deep V4L2 queue** with **triple-buffered DRM flip**:

| Pool | Depth | Reason |
|---|---|---|
| V4L2 capture (`VIDIOC_REQBUFS`) | 3 | One being filled, one being displayed, one in flight |
| DRM imported FBs | == V4L2 depth | Each V4L2 buffer pre-imported as a `drmModeAddFB2` ID at pool init; never reimported per frame |
| Compositor surfaces | 0 | EVS plane is direct — never goes through Weston |

**Two-deep is rejected**: when the display goes to scan a buffer at vsync,
V4L2 needs a fresh free buffer; with only 2 buffers, the capture thread
must wait for the previous flip event, which serializes capture and
display and adds one vsync to latency.

**Four-deep is rejected**: an extra buffer adds ~16.67 ms of queue depth
without preventing drops — drops happen at the producer, not in the queue.

## 5. Vsync alignment

Two clocks: sensor frame clock (driven by CSI PCLK / sensor PLL) and
display vsync (TCON pixel clock). They drift unless explicitly aligned.

- **Preferred**: sensor in slave (genlock) mode with an FSYNC pin driven
  from the display TCON or a shared PLL. Drift = 0.
- **Acceptable**: free-run with sensor at 60.1 fps, display at exactly
  60.0 fps. The 0.1 fps oversupply means we drop one frame every ~10 s;
  the latency budget is preserved.
- **Rejected**: sensor at 59.9 fps. Every ~10 s the capture is late and
  the display repeats the previous frame — a visible hitch.

In free-run mode the V4L2 driver MUST report `V4L2_BUF_FLAG_TIMESTAMP_SOF`
so the latency telemetry can compute sensor-SOF-to-scanout.

## 6. Kernel and scheduler

The capture-to-display path requires deterministic scheduling. Required
kernel configuration:

```
  CONFIG_PREEMPT_RT=y                       # full preemption
  CONFIG_HZ=1000                            # 1 ms timer tick
  CONFIG_NO_HZ_FULL=y                       # tickless on isolated CPUs
  CONFIG_HIGH_RES_TIMERS=y
  CONFIG_CPU_FREQ_GOV_PERFORMANCE=y         # pin gov for the EVS CPUs
```

Boot-cmdline pinning:

```
  isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3 irqaffinity=0-1
```

CPU 2 hosts the V4L2 ISR + `camera-serviced` capture thread.
CPU 3 hosts `evs-display` (DRM atomic commit + page-flip handler).
CPUs 0–1 take everything else (Weston, HMI, DVR, system).

Thread priorities (SCHED_FIFO):

| Thread | Prio | Notes |
|---|---|---|
| `csi-isr` (kernel) | 99 | already RT in PREEMPT_RT |
| `cam-capture` (DQBUF loop) | 80 | reads dma-buf, fans out FDs |
| `evs-display` (DRM commit) | 80 | symmetric |
| `evs-rvcd` overlay | 50 | parking lines |
| Weston | 20 | normal |
| DVR encode | 10 | bounded |

The capture and display threads must not share a CPU (mutual preemption
defeats the SCHED_FIFO ordering). IRQ affinity for CSI and DRM vblank is
pinned to CPUs 2 and 3 respectively via `/proc/irq/<N>/smp_affinity`.

## 7. DRM atomic configuration

Per-frame commit uses `DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT`
(already in `src/EVS/ap/drm_atomic.cpp`). For 60 fps:

- **In-fence FD** from V4L2 (`V4L2_BUF_FLAG_IN_FENCE`) on every plane
  commit so scanout waits for ISP completion deterministically — no
  spinning poll in userspace.
- **Out-fence** captured per commit, used only for telemetry; the page
  flip event remains the recycle trigger.
- **Plane property set is cached**: only `FB_ID`, `IN_FENCE_FD`, and the
  page-flip-event flag change per frame. Geometry properties are committed
  once at stream start and are not re-sent — reduces the atomic blob by
  ~80% and shaves ~80 µs off the commit path.
- **Modeset on the hot path is forbidden**: any commit returning
  `-EINVAL` because modeset is required is treated as a fault and the
  arbiter rejects the offending consumer's spec change.

## 8. Camera service fast path

The arbiter (`src/EVS/ap/camera_arbiter.h`) gains a `low_latency` field on
`StreamSpec`. When set:

1. The consumer is admitted only if there is no other active consumer of
   the same camera, **or** all other consumers are also `low_latency` AND
   use SensorVc negotiation (separate hardware paths).
2. The frame-delivery socket goes straight from the V4L2 thread to the
   consumer's Unix socket — no intermediate copy queue, no per-consumer
   ring buffer.
3. The frame seq watermark for late drops is 1: if a consumer hasn't
   acknowledged frame N when frame N+1 arrives, N+1 is dropped (or
   delivered with the "you are behind" flag, consumer's choice). This
   trades completeness for bounded latency.

This is implemented as policy, not a separate code path: the same
producer FD-fanout logic; the fast path is just "fanout factor = 1 and
backpressure policy = drop-not-queue."

## 9. Telemetry

Every frame carries the following timestamps (header in
`src/EVS/common/latency_telemetry.h`):

```
  t_sensor_sof    sensor reports start-of-frame (V4L2 SOF stamp)
  t_csi_done      CSI DMA writeback complete (V4L2 buffer ready)
  t_dqbuf         camera-serviced returned from VIDIOC_DQBUF
  t_handed_to_ap  fd delivered to evs-display
  t_commit_done   drmModeAtomicCommit returned
  t_page_flip     PAGE_FLIP_EVENT received
  t_scanout_done  OUT_FENCE_PTR signaled (line 0 of next frame)
```

The differences are exported as a Prometheus-style histogram by
`evs-managerd`. Continuous regressions show up as latency tail growth
before any user can perceive a glitch.

**Acceptance gate in CI**: a 60 s capture with synthetic load on CPUs 0–1
must show p99(t_scanout_done − t_sensor_sof) ≤ 50 ms and zero drops.

## 10. Failure handling

| Symptom | Root cause likelihood | Action |
|---|---|---|
| One frame > budget | DVFS transition; isolated-core jitter | Telemetry warning. If sustained > 0.1 % of frames, raise to alarm. |
| Repeated late DQBUF | Sensor PLL drift; CSI errors | Restart sensor; bump CSI error counter; if > 10/min, fault the camera and revert to MCU display (RVC). |
| Missing PAGE_FLIP_EVENT | DRM driver hang; modeset slipped in | Cancel pending; rebuild plane state; if recurs, hard-reset display controller (loses ≤ 100 ms). |
| Thermal throttling | Sustained 60 fps + DVR encode | Drop DVR encode to 30 fps (DVR priority is below preview); EVS keeps 60. |

The arbiter's priority table already enforces this: `RVC` and any
`low_latency` consumer outrank `DVR_LOOP`, so thermal pressure is shed
from DVR first.

## 11. What is intentionally NOT in this design

- **No GPU compositor on the EVS path.** Weston / EGLStream adds at
  minimum one vsync and unpredictable jitter from the GPU job scheduler.
- **No software ISP on the hot path.** The `isp_lite` module is for the
  MCU pre-Linux path only; the AP relies on the SoC's hardware ISP.
- **No memcpy.** All cross-process transfer is dma-buf FD passing.

## 12. Code changes landed with this design

- `src/EVS/common/latency_telemetry.h` — per-frame timestamp struct and
  histogram bucket constants. Producers fill in fields they own; the
  struct is forwarded alongside the dma-buf FD (auxiliary control via
  V4L2 metadata buffer or a side-channel struct).
- `StreamSpec::low_latency` (in `src/EVS/ap/camera_arbiter.h`) — admission
  rule enforces exclusive-or-VC isolation; non-`low_latency` consumers
  are rejected from admission when a `low_latency` consumer is active.
- Tests in `test/evs/test_camera_arbiter.cpp` cover the new admission
  rules so a future refactor cannot regress the latency contract.
