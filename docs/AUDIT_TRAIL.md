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
| A-006 | **Quartus** fitter only, 2026-09-13 | 5,661 / 18,480 (31%) | 299 / 308 (97%) | 2,380,416 / 3,153,920 (75%) | 11 / 66 (17%) | not produced | Fitter passed; assembler assertion prevented artifact and timing analysis. |

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
analysis, synthesis, and fitter pass with the resource row above. No final
timing or programming artifact exists.
**Failure/workaround:** Detached SSH `nohup`/`setsid` launch attempts exited
without starting Quartus. A managed interactive SSH session started synthesis;
see [issue 004](issues/004-vm-quartus-detached-launch.md).
**Remaining gate:** Resolve/reproduce the Quartus assembler failure and obtain
a complete timing-clean artifact, then build a controlled Pocket diagnostic
before enabling any data migration. See [issue 005](issues/005-quartus-assembler-internal-error.md).

### A-007 — External documentation audit and code-link index

**Date:** 2026-09-13
**Decision/change:** Add explicit repository-relative links to the relevant
RTL, testbenches, build record, integration files, and VM issue so a reviewer
without local workspace access can navigate from the project register.
**Alternatives and rationale:** Rely on repository search/file discovery
(insufficient: Claude could read the four top-level audit documents but could
not locate the implementation artifacts in its first pass).
**Hot/cold impact:** None.
**Evidence:** **external review** — Claude confirmed the four audit documents
were clear and evidence labels were applied consistently, but stated its first
pass was documentation-only because implementation file links were not
provided.
**Outcome/risk:** Direct code links are now indexed in
`PROJECT_REGISTER.md`. The code-level audit remains pending and the Quartus fit
is still in progress; do not treat this feedback as RTL approval.

### A-008 — Claude SDRAM RTL review and simulation-coverage additions

**Date:** 2026-09-13
**Decision/change:** Review Claude's source-level audit, then add the two
identified tests before the Phase 1 Pocket gate: framebuffer arrival during an
owned CPU operation, and independent reset deassertion in both clock-domain
orders. Promote explicit BRAM/SDRAM/MMIO decode to a hard Phase 2 prerequisite.
**Alternatives and rationale:** Leave the cases to code inspection (rejected:
display-critical arbitration and CDC reset behavior need executable evidence),
or tighten the broad decode during Phase 1 (deferred: Phase 1 MMIO does not use
the mapped window, while Phase 2 must not start without the change).
**Hot/cold impact:** The tests reinforce the existing boundary; no hot state or
mapped CPU address path changes.
**Evidence:** **external review** confirmed framebuffer priority, held CPU
requests, stable-payload toggle CDC, halfword ordering, and the `sdram_start`
pulse contract by source inspection. **simulation** now covers both contention
orders and reset deassertion orders; `make test` passes. The external finding
that `AGENTS.md` lacked the audit rule is stale against commit `fd767b3`; the
current file contains the rule.
**Resource/timing delta:** Not applicable; test and documentation changes only.
**Outcome/risk:** Phase 1 simulation coverage is stronger. The complete
top-level integration/QSF fitter pass is now recorded, but the Quartus
assembler failure leaves timing and Pocket diagnostics unresolved.

### A-009 — Phase 1 fitter pass; Quartus assembler internal failure

**Date:** 2026-09-13
**Decision/change:** Preserve the first full-build failure as an investigation
record rather than retrying a complete 45-minute flow blindly. Retry only the
Assembler against the successful fitter database to distinguish a fit failure
from a packaging/tool failure.
**Alternatives and rationale:** Treat fitter success as release-ready (rejected:
there is no `.sof`/`.rbf` or timing result), or rebuild immediately (deferred:
would destroy useful failure context without testing a narrower hypothesis).
**Hot/cold impact:** None. No data migration or Pocket diagnostic is enabled.
**Evidence:** **Quartus** — analysis/synthesis (6m12s) and fitter (38m21s)
succeeded. Fitter used 5,661 ALMs, 299 RAM blocks, 2,380,416 RAM bits, 11 DSP,
and one PLL. The final Assembler failed with `u2b_bcm_netlist != NULL` at
`asm_model_generator.h:217`; an isolated assembler retry produced no artifact.
Timing analysis did not run.
**Resource/timing delta:** +74 ALMs, +205 registers, -1 RAM block, -512 RAM
bits from A-003. Timing: unavailable, not assumed.
**Outcome, reversal/workaround, remaining risk, and next gate:** The error is
currently classified as a Quartus assembler/toolchain failure, not an RTL
success or RTL root cause. Preserve the local database/logs; next reproduce
from a known source revision and, if repeatable, isolate source/configuration
sensitivity before considering a tool-version change. Details: [issue 005](issues/005-quartus-assembler-internal-error.md).

### A-010 — Controlled baseline build clears VM/toolchain as the broad cause

**Date:** 2026-09-13
**Decision/change:** Build the exact pre-Phase-1 source revision
`7ef8f0fb84abd4ae5a6a187805fd910351ae57dc` from an archive in a separate local
ext4 VM directory. This avoids both the shared-mount limitation and any reuse
of the retained Phase 1 database.
**Alternatives and rationale:** Assume the previously recorded baseline proves
the current installation remains healthy (rejected: a fresh controlled run is
stronger evidence), or reinstall/change Quartus first (rejected: would obscure
the comparison and invalidate the matching-toolchain control).
**Hot/cold impact:** None.
**Evidence:** **Quartus** — successful complete flow: 5m58s analysis/synthesis,
35m45s fitter, 1m17s assembler, 4m12s timing analyzer; 47m12s total. It
produced `ap_core.sof` and `ap_core.rbf`; all timing checks passed.
**Resource/timing delta:** Matches A-003 baseline: 5,587 ALMs, 7,217
registers, 2,380,928 RAM bits, 11 DSP, 1 PLL. This is a control result, not a
new Phase 1 result.
**Outcome, reversal/workaround, remaining risk, and next gate:** The prior
working assumption that the assembler failure might be generic to the VM or
Quartus installation is disproved. The failure is now narrowed to the Phase 1
RTL/QSF integration delta. Next isolate its trigger in controlled variants;
Phase 2 and Pocket testing remain blocked.

### A-011 — Isolation 1 clears source inclusion as the assembler trigger

**Date:** 2026-09-13
**Decision/change:** Compile exact baseline source with only the two new SDRAM
modules included as SystemVerilog/QSF sources. The modules were deliberately
unconnected at the top level.
**Alternatives and rationale:** Start by wiring the full Phase 1 path again
(rejected: would not separate source-list/tool parsing from integrated-netlist
behavior) or inspect the internal assertion alone (rejected: no reliable root
cause can be inferred from a vendor assertion).
**Hot/cold impact:** None; this is an unconnected build-only experiment.
**Evidence:** **Quartus** — complete build after VM-crash restart: fitter,
assembler, and timing analyzer completed and `ap_core.sof`/`ap_core.rbf` were
created. Fit reported 5,587 ALMs and 300 RAM blocks. The timing analyzer ran;
no failure was reported.
**Resource/timing delta:** Resource capacity matches A-003. Registers reported
7,242 (+25); this unconnected experiment is not used to infer a functional
resource delta because fitter/timing settings and the interrupted/restarted
build context differed.
**Outcome, reversal/workaround, remaining risk, and next gate:** The former
hypothesis that adding the SystemVerilog source declarations causes the
assembler failure is disproved. The trigger is in live integration. Next test:
arbiter-only framebuffer-path integration, with CPU bridge/MMIO disconnected.

### A-012 — Isolation 2 clears arbiter framebuffer-path integration

**Date:** 2026-09-13
**Decision/change:** Insert the arbiter into the active framebuffer-to-SDRAM
path, while tying every CPU-side arbiter request inactive and omitting the CPU
bridge/MMIO entirely.
**Alternatives and rationale:** Add the bridge and MMIO together (rejected:
would not separate controller-path logic from CDC/control integration), or
infer safety from the arbiter testbench (rejected: an assembler trigger is a
post-fit integration property, not a unit-simulation result).
**Hot/cold impact:** CPU path inert; framebuffer remains the sole live owner.
**Evidence:** **Quartus** — complete successful flow, with `.sof`/`.rbf` and
timing analysis. Fitter: 5,624 ALMs / 18,480, 300 / 308 RAM blocks; total
runtime 2h53m. No Pocket claim is made.
**Resource/timing delta:** +37 ALMs relative to A-003; RAM blocks unchanged.
Timing analyzer completed without a reported failure; exact slack is not
promoted as a Phase 1 timing result because this is not the full integration.
**Outcome, reversal/workaround, remaining risk, and next gate:** The live
framebuffer arbitration route is cleared as the direct assembler trigger.
Next: instantiate and connect the CPU bridge/CDC with its system request side
held inactive, then test MMIO integration separately if that succeeds.

### A-013 — Isolation 3 clears idle bridge/CDC integration

**Date:** 2026-09-13
**Decision/change:** Instantiate the CPU bridge, connect its SDRAM-side port to
the arbiter, and hold its system-side request inactive. `mp3_soc` SDRAM MMIO
and its top-level interface wiring remain absent.
**Alternatives and rationale:** Add MMIO at the same time (rejected: would
confound bridge/CDC and CPU-register-interface effects), or treat passing bridge
unit simulations as sufficient (rejected: this experiment specifically tests
the post-fit live two-clock integration).
**Hot/cold impact:** The system request is inert; no CPU transaction, cold-data
migration, or Pocket behavior is enabled.
**Evidence:** **Quartus** — complete successful flow, 3h23m10s total; fitter,
assembler, and timing analyzer all completed and created `.sof`/`.rbf`.
Fit: 5,522 ALMs, 7,104 registers, 2,380,416 RAM bits, 299 RAM blocks, 11 DSP,
one PLL. Slow-model setup slack 1.021 ns; `clk_74a` hold slack 0.297 ns.
**Resource/timing delta:** -65 ALMs and -1 RAM block versus A-003. This is a
controlled variant, so it is not substituted for final Phase 1 resource values.
**Outcome, reversal/workaround, remaining risk, and next gate:** The live idle
bridge/CDC route is cleared as the direct assembler trigger. A source-diff
recheck found the failing full build also contains an untested, non-SDRAM
`mp3_fb` declaration-order change. The prior conclusion that only MMIO/top-level
wiring remained was premature. Next compile that residual change alone, then
test MMIO integration; retain the architecture and current Quartus version
until both results exist.

| Isolation scope | Earlier assumption | Corrected scope / evidence |
|---|---|---|
| Post-isolation-3 residual delta | Only `mp3_soc` MMIO and its top-level wiring remained. | `mp3_fb` has an earlier declaration-order change present in the failing full build but absent from isolation variants 1–3. It must be tested independently before assigning cause. |

### A-014 — External-audit reconciliation and compact-index correction

**Date:** 2026-09-13
**Decision/change:** Reconcile an external review that reported the repository
as pre-A-006/A-008 state. Verify local `main` ancestry and review artifacts,
then update the compact project register so it links issue 005 and accurately
states the distinction between full-integration fitter evidence, assembler
blocker, and passing controlled isolations.
**Alternatives and rationale:** Treat the review as current without checking
(rejected: it contradicted committed source/tests and audit entries), or ignore
it as wholly stale (rejected: its reading exposed stale wording in the compact
register that could mislead a future reviewer).
**Hot/cold impact:** None.
**Evidence:** **code-review** — `e8ddf74` is an ancestor of local `main`; its
direct review-link section is present in `docs/PROJECT_REGISTER.md`. `AGENTS.md`
contains the audit-entry requirements; arbiter and bridge testbenches contain
the CPU-first/framebuffer-arrival and both reset-release-order cases introduced
by `4af2065`. Current build evidence is A-006 and A-010–A-013.
**Resource/timing delta:** Not applicable; documentation/index correction only.
**Outcome, reversal/workaround, remaining risk, and next gate:** Pin external
reviews to a full commit SHA or direct blob URLs, not an unverified default
branch snapshot. Continue isolation 4; do not treat the full integration as
validated until it creates a timing-clean artifact and passes Pocket tests.

### A-015 — Fresh-clone external audit validates current evidence state

**Date:** 2026-09-13
**Decision/change:** Obtain a new external SDRAM audit from a fresh Git clone
pinned to `cac4ca02d95c24e19810301a5e497ab0328187ad`, following two stale
rendered-page reviews.
**Alternatives and rationale:** Reuse rendered GitHub-page fetching (rejected:
it demonstrably returned a cached old snapshot), or accept an unpinned review
(rejected: the reviewed state would not be falsifiable).
**Hot/cold impact:** None.
**Evidence:** **external review** — verified direct review links, A-006 fitter
and assembler evidence, isolation A-010–A-013, CPU-first/framebuffer-arrival
arbiter test, both bridge reset-release-order tests, AGENTS audit rules, and
the intentionally deferred Phase 2 address-decode gate. The review confirmed
isolation 4 (`mp3_fb` declaration-order delta alone) as the correct remaining
pre-MMIO discriminator.
**Resource/timing delta:** Not applicable; review evidence only.
**Outcome, reversal/workaround, remaining risk, and next gate:** The audit
loop is now reconciled. All future external reviews must state the exact commit
SHA or use direct blob URLs. Isolation 4 remains active; after its result,
test the MMIO/top-level delta only if the declaration-order change passes.

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
