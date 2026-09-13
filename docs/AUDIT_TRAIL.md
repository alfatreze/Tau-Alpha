# Tau audit trail

This is the chronological handoff log for design choices, implementation
milestones, failed attempts, reversals, and verification results. The compact
current-state index is [PROJECT_REGISTER.md](PROJECT_REGISTER.md); detailed
reasoning remains in the linked documents.

**Evidence tags:** **code-review**, **host**, **simulation**, **Quartus**, and
**Pocket**. A tag describes the evidence actually available, not the desired
confidence level.

## Hot-path / cold-path boundary

Unless a later, explicitly audited decision changes it, these are protected:

| Classification | Components / data | Rule |
|---|---|---|
| Hot path | Helix decode, PCM/audio FIFO, DMA ring, interrupts, stack, input polling, target-read pumping, EQ | Remain in BRAM/current clocking. No SDRAM migration or added unbounded contention. |
| Display-critical | Framebuffer scanout and line-fill deadline | Framebuffer wins every new SDRAM arbitration point; CPU transfers are bounded. |
| Candidate cold path | Playlist text/indexes, artwork resampling workspace, later UI/settings/log storage | May migrate only after Phase 1 hardware diagnostics and Phase 2 data-window gates pass. |
| Deferred cold code | Selected non-critical leaf UI functions | No execute-in-place decision before Phase 2 succeeds under Pocket stress. |

## Resource and timing trend

Do not infer missing numbers. A row is added only from a committed Quartus
report, and each new hardware milestone compares against the immediately prior
row.

| Entry | Evidence | ALMs | RAM blocks | Block-memory bits | DSP | Tightest reported slack | Notes |
|---|---|---:|---:|---:|---:|---:|---|
| A-003 | **Quartus** baseline, 2026-09-13 | 5,587 / 18,480 (30%) | 300 / 308 (97%) | 2,380,928 / 3,153,920 (75%) | 11 / 66 (17%) | 0.025 ns hold, fast 0C | Successful, 42m58s full compile. |
| A-006 | pending | — | — | — | — | — | Integrated SDRAM diagnostic compile in progress; do not claim a delta yet. |

## Entries

### A-001 — Tau package and baseline presentation

**Date:** 2026-09-12  
**Decision/change:** Maintain a separately named Tau package derived from
HarpMudd, placed in Pocket's Media Players category, with Tau artwork and an
ASCII `TAU` OS label.  
**Alternatives:** Alter upstream package identity (rejected: risks collision
and accidental upstream writes) or use the superscript alpha in OS metadata
(rejected after rendering failure).  
**Hot/cold impact:** Neither.  
**Evidence:** **Pocket** — packaging, artwork, MP3 playback, seeking, cover art,
and visualizers were tested.  
**Outcome/risk:** Core branding works. Superscript alpha remains graphical only;
see [issue 002](issues/002-pocket-os-unicode-metadata.md).

### A-002 — Preserve the 400×360 raster

**Date:** 2026-09-12  
**Decision:** Keep 400×360 RGB565 at 60 Hz instead of pursuing a 640×480 mode.
It is an exact 4× scale to the Pocket display and already has a working SDRAM
framebuffer pipeline.  
**Alternatives:** Change video timing through configuration only (rejected: it
requires RTL, clocking, memory-bandwidth, and output-pipeline changes) or fork
a different graphics stack (deferred: no capacity/validation benefit for the
current player).  
**Hot/cold impact:** Display-critical path retained as-is.  
**Evidence:** **Pocket** baseline display; **code-review** of existing timing
and framebuffer architecture.  
**Outcome/risk:** UI remains constrained to 400×360; richer rendering is gated
by memory capacity, not resolution.

### A-003 — Reproducible FPGA baseline

**Date:** 2026-09-13  
**Decision/change:** Establish a local Linux x86-64 Quartus 25.1std build path
and compile the unmodified core before SDRAM changes.  
**Alternatives:** Compile on the macOS shared 9p mount (rejected: Quartus cannot
reliably create `db/`) or rely only on historic upstream resource figures
(rejected: no reliable change comparison).  
**Hot/cold impact:** None.  
**Evidence:** **Quartus** — successful compile and resource/timing row above.  
**Outcome/risk:** Baseline is valid. The 0.025 ns hold margin means CDC/clocking
changes need a fresh report, not intuition. See [FPGA_BUILD.md](FPGA_BUILD.md).

### A-004 — Hybrid SDRAM direction

**Date:** 2026-09-13  
**Decision:** Reuse the proven framebuffer SDRAM controller behind a small
two-owner arbiter and CDC bridge. Start with MMIO diagnostics, migrate cold
data later, and defer SDRAM code execution.  
**Alternatives:** Rewrite the controller as multi-port, place CPU servicing in
the renderer, replace it with LiteDRAM, or reclaim only stack space. All were
rejected/deferred for larger regression risk or insufficient capacity.  
**Hot/cold impact:** Explicitly preserves the boundary above.  
**Evidence:** **code-review** and **host** resource/firmware analysis; no
hardware validation at this entry.  
**Outcome/risk:** Architecture is selected, not proven. Full rationale and
gates: [SDRAM_MEMORY_ARCHITECTURE.md](SDRAM_MEMORY_ARCHITECTURE.md).

### A-005 — Phase 1 unit-level implementation

**Date:** 2026-09-13  
**Decision/change:** Add a framebuffer-priority owner-locking arbiter and a
one-outstanding asynchronous CPU bridge. A 32-bit CPU operation is deliberately
two independent 16-bit transfers; reads explicitly stop each controller burst.
**Alternatives:** Permit a longer CPU burst (rejected: hides a scanout deadline
risk) or add CPU memory mapping first (rejected: expands the debugging surface
before the physical controller route is proven).  
**Hot/cold impact:** Maintains the boundary. The bridge is diagnostic-only and
does not map ordinary CPU loads/stores to SDRAM.  
**Evidence:** **simulation** — `tb_tau_sdram_arbiter` verifies priority/owner
routing; `tb_tau_sdram_cpu_bridge` verifies CDC mailbox, byte enables,
halfword sequencing, burst termination, and read assembly.  
**Outcome/risk:** Unit tests pass. Simulation is not a Pocket SDRAM test; no
contention, timing, or display claim is made yet.

### A-006 — Phase 1 top-level integration and build-session failure

**Date:** 2026-09-13  
**Decision/change:** Route the existing framebuffer controller port through
the arbiter, connect the diagnostic bridge through new MMIO registers, and
bump the RTL/firmware compatibility word. Playback firmware does not issue the
new commands.  
**Alternatives:** Publish before the full fit (rejected: integration is not a
Quartus-verified result) or make a cached SDRAM window immediately (rejected:
violates Phase 1 scope).  
**Hot/cold impact:** No hot data moved. Framebuffer remains the priority owner.
**Evidence:** **host** regression suite and firmware build pass; **Quartus**
compilation is in progress.  
**Failure/workaround:** Detached SSH `nohup`/`setsid` launch attempts exited
without starting Quartus. A managed interactive SSH session started synthesis;
see [issue 004](issues/004-vm-quartus-detached-launch.md).  
**Remaining gate:** Record final Quartus resources/timing, then build a
controlled Pocket diagnostic before enabling any data migration.

## Reversal ledger

This table points to conclusions that changed after evidence. Keep it visible
in review; it is not an embarrassment to delete.

| Topic | Earlier conclusion | Corrected conclusion / evidence |
|---|---|---|
| FLAC feasibility | Early estimates treated I/O as the likely binding budget. | Measured analysis found CPU/RAM constraints dominate; see the dated, retained corrections in [FLAC.md](FLAC.md). |
| Settings shell | A small runtime settings implementation appeared plausible. | It crossed protected memory layout boundaries; defer until SDRAM data capacity is proven. [SETTINGS_RUNTIME_BUDGET.md](SETTINGS_RUNTIME_BUDGET.md). |
| Video resolution | A configuration-only resolution change appeared plausible. | Current pipeline needs RTL/clock/bandwidth work; preserve 400×360 pending measurements. A-002 above. |

## Entry template

```md
### A-NNN — Short title
**Date:** YYYY-MM-DD
**Decision/change:**
**Alternatives and rationale:**
**Hot/cold impact:**
**Evidence:** **code-review | host | simulation | Quartus | Pocket** — links/logs
**Resource/timing delta:** measured values or `not applicable` / `pending`
**Outcome, reversal/workaround, remaining risk, and next gate:**
```
