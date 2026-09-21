# Tau audit trail

This is the chronological handoff log for design choices, implementation
milestones, failed attempts, reversals, and verification results. The compact
current-state index is [PROJECT_REGISTER.md](PROJECT_REGISTER.md); detailed
reasoning remains in the linked documents.

Audit identifiers are permanent and unique even when their chronological
display order reflects later recovery of earlier evidence. `make test-host`
runs `tools/check_audit_trail.py` to reject duplicate entry IDs.

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
| A-017 | **Quartus** Phase 1-equivalent isolation, 2026-09-13 | 5,706 / 18,480 (31%) | 299 / 308 (97%) | 2,380,416 / 3,153,920 (75%) | 0.283 ns hold, shown slow model | Successful 42m19s full flow; at the time, clean exact-current-source build remained the release gate (cleared by A-024). |
| A-024 | **Quartus** fresh current-source Phase 1 build, 2026-09-14 | 5,706 / 18,480 (31%) | 299 / 308 (97%) | 2,380,416 / 3,153,920 (75%) | 0.119 ns hold, fast 0C | Successful 39m28s flow; resource counts match A-017. Pocket diagnostic remains pending. |
| A-053 | **Quartus** core bridge-mux integration, 2026-09-14 | see entry | 300 / 308 (97%) | 2,380,928 / 3,153,920 (75%) | 11 / 66 (17%) | 0.086 ns minimum reported hold | Successful 3h32m29s flow; mapped-window client tied inactive. |
| A-055/A-056 | **Quartus** opt-in CPU window, 2026-09-14 | see entry | 300 / 308 (97%) | pending extraction | 11 / 66 (17%) | 0.120 ns minimum reported hold | Successful 1h18m22s flow; Phase 2 macro enabled in isolated VM copy only. |
| A-057 | **Quartus** current macro-off regression, 2026-09-16 | see entry | 300 / 308 (97%) | pending extraction | 11 / 66 (17%) | 0.118 ns minimum reported hold | Successful 43m04s flow; current default branch remains buildable. |
| A-072 | **Quartus** A-067 bridge read-timing diagnostic, 2026-09-17 | 6,196 / 18,480 (34%) | 300 / 308 (97%) | 2,380,928 / 3,153,920 (75%) | 11 / 66 (17%) | 0.125 ns hold, multicorner | Successful macro-enabled flow; Pocket gate pending. |

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

### A-016 — Isolation 4 clears the residual `mp3_fb` delta

**Date:** 2026-09-13
**Decision/change:** Apply only the `mp3_fb` declaration-order change that had
been present in the failing full build but absent from isolations 1–3.
**Alternatives and rationale:** Combine it with MMIO integration (rejected:
would reintroduce two variables after A-013 corrected that scope gap), or
assume semantic equivalence proves assembler equivalence (rejected: the
investigation concerns Quartus's post-fit behavior).
**Hot/cold impact:** None; the change preserves framebuffer behavior and this
is a Quartus-only experiment.
**Evidence:** **Quartus** — successful 1h03m46s complete flow with `.sof`/`.rbf`
and timing analysis. Fit: 5,522 ALMs, 7,104 registers, 299 RAM blocks.
Slow-model setup slack 1.021 ns; `clk_74a` hold slack 0.297 ns.
**Resource/timing delta:** No measured delta from A-013; this is expected for
the declaration-order-only change.
**Outcome, reversal/workaround, remaining risk, and next gate:** The residual
`mp3_fb` change is cleared. The exact remaining isolation target is the SDRAM
diagnostic MMIO/register expansion in `mp3_soc` and associated `core_game.vh`
wiring. Test it next; only a reproducing result justifies splitting that narrow
delta further.

### A-017 — Isolation 5 clears MMIO/top-level wiring; original assembler error does not reproduce

**Date:** 2026-09-13
**Decision/change:** Add the current `mp3_soc` diagnostic mailbox register map
and all associated `core_game.vh` signal wiring to passing isolation 4.
**Alternatives and rationale:** Attribute the original failure to MMIO without
testing (rejected: it would convert temporal coincidence into a root-cause
claim), or change Quartus now (rejected: this matched-toolchain experiment is
the decisive control).
**Hot/cold impact:** No request is issued by the existing playback firmware;
the mailbox remains diagnostic-only. No cold-data migration or Pocket claim is
enabled.
**Evidence:** **Quartus** — successful complete 42m19s flow with `.sof`/`.rbf`.
Fit: 5,706 ALMs, 7,414 registers, 2,380,416 RAM bits, 299 RAM blocks, 11 DSP,
one PLL. Slow-model setup slack 1.034 ns; shown tightest hold slack 0.283 ns.
**Resource/timing delta:** +119 ALMs and -1 RAM block relative to A-003. This
is Phase 1-equivalent, not yet the exact current-source release build.
**Outcome, reversal/workaround, remaining risk, and next gate:** All functional
FPGA deltas in the original failing build have now passed controlled flows. The
assembler assertion is non-reproducible and classified as one-off
Quartus/build-state behavior, not an RTL root cause. Fresh-build the exact
current source from local ext4; only a timing-clean artifact from that build
can enter the Pocket diagnostic gate.

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

### A-050 — Conversation-first triad and incremental RTL gate

**Date:** 2026-09-13
**Audit-ID correction:** Renumbered from a duplicate `A-017` on 2026-09-14.
`A-017` remains exclusively the Quartus Isolation 5 record above. This entry's
references were updated to `A-050`; no technical claim changed.
**Decision/change:** Add a deterministic Qwen/Codex/Claude Director, install
and configure a loopback-only Computer surface, connect Codex and Claude
directly to the official Figma MCP, add Verilator lint, and split the Icarus
suite into dependency-tracked cached targets.
**Alternatives and rationale:** Retain Cursor as the primary surface (rejected:
the user needs conversation rather than a code-centric editor); use Computer's
homogeneous subagents as the orchestrator (rejected: no reliable per-role model
routing); increase Qwen context above 16k (rejected: prior resource pressure);
or expose all Claude/MCP tools to Qwen (rejected: the initial schema was about
23,600 tokens and exceeded its 16,128-token context). The selected local Qwen
path excludes MCP, exposes six tools, and adds a narrowly anchored recovery for
the model's observed missing outer XML tool-call wrapper.
**Hot/cold impact:** Neither product hot nor cold paths are changed. This is
development tooling only.
**Evidence:** **host** — Director doctor reports Codex, Claude, Icarus 13,
Verilator 5.052, SSH, LM Studio Qwen 7B at context 16,128, and UniClaudeProxy
healthy. A direct proxy request returned `LOCAL_QWEN_OK`; the restricted harness
returned `QWEN_HARNESS_OK`; the recovered bare tool call executed a read-only
`Read` action. `make rtl-lint` completed with pre-existing nonfatal warnings.
**simulation** — the complete seven-test Icarus suite passed, and a second
Make dry run contained no `iverilog` commands, confirming compile reuse. Logs
from Director runs are written to ignored `work/triad/`.
**Resource/timing delta:** No FPGA resource/timing delta; Quartus was not run.
Local model remains the 5.44 GB Q5_K_M 7B GGUF; 14B models are unloaded.
**Outcome, reversal/workaround, remaining risk, and next gate:** The local and
review layers are operational. Qwen's post-tool summary showed one inaccurate
heading in a focused test, so deterministic gates plus Codex and Claude review
remain mandatory. Complete the local Computer account setup and agent profiles,
then run one disposable end-to-end triad task before using it on production RTL.
The future SSH/tmux Quartus launcher remains human-gated and unimplemented until
the VM host alias is explicitly supplied. Claude's Figma MCP is authenticated,
but the Claude CLI itself is not signed in, so the final audit correctly remains
unavailable until the user chooses and authenticates a Claude plan.

### A-018 — Codex-led planning and patch-only local implementation

**Date:** 2026-09-14
**Decision/change:** Replace the Qwen file/shell-tool route with a Codex-owned
plan and a local patch-only Qwen role. Codex emits a structured plan with an
explicit repository path allowlist and compact source excerpts; Qwen receives
only that packet and produces a unified diff. The Director validates diff paths
and `git apply --check`, then asks Codex to apply the exact patch. Codex plans
and performs at most one bounded repair after adversarial review. GPT-5.6 Luna
at low reasoning is the default for routine Codex planning, Figma briefs, patch
application, and review.
**Alternatives and rationale:** Let the 7B model read/edit/run shell tools
(rejected: tool-call parsing was fragile, MCP schemas overflowed its context,
and a focused test showed an inaccurate read summary); upgrade the local model
solely for tool use (deferred: the local role no longer needs tool calling); or
allow open-ended repair loops (rejected: cost and scope need a predictable cap).
**Hot/cold impact:** Neither product hot nor cold paths are changed; this is
development tooling and documentation only.
**Evidence:** **host** — Python syntax/CLI checks, schema parsing, and the
Director's plan/patch-path regression tests pass;
`make rtl-lint` completes with the pre-existing nonfatal warnings described in
A-050; `make test-rtl` passes framebuffer, target command, exact EQ preset, PCM
decay, EQ cycle, SDRAM arbiter, and SDRAM bridge simulations. The Director can
save a gate transcript to ignored `work/triad/gate.log`.
**Resource/timing delta:** No FPGA resource/timing change; no Quartus run. The
local model remains Qwen2.5-Coder-7B Q5_K_M at 16,128 context.
**Outcome, reversal/workaround, remaining risk, and next gate:** The role
boundaries and reusable guide are documented. A complete live triad run remains
pending because Claude CLI authentication and Computer account/profile setup
require user completion. The Director fails before edits when Claude is not
signed in, preventing a partial full-run. Next: complete those account steps,
run a disposable end-to-end task, inspect plan/patch/gate/review/audit artifacts,
then record the live validation result. Quartus remains human-gated.

### A-019 — Apply validated Qwen patches deterministically

**Date:** 2026-09-14
**Decision/change:** Revise A-018's patch application step. The Director now
validates the Qwen unified diff against the allowlist, runs `git apply --check`,
and applies those exact patch bytes directly. It does not grant Codex a broad
write session solely to apply the diff. The plan schema now expresses the same
`patch`/`no_change` path-count constraints as runtime validation. A Qwen
`NO_CHANGE` response under a patch plan stops safely without editing files;
binary and symlink patches are rejected. Documentation clarifies that LM
Studio is asked for 16,000 context and aligns this to the observed 16,128.
**Alternatives and rationale:** Keep Codex applying through a workspace-write
agent and trust its instruction to run the exact checked patch (rejected: the
agent retained broader write capability and no post-apply path check); manually
allow the schema to be looser than runtime validation (rejected: it creates
confusing plan-generation failures).
**Hot/cold impact:** Neither product hot nor cold paths are changed.
**Evidence:** **code-review** — Luna's read-only review identified the broad
apply capability, schema/runtime mismatch, ambiguous Qwen `NO_CHANGE` response,
and context-setting discrepancy; all four are addressed in this entry's
implementation. **host** — 11 Director plan/patch safety tests pass through
`make test-host`; `python3 tools/triad/director.py gate` passes host checks and
Verilator lint; the full `make test-rtl` suite passes. Existing lint warnings
remain unchanged from A-050.
**Resource/timing delta:** No hardware changes or Quartus run.
**Outcome, reversal/workaround, remaining risk, and next gate:** The Codex
planner still decides scope and allowed paths, while the Director is the only
patch applier and can only apply diff paths that pass allowlist validation.
Qwen may decline and stop the run; Codex must then revise the packet manually.
The full triad remains unvalidated until Claude CLI authentication and Computer
account/profile setup are completed. Next, perform one disposable live triad
run and inspect its plan/patch/gate/review/audit artifacts. Quartus remains
human-gated.

### A-020 — Defer targeted MP3 FPGA acceleration research until SDRAM is proven

**Date:** 2026-09-14
**Decision/change:** Add a post-SDRAM research gate to profile the MP3 software
decoder and evaluate narrowly targeted FPGA logic/DSP acceleration for measured
bottlenecks such as IMDCT, Huffman decode, dequantization, or the subband
synthesis/polyphase filterbank.
**Alternatives and rationale:** Start implementing a broad hardware decoder
now (deferred: SDRAM capacity and validation remain the active architecture
gate, and no per-stage profile or resource/benefit measurement exists), or
assume a PMOD I2S2 controller provides codec acceleration (rejected: it is an
audio-interface reference, not an MP3 decode engine).
**Hot/cold impact:** No implementation yet. The existing protected audio
software path remains unchanged; any future accelerator would be on the hot
path and must earn its complexity through measured end-to-end benefit.
**Evidence:** **design** — a research question and evaluation criteria have
been added to the roadmap; no hardware or performance claim is made.
**Resource/timing delta:** Not applicable; documentation-only.
**Outcome, reversal/workaround, remaining risk, and next gate:** After the
expanded SDRAM data path passes timing and Pocket concurrency/stability tests,
profile the decoder, compare candidate kernels for bit-exactness, throughput,
latency, ALM/DSP/M10K cost, clock closure, and audio underruns, then prototype
only a stage with meaningful system-level value. FPGA sound generators are
possible fixed-point/streaming references; external I2S hardware compatibility
must be evaluated separately. The software implementation remains reference
and fallback.

### A-021 — Provisional soft-lockup interpretation; corrected by A-024

**Date:** 2026-09-14
**Decision/change:** Record the live VM observation during the fresh exact-source
Quartus build check. The UTM console displayed repeated Linux watchdog reports
of CPU soft lockups naming `quartus_fit`, `quartus_asm`, and `quartus_sta`, as
well as `systemd` watchdog timeouts. The VM is marked Started, but console
input did not produce a visible shell response; the managed build session
remained open and produced no new output during a 30-second check. Completion,
exit status, reports, and programming artifacts were not verified.
**Alternatives and rationale:** Declare the build complete based on elapsed
time (rejected: there is no successful flow summary or verified artifact), or
immediately restart the VM (deferred: this could destroy the live process and
valuable Quartus database/log state).
**Hot/cold impact:** None to RTL or the audio path; this is a host/VM build
reliability observation.
**Evidence:** **host** — UTM console screenshot and unchanged managed-session
output; an SSH probe reached the forwarded service but password authentication
was rejected. These observations do not prove whether Quartus is still making
progress or whether output files are complete. The user also recalled that a
full-screen Crunchyroll video was playing on the Mac during the build. This is
recorded only as a possible host-load confounder; host utilization was not
measured, so it is not established as the cause.
**Resource/timing delta:** Not measured; no final fit/timing report inspected.
**Outcome, reversal/workaround, remaining risk, and next gate:** Treat the
current build as unverified and potentially stalled. Preserve `db/` and
`output_files/`; recover a responsive console or authenticated SSH session and
inspect process state, flow report, exit code, `.sof`/`.rbf`, and timing report
before deciding whether to wait or restart. For a future controlled run, pause
avoidable host-heavy workloads and capture resource data if possible. Do not
begin Pocket testing without a verified timing-clean artifact. Detailed
record: [issue 006](issues/006-vm-quartus-soft-lockup.md).

### A-022 — Post-build VM activity initially mistaken for compile progress; corrected by A-024

**Date:** 2026-09-14
**Decision/change:** Recheck the active build without stopping or restarting
the VM. The managed build session remained open and silent; UTM marked the VM
Started and its console showed the guest text login prompt. Host-side samples
found the QEMU process alive with CPU readings varying from 0% to 11.4% over
short intervals, and the qcow2 disk's last-modified time was 07:04:23 local.
**Alternatives and rationale:** Treat CPU or virtual-disk activity as proof
that Quartus is advancing (rejected: either can reflect guest OS/input activity
and no stage report was accessible), or stop the VM to force a status (rejected:
that risks losing the current Quartus state).
**Hot/cold impact:** None to RTL or playback paths.
**Evidence:** **host** — macOS `top`/`ps` and qcow2 metadata; QEMU uses four
TCG-emulated vCPUs and 12 GiB guest RAM. Host memory samples showed about
23 GiB used, 73–540 MiB unused, and about 5.8 GiB compressed. This supports
host resource pressure as a plausible confounder, including the user's report
of full-screen video playback, but does not prove causality or Quartus progress.
**Resource/timing delta:** Quartus resource and timing results remain
unavailable.
**Outcome, reversal/workaround, remaining risk, and next gate:** Build status
remains unknown; do not claim pass/fail and do not program the Pocket. Obtain
guest process/report/artifact status from a responsive authenticated session
before deciding to wait or restart. See [issue 006](issues/006-vm-quartus-soft-lockup.md).

### A-023 — Post-build disk changes initially mistaken for compile progress; corrected by A-024

**Date:** 2026-09-14
**Decision/change:** Perform another short, read-only progress check. The
managed build session remained open and silent. QEMU showed intermittent CPU
use from 0% to 5.3%; the qcow2 modification time advanced to 07:07:45 local.
**Alternatives and rationale:** Treat recent VM CPU/disk activity as proof
that Quartus is progressing (rejected: guest OS activity is also possible and
no Quartus report or process list is available), or declare it stalled solely
because the session is silent (rejected: the disk timestamp and CPU samples
show some VM activity).
**Hot/cold impact:** None to RTL or playback paths.
**Evidence:** **host** — macOS `top` and qcow2 metadata. Host memory remained
under substantial pressure at roughly 23 GiB used, 81–151 MiB unused, and
about 6.7 GiB compressed. Full-screen video remains a possible but unproven
contributor.
**Resource/timing delta:** Quartus resource and timing results unavailable.
**Outcome, reversal/workaround, remaining risk, and next gate:** Current
evidence supports only that the VM is not entirely idle; compile progress and
completion remain unknown. Keep VM/database intact and inspect the guest flow
report and artifacts once authenticated shell access is available. See
[issue 006](issues/006-vm-quartus-soft-lockup.md).

### A-024 — Fresh SDRAM Phase 1 Quartus build passed; stale-console diagnosis corrected

**Date:** 2026-09-14
**Decision/change:** Authenticate interactively to the guest and inspect the
completed current-source build. In `/home/taualpha/tau-current-b729a7b`,
`ap_core.flow.rpt` reports **Successful** at 01:24:08; `ap_core.done` is
stamped 01:27:38. Both `ap_core.sof` and `ap_core.rbf` are present. Flow time
was 39m28s: analysis/synthesis 5m04s, fitter 29m59s, assembler 1m03s, and
timing analysis 3m22s. A recursive comparison of the build snapshot's
`src/fpga` against the current shared project found no functional source
difference; `apf/build_id.mif` is regenerated by the pre-flow script, while
the other differences are generated build outputs. The snapshot directory has
no Git metadata, so its commit could not be queried inside the VM.
**Alternatives and rationale:** Continue treating the compile as stalled from
the old console buffer and post-build QEMU activity (rejected: the build flow
and output files prove it completed), or attribute the earlier soft-lockup
messages to this build (rejected: guest boot was 2026-09-13 12:07:22 and the
latest visible watchdog timestamp was about 3,784.94 seconds after boot,
roughly 13:10 that day, over eleven hours before the fresh build).
**Hot/cold impact:** None to RTL or playback paths. This clears only the
Quartus build gate; it does not validate SDRAM transactions on Pocket.
**Evidence:** **Quartus | host** — successful flow report, done marker, fit and
STA summaries, generated programming files, guest `uptime -s`, and source-tree
comparison. The build directory is named for snapshot `b729a7b`; current FPGA
source matches the shared tree except for the generated build ID MIF and
Quartus-generated files/directories.
**Resource/timing delta:** Fit: 5,706 / 18,480 ALMs (31%), 7,414 registers,
2,380,416 / 3,153,920 block-memory bits (75%), 299 / 308 RAM blocks (97%),
11 / 66 DSP blocks (17%), 1 / 4 PLLs (25%). These resource counts match
controlled isolation 5 (no delta). TNS is 0 and all reported slack is
positive; minimum setup slack is 0.914 ns and tightest hold slack is 0.119 ns
(Fast 1100mV, 0C). The hold margin is narrow and must be rechecked after
clocking/CDC changes.
**Outcome, reversal/workaround, remaining risk, and next gate:** This reverses
the provisional “current build stalled” interpretation in A-021–A-023. The
kernel console messages were stale from an earlier guest uptime; the host
resource samples at 07:04–07:07 were collected hours after build completion,
so they cannot establish compile-time memory pressure or video impact. The
user's full-screen Crunchyroll report is retained as an unverified possible
confounder because contemporaneous host measurements are unavailable. The
original Assembler assertion remains non-reproducible; keep its history. Next,
perform the controlled Pocket diagnostic and concurrency/stability matrix
before migrating SDRAM data. See [issue 005](issues/005-quartus-assembler-internal-error.md)
and [issue 006](issues/006-vm-quartus-soft-lockup.md).

### A-025 — Require confirmation for non-preferred Codex model tiers

**Date:** 2026-09-14
**Decision/change:** Keep GPT-5.6 Luna as the default and Luna/Terra as the
preferred models. Before launching Codex, the Director now asks for interactive
approval if the selected model is outside that pair (including GPT-6 Astra),
or if reasoning effort is `xhigh`, `max`, or `ultra` (also recognizing the
spelled-out `extra high`). A full `run` preflights both its planner and reviewer
selections before any project edits; non-interactive execution fails closed.
**Alternatives and rationale:** Rely only on the default values and the user's
environment setup (rejected: environment overrides could silently raise model
or reasoning usage); require a separate typed flag (deferred: an interactive
yes/no prompt makes the approval visible at the point of use).
**Hot/cold impact:** Neither product hot nor cold paths are affected.
**Evidence:** **host** — Director policy tests cover preferred models, Astra,
non-preferred models, high-effort aliases, full-run reviewer selection, and
interactive approval/decline. `make test` and `git diff --check` pass.
**Resource/timing delta:** No hardware changes or Quartus run.
**Outcome, reversal/workaround, remaining risk, and next gate:** The guard
controls the Director's CLI-launched Codex requests only; manually launched
Codex sessions remain under their own settings. Approval is per Director
command/run and does not persist. Next, test an approved advanced run only if
the user requests one.

### A-026 — Stage a separate Phase 1 Pocket SDRAM readback diagnostic

**Date:** 2026-09-14
**Decision/change:** Add `fw/sdram_diag.c`, a separate `sdram-diag` firmware
target, three deterministic diagnostic framebuffer states, and a reproducible
side-by-side Pocket packager. The candidate performs 183 readback checks only
at SDRAM word addresses `0x00080000–0x000FFFFE` (byte offsets 1–2 MiB), above
the reserved 1 MiB framebuffer/guard region. The successful VM RBF was copied
to host staging rather than overwriting the release bitstream.
**Alternatives and rationale:** Put the diagnostic directly in the shipping
player (rejected for the first hardware transaction: it expands the regression
surface and makes a mailbox fault look like a playback fault); overwrite the
normal TAU package (rejected: a distinct core/platform preserves side-by-side
baseline comparison); start immediately with a full concurrent 1 MiB CRC
(deferred: isolate basic wiring/address/data/byte-lane correctness before
mixing it with decoder, SD, and renderer load).
**Hot/cold impact:** The player ROM and all audio-critical BRAM code/data are
unchanged. The new ROM is developer-only and uses a currently unassigned cold
SDRAM region. No mapped SDRAM, cache, linker, decoder, FIFO, EQ, or playback
path is changed.
**Evidence:** **host | simulation | code-review** — the diagnostic compiles
warning-free for RV32IM and links to 4,895 bytes; the staged 4,872-byte ROM is
not byte-identical to the release ROM, confirming the release artifact was not
silently reused. Arbiter and CDC bridge regressions pass. The 24-fixture RGB565
snapshot check passes, including distinct running/pass/fail states, and all
three were visually inspected. Package JSON parses; reversing the staged
`bitstream.rbf_r` reproduces the VM RBF exactly; packaged diagnostic ROM equals
the staged ROM. The source RBF SHA-256 is
`0c00362795f22486af8aece80d1a3c6b1eb857e0783699a7fa6394163b1a6dc7`.
No **Pocket** claim is made.
The bundle was then installed on the mounted exFAT `Pock` volume. Recursive
comparisons of the diagnostic core/assets and byte comparisons of both platform
files passed; card-side bitstream and ROM hashes match staging. This remains
**host** installation evidence until the Pocket executes it.
**Resource/timing delta:** Firmware-only after A-024; no RTL changed and no new
Quartus run is required. A-024 remains the applicable fit/timing evidence:
5,706 ALMs, 299/308 RAM blocks, TNS 0, minimum setup 0.914 ns, minimum hold
0.119 ns.
**Outcome, reversal/workaround, remaining risk, and next gate:** Two failed
host attempts are retained. First, objcopy failed because target-specific
`OUT` was selected after the script's original `mkdir`; creating the selected
directory after the target switch fixed it without writing a ROM. Second, the
new snapshots initially raised `NameError: UI_BG`; importing that value from
`player.c` and enforcing distinct diagnostic checks fixed the renderer. The
mailbox completion status has no sequence counter, so firmware waits 128
system cycles after start before polling `busy`, with a 0.5-second timeout;
Pocket results still decide whether this is sufficient. Run five warm repeats
and five cold-boot repeats. Only after ten 183/0 passes should work proceed to
concurrent 1 MiB CRC/pattern traffic during highest-bitrate MP3 playback and
all visualizers. See [Pocket procedure](SDRAM_POCKET_DIAGNOSTIC.md).
The resolved host failures are reproduced in [issue 007](issues/007-sdram-diagnostic-staging.md).

### A-027 — Correct diagnostic core folder/shortname identity mismatch

**Date:** 2026-09-14
**Decision/change:** After repeated Pocket `Load error in 'core' / General
Error` and `Error in core setup` screens, compare the side-by-side package with
Analogue's core naming contract. Change diagnostic `metadata.shortname` from
`TAU SDRAM DIAG` to `TAU_SDRAM_DIAG`, exactly matching folder
`alfatreze.TAU_SDRAM_DIAG`, and make the packager assert the derived
`author.shortname` identity before producing output.
**Alternatives and rationale:** Diagnose SDRAM RTL or firmware (rejected: core
setup failed before the ROM ran); replace the folder with a space-containing
name (rejected: the existing underscore identifier is unambiguous and the
friendly platform name remains available for UI); modify several manifests at
once (rejected: preserve a narrow, evidence-backed correction).
**Hot/cold impact:** Neither path is affected. This changes diagnostic package
metadata only; the player ROM, diagnostic ROM, RBF, RTL, and normal TAU package
are unchanged.
**Evidence:** **Pocket** — the user reproduced both setup error screens several
times. **code-review** — the first package folder and manifest shortname did
not correspond, contrary to Analogue's documented naming convention. **host**
— the corrected packager now rejects identity mismatch and regenerates valid
JSON. Reaching the diagnostic on Pocket remains pending, so the cause is the
leading evidence-backed diagnosis rather than hardware-confirmed resolution.
**Resource/timing delta:** None; no firmware or RTL change and no Quartus run.
**Outcome, reversal/workaround, remaining risk, and next gate:** The first
installed bundle is superseded and its failures do not count toward SDRAM test
runs. The corrected paths were regenerated, reinstalled on `Pock`, verified
against staging by identity and hash, and flushed. If core setup now succeeds,
run the warm/cold matrix; if not,
continue package-level diagnosis without changing SDRAM logic. See
[issue 008](issues/008-diagnostic-core-identity-mismatch.md).

### A-028 — APF diagnostic log clears package loading; runtime state remains unknown

**Date:** 2026-09-14
**Decision/change:** Preserve and inspect the Pocket OS 2.6 developer log after
the corrected diagnostic loaded but A produced no visible response. Reclassify
the active investigation from package setup to firmware/runtime state: APF
parsed all manifests, loaded the bitstream and exact 4,872-byte ROM, completed
the firmware data slot, reached `Run`, and returned OK from Reset Exit.
**Alternatives and rationale:** Treat the APF `already running` warning as the
fault (rejected: the known-working TAU log contains the same warning); infer an
SDRAM failure from lack of reported PASS (deferred: the exact visible screen is
not yet known and APF logs cannot observe firmware MMIO); treat A as the start
command (corrected: the suite starts automatically and A is polled only after
the suite returns).
**Hot/cold impact:** Neither path is changed. This is evidence collection only.
**Evidence:** **Pocket | host | code-review** — user-observed nonresponse; APF
log SHA-256
`a750529af86d42c3f6004db6ea6832bb11ae2a0fd9f3fcb48d8f7cb5ab0bd4d9`;
comparison with the working TAU log; inspection of the diagnostic control flow.
**Resource/timing delta:** None; no code or RTL change and no Quartus run.
**Outcome, reversal/workaround, remaining risk, and next gate:** Package load
is cleared as the current blocker, but SDRAM is not passed or failed. A code
review found that a permanently busy mailbox can trigger roughly 247 sequential
0.5-second timeouts—about two minutes—before input is polled, making the test
appear frozen. Obtain the exact on-screen label/photo, then build a fail-fast
timeout reporter if a progress screen is present; investigate execution/video
first if it is black. See [issue 009](issues/009-sdram-diagnostic-nonresponsive.md).

### A-029 — Make the SDRAM diagnostic boot and timeout failures visible

**Date:** 2026-09-14
**Decision/change:** Following the user’s black-screen report, revise the
developer diagnostic ROM to draw a boot frame before checking the firmware/RTL
contract, display expected and actual version words on mismatch, and terminate
the pattern suite after its first timeout. Add the required named
version-mismatch framebuffer fixture.
**Alternatives and rationale:** Continue relying on APF logs (rejected: they
prove loading/reset but cannot observe CPU MMIO or SDRAM); infer the mismatch
from a black screen (rejected: black cannot distinguish firmware execution,
version, or framebuffer causes); change RTL instrumentation first (deferred:
the ROM-only probe is faster, reversible, and sufficient to classify the next
failure).
**Hot/cold impact:** No product hot/cold path changes. The diagnostic ROM alone
changed; normal TAU ROM, FPGA RBF, playback, decoder, FIFO, and SDRAM RTL are
unchanged.
**Evidence:** **host | code-review** — RV32IM build with warnings as errors
passes; 25 deterministic RGB565 fixtures, including visual inspection of the
version-mismatch screen, pass. The 5,568-byte ROM SHA-256 is
`6ba3bbbdf0c730c522b6bb346440f87cb4bde2228f594bd175b77a7d83f062de`.
It was copied to `/Volumes/Pock`, byte-compared to staging, and flushed.
**Resource/timing delta:** Firmware-only; no Quartus build or FPGA resource/
timing delta. A-024 remains the hardware fit/timing reference.
**Outcome, reversal/workaround, remaining risk, and next gate:** A first copy
attempt failed harmlessly when Pocket detached USB SD Access between mount and
write; the successful remount retry is verified. Relaunch now must show either
a boot/version mismatch/fail/pass screen. If it remains black, CPU execution or
framebuffer command delivery—not SDRAM correctness—is the next investigation.
See [issue 009](issues/009-sdram-diagnostic-nonresponsive.md).

### A-030 — Separate CPU execution from early framebuffer availability

**Date:** 2026-09-14
**Decision/change:** After the fail-visible diagnostic ROM was verified on the
Pocket SD card but still displayed black, add a 0.5-second boot warm-up before
the first framebuffer command and issue a read-only APF `GETFILE` request for
the already-loaded firmware slot as a CPU heartbeat. The Pocket developer log
records that request even if the screen stays black.
**Alternatives and rationale:** Interpret black as an SDRAM mailbox failure
(rejected: no mailbox operation precedes the first screen); modify FPGA RTL
instrumentation (deferred: a ROM-only, reversible probe can first distinguish
CPU execution from display delivery); rely on the existing APF Run/Reset Exit
log (rejected: it cannot observe post-reset CPU instructions).
**Hot/cold impact:** Neither product path changes. This is diagnostic-ROM-only;
the normal player ROM, FPGA RBF, SDRAM controller, decoder, and audio path are
unchanged.
**Evidence:** **Pocket | host | code-review** — Pocket log for the preceding
verified ROM proves APF loading and Reset Exit but contains no target command;
code review confirms normal player startup naturally delays framebuffer MMIO,
whereas the tiny diagnostic does not. The 5,624-byte ROM
`a34f3b994722ab7de216699f83a35bfba6f9b2cc857581f458669838d7bc169f`
builds successfully, passes the 25-fixture deterministic renderer check, and
was byte-compared after copying to `/Volumes/Pock`; Pocket result is pending.
**Resource/timing delta:** Firmware-only; no Quartus run or FPGA resource/
timing delta.
**Outcome, reversal/workaround, remaining risk, and next gate:** Pending
Pocket relaunch. A logged target request plus black output means investigate
the framebuffer/display path. No request means investigate firmware reset/
execution. See [issue 009](issues/009-sdram-diagnostic-nonresponsive.md).

### A-031 — Use APF logs to isolate the SDRAM mailbox from black video

**Date:** 2026-09-14
**Decision/change:** Two new Pocket logs recorded the A-030 `0190` heartbeat
despite black output, so replace the next temporary diagnostic ROM with a
headless one-word SDRAM mailbox preflight. It produces ordered `0190`
checkpoints after CPU entry, initial mailbox-idle confirmation, a safe-region
write, and a matching safe-region read. It intentionally makes no framebuffer
MMIO writes.
**Alternatives and rationale:** Continue revising visible screens (deferred:
the framebuffer is now the suspected dependent path and can mask the mailbox
result); call SDRAM failed from black video (rejected: no SDRAM operation is
observable in that symptom); add RTL debug instrumentation (deferred: four
APF-log checkpoints answer the immediate classification question with a
firmware-only, reversible artifact).
**Hot/cold impact:** No product path changes; diagnostic firmware only.
**Evidence:** **Pocket | host | code-review** — logs
`075944` and `075959` contain a post-Reset Exit `Target: New command [0190]`;
host build succeeded and the 25-fixture renderer regression check passes. The
headless 672-byte ROM is
`1d882cbda1958749f704e9f09bc354700a69b87b393e1ec725670fe2a4cbc249`.
Pocket result is pending.
**Resource/timing delta:** Firmware-only; no Quartus resource/timing change.
**Outcome, reversal/workaround, remaining risk, and next gate:** CPU execution
is now Pocket-confirmed; a black screen no longer implicates ROM/reset. Install
the headless preflight and count ordered `0190` entries. Four enables a focused
framebuffer/arbiter diagnosis; fewer entries locate mailbox noncompletion. See
[issue 009](issues/009-sdram-diagnostic-nonresponsive.md).

### A-032 — Correct the SDRAM arbiter's circular acceptance handshake

**Date:** 2026-09-14
**Decision/change:** Two headless Pocket preflight runs produced exactly the
first two of four `0190` checkpoints, stopping before completion of the first
safe-region SDRAM write. Correct `tau_sdram_arbiter` so request ownership and
CPU acceptance occur when the arbiter forwards an idle request, rather than
requiring `p0_available` in that same cycle. Extend the arbiter testbench to
model the controller's actual `p0_available=0` while a request is asserted.
**Alternatives and rationale:** Treat the result as an electrical SDRAM fault
(rejected: RTL contains a deterministic protocol contradiction matching the
exact failure); change framebuffer logic first (deferred: the headless probe
fails before any framebuffer command); alter `sdram_fb` availability semantics
(deferred: the smallest correction is local to the new arbiter and preserves
the known upstream controller interface).
**Hot/cold impact:** No product firmware data movement changed. FPGA arbitration
behavior changes for the diagnostic CPU port; framebuffer keeps first priority
at a genuine contention point.
**Evidence:** **Pocket | simulation | code-review** — logs `080353` and
`080407` contain exactly two checkpoints; source establishes
`p0_available = state == IDLE && ~port_req`; updated arbiter and CPU-bridge
tests pass (0 failures). **Quartus** — clean 25.1std build succeeds; **Pocket**
validation remains pending. Raw RBF SHA-256:
`840f8d9187526521124864447d72619b533a1ca629d3a6168d003c4ee2082970`.
**Resource/timing delta:** 5,832 / 18,480 ALMs (32%; prior 5,706), 300 / 308
RAM blocks (97%; prior 299), setup TNS 0 and minimum setup slack 0.465 ns;
minimum hold slack 0.120 ns. This remains a narrow but passing timing/resource
state; any further RTL/clocking change requires a fresh report.
**Outcome, reversal/workaround, remaining risk, and next gate:** The prior
Phase 1 RBF is known to stall its first mailbox write and must not be used as
SDRAM capability evidence. The corrected RBF passed Quartus and now must
repeat the headless Pocket preflight. See [issue 009](issues/009-sdram-diagnostic-nonresponsive.md).

### A-033 — Confirm the corrected SDRAM mailbox on Pocket, then restore video

**Date:** 2026-09-14
**Decision/change:** The corrected-RBF headless preflight completed all four
ordered APF `0190` checkpoints on two Pocket runs. Restore the visible bounded
SDRAM diagnostic ROM without changing the now Pocket-confirmed RBF.
**Alternatives and rationale:** Declare Phase 1 complete (rejected: one
write/read proves only the narrow mailbox transaction); retain a headless ROM
for all remaining tests (rejected: the next gate must verify the framebuffer
client that previously appeared black); immediately introduce concurrent load
(deferred until the full bounded suite and visible output are confirmed).
**Hot/cold impact:** Normal player remains unchanged. Diagnostic-only ROM
changes; the RBF is unchanged from A-032.
**Evidence:** **Pocket | host** — logs `103939` and `104220` each contain four
post-reset `0190` entries, matching the documented entry/idle/write/read
checkpoint protocol. Visible 5,688-byte ROM SHA-256
`8680a77ce89a20201d9d35470dc563d89bc6e54cf360a51798287012d366ae5d`
passes the deterministic 25-fixture renderer check and was byte-compared after
copying to `/Volumes/Pock`.
**Resource/timing delta:** ROM-only after A-032; no new Quartus result.
**Outcome, reversal/workaround, remaining risk, and next gate:** The original
black-output conclusion is revised: it was a real arbiter deadlock, not an
unclassified video fault. Launch the restored visible diagnostic and retain a
photo/log of its first result before starting the ten-run matrix. See
[issue 009](issues/009-sdram-diagnostic-nonresponsive.md).

### A-034 — First visible Phase 1 SDRAM diagnostic passes on Pocket

**Date:** 2026-09-14
**Decision/change:** Record the first visible bounded-suite result after the
corrected arbitration RBF: `PASS`, 183 readback checks, zero failures. Preserve
the user-provided screen photo and matching developer log as immutable evidence.
**Alternatives and rationale:** Treat the headless preflight as sufficient
(rejected: it covers only one round-trip); call the entire ten-run matrix
complete (rejected: this is one warm run only); change SDRAM mapping now
(deferred until repeatability and contention gates are passed).
**Hot/cold impact:** No code, RBF, or product behavior change; evidence only.
**Evidence:** **Pocket** — visible screen reports `PASS`, `READBACK CHECKS 183`,
and `FAILURES 0`; retained photo SHA-256
`07fe7af04dcf473f2584ab07f2b9bb64d83f2f323070d1429c91acb57280e6ca`;
matching developer log SHA-256
`d6e8c7b0566700f8da99a70133772ff506d340d8878947095dae448b583cd881`.
**Resource/timing delta:** None; A-032 remains the relevant Quartus result.
**Outcome, reversal/workaround, remaining risk, and next gate:** This is warm
acceptance run 1/5 and must not be generalized to cold/soak/concurrent use.
Repeat four warm runs, then power the Pocket fully off and repeat five cold
runs before the bounded Phase 1 gate is passed. See
[issue 009](issues/009-sdram-diagnostic-nonresponsive.md).

### A-035 — Close the bounded warm/cold SDRAM acceptance gate

**Date:** 2026-09-14
**Decision/change:** Record completion of the specified five-warm/five-cold
bounded SDRAM diagnostic matrix. The user reports every run as `PASS`, 183
readback checks, and zero failures, with no visual change or stall.
**Alternatives and rationale:** Infer all ten results from APF developer logs
(rejected: warm A-triggered repetitions occur inside one core session and are
not individually logged); promote SDRAM to general player storage (deferred:
concurrent scanout/audio contention remains untested); repeat indefinitely
(deferred: the agreed bounded gate is complete and has a distinct next gate).
**Hot/cold impact:** No code or product behavior changes.
**Evidence:** **Pocket user observation | Pocket log** — the user reports the
full ten-run result; finalised log SHA-256
`d4db8dd0b27fa1135eb8d5052870b4a28659e2cee0af2d4309778b2c92dae902`
confirms the final 5,688-byte diagnostic load, Reset Exit, heartbeat, and
clean unload. The log does not individually count in-core warm repeats.
**Resource/timing delta:** None; A-032 remains the current Quartus evidence.
**Outcome, reversal/workaround, remaining risk, and next gate:** The bounded
Phase 1 mailbox/byte-lane gate is passed. It does not validate soak behavior,
mapped SDRAM, or concurrent display/audio traffic. Define and run a separate
contention diagnostic before any player-memory migration. See
[issue 009](issues/009-sdram-diagnostic-nonresponsive.md).

### A-036 — Start a separate compile-gated SDRAM contention player

**Date:** 2026-09-14
**Decision/change:** Add the initial `TAU_SDRAM_STRESS` player-only firmware
path and its design/procedure document. The pump performs one outstanding,
throttled write/read/compare operation at a time in the already proven 1–2 MiB
safe region, calculates a rolling CRC-32, and is enabled only by a developer
core build. `Select+X` is reserved in that build to toggle it; plain X retains
visualizer cycling.
**Alternatives and rationale:** Create a synthetic standalone diagnostic
(rejected: it would not exercise real decoder/audio/visualizer scheduling);
run an unbounded request loop (rejected: it would measure CPU starvation rather
than the intended bounded Phase 2 style of traffic); enable it in normal TAU
(rejected: developer stress behavior must not alter release controls or files).
**Hot/cold impact:** Normal player source behavior is compile-time unchanged.
The stress build uses Phase 1 MMIO only; no mapped SDRAM, linker, decoder, or
audio-critical-memory migration occurs.
**Evidence:** **host | code-review** — stress build succeeds at 153,100 bytes
(84.9% of usable firmware RAM); ordinary rebuild restores the 152,088-byte
release ROM. Hardware and Quartus validation are pending because this is a
firmware-only use of the A-032 RBF.
**Resource/timing delta:** No RTL change, hence no new Quartus result. Firmware
image grows 1,012 bytes versus the ordinary player artifact.
**Outcome, reversal/workaround, remaining risk, and next gate:** The source
and stress ROM artifact exist, but no stress package or Pocket result exists
yet. Package it as a separate player core and validate controls/counters before
using it for the visualizer matrix. See
[SDRAM contention diagnostic](SDRAM_CONTENTION_DIAGNOSTIC.md).

### A-037 — Correct the stress core's overlength platform shortname

**Date:** 2026-09-14
**Decision/change:** After the user could not find TAU SDRAM Stress anywhere in
the Pocket core list, compare its package with the working diagnostic. The
platform ID `tau_sdram_stress` was 16 characters, above Analogue's documented
15-character platform-shortname maximum. Rename the internal platform ID to
`tau_sdram_strs` (14 characters) in all package paths and metadata, retaining
the visible name and valid 16-character core shortname. Add packager validation
for the documented platform-ID rule.
**Alternatives and rationale:** Reinstall the same package after another reboot
(rejected: user had already cold-booted, and the ID itself violates the
official limit); shorten the core shortname (rejected: core shortname has a
separate 31-character limit, and folder/metadata identity already matches);
change artwork or firmware (rejected: unrelated to discovery and both were
copied from a working structure).
**Hot/cold impact:** No player firmware or RBF change; package metadata/path
only.
**Evidence:** **code-review | host | Pocket failed** — official
[Analogue Platform Metadata](https://www.analogue.co/developer/docs/platform-metadata)
sets platform shortnames to 15 characters; official
[core.json documentation](https://www.analogue.co/developer/docs/core-definition-files/core-json)
sets core shortnames to 31. Generated host package checks validate all
platform/asset paths and the 14-character corrected ID. The old malformed-ID
files were archived under `work/diagnostics/sdram-stress/obsolete-platform-id/`
before the obsolete card paths were removed. Corrected core JSON, ROM, and RBF
were installed and compared/hashed on `/Volumes/Pock`. User performed a cold
boot and reports the core is still absent. Inspection of the mounted card shows
`cores_cache.bin` contains `TAU_SDRAM_STRESS`, `corelist_cache.bin` contains the
correct `tau_sdram_strs`, but `platforms_cache.bin` contains only the obsolete
`tau_sdram_stress` entry for this platform. Its mtime predates the corrected
platform JSON/image on the card. This supports stale/inconsistent Pocket
catalog state; the cold boot did not refresh this index. Per Analogue's
[debugging guide](https://www.analogue.co/developer/docs/debugging-aids),
Tools > Developer > Builds lists installed core folders even when platform
association is broken; user confirmed the stress core appears there, and then
successfully launched it from Builds. The stress platform Assets now also
contain a playlist and MP3 for later testing.
**Resource/timing delta:** None.
**Outcome, reversal/workaround, remaining risk, and next gate:** Preserve the
old malformed-ID card files in a host archive, remove their stale card entries,
install the corrected package, and cold boot. That retest failed. Cache
inspection shows the core is indexed but the platform cache retains the old
ID; it is visible/launchable in Tools > Developer > Builds only. On 2026-09-14,
byte-verified backups of all five catalog/index caches were saved under
`work/diagnostics/sdram-stress/pocket-cache-backup-2026-09-14/System/`, then
only those originals were removed from the card so Pocket can regenerate its
catalog. Regeneration and normal-browser visibility remain pending. See
[issue 010](issues/010-stress-platform-id-too-long.md).

### A-038 — Correct the stress telemetry acceptance record

**Date:** 2026-09-14
**Decision/change:** Reconcile stress-test instructions with firmware after the
user observed multiple `SDRAM PASS n` messages. Retain one completed pass per
visualizer as the minimum data-integrity threshold, while recording that the
promised Select + Start summary for mismatch/word/audio/FIFO counters is not
implemented.
**Alternatives and rationale:** Treat pass count alone as full acceptance
(rejected: it cannot evidence audio underrun or framebuffer FIFO deltas); stop
the Pocket test until telemetry exists (rejected: pass toasts still provide
useful bounded data-integrity evidence while the user tests).
**Hot/cold impact:** None to normal TAU; no RTL, playback, or build output
changed.
**Evidence:** **code-review | Pocket** — `fw/player.c` increments pass and word
counters and emits `SDRAM PASS` at pass completion, but has no Select + Start
stress-summary handler/renderer. User reports passes 1–5 completed without
visible error; their Select + Start attempt stopped playback, consistent with
the ordinary Start stop binding. This does not establish audio/FIFO deltas.
**Resource/timing delta:** None.
**Outcome, reversal/workaround, remaining risk, and next gate:** Initially
concluded the summary was absent based on source review; this was corrected by
A-039 after new Pocket evidence. See [issue 011](issues/011-stress-summary-telemetry-missing.md).

### A-039 — Prefer Pocket evidence over incomplete stress-summary source review

**Date:** 2026-09-14
**Decision/change:** Reverse A-038's claim that the stress summary was absent.
The user successfully invoked Select + Start on Pocket, saw diagnostic
messages, and resumed playback with A. Preserve the stop/resume interaction as
a known part of the current test workflow while investigating why the reviewed
source lacks the matching handler.
**Alternatives and rationale:** Insist the Pocket report is impossible because
the handler is absent from current source (rejected: direct hardware evidence
outranks incomplete source inspection); treat the issue as fully resolved
(rejected: current source/package provenance mismatch remains unexplained, and
exact summary values have not been logged).
**Hot/cold impact:** None to hardware RTL; current stress firmware stops audio
while the summary is shown, then user resumes with A.
**Evidence:** **Pocket | code-review** — user observed the summary and reports
two clean passes on the default visualizer. The reviewed `fw/player.c` lacks
the described handler, so identify the exact ROM hash/build source and record
summary values in a follow-up.
**Resource/timing delta:** None measured.
**Outcome, reversal/workaround, remaining risk, and next gate:** Amend issue
011 and the diagnostic instructions. Continue the matrix one complete pass per
visualizer, resume playback after each summary, and capture displayed raw
values. Reconcile running ROM with source before declaring the gate passed.

### A-040 — Pocket SDRAM stress progress: pass 3 on second visualizer

**Date:** 2026-09-14
**Decision/change:** Record the user's live stress-test update: pass 3 completed
without reported error while testing the second visualizer. Treat pass numbering
as cumulative because no counter reset on visualizer change was reported.
**Alternatives and rationale:** Count this as three passes for the second mode
(rejected: the user described it as pass 3 overall after changing visualizers);
count it as a fully characterized mode result (deferred: playback/control
matrix and summary values remain to be recorded).
**Hot/cold impact:** No source or hardware change; Pocket test continues.
**Evidence:** **Pocket** — user report, `SDRAM PASS 3` clean on visualizer 2.
**Resource/timing delta:** No Quartus/resource change.
**Outcome, reversal/workaround, remaining risk, and next gate:** Current status
is three clean cumulative passes across at least two visualizer modes. Continue
until each mode has at least one completed pass with MP3 playback active; record
which pass occurred on each mode and any audio/display symptoms. See issues
010/011 and [contention diagnostic](SDRAM_CONTENTION_DIAGNOSTIC.md).

### A-041 — Pocket SDRAM stress progress: pass 4 clean

**Date:** 2026-09-14
**Decision/change:** Record the user's live update that pass 4 completed cleanly
and they are changing to Meter Scope for pass 5.
**Alternatives and rationale:** Attribute pass 4 to a specific visualizer
(deferred: user did not identify the mode for that pass); track only the
cumulative ordinal and preserve the stated next mode.
**Hot/cold impact:** No source or hardware change; Pocket test continues.
**Evidence:** **Pocket** — user report: pass 4 okay; proceeding to Meter Scope.
**Resource/timing delta:** No Quartus/resource change.
**Outcome, reversal/workaround, remaining risk, and next gate:** Four clean
cumulative passes reported so far; Meter Scope is the next visualizer under
test. Continue one complete pass per remaining mode with playback active and
record the mode associated with each pass when known. See issues 010/011 and
[contention diagnostic](SDRAM_CONTENTION_DIAGNOSTIC.md).

### A-042 — Propose a persistent, low-overhead stress progress HUD

**Date:** 2026-09-14
**Decision/change:** Record the user's request for a more legible way to track
multi-minute stress passes. Recommend a compact stress-only HUD showing current
pass/progress percentage, elapsed time, and previous pass result/duration;
implementation is deferred until the current Pocket matrix finishes.
**Alternatives and rationale:** Full-screen progress page (rejected: hides
player and visualizer during the concurrent-load test); frequent animation or
per-operation refresh (rejected: adds avoidable framebuffer/CPU traffic and can
perturb measured contention); pass toast only (rejected: not legible enough for
the observed run length).
**Hot/cold impact:** No change yet. Proposed UI stays compiled only into the
stress build and must not change the normal TAU interface.
**Evidence:** **Pocket | design** — user reports pass notifications are hard to
follow and estimates 2–3 minutes per pass. This is un-timed user observation;
existing plan's approximately-one-minute figure is also only a rough estimate.
**Resource/timing delta:** None yet. Proposed status refresh limit: at most 1 Hz;
measure HUD-on vs. pre-HUD pass durations and audio/display behavior.
**Outcome, reversal/workaround, remaining risk, and next gate:** Add [issue
012](issues/012-stress-progress-hud.md) and register it as proposed. Finish the
current visualizer matrix, time multiple same-condition passes, then implement
and measure the HUD without changing SDRAM RTL or Quartus artifacts.

### A-044 — Keep the token-heap guard; use a size-optimised stress-only HUD artifact

**Date:** 2026-09-14
**Decision/change:** Attempted the proposed 1 Hz stress HUD in the normal
firmware optimisation profile. The linker rejected it with `no room left for
even a token heap`. Retain the 1 KiB heap guard and configure the separate
developer stress target to use `-Os`; release TAU remains on its established
profile.
**Alternatives and rationale:** Reduce/remove the linker heap guard (rejected:
it protects stray newlib allocation paths); put the HUD in normal TAU (rejected:
developer-only behavior must not ship); omit elapsed/progress data (rejected:
it defeats the user-observed testability need).
**Hot/cold impact:** No RTL/Quartus change. Only the new stress ROM is compiled
with `-Os`; the existing player artifact is not overwritten.
**Evidence:** **host** — exact `-O2` stress build fails linker heap assertion;
an exploratory all-`-Os` stress build linked at 127,764-byte ROM size. This
does not yet constitute Pocket evidence.
**Resource/timing delta:** Firmware build-profile change only. HUD redraw is
throttled to 1 Hz; measure Pocket pass duration and audio/display behavior
before drawing performance conclusions.
**Outcome, reversal/workaround, remaining risk, and next gate:** Build and
install the explicit stress-only artifact, retain its artifact hash, then run a
Pocket comparison. See [issue 012](issues/012-stress-progress-hud.md).

### A-045 — Build, package, and stage the reproducible stress HUD ROM

**Date:** 2026-09-14
**Decision/change:** Add a dedicated `player-stress` firmware target and a
stress-only 1 Hz HUD. It identifies the active visualizer, current pass/percent
and elapsed time, plus previous pass/duration; Select + Start safely refreshes
the HUD instead of stopping playback. Build, package, and replace only the
stress core's asset ROM on the mounted Pocket card.
**Alternatives and rationale:** Depend on user timing/transcription (rejected:
the current run is difficult to follow); force the HUD into the release ROM
(rejected: no developer test feature belongs in normal TAU); remove the heap
assertion to fit `-O2` (rejected: a linker safety margin is not expendable).
**Hot/cold impact:** No RTL, Quartus, normal-core, playlist, media, or save
change. Only `/Assets/tau_sdram_strs/common/tau.rom` changed on Pocket.
**Evidence:** **host** — `make firmware-sdram-stress` passed, producing a
127,764-byte ROM (SHA-256
`61d39f955bde907fb63f59cca1c560ad31ea965c9d588627252db585b2bc3b9a`).
The pre-HUD ROM (SHA-256 `2e896ae1…f295b3de`) was copied to
`work/diagnostics/sdram-stress/pre-hud-rom-2026-09-14/tau.rom`; card copy was
byte-compared. `make test-host` passed its 17 director checks, M3U parser
matrix, splash/package checks, and deterministic UI fixture check. The
director's optional Astra task-launch probe did not run because it requires an
interactive approval; this is not a firmware test failure. Pocket behavior
remains pending.
**Resource/timing delta:** The separate ROM uses `-Os` because the `-O2` HUD
build violates the token-heap guard. HUD render rate is at most 1 Hz. Treat its
timing measurements as stress-diagnostic evidence, not release-player CPU data.
**Outcome, reversal/workaround, remaining risk, and next gate:** User should
run the staged core, verify the persistent strip and safe Select + Start action,
then photograph completed-pass states for the audit. Persistent SD logging is
not implemented. See [issues 011](issues/011-stress-summary-telemetry-missing.md)
and [012](issues/012-stress-progress-hud.md).

### A-046 — Correct the stress HUD's 32-bit cycle-counter duration wrap

**Date:** 2026-09-14
**Decision/change:** Reject the first HUD's raw pass durations after Pocket
photos showed a `01:09` HUD time while the active music was already beyond two
minutes. Replace its `now - pass_started` display with a wrap-safe software
accumulator sampled from `R_CYCLES` each main-loop iteration.
**Alternatives and rationale:** Treat player elapsed time as the pass stopwatch
(rejected: it is audio-frame time and need not start with stress); widen the
RTL counter (rejected: a firmware-only diagnostic defect does not justify a
hardware interface change); retain raw modulo values (rejected: they invite a
false bandwidth conclusion).
**Hot/cold impact:** No RTL, Quartus, normal-core, media, or save change. The
fix is inside the compile-gated stress HUD firmware only.
**Evidence:** **code-review** — `mp3_soc.v` defines `cycle_ctr` as a 32-bit
counter incremented on `clk`; firmware defines `CLK_HZ` as 60,000,000.
**Pocket** — user photos show the mismatch and completed named visualizer
matrix; the raw L-times are therefore retained but explicitly invalidated.
**Resource/timing delta:** Counter wraps at 71.582788 s. The fix uses 32-bit
division/remainder only in the existing main-loop stress path and adds no
framebuffer update beyond the existing 1 Hz HUD. Pocket resource/performance
evidence is pending.
**Outcome, reversal/workaround, remaining risk, and next gate:** The stress ROM
was rebuilt, staged, and byte-verified on the mounted card (new SHA-256
`1860455e…c3bb2cd5`; prior ROM `61d39f…b2bc3b9a` archived). The user elected
to defer its >72-second smoke test to a future stress session; it is not a
blocker for the completed contention result, but duration claims remain
withheld until then. See [issue 013](issues/013-stress-hud-timer-wrap.md).

### A-047 — Accept Phase 1 contention evidence; constrain Phase 2 entry

**Date:** 2026-09-14
**Decision/change:** Accept the completed ten-mode Pocket contention matrix as
the Phase 1 reliability gate, with the Eye repeat explicitly waived after
visual review. Begin Phase 2 as a decode-and-adapter preflight rather than
mapping cached SDRAM directly through the diagnostic bridge.
**Alternatives and rationale:** Require an Eye repeat despite no materially
distinct failure signal (rejected by user); treat HUD timing validation as a
reliability blocker (rejected: it affects duration telemetry, not the checked
read/write/failure path); map VexRiscv's cached bus directly into the single
word bridge (rejected: its cache-line/burst behavior would violate bounded
framebuffer arbitration).
**Hot/cold impact:** Hot playback, decoder, FIFO, stack, target-read, and
input paths remain BRAM. Phase 2 starts with an uncached cold-data diagnostic
window only; no linker placement changes yet.
**Evidence:** **Pocket** — ten exercised modes, no reported mismatch, timeout,
audible dropout, or display corruption. **design | code-review** — existing
bridge is one outstanding 32-bit mailbox; cacheability and broad `d_is_ram`
decode require an explicit mapping/adapter decision before use.
**Resource/timing delta:** None yet; no RTL/Quartus change. Phase 2 must report
new adapter resource and timing deltas before Pocket mapping work.
**Outcome, reversal/workaround, remaining risk, and next gate:** Implement and
simulate mutually exclusive address selects and a bounded uncached mapping
adapter first. Do not migrate data or enable the cached window until alias and
Wishbone-beat behavior are proven. See
[SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md).

### A-048 — Freeze and simulate the Phase 2 address-map contract

**Date:** 2026-09-14
**Decision/change:** Add a standalone `tau_sdram_addr_decode` contract and
testbench. It makes cached/uncached BRAM, narrow MMIO, and cached/uncached
CPU-owned SDRAM windows mutually exclusive, preserving the first 1 MiB SDRAM
framebuffer/guard region and translating CPU words to controller halfwords.
**Alternatives and rationale:** Keep the existing broad `d_is_ram` expression
(rejected: proposed SDRAM addresses alias BRAM); route all `0x8...–0xB...`
addresses to MMIO (rejected: it precludes the required uncached alias); wire
the decode directly into a cacheable data path immediately (deferred: the
bounded Wishbone adapter is not yet designed or verified).
**Hot/cold impact:** No live bus, linker, firmware, RTL integration, or Pocket
artifact changed. This is a Phase 2 preflight contract only.
**Evidence:** **simulation** — `make test-rtl-sdram-decode` passes 13 checks:
window exclusivity, narrow-MMIO limits, BRAM-alias rejection, framebuffer
guard rejection, both SDRAM bounds, and identical cached/uncached physical
translation at the 1 MiB boundary.
**Resource/timing delta:** Not applicable; the standalone module is not in the
Quartus source list and no fit claim is made.
**Outcome, reversal/workaround, remaining risk, and next gate:** The old broad
decode remains live until a bounded uncached Wishbone adapter is integrated.
Next design/test that adapter; do not enable cacheable SDRAM or migrate a
workspace yet.

### A-049 — Prove a bounded uncached Wishbone adapter in isolation

**Date:** 2026-09-14
**Decision/change:** Add `tau_sdram_wb_adapter`, a separate Phase 2a adapter
between one classic uncached Wishbone beat and the existing one-word SDRAM
bridge interface. It intentionally rejects incrementing cache/burst cycles.
**Alternatives and rationale:** Reuse the adapter for cached line fills
(rejected: a line would hold the narrow bridge/scanout boundary for an
unbounded series); permit held request signals to restart after ACK (rejected:
it duplicates writes); connect it directly to the live bus before isolation
tests (rejected: a decode/handshake fault could stall normal playback).
**Hot/cold impact:** No live integration, linker, firmware, Pocket artifact,
or Quartus source-list change. It remains an isolated cold-data preflight
component.
**Evidence:** **simulation** — `make test-rtl-sdram-wb-adapter` passes classic
read/write/lane propagation, one ACK/one command behavior, held-request
deduplication, busy-bridge deferral, burst rejection, and reset-during-wait.
The complete `make test-rtl` regression suite passes.
**Resource/timing delta:** Not applicable until the module is connected and a
Quartus fit is run.
**Outcome, reversal/workaround, remaining risk, and next gate:** Integrate only
the uncached alias behind an explicit diagnostic build flag, arbitration-safe
bridge-request mux, and a firmware read/write smoke test. Cacheable SDRAM,
workspace migration, and normal package changes remain prohibited.

### A-051 — Add accepted-request ownership for the shared SDRAM bridge

**Date:** 2026-09-14
**Decision/change:** Change the uncached adapter to hold a request until an
explicit accept, and add an isolated bridge-owner mux for diagnostic MMIO and
the mapped-window client.
**Alternatives and rationale:** Let both clients pulse the existing bridge
start input directly (rejected: simultaneous starts can misattribute a
completion); rely on firmware never issuing both (rejected: correctness must
not depend on that convention).
**Hot/cold impact:** No live top-level integration or Pocket artifact changed.
**Evidence:** **simulation** — adapter acceptance/hold/reset tests and mux
priority/deferred-owner/response-routing tests pass.
**Resource/timing delta:** Not applicable pending live integration and Quartus.
**Outcome, reversal/workaround, remaining risk, and next gate:** Connect these
units only in a compile-gated Phase 2 diagnostic configuration, then fresh-build
with Quartus before staging any Pocket core.

### A-052 — Add Phase 2 preflight RTL to the Quartus manifest

**Date:** 2026-09-14
**Decision/change:** Include the address decoder, bounded adapter, and bridge
mux in `ap_core.qsf` while leaving all three uninstantiated.
**Alternatives and rationale:** Wait until live wiring to add files (rejected:
parse/source-list errors should be separated from behavioral integration);
enable the mapped window at the same time (rejected: it would alter the normal
core before the diagnostic configuration and Quartus gate exist).
**Hot/cold impact:** No live logic path changes; synthesis may parse but can
remove uninstantiated modules. No Pocket artifact is produced.
**Evidence:** **Quartus | code-review** — full compile completed successfully
on the VM in 51m15s (0 errors, 342 warnings). Manifest paths match source
files; existing RTL simulation remains the behavioral evidence.
**Resource/timing delta:** 300/308 RAM blocks, 2,380,928/3,153,920 memory bits,
11/66 DSP, and 1/4 PLLs: unchanged because modules are uninstantiated. TNS is
0; reported worst setup/hold slack is 0.465/0.120 ns. This is positive but the
hold margin remains narrow.
**Outcome, reversal/workaround, remaining risk, and next gate:** The Quartus
source-list gate passed. Continue with disabled-by-default top-level wiring,
then repeat the Quartus and Pocket gates.

### A-053 — Quartus validates the core-level bridge-owner mux

**Date:** 2026-09-14
**Decision/change:** Compile the actual `core_game.vh` integration in which
diagnostic MMIO passes through `tau_sdram_bridge_mux`; the mapped Wishbone
client is tied inactive. The Phase 1 diagnostic path now has explicit owner
and completion routing.
**Alternatives and rationale:** Treat the earlier VM launch as a valid run
(rejected: its path did not contain the project and no Quartus process or
report existed); accept source simulation alone (rejected: production top-level
wiring requires the actual Quartus flow).
**Hot/cold impact:** No Pocket artifact installed and no mapped CPU window is
enabled. The future Wishbone client is tied off.
**Evidence:** **Quartus** — full compile succeeded with 0 errors and 342
warnings; `.sof` and `.rbf` were generated. RBF SHA-256:
`5b69e78899d65c77ed74d480c1e35676570880ac19b8d228830971bb0c318895`.
**Resource/timing delta:** 7,646 registers (130 more than the uninstantiated
source-list build); 300/308 RAM blocks, 2,380,928/3,153,920 block-memory bits,
11/66 DSP, 1/4 PLL. The lowest reported setup slack is 1.201 ns and lowest
hold slack 0.086 ns, with TNS 0. Hold remains positive but narrow. Fitter
elapsed 3h21m19s; full-flow elapsed 3h32m29s, far longer than the preceding
51m15s compile.
**Outcome, reversal/workaround, remaining risk, and next gate:** The first
launch attempt used a wrong project path and exited before Quartus ran; it is
retained as a failed launch, not a build. The corrected ext4 project path
completed successfully. Next add `mp3_soc` decode/adapter behind the dedicated
diagnostic build gate and rerun simulation and Quartus before Pocket staging.

### A-054 — Wire the opt-in uncached SDRAM path through mp3_soc

**Date:** 2026-09-14
**Decision/change:** Connect the standalone decoder and classic-beat adapter to
`mp3_soc`, route its held request and response through the `core_game.vh` owner
mux, and select that address map only when a dedicated build defines
`TAU_PHASE2_WINDOW`. The macro-off/default build retains the legacy decode and
ties the mapped-window client inactive. Unsupported burst beats and unmapped
data addresses return Wishbone ERR; cached SDRAM remains unsupported.
**Alternatives and rationale:** Enable the new map in every core build (rejected:
the hardware fit, diagnostic firmware, and Pocket gates have not passed); route
cached requests to the same single-word bridge (rejected: this adapter cannot
preserve cache-line burst semantics or bounded arbitration points).
**Hot/cold impact:** Default build preserves the legacy CPU path. The enabled
diagnostic branch adds only cold data-window access; Helix, PCM/EQ, DMA, stack,
input, and target-read paths remain in BRAM.
**Evidence:** **simulation | code-review** — address decoder, adapter, mux,
existing CPU bridge, and the new end-to-end decoder/adapter/mux read/write
bench pass; `make test-rtl` passes. Local Verilator top lint was attempted but
cannot elaborate the SoC because the generated `VexRiscv` module is absent from
the standalone source tree (see issue 014); this is not counted as successful
top-level lint. The earlier A-053 Quartus result does not cover this new CPU
integration.
**Resource/timing delta:** Pending a fresh Quartus compile of both the default
and `TAU_PHASE2_WINDOW` configurations; no resource or timing change is inferred.
**Outcome, reversal/workaround, remaining risk, and next gate:** Initial
end-to-end bench assertions were false failures caused by delta-cycle sampling
and confusing byte offsets with the word-addressed VexRiscv bus; the bench was
corrected and then passed without RTL changes. Next run the opt-in Quartus
configuration, create a firmware read/write smoke diagnostic, then stage a
separate Pocket package. No ordinary Tau package or firmware may use the map
until those gates pass.

### A-055 — Launch the Phase 2-enabled Quartus validation build

**Date:** 2026-09-14
**Decision/change:** Stage the current source snapshot at
`/home/taualpha/tau-local/phase2-window-a054-20260914` on VM ext4 and define
`TAU_PHASE2_WINDOW` only in that isolated copy's QSF. Start the full Quartus
flow to elaborate and fit the enabled `mp3_soc`/`core_game.vh` branch.
**Alternatives and rationale:** Enable the macro in the tracked QSF (rejected:
would change the ordinary build before validation); use the A-053 default-mode
fit as proof of this branch (rejected: A-053 predates the CPU window wiring).
**Hot/cold impact:** Isolated diagnostic build only. No normal config, package,
or Pocket card is modified.
**Evidence:** **host | Quartus pending** — authenticated VM check found no
existing Quartus process; the staged copy passes `make check-fpga`, and the
interactive SSH session launched `make fpga` and printed the Quartus flow
command. Final reports are not yet available. The initial staging tool call
did not execute due to a mistyped local working directory; it was corrected
before any VM change.
**Resource/timing delta:** Pending; do not infer from prior builds. The last
comparable full flow (A-053) took 3h32m29s; this is an estimate only.
**Outcome, reversal/workaround, remaining risk, and next gate:** Wait for the
flow report and confirm success, fit/resource numbers, timing slack, and
`.sof`/`.rbf` hashes. A successful fit alone is not Pocket validation; next
build a dedicated uncached CPU read/write/byte-lane firmware diagnostic and
test as a separate Pocket core.

**Result update:** The first Analysis & Synthesis attempt failed after 1m28s
with a syntax error at the conditional `mp3_soc` instantiation. The preprocessor
selection was followed by the old unconditional instance line, leaving a
duplicate `mp3_soc u_soc`; no fit or hardware artifact was produced. Removed
the stale line and retained the failed attempt in issue 015 / A-056 before
relaunching from a fresh snapshot.

### A-056 — Correct duplicate SoC instantiation exposed by Quartus

**Date:** 2026-09-14
**Decision/change:** Remove the old unconditional `mp3_soc u_soc (` line left
below the new `TAU_PHASE2_WINDOW` conditional instance declaration in
`core_game.vh`.
**Alternatives and rationale:** Treat the first failure as a Quartus parser
quirk (rejected: the source visibly contained two instance headers); revert the
Phase 2 conditional (rejected: the default-off gate is needed to safely
elaborate a dedicated diagnostic build).
**Hot/cold impact:** Fixes elaboration only; default datapath and opt-in SDRAM
behavior are unchanged.
**Evidence:** **Quartus | code-review** — the first macro-enabled compile
identified the syntax error at `core_game.vh:147`; code inspection found the
duplicate header. Corrected source has not yet had a retry build.
**Resource/timing delta:** No fitter ran; not applicable.
**Outcome, reversal/workaround, remaining risk, and next gate:** The failed
attempt lasted 1m28s and generated no `.sof`/`.rbf`. A clean, refreshed
macro-enabled Quartus run is the immediate gate.

**Result update:** Corrected source was staged in a fresh ext4 directory,
`/home/taualpha/tau-local/phase2-window-a056-20260914`; only that copy's QSF
defines `TAU_PHASE2_WINDOW`. `make check-fpga` passed and the full `make fpga`
flow is now running in an interactive SSH session. Fit/timing/artifact results
remain pending; no Pocket installation has occurred.

**Progress update (22:38 WEST):** The corrected Quartus Analysis & Synthesis
log reached `Elaborating entity "mp3_soc"` with no repeat of the syntax error;
`quartus_map` remained active. This confirms the parser fix, not a completed
synthesis, fit, timing pass, or artifact build.

**Result update (2026-09-16 inspection):** The corrected isolated build
completed successfully at 23:53:04 WEST on 2026-09-14. Full-flow elapsed time
was 1h18m22s (Fitter 1h06m04s); it produced `.rbf` SHA-256
`0d01f61409f42aec6f372166f8e81f17aa1c3b0482361a03ccba3465c942217e`
and `.sof` SHA-256
`5b57004da55c5a5c379698717d97209d4a65ed74982331175518b69ddbd8fcac`.
The fit uses 7,796 registers, 300/308 RAM blocks, and 11/66 DSP blocks. TNS is
0; the minimum reported setup/hold/pulse-width slacks are 1.034/0.120/0.833 ns.
This is **Quartus** evidence for the enabled branch only. The normal macro-off
rebuild and Pocket CPU-window firmware test remain mandatory gates.

### A-057 — Launch fresh macro-off regression build

**Date:** 2026-09-16
**Decision/change:** Stage the same audited source in a separate ext4 VM copy,
`/home/taualpha/tau-local/phase2-default-a057-20260916`, without defining
`TAU_PHASE2_WINDOW`, and start the full Quartus flow.
**Alternatives and rationale:** Rely on the pre-A-054 default build (rejected:
the current source changes `mp3_soc` ports and `core_game.vh` wiring); retain
the macro in the ordinary QSF (rejected: release behavior must stay opt-in).
**Hot/cold impact:** Validation only. The normal/default branch keeps its
legacy CPU BRAM/MMIO decode; no Pocket package or card is changed.
**Evidence:** **host | Quartus pending** — audit-ID validation and
`make check-fpga` passed in the isolated VM copy; the interactive SSH session
launched the full `make fpga` flow. Final reports and artifacts are pending.
**Resource/timing delta:** Pending; do not infer from the opt-in build.
**Outcome, reversal/workaround, remaining risk, and next gate:** Confirm the
macro-off fit and compare it with A-055/A-056. Then implement the separately
packaged CPU-window firmware read/write/byte-lane smoke test; neither fit alone
authorizes a normal player to use SDRAM.

**Result update:** The macro-off flow completed successfully at 15:34:50 WEST
on 2026-09-16, in 43m04s. It produced `.rbf` SHA-256
`431c96729dbcab5011be6e8aaac7327205a344da76b3d4f4ba49d08cbaea43d3`
and `.sof` SHA-256
`f15955bca13ae1a1dbe1a9804a5f171c199f9e894f295d02d25f48f304ac4e26`.
The fit uses 7,609 registers, 300/308 RAM blocks, and 11/66 DSP blocks; TNS is
0, and the minimum reported setup/hold/pulse-width slacks are
0.357/0.118/0.833 ns. This establishes the current normal branch still fits;
the lower positive hold margin remains a timing watch item. The next gate is
the dedicated Pocket CPU-window firmware smoke package.

### A-058 — Implement a separately packaged CPU-window smoke diagnostic

**Date:** 2026-09-16
**Decision/change:** Reuse the Phase 1 diagnostic UI/test harness with the
compile-time `TAU_CPU_WINDOW_DIAG` profile. Its full-word, byte, and halfword
operations now issue volatile CPU accesses through the uncached address range
`0xA0200000–0xA02FFFFC` (physical SDRAM 2–3 MiB). Add a distinct
`TAU_SDRAM_CPU` Media Players package, separate ROM staging target, package
checksums, deterministic running/PASS/FAIL/version-mismatch frame fixtures,
and a documented Pocket procedure.
**Alternatives and rationale:** Use the existing MMIO mailbox diagnostic
(rejected: it cannot exercise `mp3_soc` address decode, adapter, CPU byte
enables, or owner mux); modify normal TAU's ROM/RBF (rejected: an experimental
destructive test must not alter the player artifact); test the cached alias
first (rejected: cached bursts are explicitly unsupported and return bus error).
**Hot/cold impact:** Developer-only test writes its bounded 2–3 MiB region.
Framebuffer/guard, Phase 1's 1–2 MiB region, audio/decoder/PCM/stack, normal
ROM, and normal package remain untouched. No user data migrates.
**Evidence:** **code-review | host** — the source uses volatile `uint32_t`,
`uint16_t`, and `uint8_t` lvalues; the compiled RISC-V disassembly confirms
`sw`, `sb`, and `sh` operations at `0xA0200600+`. `make
firmware-sdram-cpu-diag` passed (5,192-byte ROM, 2.9% of usable BRAM); the
29-state 400×360 RGB565 snapshot check, visual-review generation, audit-ID
check, Python syntax check, and independent package identity/ROM/bit-reversal
verification all pass. The staged RBF SHA-256 exactly matches A-056:
`0d01f61409f42aec6f372166f8e81f17aa1c3b0482361a03ccba3465c942217e`.
`make test` passes all host and RTL tests. Its optional Astra peer-launches
were not run because that helper requires an interactive approval; no peer-run
claim is made. No Pocket claim is made.
**Resource/timing delta:** No RTL or new Quartus result. Diagnostic ROM is
5,192 bytes; the accepted A-056 enabled RBF remains the sole staging input.
**Outcome, reversal/workaround, remaining risk, and next gate:** The legacy
version register is shared by macro-on and macro-off RBFs, so it cannot protect
against a mismatched bitstream. The packager hard-rejects a non-A-056 hash;
`SHA256SUMS.txt` and installer text provide the inspectable provenance. Host gates have
passed; next run five cold/five warm Pocket passes. A pass proves only bounded uncached
traffic; concurrent player/CRC pressure and a separate cache-line adapter are
later gates.

**Pocket checkpoint (2026-09-16):** User supplied a Pocket photo of the
initial screen showing `TAU CPU SDRAM TEST`, `PHASE 2 UNCACHED WINDOW`,
`SAFE REGION 2-3 MIB`, and `CPU LOAD STORE LANES` at 31.8°C / 60 Hz / sync ok.
This confirms the separately packaged diagnostic booted and reached its first
test stage. It is **Pocket** boot/UI evidence only—not a read/write or memory
integrity pass. Await the PASS/FAIL screen before updating the gate.

**Pocket result update:** After more than 30 seconds the same screen remained
at `FIXED PATTERNS`, with no PASS/FAIL result. This is a **Pocket** failure of
the first mapped CPU transaction completion path. It narrows the problem to the
new adapter/mux/actual CDC-bridge/arbiter composition after otherwise-successful
firmware and UI boot; see [issue 016](issues/016-phase2-cpu-window-first-transaction-stall.md).
The stand-in end-to-end simulation is therefore insufficient, not invalidated
as a unit-level result. Do not call the CPU-window gate passed or migrate data.

**Source-provenance update:** SHA-256 comparison of the five Phase 2 top-path
RTL files between the local checkout and A-056's isolated VM snapshot is an
exact match. This eliminates a local-source versus packaged-RBF mismatch. The
next test is a ROM-only mailbox preflight at the same physical 2 MiB location;
it uses the known bridge path and a distinct failure screen before any mapped
CPU access. No new Quartus result is claimed or required for that firmware-only
instrumentation step.

**Preflight-ROM update:** The updated ROM-only diagnostic compiles at 6,112
bytes (3.4% of usable BRAM), produces 31 deterministic framebuffer fixtures,
and packages with the unchanged A-056 RBF. Its ROM SHA-256 is
`a3dd9bd6b67ba740237c7e5d2990c1197d45593c573102342622fbe49eee8854`.

**Pocket preflight result:** User observed the transient `MAILBOX OK CPU
WINDOW NEXT` state, followed by the same persistent `FIXED PATTERNS` screen.
This is **Pocket** proof that the new shared owner mux, existing CDC bridge,
arbiter/controller route, and physical 2 MiB location complete a mailbox
write/read. It eliminates that composition from the leading cause. The mapped
VexRiscv-to-adapter request or its return ACK remains the failing boundary;
the next evidence must expose the actual generated-CPU request semantics,
rather than assume the stand-in testbench covers them.

**Composed-simulation update:** `tb_tau_sdram_composed_path.v` combines the
real adapter, owner mux, CDC bridge, and arbiter at 60/100 MHz with recurring
framebuffer traffic; it passes. A minimal generated-VexRiscv execution harness
was also attempted but timed out before the target access because its simplified
instruction-memory responder did not model the CPU's cached instruction bus.
It was removed and is explicitly **not** treated as generated-CPU evidence.
The normal 6,112-byte preflight ROM/package was rebuilt afterward and its ROM
hash re-verified as `a3dd9bd6b67ba740237c7e5d2990c1197d45593c573102342622fbe49eee8854`.

### A-059 — Add a Phase-2 hardware progress probe for the first CPU request

**Date:** 2026-09-16
**Decision/change:** Add `tau_sdram_cpu_window_probe.sv`, an opt-in recorder
that retains the first mapped CPU request's CTI/SEL plus adapter request,
mux accept/start, bridge busy/done, adapter done, Wishbone ACK, and unsupported
CTI milestones. In an enabled diagnostic build only, a 16-cell green/red bar
is overlaid after framebuffer scanout so it persists after a CPU freeze.
**Alternatives and rationale:** Continue extending a full generated-VexRiscv
Icarus harness (rejected for this gate: even after correcting its startup MMIO
model, it was too slow to provide bounded useful evidence); infer the stage
from the frozen firmware text (rejected: it only proves execution reached the
first access); add a normal-player debug UI (rejected: would contaminate the
release path and disappear if the CPU stalls).
**Hot/cold impact:** Diagnostic-only added registers and top-edge scanout
overlay; no normal Tau macro-off path, player storage, SDRAM mapping policy,
or user data changes. The probe observes protocol metadata only.
**Evidence:** **code-review | simulation | host** — the isolated probe test
passes first-request metadata retention, all success milestones, unsupported
CTI indication, and non-overwrite by later traffic. `make test-rtl`, audit-ID
check, whitespace check, and deterministic UI fixture check pass. The
composed adapter/mux/CDC/arbiter test remains passing. The generated-Vex
attempt is explicitly inconclusive, not simulation evidence for the request
shape. **Quartus | Pocket:** pending a fresh dedicated macro-enabled build.
**Resource/timing delta:** Pending Quartus; the probe is small control logic
plus two 16-bit video-domain synchronizer stages. No resource estimate is
treated as a fit result.
**Outcome, reversal/workaround, remaining risk, and next gate:** A-056 proves
the prior enabled build, not this instrumentation. Build/package a distinct
probe artifact, photograph the 16-cell sequence on Pocket, then make exactly
one evidence-backed correction (or add a targeted test) based on the first
missing milestone. No migration gate advances.

**Build status update:** A fresh isolated VM copy at
`/home/taualpha/tau-local/phase2-probe-a059-20260916` was staged from the
current local source. SHA-256 matches for `mp3_soc.v`
(`b6c138b5…84ab71b`), `core_game.vh` (`4cc38b75…149f28cc`), and the new probe
(`67448db0…8dab4f169`). Only that copy has `TAU_PHASE2_WINDOW` in its QSF.
The Quartus 25.1std full flow began 2026-09-16; its report, artifact hashes,
timing, and Pocket result remain pending.

**Quartus/package result update:** The isolated A-059 flow completed with 0
errors and 343 warnings in 42m29s. Its raw RBF SHA-256 is
`921d6f941b8d40dd0f662857b6fe1b34e09a22bade0f372a950c9ed8c88df97c`; the
Pocket bit-reversed hash is
`fd4d59e5d054f6f4c5e6c552fcb84bcba691bd28a847432aa5c6dccfd8578720`.
Resources are 917/1,848 LABs (50%), 7,842 registers, 300/308 RAM blocks
(97%), 2,380,928/3,153,920 block-memory bits (75%), and 11/66 DSP blocks
(17%). Positive timing is reported; the tightest reported hold slack is
0.070 ns. `TAU CPU SDRAM Probe` packages beside, rather than overwriting,
the A-056 `TAU CPU SDRAM Diagnostic`; both package hashes and the unchanged
6,112-byte ROM hash were verified on the host. **Pocket:** pending; no SD-card
write was made by this build/package step.

**Card-install update:** After the user explicitly confirmed `/Volumes/Pock`
was mounted, only the new `alfatreze.TAU_SDRAM_PROBE`, `tau_sdram_probe`, and
platform-image/metadata paths were copied. Card-side SHA-256 verification
matches the packaged bit-reversed RBF
`fd4d59e5d054f6f4c5e6c552fcb84bcba691bd28a847432aa5c6dccfd8578720` and
ROM `a3dd9bd6b67ba740237c7e5d2990c1197d45593c573102342622fbe49eee8854`;
the platform JSON byte-compares equal. This is **host** installation evidence,
not Pocket runtime evidence. Normal Tau and A-056 diagnostic paths were not
modified.

### A-060 — Correct CPU-window back-to-back classic Wishbone turnaround

**Date:** 2026-09-16
**Decision/change:** Interpret the A-059 Pocket probe: the first mapped
full-word request completed through ACK (`CTI=000`, `SEL=1111`) before the
firmware remained on `FIXED PATTERNS`. Replace the adapter's indefinite
`S_RELEASE` wait for a low CYC/STB level with a one-clock turnaround followed
by IDLE. Widen the probe scanout x-counter to 9 bits so its 128-pixel bar
does not repeat after an 8-bit wrap.
**Alternatives and rationale:** Treat the completed first request as evidence
that the overall CPU window works (rejected: the persistent firmware stall
shows a later beat remains blocked); add broader speculative instrumentation
before changing the narrow adapter (deferred: source semantics plus the probe
give a direct, testable cause); remove turnaround entirely (rejected: would
capture the just-acknowledged held request again before the registered SoC ACK
is observable).
**Hot/cold impact:** Changes only the opt-in Phase-2 adapter/probe path.
Normal macro-off Tau, BRAM hot path, audio, framebuffer ownership, and all
data-placement policy remain unchanged.
**Evidence:** **Pocket | code-review | simulation** — Pocket photo has all
request/adapter/mux/bridge/done/ACK cells green, unsupported CTI red, CTI
`000`, and SEL `1111`. The probe's two visible bars are explained by exact
8-bit x-counter wrap. `tb_tau_sdram_wb_adapter` now passes held-cycle,
back-to-back classic-beat, unsupported-burst, and reset cases; the Phase-2
path and composed CDC/arbiter tests pass. **Quartus | Pocket corrected build:**
pending.

**Pocket display reversal:** The first A-063 Pocket photo retained the known
182/183 zero-readback result, but visibly rendered only the prior 19-cell
sequence. Code review found that `sdram_probe_pixel` was widened to 280 pixels
while the enclosing `sdram_probe_bar` guard was accidentally left at 152.
Thus cells 19–34 were never displayed and this run is not evidence about the
new payload trace. The guard is corrected to 280 and a host check now compares
both scanout limits before a fresh build; no functional SDRAM logic changed.

### A-064 — Correct the visible width of the 35-cell store-payload probe

**Date:** 2026-09-17
**Decision/change:** Change the enclosing probe-bar scanout guard from 152 to
280 pixels so it covers all 35 retained cells, matching the already-correct
probe-pixel guard. Add a host check that extracts both RTL limits and rejects a
mismatch or a width other than 280.
**Alternatives and rationale:** Decode the hidden cells from the failed screen
(rejected: they were not rendered); treat A-063's first 19 cells as payload
evidence (rejected: those cells end before payload tracing begins); alter SDRAM
write logic (rejected: this is demonstrably a display-boundary defect).
**Hot/cold impact:** Diagnostic scanout only. The normal macro-off player,
request path, bridge state machine, data map, and audio path are unchanged.
**Evidence:** **Pocket | code-review | simulation** — the A-063 photo has a
152-pixel/19-cell colored bar despite the 35-cell recorder. The corrected
host scanner, probe test, bridge/mux/composed tests, audit-ID check, and
whitespace check pass. **Quartus | Pocket A-064:** pending.
**Resource/timing delta:** Pending a fresh diagnostic-only fit.
**Outcome, reversal/workaround, remaining risk, and next gate:** Build a
distinct A-064 RBF, then repeat one Pocket run and photograph the full visible
bar. Only then can the first red payload boundary guide a functional change.

**Build status update:** At 2026-09-17 09:15:01 WEST, a fresh ext4-only VM
snapshot at `/home/taualpha/tau-local/phase2-probe-a064-20260917` passed the
Quartus environment preflight with `TAU_PHASE2_WINDOW=1` only in that copy.
The full flow launched under PID 32635. This is **host** launch evidence only;
fit, timing, artifact hashes, and Pocket evidence remain pending.

**Quartus/package result update:** A-064 completed successfully with 0 errors
and 343 warnings in 47m28s. Raw RBF SHA-256 is
`60abb545fef5e6c725c84717af21b6a53f4f994d9219cd0245b0dbd1577c32dc`; the
bit-reversed Pocket RBF SHA-256 is
`da6ccf4a8d0200147d458238b96c058d58dd67146ec8890f38b78e24f36a11fe`.
The fit uses 913/1,848 LABs (49%), 7,902 registers, 300/308 RAM blocks (97%),
and 11/66 DSP blocks (17%). All reported timing is positive; tightest reported
hold slack is 0.118 ns. The separate **TAU CPU SDRAM Probe A064** package
(`alfatreze.TAU_SDRAM_PRB64`, `tau_sdram_prb64`) is hard-pinned to that RBF and
the verified A-061 ROM. Host package, audit-ID, UI-fixture, and whitespace
checks pass. Card installation and Pocket runtime evidence remain pending
explicit mounted-card confirmation.

**Card-replacement update:** After fresh mounted-card confirmation, the exact
superseded A-063 core, asset, platform JSON, and platform-image paths were
removed before copying only A-064's corresponding paths. Card-side SHA-256
matches the A-064 bit-reversed RBF and paired A-061 ROM above. Normal Tau and
the independent retained diagnostics were not touched. This is **host**
installation evidence only; Pocket A-064 runtime evidence is pending.

**Catalog-index update:** After installation, the A-064 core and platform files
were present and hash-verified, and `cores_cache.bin`/`corelist_cache.bin`
contained `TAU_SDRAM_PRB64`. However, those caches were timestamped 11:08
while the A-064 files were copied at 11:41; `platforms_cache.bin` was older at
08:33 and the platform/category indexes did not contain the new mapping. The
core is therefore valid but not yet menu-visible. A Pocket disconnect/reconnect
or cold catalog rebuild is required before treating menu absence as a package
failure. This is **host** cache evidence only.

**Cache-rebuild action:** After the user reported two cold boots and a cleared
recent-FPGA catalog still did not show A-064, metadata was rechecked against
the visible diagnostics and remained valid. Only the five regenerable Pocket
catalog/index caches (`platforms_cache.bin`, `cores_cache.bin`,
`corelist_cache.bin`, `core_viewby_platform.bin`, and
`platform_viewby_category.bin`) were removed; all core, asset, platform JSON,
and image files were preserved. The Pocket must be disconnected/reconnected so
it regenerates these indexes. This is **host** cache maintenance evidence,
not runtime or SDRAM evidence.
**Resource/timing delta:** Pending fresh build; the adapter changes one state
transition and probe x-counter one bit. No fit estimate is treated as evidence.
**Outcome, reversal/workaround, remaining risk, and next gate:** A-059
reverses the initial conclusion that the first transaction never returns. The
leading cause is a legal back-to-back classic transfer blocked by release
policy. Fresh-build a distinct A-060 probe RBF, rerun Pocket, and require a
PASS screen before considering Phase-2 migration.

**Build status update:** The complete host/RTL/UI suite passed before staging
the A-060 source in isolated VM directory
`/home/taualpha/tau-local/phase2-probe-a060-20260916`. Its Phase-2 macro is
present only in that directory's QSF, and the full Quartus 25.1std flow is now
running. The prior Pocket card package remains installed but is not a valid
test of this correction; no card write is made until this new flow is verified
and separately packaged.

**Quartus/package result update:** A-060 completed with 0 errors and 343
warnings in 47m11s. Its raw RBF SHA-256 is
`9ea5e38c20145125627b8d23c2bf4ab02b7c5b3998778cea80598bcd72b4c5ee`; the
bit-reversed Pocket RBF SHA-256 is
`4b68f7b3b6681697ed7af80718535a2e5542e347db918ee1ab9e41f4ccbb78cd`.
Resources are 939/1,848 LABs (51%), 7,848 registers, 300/308 RAM blocks
(97%), 2,380,928/3,153,920 block-memory bits (75%), and 11/66 DSP blocks
(17%). All reported timing is positive; tightest reported hold slack is
0.116 ns. The new hard-pinned package is **TAU CPU SDRAM Probe A060**
(`alfatreze.TAU_SDRAM_PRB60`, platform `tau_sdram_prb60`), deliberately
separate from A-059. Host package validation passed; Pocket install/test is
pending explicit card-mounted confirmation.

**Card-install update:** `/Volumes/Pock` was confirmed mounted before copying
only `alfatreze.TAU_SDRAM_PRB60`, `tau_sdram_prb60`, and its platform
image/metadata. Card-side hashes match the A-060 bit-reversed RBF
`4b68f7b3b6681697ed7af80718535a2e5542e347db918ee1ab9e41f4ccbb78cd` and
the unchanged ROM; platform JSON byte-compares equal. This is **host** install
evidence only. A-059, A-056, and normal Tau paths remain present and untouched.

### A-061 — Add a firmware-only mailbox-to-CPU readback discriminator

**Date:** 2026-09-17
**Decision/change:** After A-060 completed the whole matrix with 182/183
failures and zero actual values, add a distinct ROM that uses the proven
mailbox to write/read `PREFLIGHT_PATTERN` at physical 2 MiB, then reads that
same word once through the CPU uncached alias before entering the matrix. Add
a dedicated CPU readback failure UI state and fix the CPU diagnostic result
title, which was incorrectly hard-coded as the Phase-1 name.
**Alternatives and rationale:** Immediately modify the mapped write bridge
(rejected: the result does not distinguish write loss from mapped-read return
loss); rebuild FPGA logic merely for this observation (rejected: the current
A-060 RBF already contains the needed address and bridge paths); inspect only
the 182 aggregate failures (rejected: insufficient to locate the boundary).
**Hot/cold impact:** Separate developer ROM/package only; no RTL, timing,
normal player, user data, or existing diagnostic artifact change.
**Evidence:** **Pocket | code-review | host** — Pocket confirmed A-060
preflight and full transaction completion, then showed zero readbacks. The
readback ROM builds at 6,668 bytes (3.7% usable RAM), its new deterministic
frame fixture plus UI review pass, and it packages with the already-verified
A-060 RBF. **Pocket readback result:** pending.
**Resource/timing delta:** No new FPGA build. A-060 resources/timing remain
the applicable hardware evidence.
**Outcome, reversal/workaround, remaining risk, and next gate:** Install the
separate `TAU CPU SDRAM Readback A060` package only after explicit card-mounted
confirmation. A matching read proves CPU mapped-read routing; a zero read
focuses the next correction on mapping/return data, while a preflight failure
would reopen the mailbox/physical-address premise.

**Card-install update:** After explicit mounted-card confirmation, only
`alfatreze.TAU_SDRAM_RD60`, `tau_sdram_rd60`, and its platform image/metadata
were installed. Card-side hashes match the A-060 bit-reversed RBF
`4b68f7b3b6681697ed7af80718535a2e5542e347db918ee1ab9e41f4ccbb78cd` and
the A-061 ROM
`f8a7f999cb0a503c9bef0536cead0c8f2ea046382efb2626bfdc8af60c16338a`;
platform JSON byte-compares equal. This is **host** install evidence only;
all earlier Tau and diagnostic paths remain untouched.

### A-062 — Instrument first two mapped CPU request directions

**Date:** 2026-09-17
**Decision/change:** Interpret the A-061 Pocket run: it passed the CPU read of
the non-zero mailbox preflight word, then reached the matrix with 181 failures
on first launch and 182 on re-run. Extend the opt-in hardware probe from 16 to
19 cells to record first-request WE and second-request observed/WE. Under the
A-061 ROM, these map directly to the checkpoint read followed by the first
matrix store. Preserve the first-request CTI/SEL and pipeline milestones.
**Alternatives and rationale:** Diagnose mapped reads further (rejected:
A-061 directly proved the mapped read of the mailbox's non-zero word);
assume stores assert WE because firmware disassembly contains `sw` (rejected:
the FPGA boundary must observe the real generated Vex signal); rewrite the
store bridge now (rejected: direction versus downstream loss remains unknown).
**Hot/cold impact:** Opt-in diagnostic probe only. No normal Tau, data map,
audio, framebuffer ownership, or firmware feature behaviour changes.
**Evidence:** **Pocket | code-review | simulation | Quartus | host** — A-061 Pocket result and
the 181/182 reversal are recorded above. The expanded probe unit test passes
first/second request edge capture, CTI/SEL retention, direction bits,
milestones, unsupported indication, and non-overwrite. Existing adapter,
Phase-2 path, and composed tests pass. Quartus completed as recorded below.
**Pocket result:** the retained 19-cell bar reads green ×8, red ×4, green ×4,
red ×1, green ×2. This is the expected first mapped read (`CTI=000`,
`SEL=1111`, `WE=0`) followed by an observed second request with `WE=1`. The
matrix then completed with 182/183 failures and zero returned data.
**Resource/timing delta:** The dedicated fit is recorded below; the probe adds
two retained bits and the corresponding video-domain synchronizer width.
**Outcome, reversal/workaround, remaining risk, and next gate:** Mapped reads
are no longer the leading cause. The A-062 Pocket result rules out missing CPU
store direction as well. Next, trace the first store's payload and write intent
across the adapter, owner mux, CDC bridge, and SDRAM-domain sequencer before
changing any functional write logic.

**Quartus/package result update:** A-062 completed with 0 errors and 343
warnings in 46m58s. Raw RBF SHA-256 is
`d7f60eb7e52705a5f622d449312b266040399acf2940a352acebc0b77dcde0a4`; the
bit-reversed Pocket RBF SHA-256 is
`99437bac629b27bdba89c7e2a377645e7aa831bc51e56bd8e40c425a54fb2984`.
Resources are 914/1,848 LABs (49%), 7,861 registers, 300/308 RAM blocks
(97%), and 11/66 DSP blocks (17%); all reported timing is positive with 0.115
ns tightest reported hold slack. It packages separately as **TAU CPU SDRAM
Probe A062** (`alfatreze.TAU_SDRAM_PRB62`, `tau_sdram_prb62`) paired with the
verified A-061 readback ROM. Host package checks pass.

**Card-cleanup/install update:** After explicit mounted-card confirmation, the
A-062 package was installed and card-side SHA-256 matched the bit-reversed RBF
and paired ROM above. The superseded A-059, A-060, and standalone A-061
diagnostic packages (their exact core, asset, platform JSON, and image paths)
were then removed at the user's request. Normal Tau and the Phase-1 baseline
diagnostic were not touched. This is **host** install/cleanup evidence only.

### A-063 — Trace first mapped-store payload through every write boundary

**Date:** 2026-09-17
**Decision/change:** The A-062 Pocket bar proves the first matrix store reaches
the CPU Wishbone boundary with `WE=1`, while subsequent reads remain zero.
Extend the opt-in probe to 35 cells. It retains expected `FFFFFFFF`/`1111`
payload evidence at the second CPU request, adapter, owner mux, SDRAM-domain
bridge capture, and accepted lower/upper controller writes. Add a one-cycle
`bridge_wb_start` provenance pulse so the bridge explicitly excludes the
earlier diagnostic-MMIO preflight write from this evidence.
**Alternatives and rationale:** Change write logic now (rejected: A-062 only
establishes direction, not the first loss boundary); use the old generic
bridge-write state (rejected: it would be polluted by the preflight MMIO
write); infer payload from the source instruction (rejected: hardware evidence
must cover the actual routed values).
**Hot/cold impact:** Diagnostic-only observability. The normal macro-off
player does not consume the retained outputs; no user data map, audio path, or
functional write policy changes.
**Evidence:** **Pocket | code-review | simulation** — Pocket A-062 provided
the direction result above. Targeted probe, bridge, mux, Phase-2 path, and
composed-path simulations pass, including retained payload/byte-enable state
and both accepted 16-bit writes. **Quartus | Pocket A-063:** pending.
**Resource/timing delta:** Pending dedicated fit; no inference is made from
the A-062 fit because the new retained state crosses the diagnostic boundary.
**Outcome, reversal/workaround, remaining risk, and next gate:** Fresh-build a
distinct A-063 package with the unchanged A-061 ROM, photograph all 35 cells,
and trace only the first red cell. No functional fix or SDRAM migration is
authorised beforehand.

**Build status update:** At 2026-09-17 01:42:46 WEST, a fresh ext4-only VM
snapshot at `/home/taualpha/tau-local/phase2-probe-a063-20260917` was created
from the local worktree. SHA-256 matches were checked for `core_game.vh`,
`mp3_soc.v`, `tau_sdram_cpu_bridge.sv`, `tau_sdram_bridge_mux.sv`, and
`tau_sdram_cpu_window_probe.sv` before adding `TAU_PHASE2_WINDOW=1` only to
that copy's QSF. Quartus environment preflight passed and the full flow was
launched under PID 28041. This is **host** launch evidence, not a fit result;
reports, hashes, timing, and Pocket evidence remain pending.

**Failed-build update:** Quartus Analysis & Synthesis stopped after 56 seconds
with `default_nettype none` error 10162 at `core_game.vh:459`: the new
`sdram_wb_debug_wdata` output was declared in `mp3_soc` but omitted from the
top-level instance and its wire declaration. No fitter, timing, RBF, or Pocket
evidence was produced. This is a retained integration failure, not a hardware
finding. The local connection is corrected; local gates must pass before a
fresh isolated snapshot is built.

**Retry launch update:** The connection fix passed the focused local RTL,
audit-ID, UI-fixture, and whitespace gates. A new ext4 snapshot at
`/home/taualpha/tau-local/phase2-probe-a063r-20260917` passed its Quartus
environment preflight with `TAU_PHASE2_WINDOW=1` only in that copy. The full
retry started at 2026-09-17 07:06:28 WEST under PID 30161. This is **host**
launch evidence only; the original failed snapshot is retained for audit and
will not provide the retry's artifact or timing provenance.

**Quartus/package result update:** The retry completed successfully with 0
errors and 343 warnings in 1h19m08s. Raw RBF SHA-256 is
`acce05b2145b34780da31a8a315557ac64ae2416546f104c5a242f2241cbfaaa`; the
bit-reversed Pocket RBF SHA-256 is
`8f2d4693060443a83c8d06620b8d85b4f7cbd283f69e971dec5b523dae3d348e`.
The fit uses 932/1,848 LABs (50%), 7,982 registers, 300/308 RAM blocks (97%),
and 11/66 DSP blocks (17%). All reported timing is positive; tightest reported
hold slack is 0.115 ns. The separate **TAU CPU SDRAM Probe A063** package
(`alfatreze.TAU_SDRAM_PRB63`, `tau_sdram_prb63`) is hard-pinned to that RBF and
the verified A-061 ROM hash. Host package, audit-ID, UI-fixture, and whitespace
checks pass. Card installation and Pocket runtime evidence remain pending
explicit mounted-card confirmation.

**Card-cleanup/install update:** After explicit mounted-card confirmation, the
superseded A-056 CPU-window diagnostic and A-062 direction-probe packages were
removed before copying only A-063's exact core, asset, platform JSON, and
platform image paths. Card-side hashes match the A-063 bit-reversed RBF and
the paired A-061 ROM above. Normal Tau, the independent Phase-1 mailbox
baseline, and the contention-stress diagnostic were retained. Going forward,
remove superseded Tau test packages before installing a newer test package;
retain only independent regression baselines that remain useful. This is
**host** install/cleanup evidence only; Pocket A-063 runtime evidence is
pending.

### A-043 — Confirm Pocket rebuilt the corrected platform catalog

**Date:** 2026-09-14
**Decision/change:** After the user exited the stress core and remounted the SD
card, inspect the five regenerated catalog/index caches and stress media paths.
**Alternatives and rationale:** Assume cache rebuild failed because the core was
previously visible only in Developer > Builds (rejected: fresh caches now
contain the corrected platform ID); treat ordinary-menu visibility as proven
(deferred: user has not yet confirmed the menu after regeneration).
**Hot/cold impact:** Read-only host inspection; no card changes.
**Evidence:** **host** — `platforms_cache.bin`, `corelist_cache.bin`,
`core_viewby_platform.bin`, and `platform_viewby_category.bin` now contain
`tau_sdram_strs` and not `tau_sdram_stress`; `cores_cache.bin` contains
`TAU_SDRAM_STRESS`. Playlist exists and 26 MP3 files are present in the stress
assets.
**Resource/timing delta:** None.
**Outcome, reversal/workaround, remaining risk, and next gate:** Cache recovery
worked. Ask user to confirm the core appears in the ordinary Media Players
browser. Continue the user-run visualizer matrix. See
[issue 010](issues/010-stress-platform-id-too-long.md) and
[issue 012](issues/012-stress-progress-hud.md).

### A-065 — Interpret the full-width A-064 Pocket payload trace

**Date:** 2026-09-17
**Decision/change:** Decode the first complete 35-cell A-064 Pocket bar and
correct the payload expectation in the diagnostic documentation. The observed
sequence is `GGGGGGGGRRRRGGGGRGGRGGRRGGRRGGGRGGG`; cells 19, 22–23, 26–27, and
31 are red because the captured second CPU request is the first fixed-pattern
store, whose firmware value is `0x00000000`, not `0xFFFFFFFF`.
**Alternatives and rationale:** Treat red payload cells as proof that the
adapter or SDRAM path changed zero data (rejected: source review of
`fw/sdram_diag.c` shows the first store is the zero pattern); treat the bar as
proof that the later all-ones failure is fixed (rejected: the probe latches
only the first matrix store and the player still reports 181 failures at
`A0200000`).
**Hot/cold impact:** Documentation and diagnostic interpretation only; no
player, bridge, arbiter, or SDRAM controller logic changed.
**Evidence:** **Pocket | code-review** — full A-064 photo; pixel decode of the
35-cell bar; `fw/sdram_diag.c` fixed-pattern order; failure screen showing
183 checks, 181 failures, first failure `A0200000`, expected `FFFFFFFF`,
actual `00000000`.
**Resource/timing delta:** None; A-064 fit remains 913/1,848 LABs, 300/308
RAM blocks, and positive timing.
**Outcome, reversal/workaround, remaining risk, and next gate:** A-064's
display-width defect is resolved, but its payload trace is stimulus-mismatched
and cannot localise the later all-ones failure. Issue
[017](issues/017-a064-probe-stimulus-mismatch.md) is open. Build another
diagnostic-only probe after making the observed store ordinal/value
deterministic; do not change the functional path on this evidence alone.

### A-066 — Target the all-ones CPU store in the A-065 payload probe

**Date:** 2026-09-17
**Decision/change:** Retain first-request metadata, but arm payload capture on
the fourth CPU request: the A-061 order is preflight read, zero store, zero
read, then the all-ones store that produces the first observed failure. The
bridge's retained provenance similarly selects the first mapped Wishbone write
whose payload is `FFFFFFFF`, so its SDRAM-domain evidence matches the target
transaction.
**Alternatives and rationale:** Reorder the diagnostic firmware to write ones
first (rejected: it changes the long-lived smoke test rather than its observer);
infer the all-ones path from A-064's zero-store trace (rejected: it cannot
locate the later failure); capture an unbounded stream of transactions
(rejected: unnecessary state and wider diagnostic surface).
**Hot/cold impact:** Opt-in Phase-2 diagnostic recorder and provenance flags
only. The normal macro-off player, CPU-window protocol, bridge transaction
state machine, arbiter, firmware ROM, and SDRAM map are unchanged.
**Evidence:** **code-review | simulation | host** — focused probe simulation
proves that requests two and three do not arm capture and request four retains
`FFFFFFFF/F`; bridge simulation proves a non-all-ones write does not arm
provenance and a later all-ones write reaches both controller halfwords. The
full local `make -B test`, UI fixture, audit-ID, and whitespace gates pass.
**Resource/timing delta:** Pending a new macro-enabled Quartus fit; no estimate
is treated as a result.
**Outcome, reversal/workaround, remaining risk, and next gate:** The diagnostic
now observes the correct failing transaction in simulation. Stage an isolated
ext4 A-065 build, obtain Quartus reports and hashes, package under a new core
identity, and require a Pocket bar before changing functional RTL.

**VM staging/build update:** A fresh local-ext4 snapshot was created at
`/home/taualpha/tau-local/phase2-probe-a065-20260917`. Its first appended QSF
macro line was malformed by shell escaping; it was removed before any build,
then replaced with the exact line
`set_global_assignment -name VERILOG_MACRO "TAU_PHASE2_WINDOW"`. The isolated
copy passed `make check-fpga`, which confirmed Quartus 25.1std and the macro at
QSF line 773. The full flow launched as PID 35053 (child `quartus_map` PID
35081). This is **host** launch/preflight evidence only; no fit, timing,
artifact, or Pocket claim is made from it.

**Quartus/package result update:** The A-065 flow completed at 13:02:12 WEST
with 0 errors and 343 warnings in 45m17s. Raw RBF SHA-256 is
`dcdc78107dbb0dc729a71f9559f55dda3e6fd7e950c46468255f4c90e0878bdb`; SOF
SHA-256 is `e83eacb6f1a2d77774182a10e4417e47796d412f9b628373b9b8c14dfa195629`;
the bit-reversed Pocket RBF SHA-256 is
`ddc0ba874f2d46b472b608c15178b4d3fbf0df0ecb5955deee6089a5b1c02b14`.
Resource use is 6,088/18,480 ALMs (33%), 7,939 registers, 300/308 RAM blocks
(97%), and 11/66 DSP blocks (17%). Timing is positive; the tightest reported
hold slack is 0.124 ns. The host package
`alfatreze.TAU_SDRAM_PRB65`/`tau_sdram_prb65` is hard-pinned to that RBF and
the A-061 ROM hash. Package identity, hashes, UI fixture, audit-ID, and
whitespace checks pass. The Pocket card has not been modified; Pocket runtime
evidence remains pending explicit mounted-card confirmation.

**Card replacement update:** After explicit mounted-card confirmation, only the
superseded A-064 core, assets, platform JSON, and platform image were removed.
The A-065 package was copied in their place; card-side SHA-256 matches the
bit-reversed RBF `ddc0ba874f2d46b472b608c15178b4d3fbf0df0ecb5955deee6089a5b1c02b14`
and paired A-061 ROM. Normal Tau, the Phase-1 diagnostic, and the stress
diagnostic were retained. The five previously-problematic, regenerable Pocket
catalog/index caches were removed so the fresh platform mapping is rebuilt on
reconnect. This is **host** installation evidence only; no Pocket runtime or
SDRAM conclusion is implied.

### A-067 — A-065 Pocket trace moves the CPU-window boundary past the bridge

**Date:** 2026-09-17
**Decision/change:** Decode the first A-065 Pocket bar rather than infer payload
integrity from the failing screen. The 35 cells are
`GGGGGGGGRRRRGGGGRGGGGGRRGGRRGGGGGGG`. The target all-ones store is present at
the CPU source, the SDRAM-domain bridge captures `FFFFFFFF/F`, and both of its
16-bit controller requests are accepted; the final readback still returns zero.
**Alternatives and rationale:** Treat the red adapter/mux data cells as a
payload-loss boundary (rejected: their registered observation is not
transaction-locked and later bridge capture is all green); declare a physical
SDRAM defect (rejected: controller command/data timing remains unobserved);
change the controller now (rejected: next evidence can distinguish its input
latch, command drive, and read return without perturbing the normal core).
**Hot/cold impact:** Evidence and documentation only; A-065 is diagnostic-only
and the Pocket result did not change functional RTL or the normal player.
**Evidence:** **Pocket | code-review** — A-065 bar and result photo: 183
checks, 182 failures, first `A0200000` mismatch expected `FFFFFFFF`, actual
`00000000`; source review of recorder timing and bridge debug capture.
**Resource/timing delta:** None beyond A-065's recorded Quartus result.
**Outcome, reversal/workaround, remaining risk, and next gate:** The all-ones
transaction reaches and completes the bridge, so the active boundary is the
controller-facing write/read path. [Issue 018](issues/018-phase2-post-bridge-write-readback.md)
requires a controller-boundary recorder and controller-facing simulation before
another Quartus/Pocket cycle or any functional SDRAM change.

### A-068 — Add a transaction-bound controller-edge provenance recorder

**Date:** 2026-09-17
**Decision/change:** Add eleven retained A-066 diagnostic bits after A-065's
35 bridge-path cells. The new recorder captures the target CPU-owned all-ones
write as latched by `sdram_fb`, its registered external WRITE/DQ/DQM values,
and the following CPU read/`READ_OUTPUT` halfword predicates. The overlay grows
to 46 eight-pixel cells (368 pixels) and its renderer/check are widened with it.
**Alternatives and rationale:** Infer controller integrity from bridge capture
alone (rejected: A-065 already proves that does not explain zero readback);
sample on `p0_data_available` (rejected: it is asserted one convenience cycle
before `READ_OUTPUT`); trace every SDRAM transaction (rejected: a one-way
targeted recorder is lower risk, smaller, and sufficient for this boundary).
**Hot/cold impact:** Opt-in `TAU_PHASE2_WINDOW` diagnostics only. The normal
macro-off player, mapped window protocol, firmware ROM, cache policy, and
physical SDRAM timing parameters are unchanged.
**Evidence:** **code-review | simulation | host** — `tb_tau_sdram_cpu_window_probe`
passes the added two-stage clock-domain capture; `tb_sdram_fb_controller_probe`
passes with an independent DQ model that observes `WRITE/FFFF/DQM=00` and then
returns zero during `READ_OUTPUT`; arbiter/composed-path regressions, UI
fixture, audit-ID and whitespace checks pass. Icarus cannot elaborate this
controller due to the upstream unpacked SystemVerilog struct; Verilator 5.052
was used for this focused controller test. This is not Quartus or Pocket
evidence.
**Resource/timing delta:** Pending a fresh macro-enabled Quartus fit. The
additional debug registers and 88 pixels of overlay may change the previous
A-065 fit; no resource/timing claim is carried forward.
**Outcome, reversal/workaround, remaining risk, and next gate:** The next
Pocket image can distinguish controller input latch, registered write pins,
and sampled zero/all-ones read evidence. Stage a fresh ext4 A-066 build, record
Quartus resources/timing/hashes, package under a new identity, remove the
superseded A-065 package from the mounted card, then take one cold-boot bar and
failure photo. Do not modify functional SDRAM behavior before that gate.

### A-069 — A-066 Quartus staging paused: VM shared folder is not mounted

**Date:** 2026-09-17
**Decision/change:** Do not launch an A-066 Quartus fit from an unknown or
stale VM source tree. Authenticated VM inspection found Quartus 25.1std
available and prior isolated A-059–A-065 workspaces present, but
`/home/taualpha/tau-workspace` is not an active mount after the VM reboot.
**Alternatives and rationale:** Export the complete working tree directly over
SSH (not taken: the environment rejected that repository-data egress without
a fresh payload/destination-specific approval); build an older A-065 staging
tree (rejected: it omits A-066); remount the established UTM `share` path with
the VM's privileged mount command (pending fresh explicit approval).
**Hot/cold impact:** None; no VM source was changed and no Quartus process was
started.
**Evidence:** **host** — authenticated SSH reports Quartus Prime Lite 25.1std
Build 1129, lists the prior local-ext4 workspaces, and `findmnt` does not show
the configured 9p share.
**Resource/timing delta:** Not applicable; no A-066 fit exists.
**Outcome, reversal/workaround, remaining risk, and next gate:** The staged
build remains intentionally blocked rather than claiming A-066 is compiling.
After explicit approval to restore the known UTM share mount (or to copy this
exact tree over SSH), stage a fresh local-ext4 snapshot and run the normal
Quartus flow. See [issue 004](issues/004-vm-quartus-detached-launch.md) for
the persistent build-environment record.

### A-070 — A-066 staging excludes host-only symlink trees

**Date:** 2026-09-17
**Decision/change:** Restore the known UTM 9p `share` mount and stage A-066 on
VM-local ext4, excluding the repository's host-only `toolchain` and
`.venv-cptr` trees. The initial all-files copy stopped on macOS symlink loops
inside those trees; each partial A-066 staging directory was removed before
the clean copy was retried. The clean snapshot passed the FPGA environment
preflight and then completed Quartus successfully.
**Alternatives and rationale:** Follow host symlinks (rejected: produces
infinite `Too many levels of symbolic links` failures and these macOS runtime
trees are not FPGA inputs); build from the 9p mount (rejected: prior Quartus
work uses local ext4 to avoid shared-filesystem instability); reuse A-065
(rejected: it lacks the A-066 recorder).
**Hot/cold impact:** VM staging only. No source file was changed to work around
the host symlinks; the excluded trees are not consumed by Quartus. The staged
QSF adds only the opt-in `TAU_PHASE2_WINDOW` macro.
**Evidence:** **host | Quartus** — authenticated VM mount inspection; clean
source includes `tb_sdram_fb_controller_probe.v`; Quartus 25.1std preflight
passes; the A-066 flow succeeds at 17:55:45 WEST with 0 errors and 343
warnings in 47m51s. Raw RBF SHA-256 is
`8b4e1b75960ab36f1ae168b51a32626b3ab42ee8ff07e10d4bf194258d574293`; SOF
SHA-256 is `053367c72339367a063b09e8c628a2e63fa3e1566988ab3c97379eb8967d075b`.
**Resource/timing delta:** 6,150/18,480 ALMs (33%), 7,987 registers,
300/308 RAM blocks (97%), 11/66 DSP blocks (17%); positive worst-case setup
slack 0.338 ns and hold slack 0.126 ns, with design-wide TNS 0.0. Compared to
A-065: +62 ALMs, +48 registers, unchanged RAM/DSP, setup improves by 0.004 ns
and hold improves by 0.002 ns.
**Outcome, reversal/workaround, remaining risk, and next gate:** An explicit
approved SSH copy transferred only the verified RBF to the local workspace;
its local SHA-256 matches the Quartus result. `--probe-a066` then produced the
hard-pinned `alfatreze.TAU_SDRAM_PRB66`/`tau_sdram_prb66` bundle with
bit-reversed RBF SHA-256
`3f6f793d2096a2eac8b9528b86e2f600743887def60fd6901293d0f9c1526605` and
the A-061 ROM hash. Package identity and whitespace gates pass. A-066 now
awaits only a mounted-card replacement of superseded A-065 and the first
Pocket controller-boundary bar.

### A-071 — A-066 isolates the all-zero CPU result to the bridge read sample

**Date:** 2026-09-17
**Decision/change:** Decode the complete A-066 Pocket bar and correct the
two-halfword bridge's read sampling. The 46 cells are
`GGGGGGGGRRRRGGGGRGGGGGRRGGRRGGGGGGGGGGGGGGGGRG`. `sdram_fb` receives
the target `FFFF` write, drives `WRITE/FFFF/DQM=00`, and later observes an
all-ones read halfword; the CPU result remains zero.
**Alternatives and rationale:** Continue investigating physical SDRAM writes
(rejected: controller-side evidence proves they persist); treat the controller
read recorder as misaligned (rejected: it samples only `READ_OUTPUT` and its
focused model distinguishes zero from all ones); delay sampling globally in
`sdram_fb` (rejected: framebuffer clients may intentionally use its early
notification). Correct only the CPU bridge, whose state machine explicitly
consumes the early notification as data.
**Hot/cold impact:** The functional bridge changes only mapped CPU reads: each
16-bit read holds `m_end_burst_req` low through the first availability cycle,
then captures `m_q` on the next asserted cycle. Normal macro-off playback has
no CPU-window requester, but a fresh full-core fit/Pocket regression remains
mandatory.
**Evidence:** **Pocket | code-review | simulation** — A-066 failure photo:
183 checks, 181 failures, first `A0200000`, expected `FFFFFFFF`, actual
`00000000`; cells 35–43 green, 44 red, 45 green. Source review shows
`sdram_fb` advertises `next_cycle_is_read || READ_OUTPUT`, while the old bridge
sampled its first `m_data_available`. The updated bridge test injects early
zero then valid `BEEF/CAFE` and passes; composed-path, controller-recorder,
and probe regressions pass. This is not yet Quartus or a passing Pocket result.
**Resource/timing delta:** Pending a fresh macro-enabled Quartus fit.
**Outcome, reversal/workaround, remaining risk, and next gate:** The previous
controller/physical-SDRAM suspicion is reversed: the observed defect is a
bridge fast-input timing error. Build and package a new distinct diagnostic
artifact, preserve A-066 evidence until its successor is verified, and require
one cold-boot Pocket run before using the CPU window for cold data.

**Card replacement update:** With the card explicitly mounted, only
`alfatreze.TAU_SDRAM_PRB65`, `tau_sdram_prb65` assets/platform JSON/image, and
the five regenerable Pocket catalog/index cache files were removed. The A-066
core, assets, platform JSON, and platform image were copied from the verified
bundle. Card-side SHA-256 matches packaged RBF
`3f6f793d2096a2eac8b9528b86e2f600743887def60fd6901293d0f9c1526605` and
the A-061 ROM `f8a7f999cb0a503c9bef0536cead0c8f2ea046382efb2626bfdc8af60c16338a`.
The retained independent baselines are normal TAU, `TAU_SDRAM_DIAG`, and
`TAU_SDRAM_STRESS`. This is **host** installation evidence only; the first
Pocket A-066 observation is still required.

### A-072 — A-067 bridge read-timing fix passes a fresh macro-enabled Quartus flow

**Date:** 2026-09-17
**Decision/change:** Fit the A-071 two-notification bridge capture change in a
fresh local-ext4 VM stage with `TAU_PHASE2_WINDOW` enabled. The candidate
artifact is intentionally a distinct A-067 diagnostic, not a normal Tau build
or an overwrite of retained A-066 evidence.
**Alternatives and rationale:** Reuse the A-066 RBF (rejected: it contains the
known stale-first-notification bridge behaviour); change `sdram_fb`'s global
early availability protocol (rejected: other clients may rely on it); or claim
the focused simulation as hardware proof (rejected: the fast-input placement
and full-core timing require a new fit and Pocket observation).
**Hot/cold impact:** Only opt-in mapped CPU reads change: each bridge halfword
waits through the intentionally early availability indication and samples on
the following asserted availability. Audio, framebuffer ownership, normal
macro-off Tau, and all proposed cold-data migration remain unchanged.
**Evidence:** **simulation | Quartus | host** — bridge, composed-path,
CPU-window probe, controller-probe, audit-integrity, UI-renderer, and whitespace
checks passed before staging. Quartus 25.1std reports a successful flow; raw
RBF SHA-256 is `fb2b8b1db6ec67384e244f089f47f585317c30c0573d762f3c44970b68b8f580`
and SOF SHA-256 is `3799178108e7d85cbf68e4633e55720b5abb1eed728a3cf3a4194ea14a5e43ae`.
**Resource/timing delta:** 6,196/18,480 ALMs (34%), 7,970 registers,
300/308 RAM blocks (97%), and 11/66 DSP blocks (17%). Multicorner setup slack
is +0.932 ns, hold slack is +0.125 ns, and design-wide TNS is 0.0. Compared
with A-066: +46 ALMs, -17 registers, unchanged RAM/DSP, setup improves by
0.594 ns and hold decreases by 0.001 ns. The narrow hold margin remains a
standing clocking/CDC caution rather than a license for intuition.
**Outcome, reversal/workaround, remaining risk, and next gate:** The raw RBF
was copied through the approved SSH path and hash-matched locally. The
hard-pinned `TAU_SDRAM_PRB67` package has bit-reversed Pocket RBF SHA-256
`8c03edc7bcff3810e18bcd34380fae864546140678955ee8e85b4849709bd128` and
the existing readback ROM SHA-256
`f8a7f999cb0a503c9bef0536cead0c8f2ea046382efb2626bfdc8af60c16338a`.
With the card mounted, A-066 was removed and only A-067 plus its assets and
platform metadata were installed; the five regenerable catalog caches were
cleared. Card-side hashes match the package. This is **host** evidence only.
The A-067 cold-boot Pocket run then failed its established MMIO preflight with
`0x5DB54350` instead of `0x43505550`; see A-073. This invalidates the A-071
capture-delay hypothesis as a functional fix. The bounded uncached window and
all cold-data migration remain blocked.

### A-073 — A-067 Pocket preflight failure rejects the delayed bridge capture

**Date:** 2026-09-17
**Decision/change:** Record the first A-067 cold-boot Pocket observation and
revert its delayed `m_data_available` capture. The screen shows **MAILBOX
PREFLIGHT FAIL**, `ACTUAL 5DB54350`, and explicitly states that the CPU window
was not attempted. The preflight writes/reads `0x43505550` through the same
MMIO → owner mux → CDC bridge path that A-066 had passed before its CPU test.
**Alternatives and rationale:** Treat the result as a stale package (rejected:
the card-side A-067 hashes were verified and the A-067-specific ROM UI ran);
continue to CPU-window testing anyway (rejected: firmware deliberately stopped
before it); retain the A-071 capture delay because its focused simulation passed
(rejected: the Pocket regression outweighs the incomplete model). Restore the
known-good controller-client capture contract and add response provenance at
the bridge boundary instead of guessing where the all-zero CPU result arose.
**Hot/cold impact:** The A-067 functional change is removed. A-074 adds only
diagnostic registers and 24 pixels to the macro-only top bar; normal Tau,
audio, scanout ownership, and all cold-data policy remain untouched.
**Evidence:** **Pocket | code-review | simulation** — user photo of A-067
shows the exact preflight failure above. A-066 previously crossed this same
preflight and reached 183 CPU-window checks, so this is a real regression in
the changed bridge read path. The restored bridge test now models the documented
first-availability capture and verifies an all-ones follow-read response;
the extended probe test and UI coverage pass. The A-074 fit and package are
recorded in the dated follow-up below; no A-074 Pocket observation existed at
the time of this A-073 entry.
**Resource/timing delta:** Not applicable pending a new fit. The A-067
resource/timing report remains valid only for the rejected capture-delay
artifact, not for A-074.
**Outcome, reversal/workaround, remaining risk, and next gate:** The A-071
claim that delayed capture fixes the CPU result is reversed. A-074 will retain
the first mapped follow-read's assembled bridge response in three appended bar
cells: seen, zero, all-ones. A fresh macro-enabled Quartus build and one
cold-boot Pocket run must classify whether the bridge itself returns `FFFF` or
the later owner-mux/Wishbone response path loses it.

**Quartus seed-screen update:** A-074 seed 1 completed successfully in 44m33s
with raw RBF SHA-256
`cca1694723af01da931ce3fc9e3bf18f62e0a2e5653e40708895b2397b78913e`,
6,153/18,480 ALMs, 7,973 registers, 300/308 RAM blocks, and 11/66 DSP blocks.
However, multicorner setup slack is only +0.018 ns (hold +0.124 ns, TNS 0.0).
Although positive, that is materially below A-067's +0.932 ns and too narrow
to promote based on one routing seed. It is intentionally not packaged or
installed. An isolated seed-2 repeat was required before selecting an artifact.

### A-074 — Seed-2 fit signs off the bridge-response probe package

**Date:** 2026-09-17
**Decision/change:** Select the isolated A-074 seed-2 Quartus result for
hardware screening and package it as `alfatreze.TAU_SDRAM_PRB74` /
`tau_sdram_prb74`. Seed 2 completed with 0 errors in 49m24s, 6,153/18,480
ALMs, 7,973 registers, 300/308 RAM blocks, and 11/66 DSP blocks. Multicorner
setup slack is +0.662 ns, hold slack +0.119 ns, and TNS 0.0. The raw RBF SHA-256
is `d4b6295d168351704dc185abf358bb230be5cc2b77460a3adbaeea48c95b6c98`; the
bit-reversed package SHA-256 is
`794d5c9b1a9b646959a687dbaa2325beb821a006d671941896cf039f6b95d3db`.
**Alternatives and rationale:** Promote seed 1 (rejected: +0.018 ns setup
margin is too narrow); keep screening indefinitely (rejected: seed 2 restores
substantial timing margin while preserving the same resource footprint); use
the normal player bitstream (rejected: it cannot expose the bridge-response
provenance cells). The local package passed identity, SHA-256, UI snapshot,
audit uniqueness, and diff checks.
**Hot/cold impact:** Diagnostic-only macro path; normal Tau audio, scanout,
hot-path RAM, and all cold-data policy remain unchanged. No CPU-window pass or
SDRAM migration is authorized by this fit alone.
**Evidence:** **Quartus | host | code-review | simulation** — fit report and
hashes are host evidence, focused bridge/probe tests pass in simulation, and
the package metadata identifies the intended A-074 core. **Pocket evidence is
pending** because the environment approval service rejected the authorized
card-transfer operation; the card still contains the rejected A-067 probe.
**Outcome, remaining risk, and next gate:** A-074 seed 2 is the signed-off
candidate for one cold-boot Pocket observation. After transfer, remove only
the superseded A-067 package/cache entries, install A-074, and record the
49-cell bar plus complete result. The bridge-response cells must classify
whether the assembled response is all ones before any CPU-window matrix or
cold migration decision.

### A-075 — A-074 Pocket run proves bridge response is all ones

**Date:** 2026-09-18
**Decision/change:** Record the first A-074 cold-boot Pocket observation. The
diagnostic completed all 183 checks with 181 failures at
`A0200000`, expected `FFFFFFFF`, actual `00000000`. The 49-cell bar was
`GGGGGGGGRRRRGGGGRGGGGGRRGGRRGGGGGGGGGGGGGGGGRGGRG`.
**Interpretation:** A-074 cells 46–48 decode as `G-R-G`: the bridge saw the
following mapped read, its assembled 32-bit response was not zero, and it was
all ones. This is **Pocket** evidence that the controller/bridge assembly
returns `0xFFFFFFFF`; the loss is later in the owner-mux/Wishbone response
return path. The existing red cells 22–23 and 26–27 remain registered-signal
instrumentation caveats and are not used to override the transaction-locked
bridge result.
**Alternatives and rationale:** Attribute the result to physical SDRAM
failure (rejected: the controller-boundary and bridge-response evidence both
show all-ones data); repeat the same probe without changing RTL (deferred:
the boundary is now classified and another identical run adds little); move
to cached/cold migration (rejected: the uncached response path is still
unproven). The A-074 package identity and Quartus timing remain unchanged.
**Hot/cold impact:** No product path changed; no cold-data migration or CPU
window promotion is authorized.
**Outcome, remaining risk, and next gate:** Keep issue 018 open, revise its
boundary to the owner-mux/Wishbone return path, and add a focused probe or
simulation assertion that compares the bridge's assembled response with the
Wishbone `DAT_MISO`/ACK seen by the CPU. Do not run the uncached smoke matrix
until that return path is corrected and re-qualified.

### A-076 — Add a successor probe for CPU-facing Wishbone return data

**Date:** 2026-09-18
**Decision/change:** Add an opt-in `RETURN_PATH_MODE` to the existing 49-cell
CPU-window probe. In that mode, cells 46–48 capture the fifth-request
CPU-facing ACK/data result (seen, zero, all ones), while the A-074 default mode
continues to capture bridge assembly. Expose only `dACK` and `dDAT_MISO` from
`mp3_soc` under the diagnostic wiring; no product data path is changed.
**Alternatives and rationale:** Add more pixels (rejected: the 400-pixel
scanout leaves little room and would create another layout/package change);
replace the existing A-074 bridge evidence (rejected: it is needed as the
known-good boundary); infer the CPU value from firmware alone (rejected: that
cannot distinguish an ACK/data timing loss at the CPU-facing bus). A focused
simulation test is added before any Quartus build.
**Hot/cold impact:** Diagnostic-only; no normal Tau, audio, scanout, cache, or
cold-workspace behavior changes.
**Evidence:** **code-review | simulation | Quartus** — return-mode probe
simulation passes with a fifth-request zero result, and all prior CPU-window/
composed-path tests still pass. On 2026-09-19 the isolated local-ext4 VM stage
defined both `TAU_PHASE2_WINDOW` and `TAU_PHASE2_RETURN_PROBE`; Quartus 25.1
completed successfully in 43m35s with 0 errors, 6,207 / 18,480 ALMs (34%),
8,011 registers, 300 / 308 RAM blocks (97%), and 11 / 66 DSP blocks (17%).
The worst multicorner setup/hold slacks are +0.972 ns / +0.268 ns. The raw RBF
SHA-256 is `9ef62ebc4002abf4f5d29c84c59c08d997c18369d55e7c134beac5b97c832ef1`.
**Host packaging** then produced the separately identified A-076 bundle with
bit-reversed RBF SHA-256
`212e1761d2b4107e6d933e11bada800dfdaf78c8fc83ad9c1f36788e65fc747a` and the
unchanged A-061 readback ROM SHA-256
`f8a7f999cb0a503c9bef0536cead0c8f2ea046382efb2626bfdc8af60c16338a`.
**Host card-install evidence:** A-076's installed Pocket RBF and ROM matched
those two package hashes; only superseded A-067/A-074 CPU probes were removed.
This establishes file provenance, not that Pocket rebuilt its catalog or ran
the core. **Pocket execution:** the supplied A-076 photo shows 183 checks,
181 failures, first byte address `A0200000`, expected `FFFFFFFF`, actual
`00000000`, and the decoded 49-cell bar
`GGGGGGGGRRRRGGGGRGGGGGRRGGRRGGGGGGGGGGGGGGGGRGGGR`.
Cells 46–48 are `G-G-R`: the CPU-facing ACK was observed and the data sampled
at that ACK was zero, not all ones.
**Outcome, remaining risk, and next gate:** Together with A-075's observed
bridge-assembled `FFFFFFFF`, A-076 proves that zero is present at the final
`mp3_soc` CPU-facing data return when the target read acknowledges. The next
diagnostic must sample the adapter's `sdram_wb_cpu_rdata` at its ACK, then
compare that retained result with the already-observed final CPU bus fact.
This separates the adapter/mux response boundary from `mp3_soc`'s registered
return selector. Do not infer a cold-data migration result from this failure.

### A-077 — Add an adapter-return discriminator before the CPU selector

**Date:** 2026-09-19
**Decision/change:** Add `RETURN_PATH_MODE=2` to the same 49-cell probe and
expose the adapter's registered `sdram_wb_cpu_rdata` from `mp3_soc` for
diagnostic-only observation. In this mode, cells 46–48 retain the target
fifth-read adapter ACK, adapter data-zero predicate, and adapter data-all-ones
predicate. A-076 remains the separate, observed final CPU-bus reference.
**Alternatives and rationale:** Infer the adapter result from the A-076 zero
(rejected: A-076 observes only the later CPU-facing bus); add a wider overlay
that shows both values in one build (rejected: would change the reviewed 49-cell
layout and create a less comparable Pocket artifact); change the return logic
before locating the loss (rejected: it risks another plausible but unverified
fix). The mode-2 successor isolates one boundary without changing any product
return behavior.
**Hot/cold impact:** Diagnostic-only output wiring and an opt-in probe mode.
Normal Tau, audio, scanout, cache behavior, and the macro-off map are unchanged.
**Evidence:** **simulation | code-review | Quartus** — the new focused
adapter-return test retains `G-R-G` for an all-ones adapter result; existing
bridge-response, CPU-return, and composed-path tests pass unchanged. The
2026-09-19 isolated local-ext4 build defined `TAU_PHASE2_WINDOW` and
`TAU_PHASE2_ADAPTER_PROBE`, completed in 44m09s with 0 errors, 6,162 / 18,480
ALMs (33%), 8,053 registers, 300 / 308 RAM blocks (97%), and 11 / 66 DSP
blocks (17%). Its limiting multicorner setup/hold slacks are +0.801 ns / +0.115
ns. Raw RBF SHA-256 is
`53b11ee8fbfd2ff401a8a84255c88c4edd994333210933dfb1825d8b6bc6806f`.
**Host packaging/card-install evidence:** The separate A-077 package has a
bit-reversed RBF SHA-256 of
`1dd410d81ad9fa646a498bdcbead7d52cc5a7330fb0502d15dc489b40dd71918` and the
known A-061 ROM SHA-256
`f8a7f999cb0a503c9bef0536cead0c8f2ea046382efb2626bfdc8af60c16338a`.
Those hashes were verified after installation on the mounted Pocket card; only
the completed A-076 predecessor was removed. This is **host** provenance, not
Pocket catalog/execution evidence. The normal core list initially omitted
A-077 even though `cores_cache.bin` and `corelist_cache.bin` contained its
identity: `platforms_cache.bin`, `core_viewby_platform.bin`, and
`platform_viewby_category.bin` had no A-077 mapping. All five regenerable
catalog indexes were byte-backed-up under
`work/diagnostics/sdram-cpu-probe-a077/pocket-cache-backup-2026-09-19/System/`
and then removed from the card to force a Pocket rebuild. No core, asset,
music, save, or normal Tau file was touched. This remains **host** cache
maintenance evidence; Pocket catalog/execution evidence is pending.
**Pocket execution:** The supplied cold-boot A-077 photograph reports 183
checks, 181 failures, first byte address `A0200000`, expected `FFFFFFFF`, and
actual `00000000`. Its 49-cell status bar decodes to
`GGGGGGGGRRRRGGGGRGGGGGRRGGRRGGGGGGGGGGGGGGGGRGGGR` when calibrated against
the fixed first 46 cells. Cells 46–48 are `G-G-R`: the adapter ACK was
observed, while the adapter's registered return data was zero, not all ones.
This is **Pocket** evidence; the first result photo is retained in the task
record rather than treated as a simulated classification.
**Outcome, remaining risk, and next gate:** A-075 already observed the bridge
assemble `FFFFFFFF`; A-077 now proves that zero is present by the
bridge-mux/Wishbone-adapter boundary, before `mp3_soc`'s registered selector.
The selector is therefore no longer the primary suspect. The next bounded
probe (A-079; A-078 is reserved for the unrelated plan-schema compatibility
record) must retain the owner mux's `wb_done` and `wb_rdata` at the target
read, alongside the existing adapter result. `wb_rdata=FFFFFFFF` with an
adapter zero isolates the adapter capture/ACK timing; mux zero instead
isolates the owner-mux latch or its preceding bridge handoff. No package, fix,
or cold-data migration is authorized before that result is observed.

### A-079 — Add an owner-mux return discriminator before adapter capture

**Date:** 2026-09-19
**Decision/change:** Add `RETURN_PATH_MODE=3` to the existing fixed 49-cell
diagnostic overlay. In this mode, cells 46–48 retain the target fifth read's
owner-mux `wb_done`, `wb_rdata == 0`, and `wb_rdata == FFFFFFFF` predicates.
The signals are already the mux outputs wired into `tau_sdram_wb_adapter`, so
the probe adds diagnostic observation only; it changes no owner, request,
ACK, or product data-path behavior. It is selected only by the dedicated
`TAU_PHASE2_MUX_PROBE` macro, ahead of the older probe-mode macros.
**Alternatives and rationale:** Change adapter capture timing immediately
(rejected: A-077 localizes the loss to a remaining two-boundary interval but
does not say which boundary is wrong); expose both mux and adapter values in a
wider UI (rejected: loses the stable, photographed 49-cell format); infer mux
data from A-075 (rejected: that older evidence observes bridge assembly, not
the mux output at `wb_done`). This successor preserves one observable boundary
and gives two mutually exclusive conclusions.
**Hot/cold impact:** Diagnostic-only mode and overlay inputs; normal Tau,
audio, scanout, cache behavior, address map, and macro-off synthesis are
unchanged.
**Evidence:** **simulation | code-review | Quartus | host packaging** — the new
focused mux-return test captures `G-R-G` for an all-ones response at `wb_done`.
Existing CPU-window, CPU-return, adapter-return, bridge-mux, Wishbone-adapter,
standalone Phase 2, and composed-path simulations pass unchanged. The
2026-09-19 isolated local-ext4 Quartus build defined
`TAU_PHASE2_WINDOW` and `TAU_PHASE2_MUX_PROBE`, completed successfully in
43m55s with 0 errors, 6,173 / 18,480 ALMs (33%), 8,044 registers, 300 / 308
RAM blocks (97%), and 11 / 66 DSP blocks (17%). Its minimum reported
multicorner setup/hold slacks are +0.787 ns / +0.282 ns; the pre-existing
unconstrained-path warning remains a warning, not a claim of universal timing
closure. Raw RBF SHA-256 is
`246202a00fab50c6b96a38e8dd1acc4d835e5041b9d1784f325d2223421531e8`.
Host packaging produced the separately identified A-079 bundle with
bit-reversed Pocket RBF SHA-256
`6ce93f8713ea2e3d2dc74ae98ed215cfb7d84006b393ecd92619665ab47a93dd` and
the unchanged A-061 readback ROM SHA-256
`f8a7f999cb0a503c9bef0536cead0c8f2ea046382efb2626bfdc8af60c16338a`.
**Host card-install evidence:** Both hashes were verified after installation
on the mounted Pocket card. Only the superseded A-077 files were removed:
its core directory, platform asset directory, platform JSON, and platform
image. Normal Tau and the independent SDRAM Diagnostic/Stress cores were not
changed. The five regenerable Pocket catalog indexes were copied with SHA-256
provenance under
`work/diagnostics/sdram-cpu-probe-a079/pocket-cache-backup-2026-09-19/System/`
and then cleared so Pocket can rebuild the A-079 platform/category mapping.
This is **host** provenance; Pocket execution remains pending and must not be
inferred from it. **Pocket execution:** The supplied A-079 photo reports 183
checks, 181 failures, first byte address `A0200000`, expected `FFFFFFFF`, and
actual `00000000`. The stable bar runs at its top scan lines calibrate to
`GGGGGGGGRRRRGGGGRGGGGGRRGGRRGGGGGGGGGGGGGGGGRGGGR`.
Cells 46–48 are `G-G-R`: owner-mux completion was observed, its registered
return data was zero, and it was not all ones. This is **Pocket** evidence.
**Outcome, remaining risk, and next gate:** A-075's bridge-internal recorder
observed an assembled `FFFFFFFF`, while A-079 observes zero at the owner-mux
output. The adapter and `mp3_soc` selector are now excluded as primary
suspects. The live boundary is the bridge's system-domain response signals
(`sys_done`/`sys_rdata`) into `tau_sdram_bridge_mux`, or the mux's response
latch. Before changing either, add an end-to-end simulation that drives the
bridge response with the production registered completion timing and asserts
that the mux retains `FFFFFFFF` when it raises `wb_done`. The test must fail
against the observed timing before any candidate fix is accepted.
**Outcome, remaining risk, and next gate:** Build the exact source with
`TAU_PHASE2_WINDOW` and `TAU_PHASE2_MUX_PROBE`. A Pocket `G-R-G` result means
the mux presents all ones and A-077's adapter zero implicates the adapter
capture/ACK timing. A `G-G-R` result means the mux itself presents zero,
implicating its registered latch or the immediate bridge-to-mux handoff. In
either outcome, keep cold-data migration disabled; first correct, regress, fit,
and repeat the focused Pocket gate.

### A-078 — Keep Codex plan schema within CLI structured-output subset

- **Date:** 2026-09-19
- **Decision:** Remove conditional `allOf` branches from `tools/triad/schemas/codex-plan.schema.json` after Codex CLI 0.147.0 rejected the schema before planning. Preserve the same conditional guarantees in `director.py` runtime validation.
- **Alternatives considered:** Keep `allOf` (blocked by the installed CLI); weaken runtime validation (unsafe and rejected).
- **Scope:** Orchestration/schema compatibility only; no HDL, Quartus, SSH, or hardware changes.
- **Evidence:** Reproduced `invalid_json_schema ... 'allOf' is not permitted`; base schema and Director tests are the acceptance gate.
- **Outcome/readiness update (2026-09-19):** The original next gate was a
  read-only Director readiness probe. **Host** evidence in
  `work/triad/gate.log`, `work/triad/codex-review.md`, and
  `work/triad/claude-audit.md` shows that a no-change run reached planning,
  local host gates, clean Codex review, and Claude audit. Qwen was skipped and
  no implementation changed. The Astra approval/STOP lines are expected stdout
  from `CodexApprovalPolicyTests`, not a model launch. The gate is closed; no
  RTL simulation, Quartus, SSH, or Pocket operation occurred.

### A-080 — persistent CPU SDRAM diagnostic result record

**Date:** 2026-09-19
**Decision/change:** Add a dedicated, 64-byte nonvolatile diagnostic data slot
(ID 5) to CPU-window diagnostic packages. `fw/sdram_diag.c` writes a bounded
`TLOG` schema-1 record into the existing APF-visible datatable, issues target
`0184` to copy it to the save slot, then issues target `0188` to flush it.
The target-command selector expands from two to three bits and the
FPGA/firmware interlock advances from rev 22 to rev 23.

**Alternatives and rationale:** Manual screenshot only remains useful visual
evidence but needs transcription. A raw framebuffer dump would be larger,
non-PNG, and unnecessary during an SDRAM investigation. An append/ring log is
deferred until this bounded latest-result record proves itself on Pocket.
The selected slot leaves music, artwork, playlist, and settings slots outside
the diagnostic writer's reach.

**Evidence:** **simulation | host | Quartus** — `make test-rtl-tgt` passes read, write,
and flush completion sequencing; the flush-specific pulse is asserted exactly
once. `bash fw/build.sh sdram-cpu-readback` produces the diagnostic ROM. The
decoder rejects invalid size, magic, schema, and checksum, and decodes a known
failing fixture. An isolated `TAU_PHASE2_WINDOW` + `TAU_PHASE2_MUX_PROBE`
Quartus 25.1std build completed in 56m13s with 0 errors; raw RBF SHA-256 is
`f21a9ba0fe0d4d43d49c3d2f102eda8fdc5445516581928fc87730687a14baa4`.
The fit uses 300/308 RAM blocks and reports minimum hold slack +0.114 ns;
like prior builds, it warns that the design is not fully constrained. The
A-080 packager generated and host-validated the unique package, including only
firmware slot 1 and writable 64-byte log slot 5. No Pocket result exists.

**Resource/timing delta:** 300/308 RAM blocks; +0.114 ns tightest reported
hold slack; no resource migration is authorised.

**Outcome, remaining risk, and next gate:** `0188` requires new RTL wiring, so
the old RBF cannot test this firmware. The matching rev-23 diagnostic RBF and
ROM are now packaged; on Pocket compare the decoded save record against the
terminal screen. Only then classify persistence as **Pocket** verified.

**Installation evidence:** **host** — A-080 was copied to the mounted Pocket
card after removing only A-079 (`Cores`, `Assets`, platform JSON/image). The
on-card bit-reversed RBF SHA-256 is
`c892ae7089484e099090391b6f7aba3551d1b58cedb8415f3ead6eef42099e16`, ROM is
`0aa105744c24eb756b363d8b0fd4ba8eebb30b221693e12147f2920705b95f8a`, and the
pre-created 64-byte save file is
`f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b`.
The five regenerable Pocket catalog indexes were backed up in
`work/diagnostics/sdram-cpu-probe-a080/pocket-cache-backup-2026-09-19/System/`
and cleared. This is installation evidence only, not a Pocket execution result.

**Pocket result (failure):** Two native Pocket screenshots,
`20260919_193036.png` and `20260919_210853.png`, show the ordinary A-079
terminal result (183 checks, 181 failures, `A0200000`, expected `FFFFFFFF`,
actual `00000000`). The on-card A-080 RBF/ROM hashes remained correct, but its
dedicated 64-byte `last-result.tlog` was still all zero; the decoder rejected
it with `bad magic: 0x00000000`. Therefore A-080 proves neither target write
nor flush on Pocket. This is a persistence failure, not new SDRAM evidence;
see [issue 019](issues/019-a080-result-log-not-persisted.md).

### A-081 — stage an independent PSRAM capacity evaluation

**Date:** 2026-09-19
**Decision/change:** Record an early, no-RTL PSRAM evaluation plan. It uses
Pocket's otherwise idle 32 MiB Cellular PSRAM as a possible CPU-owned cold-data
path, beginning with a single-owner asynchronous controller at the existing
60 MHz system clock, a diagnostic MMIO mailbox, and only then an explicit
uncached CPU window. It reserves the last word of every die (`0x3FFFFF`) until
four FPGA shadow registers safely implement it.

**Alternatives and rationale:** Extend the current SDRAM port first (not
replaced; its Phase 2 failure remains open, but that work shares the real-time
framebuffer controller and a 60/100 MHz CDC); begin with 133 MHz PSRAM bursts
(deferred because a new clock/CDC and burst contract would obscure the first
hardware result); immediately migrate `pl_text` (rejected until a standalone
diagnostic and mapped CPU return are Pocket-proven). The asynchronous path
provides the smallest independent response path and allows the SDRAM lesson of
transaction-locked response observation to be applied from the start.

**Hot/cold impact:** Planning and documentation only; no FPGA pins, RTL,
firmware memory placement, normal Tau package, audio path, display path, or
SDRAM diagnostic behaviour changes.

**Evidence:** **code-review | research** — Tau's `core_top.v` presently ties
both `cram0` and `cram1` inactive while the QSF already assigns their 1.8 V
pins. Analogue documents two 16 MiB AS1C8M16PL Cellular PSRAM chips, each with
two CE-selected dies, asynchronous low-latency operation, a 133 MHz
synchronous-burst option, and a configuration-sequence hazard at each die's
last word. No simulation, Quartus, or Pocket PSRAM evidence exists.

**Resource/timing delta:** not applicable; no implementation.

**Outcome, remaining risk, and next gate:** The plan is recorded in
`PSRAM_EVALUATION_PLAN.md`. The first gate is P0: derive an explicit 60 MHz
asynchronous timing contract from the AS1C8M16PL datasheet and prove macro-off
idle pins before adding a controller. SDRAM issue 018 and all cold-data
migration gates remain open and independent.

### A-082 — expose target write and flush outcome on the diagnostic screen

**Date:** 2026-09-19

**Decision/change:** Add a firmware-only successor to A-080. It uses the same
rev-23 A-080 RBF and the same dedicated slot 5, but displays `WRITE` and
`FLUSH` command state (`D` complete, `T` local timeout, `-` not attempted) and
APF result code on the terminal screen. The record schema and SDRAM test are
unchanged.

**Alternatives and rationale:** Guess whether the zero file means APF rejected
the slot, the bridge pointer was wrong, or flush was not reached (rejected;
indistinguishable from the card). Add a new RTL probe (deferred; existing
rev-23 wiring already exposes target completion/error, so a firmware-only
display isolates the next boundary without another Quartus fit).

**Hot/cold impact:** Diagnostic firmware and dedicated save slot only. No audio,
normal Tau package, framebuffer arbitration, or SDRAM transaction behavior
changes.

**Evidence:** **host | simulation** — `bash fw/build.sh sdram-cpu-log-probe`
produced ROM SHA-256
`066965d7fcb12d13f778cc99b84b3c79f0b2675b0b7f85cd4da872af7bf10b99`.
`make test-rtl-tgt` still passes read/write/flush sequencing. The packager
host-validated the isolated A-082 package and its 64-byte save slot. The RBF is
the already Quartus-verified A-080 RBF; no new RTL requires a Quartus build.

**Resource/timing delta:** none; same A-080 RBF.

**Outcome, remaining risk, and next gate:** Install A-082 after removing only
A-080. A Pocket screen must tell whether the write command times out, returns
a nonzero APF error, or completes before the file remains zero. That outcome
selects the next change; do not alter SDRAM logic based on this result.

**Installation evidence:** **host** — A-082 replaced only A-080 on the mounted
card. The RBF SHA-256 is
`c892ae7089484e099090391b6f7aba3551d1b58cedb8415f3ead6eef42099e16`, ROM is
`066965d7fcb12d13f778cc99b84b3c79f0b2675b0b7f85cd4da872af7bf10b99`, and the
new zeroed save file is
`f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b`.
Regenerable Pocket catalog indexes were backed up under
`work/diagnostics/sdram-cpu-probe-a082/pocket-cache-backup-2026-09-19/System/`
then cleared. Pocket result pending.

**Pocket result:** Screenshot `20260919_214432.png` reports `WRITE D ERR 0`
and `FLUSH T ERR 0`: `0184` completed with result code zero, but `0188` did not
return before the local timeout. The A-082 save file remained all zero when
inspected. This narrows the persistence fault to flush behavior and/or deferred
nonvolatile-save lifecycle; it does not establish an SDRAM-path conclusion.
The next minimal gate is a root-menu core exit, then card remount and log
inspection; see [issue 019](issues/019-a080-result-log-not-persisted.md).

**Post-Quit Pocket result:** The A-082 file remained exactly 64 zero bytes
after root-menu Quit and card remount. Normal nonvolatile shutdown did not
persist the acknowledged `0184` write. The next gate is firmware-only slot
readback into a safe datatable buffer before exit, not an SDRAM logic change.

### A-083 — read slot 5 back before shutdown

**Date:** 2026-09-19

**Decision/change:** Add a firmware-only A-083 diagnostic that retains A-082's
write/flush status and immediately reads four bytes from slot 5 back to safe
datatable word 216. It renders the target read outcome and returned word.

**Alternatives and rationale:** Infer payload state from the zero SD file
(rejected; it cannot distinguish a no-op slot write from a persistence fault).
The readback has the same APF target-command contract but creates a direct
pre-shutdown observation. It reuses the rev-23 RBF and changes no SDRAM logic.

**Hot/cold impact:** Diagnostic firmware and dedicated log slot only.

**Evidence:** **host | simulation** — ROM SHA-256
`acbae22769c4ff47f5b134a37338de4c3e75fa091e9ba531afe0900d5b4f6c0f`;
target-command sequencing regression passes; isolated package slot check passes.

**Resource/timing delta:** none; same A-080 RBF.

**Outcome, remaining risk, and next gate:** A-083 replaces A-082. The Pocket
screen's `READ` state/error/data selects the next APF persistence investigation.

**Pocket outcome:** **Pocket | host** — Screenshot `20260919_215538.png`
reports `WRITE D ERR 0`, `FLUSH T ERR 0`, and `READ D ERR 0 DATA 00000000`.
The mounted slot-5 file is still 64 zero bytes. The acknowledged write was not
observable even before shutdown, so persistence-only is no longer an adequate
explanation. This remains unrelated to the SDRAM zero-read fault.

### A-084 — explicitly open the diagnostic result slot before writing

**Date:** 2026-09-19

**Decision/change:** Add a firmware-only A-084 successor. It performs `0190`
on slot 5, copies APF's returned 256-byte descriptor from words 64..127 to the
separate `0192` parameter buffer at words 128..191, then opens slot 5 with
`0192` before the diagnostic writes or reads it. The screen reports OPEN,
WRITE, FLUSH, and immediate READ outcomes.

**Alternatives and rationale:** Construct a new `0192` structure by guessing
its fields (rejected; the established playlist path deliberately avoids that
and past mistakes came from guessed layouts). Change SDRAM wiring (rejected;
A-083 is target-slot behavior, not SDRAM evidence). Reuse APF's own descriptor
(selected; production-proven pattern and minimal isolated discriminator).

**Hot/cold impact:** Dedicated diagnostic firmware and result slot only. No
audio, normal player behavior, SDRAM arbitration, or FPGA RTL changes.

**Evidence:** **code-review | host** — `bash fw/build.sh sdram-cpu-log-open`
produced ROM SHA-256
`ab68e6610f9b022040267a880813ed988ffd7b82987f4f8a4d42758128fd8c64`.
`python3 tools/package_sdram_cpu_diagnostic.py --probe-a084` validated an
isolated package and zeroed 64-byte slot. The RBF is the existing
Quartus-verified A-080 RBF; no new RTL means no Quartus fit is required.

**Resource/timing delta:** none; same A-080 RBF.

**Outcome, reversal/workaround, remaining risk, and next gate:** Install A-084
in place of A-083. If OPEN succeeds and the immediate read returns `544C4F47`,
separate in-memory write correctness from delayed flush/persistence. If OPEN
or READ errors, inspect APF slot definition/lifecycle requirements. Pocket
evidence pending.

**Installation evidence:** **host** — A-084 replaced only A-083 on the mounted
card. Its bit-reversed RBF SHA-256 is
`c892ae7089484e099090391b6f7aba3551d1b58cedb8415f3ead6eef42099e16`, ROM is
`ab68e6610f9b022040267a880813ed988ffd7b82987f4f8a4d42758128fd8c64`, and the
initial 64-byte save file is
`f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b`.
The five regenerable Pocket catalog indexes were backed up under
`work/diagnostics/sdram-cpu-probe-a084/pocket-cache-backup-2026-09-19/System/`
then cleared, so Pocket will rebuild its catalogue for the new core. Pocket
result pending.

**Pocket outcome:** **Pocket | host** — Screenshot `20260919_220843.png`
reports `OPEN D ERR 0`, `WRITE D ERR 0`, `FLUSH T ERR 0`, and `READ T ERR 0`.
The mounted file still contains 64 zero bytes. This reverses the working
assumption that an accepted `0192` immediately enables a target read; it is
consistent with the production player's already-recorded post-open settling
behavior. It remains a diagnostic-slot/APF result, not SDRAM evidence.

### A-085 — wait for two bounded post-open reads before result write

**Date:** 2026-09-19

**Decision/change:** Add a firmware-only A-085 successor. After A-084's
descriptor-copy open, it issues `0180` reads with 100-ms deadlines roughly
30 ms apart, requiring two consecutive successes before target write, flush,
and readback. The final screen shows READY state and attempt count.

**Alternatives and rationale:** Fixed sleep (rejected; hides whether the slot
was actually usable and makes the delay arbitrary). Immediate write/read
(rejected by A-084). Two successful reads (selected; mirrors the established
player settle rule while giving a bounded, Pocket-visible measurement).

**Hot/cold impact:** Diagnostic firmware and isolated slot only; no FPGA RTL,
audio path, normal player behavior, or SDRAM ownership changes.

**Evidence:** **code-review | host | simulation** — build ROM SHA-256
`bb91947cbf652d3e4ab281463378f662fe789087c9550476320fcbfb58028557`;
package validation passed; `make test-rtl-tgt` passes. Same Quartus-verified
A-080 RBF, therefore no new fit is required.

**Resource/timing delta:** none; same RBF. Firmware is 9,456 bytes (5.2% of
usable instruction RAM); settle cap is 16 × (100 ms command deadline + 31 ms
spacing), before the normal SDRAM test begins.

**Outcome, remaining risk, and next gate:** Replace only A-084 with A-085.
If READY completes and immediate post-write READ returns `544C4F47`, proceed to
flush/persistence isolation. If READY cannot complete, investigate APF's
deferred-slot definition rather than changing SDRAM. Pocket evidence pending.

**Installation evidence:** **host** — A-085 replaced only A-084 on the mounted
card. Bit-reversed RBF SHA-256 remains
`c892ae7089484e099090391b6f7aba3551d1b58cedb8415f3ead6eef42099e16`; A-085
ROM SHA-256 is `bb91947cbf652d3e4ab281463378f662fe789087c9550476320fcbfb58028557`;
the new isolated save file begins as the verified 64 zero bytes
`f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b`.
The same five regenerable catalog indexes were backed up under
`work/diagnostics/sdram-cpu-probe-a085/pocket-cache-backup-2026-09-19/System/`
and cleared. Pocket evidence pending.

**Pocket outcome:** **Pocket | host** — Screenshot `20260919_221941.png`
reports `OPEN D ERR 0`, `READY D #02`, `WRITE D ERR 0`, `FLUSH T ERR 0`, and
`READ T ERR 0`; its result file remains zero. This validates the bounded
post-open readiness gate, but reverses the assumption that its later read
classified the write: the timed-out flush was issued first and can still occupy
the single target-command bridge.

### A-086 — validate write before issuing the known-problematic flush

**Date:** 2026-09-19

**Decision/change:** Add firmware-only A-086. It retains A-085's successful
open and readiness gate, but performs immediate slot readback after `0184` and
only then sends `0188` flush. The status screen still reports all outcomes.

**Alternatives and rationale:** Treat A-085's post-flush read timeout as a
failed write (rejected; the flush may leave the one-command bridge waiting).
Remove flush entirely (rejected; it remains useful persistence evidence after
the direct write verdict). Reorder read before flush (selected; isolates the
write boundary without changing hardware).

**Hot/cold impact:** Diagnostic firmware and isolated slot only. No FPGA RTL,
audio, normal player behavior, or SDRAM logic changes.

**Evidence:** **code-review | host | simulation** — ROM SHA-256
`b19a8e6b22e4092bc7963e8882a13f5d7562ea5b1390d094330fe30ca3d51173`;
package validation and `make test-rtl-tgt` pass. Same Quartus-verified A-080
RBF; no fresh Quartus build is required.

**Resource/timing delta:** none; same RBF. Firmware is 9,468 bytes (5.3% of
usable instruction RAM).

**Outcome, remaining risk, and next gate:** Replace only A-085 with A-086.
`READ D ERR 0 DATA 544C4F47` establishes in-memory write success before flush;
zero or error selects the APF write/bridge-address investigation. Pocket
evidence pending.

**Installation evidence:** **host** — A-086 replaced only A-085 on the mounted
card. Its bit-reversed RBF SHA-256 remains
`c892ae7089484e099090391b6f7aba3551d1b58cedb8415f3ead6eef42099e16`; ROM is
`b19a8e6b22e4092bc7963e8882a13f5d7562ea5b1390d094330fe30ca3d51173`; its
new 64-byte isolated save file has hash
`f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b`.
Regenerable catalog indexes were backed up under
`work/diagnostics/sdram-cpu-probe-a086/pocket-cache-backup-2026-09-19/System/`
then cleared. Pocket evidence pending.

**Pocket outcome:** **Pocket | host** — Screenshot `20260919_222659.png`
reports `OPEN D ERR 0`, `READY D #02`, `WRITE D ERR 0`, `READ D ERR 0 DATA
00000000`, and `FLUSH T ERR 0`. Since READ completed before FLUSH, this is a
direct target write-path failure. The save file remains zero; the next gate is
to display the local payload words 200–203 before 0184 and compare them with
target readback. SDRAM arbitration is out of scope.

### A-087 — display local payload words before the target write

**Date:** 2026-09-19

**Decision/change:** Add firmware-only A-087 (`TAU_LOG_SOURCE_PROBE`). After the
record is written to datatable words 200–215, the CPU samples words 200–203 and
the terminal screen shows them (`S w200 w201` / `S w202 w203`) beside the A-086
OPEN/READY/WRITE/READ/FLUSH lines.

**Alternatives and rationale:** Change SDRAM arbitration (rejected; the failure
is at the APF write boundary, not the SDRAM path). Rebuild the FPGA (rejected;
the A-080 RBF is unchanged). Show local words (selected; separates a missing
payload from an APF address/slot mapping fault).

**Hot/cold impact:** Diagnostic firmware and isolated slot 5 only.

**Evidence:** **host** — `fw/build.sh sdram-cpu-log-source` builds 9,876 bytes
(5.5% of usable RAM); packaged with `--probe-a087`. ROM SHA-256
`1dff6c3fde7c82040aa3390a96fcf0739fd5fcdec52dfbc976daafd34c8b0fda`; RBF
unchanged (`c892ae70...9e16`). Simulation and Pocket not yet run.

**Resource/timing delta:** none; same RBF.

**Outcome, remaining risk, and next gate:** Bundle at
`work/diagnostics/sdram-cpu-probe-a087/pocket`, **not yet installed on the card**.
Local `544C4F47 00010040` with target readback zero means APF is not receiving
or mapping the payload; local zeros means the datatable write path is at fault.
Pocket evidence pending.

**Installation evidence:** **host** — A-087 replaced only A-086 on the mounted
card (A-086 core, platform, image, assets, save and settings removed). RBF
`c892ae70...9e16` and ROM `1dff6c3f...0fda` verified on the card by SHA-256;
the new isolated save file is the verified 64 zero bytes `f5a5fd42...fb4b`.
The five regenerable catalog indexes were backed up under
`work/diagnostics/sdram-cpu-probe-a087/pocket-cache-backup-2026-09-19/System/`
and cleared; the removed A-086 result file is kept in `a086-removed/` beside
them. The card was unmounted after `sync`. Installation evidence only.

**Pocket outcome:** **Pocket | host** — Screenshot `20260919_223832.png` reports
`OPEN D ERR 0`, `READY D #02`, `WRITE D ERR 0`, `FLUSH T ERR 0`,
`READ D ERR 0 DATA 00000000`, and source words `S 544C4F47 00010040` /
`S 00000304 4D503317` (magic, schema, stage|fail|timeout, version). The local
datatable payload is intact at 200–203 immediately before `0184`, yet target
readback is zero and `last-result.tlog` is still 64 zero bytes (SHA-256
`f5a5fd42...fb4b`). This rules out a missing/corrupt source payload; the fault is
in how APF accepts or maps the slot-5 write (bridge address, offset, slot
state, or a write reported OK without transfer). SDRAM remains out of scope.

### A-088 — correct the datatable bridge base address

**Date:** 2026-09-19

**Decision/change:** Firmware-only. `LOG_BRIDGE_ADDR` and `LOG_READ_BRIDGE_ADDR`
in `fw/sdram_diag.c` used base `0xF8000000`; the datatable is bridged at
`0xF8002000` (`core_game.vh` maps word 64 to `0xF8002100`, and
`DIAGNOSTIC_RESULT_LOG.md` already specified `0xF8002320`). Both now derive from
`DT_BRIDGE_BASE = 0xF8002000`. The A-087 source-word display is kept.

**Alternatives and rationale:** Further APF slot/parameter experiments (rejected
until this address error is removed; it fully explains A-082..A-087: 0184 read
its payload from outside the datatable and 0180 stored outside it, while local
`dt_write`/`dt_read` and the OPEN command, which uses the correct struct
addresses, all looked healthy).

**Hot/cold impact:** Diagnostic firmware and isolated slot 5 only.

**Evidence:** **code-review | host** — ROM SHA-256
`c3e2145740690f4ed47d504e2d93c2dcc6f7489c6e1bb95c24f18155764dcccc` (9,876
bytes); same RBF `c892ae70...9e16`; packaged at
`work/diagnostics/sdram-cpu-probe-a088/pocket`. **Reversal:** the A-082..A-087
conclusions of a "target write-path failure" are superseded; those probes tested
a wrong address.

**Resource/timing delta:** none.

**Outcome, remaining risk, and next gate:** Installed (see below). Expect
`READ D ERR 0 DATA 544C4F47` and a decoded `last-result.tlog`. Flush timeout may
persist independently; if the read passes, investigate it separately.

**Installation evidence:** **host** — A-088 replaced only A-087 on the mounted
card (A-087 core, platform, image, assets, save and settings removed). ROM
`c3e21457...dccc` and RBF `c892ae70...9e16` verified on the card by SHA-256; the
new isolated save file is the verified 64 zero bytes `f5a5fd42...fb4b`. The five
regenerable catalog indexes were backed up under
`work/diagnostics/sdram-cpu-probe-a088/pocket-cache-backup-2026-09-19/System/`
and cleared; the removed A-087 result file is in `a087-removed/`. Pocket
evidence pending.

**Pocket outcome:** **Pocket | host** — Screenshot `20260919_224340.png`
reports `OPEN D ERR 0`, `READY D #02`, `WRITE D ERR 0`, `FLUSH T ERR 0`,
`READ D ERR 0 DATA 544C4F47`. The address fix is **confirmed**: the record now
reaches active slot 5 (A-082..A-087 "write-path failure" was the wrong bridge
base). `last-result.tlog` on the card is still 64 zero bytes: persistence to the
SD file has not happened. The remaining fault is the `0188` flush (local
timeout) and/or the slot's persistence parameters (`0x22`, no nonvolatile bit).

### A-089 — mark the result slot nonvolatile

**Date:** 2026-09-19

**Decision/change:** Packaging-only. Same A-088 ROM (`c3e21457...dccc`) and RBF
(`c892ae70...9e16`); slot 5 `parameters` changes from `0x22` to `0x86`
(core-specific | nonvolatile | deferload, matching the `0x84` save slots of
other cores on the card). Added `slot_parameters` to the packager profile.

**Alternatives and rationale:** Firmware changes (rejected; A-088 proved the
slot write works). Leave `0x22` (rejected; it lacks the nonvolatile bit, a
plausible reason APF never answers `0188` or writes the file). The bit meanings
are from memory of the Analogue docs and are **unverified** in this repo.

**Hot/cold impact:** Diagnostic slot 5 only.

**Evidence:** **host** — bundle `work/diagnostics/sdram-cpu-probe-a089/pocket`.

**Installation evidence:** **host** — A-089 replaced only A-088 on the mounted
card; ROM and RBF SHA-256 verified; zero 64-byte save file `f5a5fd42...fb4b`;
catalog indexes backed up under
`work/diagnostics/sdram-cpu-probe-a089/pocket-cache-backup-2026-09-19/System/`
and cleared; A-088 removed with its result file kept in `a088-removed/`.
Pocket evidence pending. Pass: `FLUSH D ERR 0` and a decodable file.

**Pocket outcome:** **Pocket | host** — Screenshot `20260919_224639.png` is
identical to A-088: `OPEN/READY/WRITE` OK, `READ D ERR 0 DATA 544C4F47`,
`FLUSH T ERR 0`; `last-result.tlog` is still 64 zero bytes. **Reversal:** the
`0x22` -> `0x86` parameter hypothesis is **not supported**; the flush timeout is
independent of the nonvolatile bit. Next: inspect the `0188` command path
(RTL handshake and whether this Pocket firmware answers it at all).

**Quit test (A-089):** **Pocket | host** — after quitting to the menu and
relaunching, `last-result.tlog` was still the 64 zero bytes (`f5a5fd42...fb4b`).
Sleep is not supported by the Pocket for this core, so sleep/wake (upstream check
C4) remains untested. No Quit or boot hang was observed with the nonvolatile bit.

### A-090 — slot-table size, flush timing, post-flush re-read

**Date:** 2026-09-19

**Decision/change:** Firmware-only (`TAU_LOG_TABLE_PROBE`, `fw/build.sh
sdram-cpu-log-table`; `--probe-a090`). Shows on screen: slot 5's size from
APF's `{id,size}` table before (`B`) and after (`A`) `0184` with a table-hash
`=`/`!` (upstream checks C1/C2); the `0188` flush now waits 10 s and reports its
duration in ms (`F`); and a second slot read after the flush (`R2`). Word 216 is
cleared before each read so stale data cannot pass. Slot 5 `parameters` revert
to `0x22` (A-089's nonvolatile bit had no effect and upstream's nonvolatile slot
hung the Pocket).

**Alternatives and rationale:** interact.json persist channel (deferred; kept as
the fallback if this fails); RTL changes (rejected; the bridge handshake matches
the working read/write path).

**Hot/cold impact:** Diagnostic firmware and isolated slot 5 only.

**Evidence:** **host** — ROM SHA-256
`af9a2010c7672b3543440a181576406bec012398043175ac585b0986e3c01253` (10,680
bytes, 5.9% of RAM); same RBF `c892ae70...9e16`. Predictions, written before the
run: P1 `T5 B/A` = `00000040`; P2 table hash `=`; P3 `READ` and `R2` =
`544C4F47`; P4 flush either returns within 10 s (`F` < 10000MS, `FLUSH D`) or
still times out at ~10000MS, in which case `0188` is unsupported here and the
file stays zero.

**Installation evidence:** **host** — A-090 replaced only A-089 on the mounted
card; ROM and RBF SHA-256 verified; zero 64-byte save `f5a5fd42...fb4b`;
catalog indexes backed up under
`work/diagnostics/sdram-cpu-probe-a090/pocket-cache-backup-2026-09-19/System/`
and cleared; A-089 removed (result file kept in `a089-removed/`). Pocket
evidence pending.

**Pocket outcome:** **Pocket | host** — Screenshot `20260919_230517.png`:
`OPEN/READY/WRITE` OK, `FLUSH T ERR 0`, `READ D ERR 0 DATA 544C4F47`,
`T5 B 00000040 A 00000040 =`, `F 10000MS R2 T 00000000`. `last-result.tlog` is
still 64 zero bytes. Against the predictions: table size 64 and table intact
(P1, P2 confirmed); first read `544C4F47` (P3 half confirmed; `R2` did **not**
read, it timed out); the flush ran the full 10 s and never answered (P4: `0188`
is unanswered by this Pocket/firmware). The timed-out flush leaves the
single-command bridge occupied, which is why `R2` also timed out and matches
A-085. The longer run time you noticed is the 10 s flush wait plus the extra
read timeout. SDRAM check this run: 183 checks, 181 failures, `ACTUAL` shown (A-088/89
ended earlier at 50/48 with `TIMEOUT CODE`; not investigated here).

**Conclusion:** slot 5 is valid in APF's table, the write reaches APF's slot,
but `0188` never completes and nothing is persisted to the SD file, including
after Quit. **Upstream correction:** `docs/A088_UPSTREAM_CHECKS.md` said
upstream rewrote `settings.bin` via `0184`; upstream's ROADMAP says `0184`
destroyed three libraries and is off permanently, its `nonvolatile` slot hung
the Pocket, and only `interact.json` persistence works. Do not treat upstream as
evidence that `0184` persists a file. Recommended next: stop chasing `0184`
persistence and move to the `interact.json` compact-result channel.

### A-091 — publish the result through interact.json persist (Pocket PASS)

**Date:** 2026-09-19

**Decision/change:** Firmware + packaging, no RTL. `TAU_LOG_INTERACT_PROBE`
(`fw/build.sh sdram-cpu-log-interact`, `--probe-a091`) publishes the 16-word
TLOG record to the existing 16 `set_reg` words (`R_SET_IDX 0x6C`, `R_SET_DAT
0x70`; RTL `core_game.vh` section 8) that APF stores itself to
`Settings/<core>/Interact/_core/interact_persist.json` when the core quits.
The diagnostic core declares 16 `slider_u32` persist variables (ids 30..45,
`0x20000000 + 4*i`, range 0..2147483647). APF stores signed int32, so words
0..14 carry their low 31 bits and word 15 carries the withheld top bits (upstream
lesson, `fw/player.c` ~815). No data slot, `0184`, `0188`, or Saves file; the
10 s flush wait and wedged bridge are gone. The screen shows `PUBLISHED 16
WORDS` and read-backs of words 0, 7, 12 and 15. `tools/decode_tau_diag_log.py
--interact <interact_persist.json>` rebuilds and validates the record (checked
on a synthetic file with `A0200000` and `FFFFFFFF`).

**Alternatives and rationale:** keep pursuing `0184`/`0188` (rejected; A-090 shows
`0188` is unanswered and upstream's ROADMAP marks `0184` unsafe and off);
compact pass/fail code only (unnecessary: the diagnostic core has all 16 words
free, unlike the player).

**Hot/cold impact:** Diagnostic core only. Uses the same Quartus-verified A-080
RBF; the settings words are shared FPGA state but this core has no player.

**Evidence:** **host** — ROM SHA-256
`63c89cf63ed3c35dfc188d3e1a82d27e1132b256d1cbbd2f9813ee1beb461534` (7,752 bytes,
4.3% of RAM); RBF `c892ae70...9e16`; bundle at
`work/diagnostics/sdram-cpu-probe-a091/pocket` (16 variables, slot 5 absent).
Not yet run on hardware or installed. Predictions, written before the run: P1
the screen shows `W0 544C4F47` and `WF` = top-bit mask (`0` unless a word has
bit 31 set; `A0200000` on a failure sets bit 8 of word 8 => `WF` bit 8);
P2 after Quit, `interact_persist.json` has ids 30..45 with non-zero values that
`decode_tau_diag_log.py --interact` validates (checksum OK); P3 the core still
boots and quits normally. **Risks:** APF may not save on a hard power-off (Quit
first); the 16 variables appear in the Core Settings menu and could be edited by
hand; the R_SET readback depends on the clk_74a toggle landing within 10 ms.

**Installation evidence:** **host** — A-091 replaced only A-090 on the mounted
card (A-090 core, platform, image, assets, save and settings removed; its result
file is in `a090-removed/`). ROM `63c89cf6...1534` and RBF `c892ae70...9e16`
verified on the card by SHA-256; the card's `interact.json` has 16 persist
variables and no Saves file was created. Catalog indexes backed up under
`work/diagnostics/sdram-cpu-probe-a091/pocket-cache-backup-2026-09-19/System/`
and cleared. Pocket evidence pending. Pass: decodable `interact_persist.json`
(after Quit) that matches the screen.

**Pocket outcome:** **Pocket | host** — **PASS for the persistence channel.**
After Quit, `Settings/alfatreze.TAU_SDRAM_PRB91/Interact/_core/interact_persist.json`
(copy kept at `work/diagnostics/sdram-cpu-probe-a091/pocket-result/`) decodes
with `tools/decode_tau_diag_log.py --interact`: magic, schema and XOR checksum
valid (`0x47896EF5`); stage 4, run 1, 183 checks, 181 failures, first failure
`0xA0200000` expected `0xFFFFFFFF` actual `0x00000000`, `timed_out` false,
`status0 0x53444641`, core version `0x4D503317`. Predictions: P2 confirmed
(ids 30..45, validated record); P3 confirmed as reported by the user (core quit
normally; no hang reported). P1 is **confirmed** by screenshot `20260919_232118.png`
(`PUBLISHED 16 WORDS`, `W0 544C4F47`, `W7 000000B5` = 181, `WC 478E986A`, `WF
00000300` = top bits of words 8 and 9). That screen is a second run: the persist
file was rewritten at 23:21 and its checksum (`0x478E986A`) now matches `WC`
exactly; the earlier 23:18 file (checksum `0x47896EF5`) is kept beside it in
`pocket-result/` as `interact_persist-run-23-18.json`. The record agrees with the earlier
screen evidence for the SDRAM failure (181/183 at `A0200000`, actual zero).
This closes the result-log path: the SD-card record now works without `0184`
or `0188`. Issue 019 can be resolved on this evidence. It says nothing about the
SDRAM return-path fault itself, which remains open.

### A-092 — mailbox-vs-CPU-window discriminator (Pocket result: reads lag one beat)

**Date:** 2026-09-20

**Decision/change:** Start on the SDRAM CPU return path with a firmware-only
probe that needs no Quartus build. `TAU_DISCRIMINATOR_PROBE`
(`fw/build.sh sdram-cpu-disc`, `--probe-a092`) replaces the 183-check matrix
with 14 raw observations that separate the proven mailbox path (DIAG owner)
from the CPU window (WB owner) and use distinctive data so lag, swap, shift or
address error is visible, then publishes them through the A-091 interact.json
channel (`decode_tau_diag_log.py --interact --raw`). Same A-080 RBF.

**Why (review of A-060..A-079 and issue 018):** the mailbox write/read works
(A-061 preflight), a CPU read of a mailbox-written word works, but a CPU-written
word reads zero at the CPU. The bridge recorder (A-074/A-075) said the follow
read assembled `FFFFFFFF`, the controller recorder (A-066) sampled `FFFF` at
`READ_OUTPUT`, yet the mux/adapter/CPU (A-076/A-077/A-079) see zero. Those are
different builds and different recorders, never both on the same read. Also,
181/183 failures (only 2 passes) means even expected-zero reads fail, so reads
are not simply "always zero". The existing composed-path simulation is a stub
(constant `CAFE`, no readback assert), so it cannot show the failure; a real
end-to-end gate needs a functional SDRAM model around `sdram_fb`.

**Observations (words, all raw 32-bit):** w0 `44534331`; w1 mailbox write/read
of A=`12345678`; w2 CPU read of A; w3 mailbox read of B after a CPU write of
`A5C33C5A` (neighbours B-4=`0DEFACED`, B+4=`0BADF00D` pre-seeded by mailbox);
w4/w5 CPU read of B twice; w6/w7 CPU read of B+4/B-4; w8 CPU read of a
mailbox-written `FFFFFFFF`; w9 mailbox read of D after a CPU `FFFFFFFF` write;
w10 CPU read of D; w11 CPU read of D after a CPU zero write; w12 mailbox read of
D; w13 bitmask of failed mailbox commands; w14 CPU read of A again at the end.

**Predictions (before any run):** P1 w1=w2=w14=`12345678`. P2 w6=`0BADF00D`,
w7=`0DEFACED`. P3 w8=`FFFFFFFF` (reads are fine when the CPU did not write).
**Decision table for w3/w4:** w3=`A5C33C5A` and w4/w5=0 -> the write lands and
the CPU read after a CPU write is wrong (suspect write recovery/bank state, not
the mux); w3 wrong or 0 -> the CPU write does not land or lands elsewhere (check
w6/w7 for a shifted neighbour, halves swapped => lane fault); w4=w6 or w7 ->
CPU read address off by one word; w4 differs from w5 -> read data is lagging.

**Evidence:** **host** — ROM SHA-256
`d8a991e876d5a09f4cd70598f8292d1e17d8af39fdf6813d0331e0727f3e8a57` (5,320
bytes, 3.0% of RAM); RBF `c892ae70...9e16`; bundle at
`work/diagnostics/sdram-cpu-probe-a092/pocket`. Not yet run or installed.

**Alternatives and rationale:** another 44-minute Quartus probe (deferred until
this cheap run localises write vs read); a functional SDRAM model plus
production-timing regression (still required before any RTL change, per A-079).

**Installation evidence:** **host** — A-092 replaced only A-091 on the mounted
card (A-091 core, platform, image, assets and settings removed; its persist file
is kept under `pocket-cache-backup-2026-09-20/a091-removed/`). ROM `d8a991e8...8a57`
and RBF `c892ae70...9e16` verified on the card by SHA-256; 16 persist variables
present. Catalog indexes backed up under
`work/diagnostics/sdram-cpu-probe-a092/pocket-cache-backup-2026-09-20/System/`
and cleared. Pocket evidence pending; Quit before removing the card.

**Outcome, remaining risk, and next gate:** Do not change RTL or promote the CPU
window from this result alone.

**Pocket outcome:** **Pocket | host** — screenshot `20260919_233156.png` matches the
decoded `interact_persist.json` (copies in `pocket-result/`; the screen's `F`
value `0F78` is the top-bit mask). Words: w1 `12345678`, w2 `12345678`, w3
`A5C33C5A`, w4 `A5C33C5A`, w5 `A5C33C5A`, w6 `A5C33C5A`, w7 `0BADF00D`, w8
`FFFFFFFF`, w9 `FFFFFFFF`, w10 `FFFFFFFF`, w11 `FFFFFFFF`, w12 `00000000`,
w13 `0` (no mailbox command failed), w14 `12345678`. Predictions: P1 confirmed;
P3 confirmed; **P2 refuted** (w6 should be `0BADF00D`, w7 `0DEFACED`).
**Findings:** the SDRAM and the write path are fine: CPU writes landed (w3, w9)
and the zero write landed (w12 `0`), all checked through the mailbox. CPU reads
are correct when a mailbox operation precedes them (w2, w4, w8, w10, w14) but
wrong when they follow another CPU beat back-to-back: w6 (B+4) returned B's
data, w7 (B-4) returned B+4's data (`0BADF00D`, the pre-seeded value), and w11,
a load right after a zero store to D, returned the old `FFFFFFFF` although
w12 proves the zero had landed. Every wrong read equals the data the previous
beat returned or fetched: a one-beat lag in the return, not corruption.

### A-093 — adapter starts each CPU beat twice (root cause of issue 018)

**Date:** 2026-09-20

**Decision/change:** `sim/tb_tau_sdram_wb_return_regression.v` (new,
`make test-rtl-sdram-wb-return`, part of `make test-rtl`) drives the real
adapter, owner mux, CDC bridge and arbiter with a controller-contract memory
model, an `mp3_soc`-style registered ACK (`dACK <= wb_ack`), and a master that
either holds STB into the next beat (gap 0) or idles (gap 3). It asserts data
and that each Wishbone beat causes exactly one bridge request. **It fails on
the pre-fix RTL and reproduces the Pocket symptoms**: back-to-back load B
returns A's data, load C returns B's, "load after zero store" returns the old
`FFFFFFFF`, and 18 beats produced 19 bridge requests.
**Root cause:** `tau_sdram_wb_adapter` (A-060) left `S_RELEASE` for `S_IDLE`
after one cycle. `mp3_soc` registers the ACK again before the CPU sees it, so
at that moment the CPU still presents the just-completed beat. `S_IDLE` accepted
it a second time and issued a duplicate bridge request. The next beat had to wait
for the duplicate; the duplicate's completion (carrying the previous beat's
data, or the zero of a write) was taken as that next beat's ACK, and the chain
repeats. This explains every earlier observation: A-074's bridge recorder saw
`FFFFFFFF` on a real read that the CPU never received; A-076/077/079 saw the
stale zero at ACK; the matrix returned each store's neighbour's data
(181/183 failures); mailbox operations in between drain the duplicate, so those
reads were right; A-061's single CPU read of a mailbox-written word passed.
**Fix:** `S_RELEASE2` added, so the adapter re-enters `S_IDLE` two cycles after
its ACK (`src/fpga/core/tau_sdram_wb_adapter.sv`). The regression passes at gap 0
and 3 with one bridge request per beat; `make test-rtl` (all 18 suites) and
`make test-host` (92 unique audit IDs) pass.
**Alternatives:** gate the adapter's STB with `~dACK` in `mp3_soc` (equivalent,
but spreads the fix across two files); waiting for STB to drop (rejected, A-060
deadlock). **Hot/cold impact:** only the opt-in Phase-2 window path
(`TAU_PHASE2_WINDOW`); the macro-off player is unchanged. **Timing:** one extra
FSM state; **Quartus: pending**, and this is a functional RTL change, so it needs
a fresh fit and Pocket run. **Evidence:** **simulation | Pocket (A-092)**.
**Reversal:** issue 018's "owner-mux latch / bridge handoff" suspicion (A-079)
was wrong; the mux and bridge are correct and the defect was the adapter's
re-accept of a completed beat.
**Quartus launch (host evidence only):** at 2026-09-20 00:17:54 WEST a fresh
ext4 snapshot `/home/taualpha/tau-local/phase2-adapter-fix-a093-20260920` was
staged from the shared workspace (excluding `toolchain`, `work`, `.git`,
`UniClaudeProxy`, the host `.venv-cptr`, and Quartus db/output dirs). It differs
from the shared source only by the same two macro lines as A-080
(`TAU_PHASE2_WINDOW=1`, `TAU_PHASE2_MUX_PROBE=1`) so the rev-23 flush wiring, the
A-079 mux probe and the existing A-091/A-092 firmware remain compatible. `make
check-fpga` passed in the snapshot and `make fpga` launched (`quartus-a093.log`,
PID 28654; expect about 45 minutes). No fit, timing, artifact or Pocket result
exists yet. A stale idle A-067 `make fpga` session from 2026-09-17 is still in
the process table and was left untouched.

**Quartus result:** **Quartus** — the `phase2-adapter-fix-a093-20260920` flow
finished **Successful** at 2026-09-20 00:58:09 WEST in 44m03s (synthesis 5m32s,
fitter 33m06s, assembler 1m06s, timing 3m44s), 0 errors. Resources: 6,106 /
18,480 ALMs (33%), 8,060 registers, 300 / 308 RAM blocks (97%), 11 / 66 DSP
blocks, 1 / 4 PLLs. Multicorner worst-case setup +1.106 ns, hold +0.111 ns, TNS
0 (limiting hold is the `general[0]` PLL output; the design still warns about
unconstrained paths, as in earlier builds). Raw RBF SHA-256
`e16ffe9dff4dfc8efda868c995bb7ecc371d3537af4b4d8091bec926efd84d9d` (copied to
`work/diagnostics/sdram-cpu-probe-a093/fpga/ap_core.rbf`, hash re-verified on the
host).
**Packaging:** `--probe-a093` (new `EXPECTED_A093_PROBE_RBF_SHA256`,
`tau_sdram_prb93`, `alfatreze.TAU_SDRAM_PRB93`) pairs the fixed RBF with the
unchanged A-091 matrix ROM (`63c89cf6...1534`, 7,752 bytes), so the run prints
the usual screen and publishes the 183-check record via interact.json.
Bit-reversed Pocket RBF SHA-256
`c765cabbc15308f198fab7449efbb6298f57d6af9d68a60cde049389f79748b3`; bundle at
`work/diagnostics/sdram-cpu-probe-a093/pocket`.
**Installation evidence:** **host** — A-093 replaced only A-092 on the mounted
card (A-092 core, platform, image, assets and settings removed; its persist file
kept under `pocket-cache-backup-2026-09-20/a092-removed/`). ROM `63c89cf6...1534`
and bit-reversed RBF `c765cabb...48b3` verified on the card by SHA-256; 16
persist variables present. Catalog indexes backed up under
`work/diagnostics/sdram-cpu-probe-a093/pocket-cache-backup-2026-09-20/System/`
and cleared. Pocket evidence pending; Quit before removing the card.
**Predictions (before the run):** P1 the version interlock still passes (rev 23);
P2 the matrix reports 183 checks with 0 failures (PASS) and the persisted record
decodes to `failures 0`; P3 if failures remain, they are no longer the
lag pattern (actual = previous beat's data) and the A-092 discriminator ROM
(same RBF) is the follow-up.
**Pocket outcome:** **Pocket | host** — **PASS.** Screenshot `20260920_004648.png`
(copy in `pocket-result/`) shows `PASS`, 183 readback checks, 0 failures,
`W0 544C4F47`, `W7 00000000`, `WC 18511A0D`, `WF 00000000`. The persisted
`interact_persist.json` (written at 00:46 after Quit) decodes with a valid
checksum (`0x18511A0D`, matching `WC`): stage 4, run 1, core version
`0x4D503317`, `failures 0`, `timed_out false`, `status0 0x53445041` (was
`0x53444641` in every failing run). Predictions: P1 (interlock passes) and P2
(183 checks, 0 failures) confirmed; P3 not needed. The previous 181/183
failures at `A0200000` are gone with only the adapter's second release cycle
changed, which confirms A-093's root cause on hardware.
**Scope of the claim:** this is Pocket evidence for the limited uncached
CPU-window data path only (word, byte and halfword lanes over physical 2-3 MiB
under the diagnostic's own traffic). It does not authorise cached access,
execution from SDRAM, linker placement, or cold-data migration, and it says
nothing about sustained audio/scanout contention. The A-092 discriminator ROM
was not needed.
**Reversal ledger:** the A-079 conclusion that the bridge-to-mux handoff or the
mux latch was the live boundary was wrong; the defect was the adapter's
double-issue of each completed beat.
**Repeat runs:** two further cold-boot runs of the same A-093 package also
passed, per the user on 2026-09-20. **Partly verified:** before A-094 replaced
A-093 the card's persist file (mtime 00:51, after the 00:46 screenshot) was
decoded: stage 4, 183 checks, `failures 0`, `timed_out false`, checksum
`0x1851CA53` (different from the first run's `0x18511A0D`, so a later run).
Copy: `pocket-result/interact_persist-later-run-00-51.json`. No screenshots exist
for the repeat runs, and the file holds only the last run, so at most one repeat
is independently confirmed.
**Next gate:** decide the promotion gates (stress with scanout and audio contention, cached window design)
before any real use of SDRAM for player data. Do not promote
the CPU window or migrate cold data on simulation evidence alone.

### A-094 — cost of the uncached SDRAM window (Pocket result: about 50 cycles per access)

**Date:** 2026-09-20

**Decision/change:** With the CPU window now passing (A-093), start the promotion
work with the cheapest question: what does an uncached SDRAM access cost?
Firmware-only `TAU_LATENCY_PROBE` (`fw/build.sh sdram-cpu-latency`,
`--probe-a094`) measures, with the 60 MHz core cycle counter and scanout running,
256-op loops over the 2-3 MiB region: an empty loop (overhead), sequential
write and read, 4 KiB-stride write and read, write-then-read of one word,
read-modify-write, per-op min/max read and max write cycles (refresh and scanout
stalls), a 256 KiB-stride hop, and a data-mismatch count. Raw words go through
the interact.json channel; the screen shows cycles per operation. Same A-093
RBF (`e16ffe9d...4d9d`); ROM SHA-256
`797f9ac4cfdd590417a866deca82c446ed2fa695adc9c26f4c9703db3e6225ce` (6,156 bytes).
**Why:** the candidate cold buffers (`pl_text` 12 KiB, `art_acc` 11 KiB, maps,
about 25.8 KiB) are only worth moving if the cost per access is tolerable; this
also gives the CPU cost of the future cached-window refill.
**Predictions (before the run):** each uncached access costs tens of cycles
(two 16-bit controller operations plus two CDC crossings), reads slower than
writes; the max per-op cost exceeds the min by a refresh/scanout stall; the
mismatch count is 0.
**Status:** built and packaged at `work/diagnostics/sdram-cpu-probe-a094/pocket`.
**Installation evidence:** **host** — A-094 replaced only A-093 on the mounted card
(A-093 core, platform, image, assets, save and settings removed; its persist file
is in `pocket-cache-backup-2026-09-20/a093-removed/`). ROM `797f9ac4...25ce` and
bit-reversed RBF `c765cabb...48b3` (the same fixed RBF as A-093) verified on the
card by SHA-256; 16 persist variables present; catalog indexes backed up under
`work/diagnostics/sdram-cpu-probe-a094/pocket-cache-backup-2026-09-20/System/` and
cleared. Pocket result pending; Quit before removing the card.

**Pocket outcome:** **Pocket | host** — screenshot `20260920_010008.png` and the
decoded `interact_persist.json` (copies in `pocket-result/`) agree. N = 256 ops
per test, 60 MHz core cycle counter, scanout running, region 2-3 MiB. Cycles per
op (total / 256), with the empty-loop overhead of 4.1 cycles per iteration:

| Test | Cycles/op | Net of loop |
|---|---:|---:|
| Sequential write | 53.6 | 49.5 |
| Sequential read | 51.8 | 47.7 |
| 4 KiB-stride write | 31.9 | 27.8 |
| 4 KiB-stride read | 53.0 | 48.9 |
| Write then read, same word (per pair) | 83.9 | 79.8 |
| Read-modify-write | 78.7 | 74.6 |
| 256 KiB-stride hops (4 writes + 4 reads, per op) | 42.4 | (not netted) |

Per-op read min 43 cycles, max 360; per-op write max 324; data mismatches 0 (`BAD
0`). At 60 MHz an uncached access is about 0.8 us (min 0.7 us), and the worst
single access about 6 us (stall from refresh/scanout arbitration).
**Predictions:** tens of cycles per access confirmed (about 50); worst case above
the best confirmed (360 vs 43); 0 mismatches confirmed; **"reads slower than
writes" refuted** (sequential read 51.8 vs write 53.6, effectively equal).
**Unexplained:** 4 KiB-stride writes cost 31.9, well below sequential writes
(53.6), and the read-modify-write's implied write half (about 27) matches it. The
cause is not established; a repeat run, or a per-op write histogram, would show
whether it is systematic or an arbitration/scanout phase effect. Not used for any
conclusion.
**Implications (estimates, not measurements of Tau's code):** the uncached window
is roughly 10-20x the cost of a BRAM access, and it applies per access, so a byte
load costs the same as a word load. A structure touched a few hundred times per
UI frame (the playlist name strings behind `pl_text`) would cost well under a
millisecond per frame. A buffer walked per pixel during artwork decode
(`art_acc`) needs its access count measured before any move; the per-access cost
also competes with audio refill for CPU time. A cached window (line fills)
would amortise this, but needs its own beat-decomposing adapter.
**Next gate:** count accesses per artwork load and per UI frame for the candidate
buffers (firmware counter or static analysis), then decide uncached-first versus
building the cached window. Margin, contention, and product-build gates in
`docs/CURRENT_STATUS.md` are unchanged.
**Skill registration:** the `analogue-pocket-dev` skill could not be registered
into this session (`Skill` reports it unknown; `ListSkills` sees only claude.ai
skills). Project-local skills are discovered at session start, so a fresh session
opened in this repository is needed for it to appear in the skill list. Its files
were read directly and used meanwhile.

**Skill/KB update (same date):** the project-local `analogue-pocket-dev` skill
(`.claude/skills/`) was consulted. New hardware-validated entries were added to
its knowledge base from our own audit ids: KB-022 (0188 unanswered, A-090),
KB-023 (datatable base 0xF8002000, A-088), KB-024 (adapter must not re-accept a
completed beat, A-093), KB-025 (interact.json persist record, A-091/A-093);
notes were appended to KB-001, KB-004, KB-007, KB-008 and KB-021 and to open
questions OQ-1, OQ-2 and OQ-6 (`kb.py validate` passes, 25 entries). Two skill
points change our plan: (1) KB-004/KB-001 suggest the zero save file after Quit
was caused by APF reading the slot back from the core at the slot `address`,
which Tau's slot 5 did not have or serve; untested, and unnecessary now that
interact.json works. (2) KB-011/KB-021: fit results vary about 1.2 ns by seed
and CL/phase margin is unmeasured, while A-093's hold slack is only +0.111 ns,
so any promotion RTL needs several seeds and a soak.

### A-095 — access counts for the candidate cold buffers (static analysis)

**Date:** 2026-09-20

**Decision/change:** No code or hardware change. Read `fw/art.inc`,
`fw/playlist.inc` and `fw/player.c` to count how often each candidate buffer
from the Phase 2 list is touched, and price those counts with the A-094
measurements (uncached window: about 48 cycles per read and 74.6 per
read-modify-write, net of loop, at 60 MHz). These are **code-review estimates**
(no compiler output or firmware counters were used; loads-per-compare may differ
by up to about 2x); BRAM cost is assumed 2-4 cycles per access, not measured.

| Buffer (bytes) | When touched | Accesses | Extra cost in the window |
|---|---|---:|---|
| `pl_text` (12,288) + `pl_off`/`pl_order` (1,024) | one playlist load: byte copy from `tagbuf`, parse, hash, name pass | up to about 75k | up to about 60 ms worst case; a small playlist (a few hundred bytes) is a few ms |
| same | one track change (`pl_open_name`, a name of tens of bytes) | a few hundred | well under 1 ms |
| same | one playlist-browser redraw (about 10 rows of names) | about 1k | about 1 ms |
| same | shuffle (256-entry Fisher-Yates) | about 1k | about 1 ms |
| `art_acc` (11,040) | full decode, 455 px cover (207,025 source pixels x 6 accumulator accesses, plus 1 `art_xmap` load) | about 1.45M | about 0.8-0.9 s (3 RMW at 74.6 + one read per pixel) |
| `art_acc` | reduce mode, 1494x1497 cover (about 35k block pixels) | about 0.25M | about 0.16 s |
| `art_acc` | row flush and slot clear (about 92 rows x 92 cells x 6, plus clear) | about 56k | about 45 ms |
| `art_xmap`/`art_yslot` (2,048) | read once per source pixel / row in the inner loop | per pixel | keep in BRAM: tiny and hottest |

**Findings:**
1. `pl_text` is filled by a CPU copy from `tagbuf`, not by APF DMA, so it can
   live behind the uncached alias. All its use is cold (load, parse, redraw).
   `pl_text` + `pl_off` + `pl_order` = 13,312 B (13 KiB) for a cost of tens of
   milliseconds once per playlist load. An uncached-first move is adequate; no
   cached window is needed.
2. `art_acc` would add about 0.8-0.9 s to a 455 px full decode and 0.16 s to a
   reduce-mode one. `player.c` records that the decode is already 2801 ms of a
   3731 ms track load, so this is roughly +30% on a load that happens once per
   album (a cover-signature cache skips repeats). It is not worth moving
   uncached as written.
3. The Phase 2 exit criterion is at least 24 KiB recovered
   (`SDRAM_MEMORY_ARCHITECTURE.md`). `pl_*` alone gives 13 KiB. Reaching 24 KiB
   needs `art_acc` (11,040 B), which needs either the cached window or a
   restructure that accumulates a run of source pixels per destination cell in
   registers so each cell is read-modified-written once per run (about 5x fewer
   window accesses at this cover size). Both change decode code and need their
   own measurement.
**Decision needed (user):** accept `pl_*` (13 KiB) as the first Phase 2 step and
defer `art_acc`, or plan the restructure/cached window to reach 24 KiB.
**Next gate:** the margin, contention and product-build gates in
`docs/CURRENT_STATUS.md` still apply before any move. A firmware access counter
on a player build would replace the estimates above with measured counts.

### A-096 — measured size of a minimal settings menu against the link gap

**Date:** 2026-09-20

**Decision/change:** No product change. Built a throwaway seven-row settings home
(Colour, Meter, EQ, Repeat, Shuffle, Resume, Screen blank) behind
`TAU_SETTINGS_PROTO`, drawn like the playlist overlay, editing the existing
controls with the same side effects as their current key bindings, and linked it
into the player. This is the "isolated size report" step of
`docs/SETTINGS_RUNTIME_BUDGET.md`. The prototype, its hook patch and a
relaxed-assert link script are kept in `work/diagnostics/settings-size-proto/`;
`fw/player.c` and `fw/build.sh` were restored, the product ROM in `dist/` is
unchanged (an accidental rebuild of it was reverted with `git checkout`), and the
prototype was built to a scratch output directory.

**Measured (Icarus/toolchain `size`, same flags as the player build):**

| | `.text`+`.rodata` | `.data` | `.bss` | Image + BSS | Heap gap |
|---|---:|---:|---:|---:|---:|
| Baseline player | 151,324 | 764 | 61,578 | 213,666 | 3,408 B |
| With the 7-row menu | 154,312 | 764 | 61,586 | 216,662 | 416 B |
| Delta | +2,988 | 0 | +8 | **+2,996** | -2,992 |

The linker requires a heap gap of at least 1,024 B, so the baseline has only
2,384 B of slack and the prototype misses by 608 B. The unmodified link of the
prototype fails with the exact earlier message, "no room left for even a token
heap", which reproduces and explains the rejected settings prototype.
**Findings:**
1. Almost all the cost is code and strings (+2,988 B), not data (+8 B). SDRAM data
   cannot pay for it directly; only freeing on-chip RAM elsewhere can, because
   image, BSS, heap and stack share the 256 KiB (see A-095).
2. Moving `pl_text`+`pl_off`+`pl_order` (13,312 B, A-095) would leave a heap gap of
   3,408 + 13,312 - 2,996 = **13,724 B** with this menu in place, about 12.7 KiB
   above the 1 KiB minimum and roughly 4x the prototype's size.
3. So the minimal settings home does **not** need the 24 KiB Phase 2 target or the
   `art_acc` move. 13 KiB is enough for it with room to spare.
**Caveats:** the prototype is one flat list with no groups, previews, confirmation
or Advanced section, no new persisted words, and no Figma layout; the approved
design in `SETTINGS_ARCHITECTURE.md` will be larger. A plausible 2-4x range
(about 6-12 KiB) is a guess, not a measurement. Hardware behaviour of the menu
was not tested (compile and link only); the block-RAM fit is unaffected.
**Decision/next gate:** treat the 24 KiB criterion as unsupported by any measured
need and revisit it once the real settings design has a size report. The first
Phase 2 move can be the playlist buffers alone, subject to the margin,
contention and product-build gates in `docs/CURRENT_STATUS.md`.

### A-097 — long soak of the fixed CPU window (Pocket PASS: 264M checks)

**Date:** 2026-09-20

**Decision/change:** First of the promotion gates (margin): a firmware-only soak on
the A-093 RBF. `TAU_SOAK_PROBE` (`fw/build.sh sdram-cpu-soak`, `--probe-a097`)
repeats forever, with scanout running: the 183-check matrix, then 128 random
write-then-read pairs over the whole 2-3 MiB window (xorshift data and
addresses), then a 64-word block write followed by a verify. Cumulative counters
(passes, checks, failures split matrix/random, matrix timeouts, elapsed seconds,
min/max pass time, and the first failure's pass number, address, expected and
actual) are published through interact.json every 8 passes and on any failure.
The screen shows the same counters. Decode with
`tools/decode_tau_diag_log.py --interact --soak` (checked on a synthetic record).
Same RBF `e16ffe9d...4d9d`; ROM SHA-256
`3a4cfbbdee84d34f91bd6703a8aba0b788068f12833fa2ffe33492781c600f0a` (10,400 bytes,
5.8% of RAM); bundle at `work/diagnostics/sdram-cpu-probe-a097/pocket`.
**Why:** A-093 passed the 183-check matrix on three cold boots, but a few short
runs do not show timing margin (KB-011/KB-021: seed variation, CL2 at 100 MHz,
+0.111 ns worst hold slack). A soak with varied addresses and data across many
refresh and scanout phases is the cheap first check. It does not include audio
playback (the separate contention gate).
**Predictions (before the run):** over at least 30 minutes, 0 failures in both
matrix and random parts and no matrix timeouts; pass time steady (about tens of
ms) with an occasional longer pass from stalls; if any failures appear they are
sporadic, not the lag pattern, and would point to margin rather than logic.
**Run plan:** install, launch, leave it running for 30 to 60 minutes with the
Pocket on power, photograph the screen near the start and just before quitting,
Quit to the menu, then remount the card. A hard power-off or pulling the card
without Quit loses the record.
**Status:** built and packaged.
**Installation evidence:** **host** — A-097 replaced only A-094 on the mounted card
(A-094 core, platform, image, assets, save and settings removed; its persist file
is in `pocket-cache-backup-2026-09-20/a094-removed/`). ROM `3a4cfbbd...0f0a` and
bit-reversed RBF `c765cabb...48b3` (the A-093 fixed RBF) verified on the card by
SHA-256; 16 persist variables present; catalog indexes backed up under
`work/diagnostics/sdram-cpu-probe-a097/pocket-cache-backup-2026-09-20/System/` and
cleared. Pocket result pending; Quit before removing the card.

**Pocket outcome:** **Pocket | host** — **PASS.** The persisted record (after Quit,
copy in `pocket-result/`) decodes to: 705,160 passes, 264,435,000 checks, elapsed
0:30:15, **0 failures** (matrix 0, random 0), 0 matrix timeouts, no first-failure
data, per-pass time 0.852 ms minimum and 3.935 ms maximum. Screenshots
`20260920_011745.png` (start: 824 passes, 309,000 checks, 0:00:02) and
`20260920_014753.png` (just before Quit: 703,016 passes, 263,631,000 checks,
0:30:09, `FAILS 0`) agree with the record (checks = 375 per pass: 183 matrix +
128 random pairs + 64 block words). Predictions: 0 failures, no timeouts, steady
pass time with occasional longer passes: all confirmed.
**What this supports:** with scanout running, about 2.6 x 10^8 read-back checks
over 30 minutes at room temperature produced no error, which bounds the failure
rate below about 1.1 x 10^-8 per check at roughly 95% confidence (rule of
three). It supports the uncached window at CL2/100 MHz for this traffic on this
unit for that long.
**What it does not cover:** only the 2-3 MiB region (the window spans about 63
MiB); no audio or other engine traffic beyond scanout; one temperature and one
unit; the A-093 build's thin hold slack (+0.111 ns) is unchanged; a fixed traffic
mix. Cosmetic: the soak screen shows a clipped stale phase label in the progress
strip (the matrix's own progress text), which does not affect the record.
**Next gate:** full-window address-line coverage, a 1 MiB CRC under concurrent
framebuffer drawing, and access-time counters (A-100), then the product RTL and
contention gates.

### A-100 — whole-window coverage, CRC under drawing, access counters (Pocket PASS)

**Date:** 2026-09-20

**Decision/change:** Firmware-only `TAU_FULL_PROBE` (`fw/build.sh sdram-cpu-full`,
`--probe-a100`, decoder `--interact --full`) on the A-093 RBF, closing coverage
gaps left by the matrix and the A-097 soak, which only touch physical 2-3 MiB:
1. **Address lines over the whole window.** A unique per-address word is written at
   0x100000 and at 0x100000 | 2^k for every k = 2..25 except 20, plus the pair
   0x200000/0x300000 (which differs only in bit 20), 26 addresses in all, then read
   back in reverse order, then the complements are written and checked (52 checks).
   A host check confirms the list contains a single-bit pair for every line 2..25
   and stays inside 1-64 MiB. This covers column, row and both bank bits
   (byte bits 24-25).
2. **1 MiB CRC under concurrent drawing.** Physical 8-9 MiB, three rounds: a
   pseudo-random fill with a CRC32 per 64 KiB block, then a read-back CRC32 per
   block; a framebuffer RECT is issued every 64 words in both passes so the draw
   engine runs against the CPU window.
3. **Counters.** Per-access max read and write cycles during the CRC passes, the
   draw-engine stall counter (`R_FB_STALL`), block-0 CRCs, and the number of window
   accesses.
ROM SHA-256 `6a7567d1aa9912366aa8a3d20292ce0d09b86ee8bde7688ed16354124f85044f`
(7,852 bytes, 4.4% of RAM); RBF `e16ffe9d...4d9d`; bundle at
`work/diagnostics/sdram-cpu-probe-a100/pocket`. Platform id `tau_sdram_p100` (the Pocket limit is 15 characters). Renumbered from
A-098 because a parallel PSRAM session claimed A-098 and A-099 in this trail.
**Installation evidence:** **host** — A-100 replaced only A-097 on the mounted card
(A-097 core, platform, image, assets, save and settings removed; its persist file
is in `pocket-cache-backup-2026-09-20/a097-removed/`). ROM `6a7567d1...5044f` and
bit-reversed RBF `c765cabb...48b3` (the A-093 fixed RBF) verified on the card by
SHA-256; 16 persist variables present; catalog indexes backed up under
`work/diagnostics/sdram-cpu-probe-a100/pocket-cache-backup-2026-09-20/System/` and
cleared. Pocket result pending; Quit before removing the card.

**Pocket outcome:** **Pocket | host** — **PASS.** Screenshot `20260920_015608.png` and
the decoded persisted record (copies in `pocket-result/`, with the mid-run
screenshot `20260920_015601.png` showing the concurrent drawing) agree: address
lines 52 checks, 0 failures; CRC 3 rounds, 0 block mismatches, block-0 CRC written
and read both `0xD7F900C4`; maximum access time read 360 cycles (6 us), write
350; draw-engine stall count 0; 1,572 thousand window accesses (3 rounds x 2 x
262,144 words). Predictions: all confirmed (worst access far below 1,000).
**Observations:** the worst read equals A-094's idle worst case (360), so drawing at
this load did not raise the bound; a stall count of 0 shows the draw command queue
never filled, so this is a light concurrent load (one 60x40 RECT per 64 words), not
a heavy contention test.
**Still not covered:** the top word of the window (0xA3FFFFFC; the highest tested
address is 33 MiB), out-of-window accesses on hardware (below 1 MiB and above
64 MiB; decode is simulated only), audio playback and other engine traffic,
temperature, the product RTL, and the cached alias. Combined with A-097 (30 min,
264M checks, 0 failures) the uncached window is now supported across its address
lines and for sustained traffic on this unit.
**Next gate:** probe-free product RTL built on several seeds (timing margin,
block-RAM count), then the contention test with real playback.
**Predictions (before the run):** 52 address-line checks with 0 failures; 3 CRC
rounds with 0 block mismatches and block-0 write and read CRCs equal; maximum
read/write access time stays below about 1,000 cycles (A-094 idle worst case was
360 and 324) even with the engine drawing; a nonzero draw-engine stall count is
possible and is informational. Any address-line failure would name the failing
location and indicate a decode or wiring fault for that line; a CRC mismatch would
localise to a 64 KiB block.
**Not covered even by this:** audio playback, other engine traffic, temperature,
the product RTL, and the cached alias.

### A-101 — product-candidate RTL with the CPU window, multi-seed fits (complete; seed 4 selected)

**Date:** 2026-09-20

**Decision/change:** Gate 4 of the promotion list: build the CPU-window RTL
**without any probe macro** (only `TAU_PHASE2_WINDOW=1`, the fixed adapter of A-093
and the rev-23 wiring), on several placer seeds, to measure timing margin and
resources before any product use. KB-011 (skill knowledge base) says fit results
vary about 1.2 ns by seed and to compare fast-corner hold, and the A-093 probe
build had only +0.111 ns worst hold. Player firmware is unchanged
(`EXPECT_VERSION 0x4D503317` already matches); no firmware uses the window yet.
**Method:** fresh ext4 snapshots on the VM
`phase2-product-a101-s1-20260920` and `phase2-product-a101-s2-20260920`, staged from
the shared workspace at the committed tree (excluding `toolchain`, `work`, `.git`,
`.claude`, `docs/vendor`, `UniClaudeProxy`, the host venv and Quartus output), each
with `TAU_PHASE2_WINDOW=1` and `SEED n` appended to the qsf. `make check-fpga`
passed in both. Both `make fpga` runs launched at 2026-09-20 02:36 WEST in
parallel (4 vCPU; logs `quartus-a101-s1.log`, `quartus-a101-s2.log`). Seeds 3 and 4
follow after these finish. The stale idle A-067 session on the VM is untouched.
**Acceptance (set before the results):** 0 errors; timing TNS 0 with positive
setup and hold in all corners; RAM blocks not above 300 / 308; report ALM, register
and DSP deltas against the A-093 probe build (6,106 ALMs, 8,060 registers, 11 DSP);
prefer the seed with the best fast-corner hold slack, and require it to beat the
A-093 build's +0.111 ns or explain why not.
**Interim result (seeds 1 and 2, both flows Successful, 0 errors):**

| | Seed 1 | Seed 2 | A-093 probe build |
|---|---:|---:|---:|
| Flow time | 53m41s (finished 03:26:47) | 49m48s (03:22:24) | 44m03s (single build) |
| ALMs | 6,124 (33%) | 6,108 (33%) | 6,106 |
| Registers | 7,999 | 8,042 | 8,060 |
| RAM blocks / DSP | 300/308, 11 | 300/308, 11 | 300/308, 11 |
| Worst setup (slow 85C, 100 MHz `general[3]`) | +0.443 ns | +0.524 ns | +1.106 ns |
| Worst hold (fast 0C, `general[0]`) | **+0.115 ns** | +0.105 ns | +0.111 ns |
| TNS | 0 | 0 | 0 |
| Raw RBF SHA-256 | `89d7fd63...4256` | `687c78c6...55eb` | `e16ffe9d...4d9d` |

Both stay at 300/308 RAM blocks. Against the criteria: only seed 1's worst hold
(+0.115 ns) beats A-093's +0.111 ns, by 4 ps, which is not a meaningful
difference; seed 2 is 6 ps below it. The worst hold is about +0.1 ns in every build
so far (baseline 0.025), i.e. seed choice is not moving it much, so hold is a
property of these clock crossings, not of the window. **Setup margin is lower than
the probe build** (+0.44/+0.52 ns vs +1.11 ns) on the 100 MHz SDRAM-side clock,
still positive with TNS 0. Copies of both RBFs: `work/diagnostics/sdram-product-a101/s1|s2`.
Seeds 3 and 4 launched at 03:36 WEST to complete the comparison; pick after those
finish. This is **Quartus** evidence only.
**Final result (all four seeds Successful, 0 errors, TNS 0, 300/308 RAM blocks, 11 DSP):**

| Seed | ALMs | Registers | Worst setup | Worst hold | Flow time | Raw RBF SHA-256 |
|---|---:|---:|---:|---:|---|---|
| 1 | 6,124 | 7,999 | +0.443 ns | +0.115 ns | 53m41s | `89d7fd63...4256` |
| 2 | 6,108 | 8,042 | **+0.524 ns** | +0.105 ns | 49m48s | `687c78c6...55eb` |
| 3 | 6,099 | 8,043 | +0.390 ns | +0.116 ns | 54m37s | `fcfe81ef...f01b` |
| 4 | 6,106 | 8,053 | +0.464 ns | **+0.123 ns** | 50m52s | `ed34a6bc...90eb` |
| A-093 probe build | 6,106 | 8,060 | +1.106 ns | +0.111 ns | 44m03s | `e16ffe9d...4d9d` |

Spread across seeds: setup 0.39-0.52 ns, hold 0.105-0.123 ns (about 18 ps), i.e. hold
barely moves with the seed (KB-011's roughly 1.2 ns variation did not appear here).
**Selection (rule set before the results: best fast-corner hold, must beat A-093's
+0.111 ns):** **seed 4** (hold +0.123 ns, 12 ps above A-093; setup +0.464 ns, second best).
Seed 1 (+0.115) also qualifies but by 4 ps; seed 2 has the best setup but its hold is below
the A-093 figure. The differences are within placer noise, so this is a tie-break, not a
finding that seed 4 is materially safer. All four RBFs are in
`work/diagnostics/sdram-product-a101/s1..s4/ap_core.rbf`. Note that every seed has less setup
margin (0.39-0.52 ns) than the probe build (1.11 ns); it is positive on the 100 MHz
SDRAM-side clock with TNS 0. This is **Quartus** evidence only; the gate run on Pocket
(A-102) currently uses seed 1 for bring-up and should be repeated on seed 4.
**Status:** complete for the build gate; product-candidate RBF selected (seed 4); Pocket
gate pending (A-102).

### A-102 — CPU-window contention stress with real playback (realistic-load pass on Pocket)

**Date:** 2026-09-20

**Decision/change:** Gate 3 of the promotion list. The existing Phase 1 harness
(`TAU_SDRAM_STRESS`/`TAU_STRESS_HUD`, `docs/SDRAM_CONTENTION_DIAGNOSTIC.md`) drives
an MMIO-mailbox pump next to real playback. New `TAU_SDRAM_STRESS_WINDOW=1`
(`fw/build.sh player-stress-window`) replaces the pump with CPU-window traffic:
each operation writes a pattern word through the uncached alias
(`0xA0000000 + byte offset`, physical 1-2 MiB, the same region and address-derived
pattern as Phase 1) and reads it back, checking every word; a CRC accumulates per
1 MiB pass. **Select+X** now cycles off, level 1 (about 16,000 ops/s, the Phase 1
rate), level 2 (about 64,000) and level 3 (about 128,000, roughly 20% of the CPU).
The HUD line gains `R<level> M<max window access cycles> U<audio underruns since
start> S<draw-engine stall cycles since start>`. Before the first window store the
pump runs a preflight (mailbox write, CPU-window read of the same word) and
refuses with the toast "NO SDRAM WINDOW" otherwise, because on a bitstream without
the window the alias decodes to MMIO.
**Link finding:** the Phase 1 stress ROM **no longer links on the current tree**:
`player-stress` leaves a 624-byte heap gap (1,024 required) and the window variant
224 bytes, since the player has grown. `fw/link.ld` now has
`PROVIDE(_min_heap = 1024)` and the stress targets pass
`-Wl,--defsym=_min_heap=128`. Justification: `malloc` is the fixed arena in
`alloc.c` (FLAC also uses it) and nothing links `printf`, so `_sbrk` is only a
fallback. Verified: the product player builds to the same SHA-256 (`b365dc2a...`)
with the old and the new link script. The committed `dist/` ROM differs from a fresh
build only because it predates rev 23. Size: stress-window ROM 155,200 bytes; text
154,432, bss 61,650.
**Packaging:** `tools/package_sdram_stress.py --window --rbf <raw> --rbf-sha256 <hash>`
(refuses an RBF whose hash differs) builds `alfatreze.TAU_SDRAM_WSTRESS` /
`tau_sdram_wst` from the normal TAU core JSON. For early bring-up it was packaged with
the A-093 RBF (`e16ffe9d...4d9d`; its top 8 scanlines carry the probe overlay, so
judge top-edge corruption only on the A-101 build). ROM SHA-256
`d11309cc6f2103267e86389d4b5ae5ffd9f07d34eec97fb32ae82f1b7bfe0663`; bit-reversed
RBF `c765cabb...48b3`; bundle `work/diagnostics/sdram-stress-window/pocket`.
**Protocol and acceptance (set before any run):** high-bitrate MP3 with artwork and a
playlist. (1) Per visualizer, a stress-off baseline, then level 1 for at least one
full pass (about 16 s). (2) On at least three heavy visualizers, levels 2 and 3 for at
least 5 minutes each, with seek, pause/resume, artwork loads and track/playlist changes.
(3) A cold-boot repeat. Pass: zero `SDRAM MISMATCH`/timeout, `U` (underruns) 0 at the
levels where CPU load is not the cause, no audible dropout, no tearing or corruption.
Record `M` and `S` at each level. **Confound to keep in mind:** at level 3 an underrun
could be CPU starvation (the pump costs about 20% of the CPU) rather than SDRAM
contention; `S`, `M`, and a stress-off run at similar CPU load are needed to separate
them. There is no BRAM-only control at the same CPU cost yet.
**Predictions:** levels 1-2 with 0 mismatches and 0 underruns; `M` similar to the idle
360-cycle bound (higher if the engine is busy), `S` growing but bounded; level 3 may show
underruns, which would be recorded, not hidden.
**Repackaged with the A-101 seed-1 product-candidate RBF (2026-09-20):** the bring-up
package now uses the probe-free build (raw SHA-256 `89d7fd63...4256`, verified before
packaging) instead of the A-093 probe RBF, so the top scanlines carry no diagnostic
overlay and edge corruption can be judged. Bit-reversed Pocket RBF SHA-256
`898210a6bbeea82fa9c91f012c52e89f37c98291cf20c740cd499040cf9f1b6b` (bit reversal
re-checked against the raw file); ROM unchanged, SHA-256
`d11309cc6f2103267e86389d4b5ae5ffd9f07d34eec97fb32ae82f1b7bfe0663`. Seed 1 is the
bring-up choice only; the final gate seed is decided after A-101 seeds 3 and 4.
Bundle: `work/diagnostics/sdram-stress-window/pocket` (core
`alfatreze.TAU_SDRAM_WSTRESS`, platform `tau_sdram_wst`); the test tracks go to
`Assets/tau_sdram_wst/common/` (playlist.m3u sits in `common/`).
**Installation evidence:** **host** — the window-stress core replaced the A-100 probe on
the mounted card (A-100 core, platform, image, assets and settings removed; its persist
file is in `work/diagnostics/sdram-stress-window/pocket-cache-backup-2026-09-20/a100-removed/`).
Installed `alfatreze.TAU_SDRAM_WSTRESS` / `tau_sdram_wst`; on-card SHA-256 matches
the ROM (`d11309cc...0663`) and the bit-reversed seed-1 RBF (`898210a6...1b6b`). The
five test tracks and `playlist.m3u` were copied to `Assets/tau_sdram_wst/common/`
(`TAU A-102 stress/` two files, `Controls/` three files; 67 MB) and byte-compared against
the staged tree with no differences. The older `alfatreze.TAU_SDRAM_STRESS` (Phase 1,
old RBF) and all other cores were left in place. Catalog indexes backed up under
`work/diagnostics/sdram-stress-window/pocket-cache-backup-2026-09-20/System/` and
cleared. Pocket result pending.
**First Pocket session (partial, user-reported):** a short test on the installed core:
no audible problem at any point; the user saw one instance of the underrun counter
incrementing (track, level and action at that moment not yet recorded); the `S` (draw
stall) field was never visible. One screenshot from the session
(`20260920_031057.png`, loading screen) shows the HUD strip at the screen bottom:
`ST BARS OFF L- --:-- R0 M0 U0 S0`, so the window core, the HUD and the counters ran. **Cause of
the invisible `S`:** while stress runs the line reached about 48 characters
(`ST BARS P1 61% 01:09 L2 00:31 R2 M360 U0 S...`) against a 360 px clip
(`UI_INNER_W`), which cut off the fields appended at the end. **Fix:** in window mode
the counters now come first and the line is shorter: `[FAIL n] U<underruns> M<worst
cycles> S<stall ms> R<level> [P<pass> <pct>%] [L<n> mm:ss]` (S is in milliseconds;
`R_FB_STALL` cycles / 60,000). ROM rebuilt, 154,800 bytes, SHA-256
`dc8eeeb4cbcc32cf5502521830e465d49213e7e65a2ea16a20207a442ce9c17a`; the bundle in
`work/diagnostics/sdram-stress-window/pocket` was repackaged with the same seed-1 RBF.
**Card update (host):** the ROM on the card was replaced with the new one (`Assets/tau_sdram_wst/common/tau.rom`, SHA-256 `dc8eeeb4...c17a`, byte-compared with the staged file); the previous ROM (`d11309cc...0663`) is kept in `pocket-cache-backup-2026-09-20/rom-replaced/`, and the catalog indexes were backed up there again and cleared. The RBF and test music are unchanged. The
single underrun is **not** yet classified: it needs the track, stress level and action
(load, cover decode, seek, track change) at the time, and whether it occurred with
stress off.
**Slip (recorded):** while rebuilding, an unintended run of the Phase 1 target
(`fw/build.sh player-stress`) overwrote the staged `work/diagnostics/sdram-stress/tau.rom`.
It was restored from the copy inside `work/diagnostics/sdram-stress/pocket/` (127,876
bytes, SHA-256 `1860455e...`, identical to the ROM on the card); I cannot prove the
pre-slip staged file was byte-identical to it. `work/` is untracked, and that Phase 1
core uses the older rev-22 RBF and is superseded by the window stress core.
**Second Pocket session (12 screenshots, copies in `work/diagnostics/sdram-stress-window/session1-screenshots/`, new-HUD ROM `dc8eeeb4...c17a`):**
four tracks, HUD `U<underruns since stress start> M<worst window access, cycles> S<draw stall ms> R<level> P<pass> <pct>% L<n> <pass time>`.

| Track | Level | Values seen |
|---|---|---|
| 1 (320/44.1, 1400 px cover) | R0, R1 P1 4%, R2 P1 72%, R3 P2 64% | U0 M0 -> U0 M362 -> U1 M366 -> U1 M366, L1 00:57 |
| 2 (320/48, 455 px cover) | R1 6%, R2 53%, R3 P2 25% | U0 M367 -> U1 M367 -> U1 M367, L1 01:06 |
| 3 (VBR 07, 292 kbps) | R1 14%, R2 70%, R3 P2 28% | U0 M365 -> U4 M365 -> U13 M365, L1 01:10 |
| 4 (128 kbps CBR control) | R1 9%, R3 P2 9% | U0 M362 -> U10 M364, L1 00:50 |

Consistent findings: **no SDRAM mismatch or timeout** anywhere; the worst window access
stayed at **362-367 cycles**, the same as the idle 360 of A-094/A-100 (so playback,
artwork loads and drawing did not raise it); **S stayed 0 ms** (the draw engine never
stalled); no audible problem (user). The window and the HUD work with the real player.
**Correction to A-102's own design:** the pass times (50-70 s for 262,144 words) show an
average of 3,700-5,200 ops/s at every level, not the intended 16k, 64k and 128k. The pump
did one operation per main-loop pass (about 4,400 passes/s), so levels 2 and 3 were
loop-limited to the level-1 rate (about 1% of the CPU). This session therefore
validates the window at roughly the Phase 1 traffic rate only, **not** heavy contention.
**Underruns, unclassified:** U rose with time on tracks 3 and 4 (up to 13 and 10) but only
to 1 on tracks 1 and 2. The user reports that taking a screenshot often stops the track
(a key press) and A is tapped to resume; 8 of the 12 screenshots show STOPPED. Every
stop/resume flushes the FIFO (`pcm_flush`, which also clears the sticky underrun flag),
and the first underrun of each new epoch is counted, so restart artefacts probably explain
the increments. It is not established: no stress-off baseline with the same actions exists.
**Fix (built, not yet on the card):** the pump now performs all operations that have
fallen due per pass (at most 32) to hold a true target rate of 16k, 40k and 72k ops/s at
levels 1-3 (at about 170 cycles per operation, roughly 4.5%, 11% and 20% of the CPU, against an
estimated 24% headroom; level 3 may therefore also show CPU starvation); the HUD shows
the achieved rate (`K<k ops/s>`), and underruns are split into `E` (early, within 1 s of a
flush: start, seek, resume) and `L` (late, steady playback). New HUD:
`E<n> L<n> M<cycles> S<ms> R<level> K<k ops/s> P<pass> <pct>%`. ROM 155,116 bytes, SHA-256
`591061308a79ed2b5f95561a4fca6ae5e43e116012cf59144451e4cbb461be71`; bundle repackaged
(seed-1 RBF). **Card update (host):** the ROM on the card was replaced with this build
(`Assets/tau_sdram_wst/common/tau.rom`, byte-compared with the staged file); the previous
ROM (`dc8eeeb4...c17a`) and the catalog indexes are backed up in
`pocket-cache-backup-2026-09-20/rom-replaced/`, and the indexes were cleared. RBF and test
music unchanged.
**Protocol addition:** take few screenshots (one per level per track), tap A after each, and
run the same actions with stress off (R0) for a baseline; late underruns (`L`) in steady
playback are the meaningful contention signal.
**Third Pocket session (12 screenshots, `work/diagnostics/sdram-stress-window/session2-screenshots/`;
card ROM `59106130...be71`, seed-1 RBF, tracks 1-3 at levels R0-R3, no audible issue heard):**

| Track | R0 | R1 | R2 | R3 |
|---|---|---|---|---|
| 1 (320/44.1, 1400 px) | E1 L0 M0 S1 | E0 L0 M362 S0 K4.0 P1 95% (STOPPED) | E0 **L1** M362 S0 K7.9 P4 87% (STOPPED) | E0 **L2** M362 S0 K13.0 P8 67% |
| 2 (320/48, 455 px) | E1 L2 (carried) M362 S0 | E0 L0 M362 S0 K3.2 P1 73% | E0 L0 M362 S0 K5.3 P3 8% | E0 L0 M362 S0 K8.2 P5 45% |
| 3 (VBR 07) | E1 L0 M362 S0 | E2 L0 M362 S0 K3.7 P1 67% | E2 L0 M362 S0 K7.1 P3 43% | E4 L0 M362 S0 K11.6 P6 41% |

Findings: no mismatch, timeout or FAIL; the worst window access stayed at 362 cycles at every
level (the idle bound); the draw engine did not stall (S 0). Tracks 2 and 3 had **no late
underruns at any level** (L0). Track 1 showed L1 at R2 and L2 at R3; both increments coincide
with screenshots showing STOPPED (a stop and A-resume, which reloads the track and its
1.4 MB cover), and the time-since-flush rule used here classifies a restart that follows a
long load as late, so they are **not attributed to SDRAM contention** (unproven either
way). The E counts are restarts and loops as expected.
**Rate shortfall:** achieved K was 3.2-4.0k, 5.3-7.9k and 8.2-13.0k ops/s at R1-R3 against
targets of 16k, 40k and 72k. The pump runs from `poll_input()`, which is mostly called from
the loop that waits for the audio FIFO to drain, so its call rate (not the pacing) limits the
load. At about 170 cycles per operation, 13k ops/s is roughly 4% of the CPU and about 2% of
SDRAM time: this session is a valid functional and light-contention pass, **not** the
heavy-contention gate.
**Fix (built, ROM `55384a5596eac32f189eaba073d31f25d6c0236c9e77202553fa7484cb9bc20a`, 155,116
bytes, bundle repackaged, not on the card):** level 1 stays paced at 16k ops/s; levels 2 and 3
are unpaced bursts of 8 and 32 operations on every pump call, consuming the idle wait time, with
K reporting what is achieved; underruns are now classified by decoded frames since the last
flush (early if fewer than 9 frames, about 0.2 s of audio), which is immune to long cover
decodes. Bursts are at most about 90 us, against a 43 ms FIFO.
**Seed 4 swap (host, 2026-09-20):** at the user's request the stress core was repackaged with
the A-101 **seed-4** RBF (raw `ed34a6bc...90eb`, hash checked before packaging, bit reversal
re-verified; bit-reversed `2e9aaf0e66cebcb71d4f83e59ea2cd8e91b4c6d9e52a0ed430df2c158aaa3cd7`)
and the new ROM `55384a55...bc20a`, and both files on the card
(`Cores/alfatreze.TAU_SDRAM_WSTRESS/bitstream.rbf_r`, `Assets/tau_sdram_wst/common/tau.rom`)
were replaced and byte-compared with the bundle. Backups of the seed-1 RBF (`898210a6...1b6b`),
the previous ROM (`59106130...be71`) and the catalog indexes are in
`pocket-cache-backup-2026-09-20/seed4-swap/`; the indexes were cleared. Test music unchanged.
Seed 1 results (sessions 2 and 3) remain valid as bring-up evidence for the window logic; the
gate runs from here use seed 4 with the burst ROM, so they are not directly comparable to
the seed-1 sessions.
**Fourth Pocket session: seed-4 RBF + burst ROM (14 screenshots, `session3-screenshots/`; card
verified `2e9aaf0e...3cd7` and `55384a55...bc20a`; no audible issue heard, per the user):**

| Track | R0 | R1 | R2 | R3 |
|---|---|---|---|---|
| 1 (320/44.1, 1400 px) | E1 L0 M0 S1 | E0 L0 M373 S0 K2.3 P1 95% | E0 L0 M373 S0 K14.9 P5 91% | E0 L0 M373 S0 K17.4 P12 18% |
| 2 (320/48, 455 px) | E1 L0 M373 S0 | E0 L0 M373 S0 K3.0 P1 35% | E0 L0 M373 S0 K10.6 P3 20% | E0 L0 M373 S0 K10.7 P7 78% |
| 3 (VBR 07) | E1 L0 M373 S0 | E0 L0 M371 S0 K3.7 P1 54% | E2 L0 M371 S0 K13.2 P2 77% | E2 L0 M372 S0 K16.2 P6 40% |
| 4 (128 kbps CBR) | E3 L0 M372 S0 | (not shown) | (not shown) | E0 L0 M366 S0 K22.6 P3 4% ("SDRAM PASS 2") |

Findings: **zero late (steady-playback) underruns (L0) on all four tracks at every level**;
the few early underruns (E1-E3) are track loads and restarts; **no mismatch, timeout or FAIL**;
the draw engine never stalled (S0); the worst window access was 366-373 cycles (about 6.2
us; 11 cycles above the 362 seen with the seed-1 RBF, an inconsequential difference that
cannot be attributed to seed or ROM from this data). Achieved rates were 2.3-3.7k ops/s at
R1, 10.6-14.9k at R2 and 10.7-22.6k at R3 (maximum about 45,000 window accesses per second,
roughly 6% of the CPU and 3-4% of SDRAM time): the pump still only runs in the CPU's idle
time, so K is limited by the CPU, and this is a **realistic-load** result, not a saturation
test. The 22.6k point is the 128 kbps control, where the decoder leaves the most idle time.
**Assessment against A-102's acceptance (set before the runs):** met for tracks 1-4 at the
achieved rates: zero mismatches or timeouts, `L` 0 where CPU load is not the cause, no
audible dropout, no visible tearing reported. **Not yet covered:** a saturating burst level
(higher per-call bursts), FLAC (project decision: unverified), a cold-boot repeat, and an
explicit seek/pause/artwork-change sequence under stress (restarts under stress are visible as
E counts but were not logged as a deliberate test). The expected Phase 2 use (playlist
buffers: about 75k accesses per playlist load, hundreds per track change, about 1k per UI
redraw, per A-095) is far below the tested 10-22k accesses per second sustained.
**Test music (staged copy in work/):** `work/test-music/tau_sdram_wst/common/`
(README there). The search of local libraries found only the 13-track Nausicaa OST
(VBR, 44.1 kHz, three copies on the card, 707 px covers), one 128 kbps CBR track
(`merry-farm.mp3`, 136 s), and short game sound effects; nothing long at 320 kbps
CBR or 48 kHz. A web search found sources (free-stock-music.com states 320 kbps
MP3 under CC BY 4.0; Scott Buckley CC BY 4.0) but no verifiable file specs, so
nothing was downloaded. Instead LAME 4.0 was installed (Homebrew, with mpg123) and
two 320 kbps CBR files were encoded from four local OST tracks (44.1 kHz with a
1400 px cover taking the reduce path; 48 kHz with a 455 px cover taking the FULL
decode path), tagged with ID3v2.3, joined into a playlist with the VBR and 128 kbps
controls, and verified (header scan, full decode, `tools/library_check.py`).
**Status:** built and packaged; **not installed**, no hardware result. The final gate
run should use the A-101 product-candidate RBF once its seeds are chosen.

## Reversal ledger

This table points to conclusions that changed after evidence. Keep it visible
in review; it is not an embarrassment to delete.

| Topic | Earlier conclusion | Corrected conclusion / evidence |
|---|---|---|
| Sep 14 Quartus progress check | Stale console watchdog text and post-build VM activity suggested the active build might be stalled. | Authenticated inspection found a successful flow at 01:24, done marker at 01:27, valid `.sof`/`.rbf`, and passing timing. Guest boot/watchdog timestamps predate that run by over eleven hours. A-024; issue 006. |
| FLAC feasibility | Early estimates treated I/O as the likely binding budget. | Measured analysis found CPU/RAM constraints dominate; see the dated, retained corrections in [FLAC.md](FLAC.md). |
| Settings shell | A small runtime settings implementation appeared plausible. | It crossed protected memory layout boundaries; defer until SDRAM data capacity is proven. [SETTINGS_RUNTIME_BUDGET.md](SETTINGS_RUNTIME_BUDGET.md). |
| Video resolution | A configuration-only resolution change appeared plausible. | Current pipeline needs RTL/clock/bandwidth work; preserve 400×360 pending measurements. A-002 above. |
| Stress summary telemetry | Source review concluded Select + Start summary did not exist. | User invoked it on Pocket and resumed with A; summary exists in running binary but source provenance is unresolved. A-038/A-039; issue 011. |
| Stress HUD pass durations | First HUD readings were treated as potential timing evidence. | Pocket photos plus source review show `R_CYCLES` wraps every 71.58 s; raw L-times are modulo remnants, not durations. A-046; issue 013. |

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

### B-001 — PSRAM P0 contract and P1 controller simulation (sim only, no hardware)

**Date:** 2026-09-20

**Decision/change:** Started the PSRAM series (`docs/PSRAM_IMPLEMENTATION_PLAN.md`).
Added `docs/PSRAM_TIMING_CONTRACT.md` (values tagged by source; every datasheet
value OPEN), `tools/check_psram_idle.py` (static: all `cram0_/cram1_` outputs idle
with PSRAM macros off; negative-tested), `src/fpga/core/tau_psram_async.sv`
(async multiplexed-address controller: two ordered 16-bit ops per CPU word, one CE#
at a time, guard word refused, DQ driven only in address/write phases, held
response, power-up hold-off, slow-timing dials), `src/fpga/core/tau_psram_bus.sv`
(classic Wishbone, ACK/ERR only after the response register loads, two-cycle
release per KB-024), `sim/psram_chip_model.v` (strict four-die model that returns X
until t_acc and reports CE overlap, short pulses, missing setup, lane changes and
guard access), `sim/tb_tau_psram_async.v`, `sim/tb_tau_psram_wb_return_regression.v`.
Makefile: `test-rtl-psram-{idle,async,wb-return,mutation}`, all in `test-rtl`.
Nothing is wired into `core_top.v`, the CPU decode, firmware or any package.
**Why:** P0/P1 carry no hardware risk and need neither the Quartus VM nor the card.
The AS1C8M16PL datasheet could not be fetched (Alliance URLs return the site HTML),
so timings come from the agg23 reference controller (its comments call two values
guesses) and are held as parameters.
**Result (simulation):** controller test PASSED (0 failures, 0 model errors;
about 1,000 writes and 890 reads per chip; hold-off, walking 0/1, address lines,
byte lanes, random traffic over all four dies, boundaries, guard refused with no
chip access, reset mid-read and mid-write, slow dials +3/+3). Bus regression PASSED
(29 beats -> 29 controller requests at gap 0 and gap 3, registered ACK). Mutants
killed: `T_ACC=4` (early capture), `REL_CYC=1` (reproduces the A-092 duplicate
request, 29 beats -> 30 requests), `MUT_EARLY_ACK=1` (ACK before response loaded;
first version of the check was too weak and the mutant survived until an ACK-cycle
assertion was added). A separate `T_WP=2` mutant also fails (not in the Makefile).
`make test` exits 0. Measured cost at the conservative defaults: 22 clocks per
32-bit write, 20 per read (controller only), about 2x cheaper than the 48-50 clocks
of uncached SDRAM (A-094); simulated, not measured on hardware.
**Limits:** the model timings are the same provisional numbers the controller was
built to, so passing shows internal consistency, not datasheet compliance. No I/O
constraints, Quartus fit or board-level DQ turn-around evidence exist yet.
**Next:** owner supplies the datasheet; close P0 open rows; then P2 (mailbox,
I/O constraints, `interact.json` publishing, the packager probe flag for the next free audit id).

### B-002 — datasheet check of the PSRAM controller (sim only, found a real bug)

**Date:** 2026-09-20

**Decision/change:** The AS1C8M16PL-70BIN datasheet (`docs/vendor/DOC012312972.pdf`,
Rev 1.0 preliminary Aug 2018) was supplied and read (54 PDF pages, text extracted
with pypdf installed in the session scratchpad only). `docs/PSRAM_TIMING_CONTRACT.md`
was rewritten with datasheet-verified values and PDF page references. Controller
default `T_ACC` 6 -> 8. `sim/psram_chip_model.v` gained `tOE`, `tAA`, `tCO`,
`tAADV`-from-ADV#-rising, `tCW`, `tCPH`, `tCEM` and an OE#-during-address check.
Makefile mutation target now also kills `T_ACC=6` and `T_ACC=7`.
**Why:** the B-001 timings were provisional (agg23 reference values).
**Finding:** the B-001 default sampled read data about 1 clock (16.7 ns) after OE#
fell and about 3 clocks (50 ns) after ADV# rose. The datasheet requires `tOE` <= 20
ns from OE# low, and `tAADV` 70 ns (origin edge not stated; safe reading is from
ADV# rising). The B-001 model did not check `tOE`, so it passed. On hardware this
would have produced marginal or wrong reads. Not a hardware result; found before
any build.
**Datasheet facts closed:** software-access hazard is two async reads then two async
writes at 3FFFFFh, third-cycle data selects RCR/BCR/DIDR (p.19); power-up init
150 us (p.7); async is the power-up default, BCR 9D1Fh, RCR 0010h (pp.21, 26);
WAIT ignored in async (pp.5, 13); CRE may be tied low (p.19); CE# high between ops
>= 5 ns, CE# low <= 4 us (pp.30-33); write timings tWP 45, tDW 20, tAW/tCW/tVS 70.
**Result (simulation):** controller test PASSED (0 model errors), bus regression
PASSED, five mutants killed (`T_ACC=4/6/7`, `REL_CYC=1`, `MUT_EARLY_ACK=1`). Cost at
the checked defaults: 22 clocks per 32-bit write, 24 per read (controller only),
still about 2x cheaper than uncached SDRAM (48-50 clocks, A-094).
**Still open (hardware):** origin of `tAADV`; whether defaults-only operation works
on Pocket (p.3 caption says registers need setting after power-on, p.7 says defaults
load); board-level skew and I/O constraints; datasheet is a preliminary revision.
**Next:** P2 (mailbox, I/O constraints, `interact.json` publishing, the packager probe flag for the next free audit id).


### A-103 — playlist-buffers-to-SDRAM design spec (design only)

**Date:** 2026-09-20
**Evidence:** code-review (no build, no hardware)
**Change:** added `docs/PLAYLIST_SDRAM_MOVE_SPEC.md`: move `pl_text`/`pl_off`/`pl_order`
(13,312 B) behind the uncached alias at `0xA0100000` via a NOLOAD linker section, with a
preflight, feature-off fail-safe (no BRAM fallback), product RTL = A-101 seed 4 unchanged,
and an 8-row Pocket regression matrix. Confirmed from source: alias = `0xA0000000 + physical`,
byte/halfword access supported (A-093), macro-off decode aliases into MMIO, only the CPU
touches the buffers. Also checked the skill KB: KB-026/027 already hold their own bodies
and KB-030/031 are complete local entries, so no repair was needed. Step-1 hardware run
(cold boot + seek/pause/cover under stress) still awaits the user.

### A-104 — A-102 cold-boot repeat with seek/pause/track change (Pocket, no failure)

**Date:** 2026-09-20
**Evidence:** Pocket (8 screenshots, card clock 10:48-10:53, copies in
`work/diagnostics/sdram-stress-window/session4-screenshots/`). Card verified read-only before
the run: all 14 bundle files SHA-256-identical (seed-4 RBF `2e9aaf0e...`, burst ROM `55384a55...`).
**Observed** (HUD `E L M S R K P`): track 1 R0 (00:08, cold start) E7 L0 M0 S2; R1 stopped at
02:28 E15 L0 M372 S0 K3.8; R2 restarted at 00:05 E35 L0 M372 K15.2; R3 E56 L0 M372 K17.0 P8;
track 2 (455 px cover) R0 E73 L0; R1 E15 L0 M365 K3.2; R2 E37 L0 M373 K9.2; R3 E57 L0 M373
K11.1 P7. No FAIL/mismatch text, S 0 (the S2 at cold-boot R0 precedes any window traffic, M0),
worst window access 365-373 cycles (idle bound), L (late underruns) 0 in every frame, no audible
issue reported. Pump reached K up to 17.0k ops/s, similar to A-102's 22.6k.
**Reading:** E rises with every stop/seek/resume/track change (expected restart artefacts, KB-031);
E/S/K counters are reset when stress cycles (values drop between levels). Covers: cold boot,
stop, seek, resume, track change with a different cover size, at R0-R3.
**Limits:** no screenshot was taken during a cover decode itself, so cover-change-under-stress
is only inferred from the track change; the persist file holds only settings (no diagnostic
record in this core); saturating level not run (skipped by decision); the constant red/green
strip along the top edge is the same in sessions 2-4 (also in the A-102 session with the same
RBF family) and is treated as a fixed UI element, not corruption, but its source is not
identified in this session.
**Verdict:** step 1 gate passed at realistic load; no contention signal.

### A-105 — playlist buffers in SDRAM: firmware built and packaged (not installed)

**Date:** 2026-09-20
**Evidence:** host (build, link map, `make test-host`); no simulation or hardware yet.
**Change** (implements `docs/PLAYLIST_SDRAM_MOVE_SPEC.md`, all behind `TAU_PL_SDRAM`, default 0):
`pl_text`, `pl_off`, `pl_order` (13,312 B) get `__attribute__((section(".sdram")))`; `fw/link.ld`
gains region `sdram` at `0xA0100000`, length 0x3400, and an `.sdram (NOLOAD)` output section
(ALLOC only, no LOAD, so the ROM carries nothing). `pl_load()` first calls `pl_sdram_ready()`,
which runs once: pattern through the Phase 1 mailbox at physical 1 MiB, read back through the
window (a read, harmless on a bitstream without the window), then two patterns on the first and
last word of the section through the window. Failure sets `PL_ERR_SDRAM`, the playlist stays off
(no BRAM fallback) and the toast says `NO SDRAM PLAYLIST`. New build target
`fw/build.sh player-sdram-pl` (-> `work/diagnostics/playlist-sdram/tau.rom`); packager mode
`tools/package_sdram_stress.py --playlist-sdram --rbf ... --rbf-sha256 ...` builds core
`alfatreze.TAU_PLSDRAM` / platform `tau_plsdram`.
**Results:** the product build (`player`) is byte-identical to before (`b365dc2a...c842`, 152,088 B);
the new toast branch is compiled out. SDRAM ROM 152,436 B, SHA-256
`63e605e9e551229b4e06c1e41ba4b33c92ff8170c4b78f6736267d0ec92029fe` (+348 B: the preflight).
ELF: `.sdram` at `a0100000` size 0x3400 (pl_order a0100000, pl_off a0100200, pl_text a0100400);
heap gap 0x3FF0 = 16,368 B against the 1,024 B minimum (about 3,056 B before this change).
Bundle `work/diagnostics/playlist-sdram/pocket` with the A-101 seed-4 RBF (raw `ed34a6bc...90eb`,
bit-reversed `2e9aaf0e...3cd7`). `make test-host` passes.
**Not done:** no card write, no Pocket run. Test music/playlist for this platform must be copied
to `Assets/tau_plsdram/common/` at install time (same procedure as the window stress core).
Regression matrix rows 1-8 of the spec are pending; a BRAM-vs-SDRAM playlist-hash comparison
needs a way to read `pl_sig`/order on screen, to be decided when installing (the existing
overlay shows names and order, which is the practical check).

**A-105 installation (host, 2026-09-20):** installed `alfatreze.TAU_PLSDRAM` / platform `tau_plsdram` on the
card (volume `Pock`) beside the existing cores, nothing removed. All 14 bundle files SHA-256-identical
on the card (ROM `63e605e9...029fe`, bit-reversed RBF `2e9aaf0e...3cd7`); test music + `playlist.m3u`
copied to `Assets/tau_plsdram/common/` and diffed identical to `work/test-music/tau_sdram_wst/common`
(macOS `._*` files created by the copy were removed). Catalog indexes backed up to
`work/diagnostics/playlist-sdram/pocket-cache-backup-2026-09-20/System/` and cleared. The user had
cleaned out `Memories/Screenshots` beforehand. Pocket result pending: run matrix rows 2-8
(row 1 needs a no-window RBF).

### A-106 — playlist buffers in SDRAM on Pocket (A-105 build): pass on the 5-track list

**Date:** 2026-09-20
**Evidence:** Pocket (5 screenshots, card clock 11:09-11:13, copies in
`work/diagnostics/playlist-sdram/screenshots/`) plus the user's statement that everything in the
regression list was exercised and no audio problem was heard (user-reported, not all of it
photographed).
**Screenshots:** (1) overlay `PLAYLIST 1/5`, all five names correct and in file order
(`01 320CBR 44k1 stereo`, `02 320CBR 48k stereo`, `VBR 07 Contact with the Ohmu`, `CBR128 44k1
merry-farm`, `VBR 02 Stampede of the Ohmu`), track 1 playing at 00:17; (2) shuffle on: same names,
order permuted (VBR 02 first, current track 01 second), `2/5` in the overlay and `1/5` on the status
line, playback continued at 00:31; (3) `SEEK + 10s` on track 1 (1400 px cover) at 03:59, no glitch;
(4) track 2 (455 px cover) at 01:54 with shuffle; (5) track 3 (VBR, 64 kbps 44.1 kHz, album art) at
00:05. No error toast, no `NO SDRAM PLAYLIST`, no garbled name, playback normal.
**Reading:** the window preflight passed, the playlist loaded through `pl_text`/`pl_off`/`pl_order`
in SDRAM, names render correctly (every character read through the uncached window), shuffle
permutes `pl_order`, navigation across tracks works, and seeking and cover loads were fine.
**Limits:** only the 5-track list was tested (no 240-track or truncation case, matrix rows 3-4); no
screenshot of the boot toast; number of cold boots is user-reported only; no underrun counters (the
product build has no HUD), so "no audio issue" is by ear; row 1 (old no-window RBF, must refuse
without side effects) has not been run.
**Verdict:** rows 2, 5, 6, 7 pass on the small list. Remaining before the move is promoted:
rows 1, 3, 4 (large and clipped lists, negative test), then product packaging with the settings
menu work that this recovered space is for.

### A-107 — large-playlist test lists prepared for the SDRAM playlist (host only)

**Date:** 2026-09-20
**Evidence:** host (generator + parse model); not yet on the card, no Pocket result.
`tools/make_large_playlists.py` writes `work/test-music/tau_plsdram_large/` (`large240.m3u`,
`overflow_count.m3u`, `overflow_text.m3u`, `README.txt`) and prints the expected result from a model
of the firmware read/parse (PL_MAX 256, PL_TEXT_MAX 12,288): `large240` 9,798 B, 240 tracks, no clip,
missing-file markers at entries 50/100/150/200; `overflow_count` 300 lines -> 256 tracks, clipped;
`overflow_text` 19,722 B -> about 188 tracks, clipped by the text buffer (the last line may be cut).
Each list starts with real tracks so the first entry plays; the rest are deliberately missing files
(overlay/scroll test only). These exercise the top of the 13,312 B `.sdram` section (matrix rows 3-4).
Install procedure: copy the three `.m3u` files to `Assets/tau_plsdram/common/`, pick each via the
playlist file menu (needs user approval before writing to the card).

**A-107 installation (host, 2026-09-20):** the three lists and README were copied to
`Assets/tau_plsdram/common/` on the card (byte-compared identical, `._*` files removed); nothing else on the
card changed. Pocket result pending.

### A-108 — large playlists in SDRAM on Pocket: counts match the model (matrix rows 3-4)

**Date:** 2026-09-20
**Evidence:** Pocket (4 screenshots, card clock 11:25-11:26, copies in
`work/diagnostics/playlist-sdram/screenshots-large/`); user reports all three lists were tested.
`large240.m3u`: `PLAYLIST 1/240` with the first nine rows in the expected cycle, status `1 / 240`; at the
end `240 / 240`, last row `VBR 02 Stampede of the Ohmu` (as predicted). `overflow_count.m3u`: `256 / 256`,
last rows `m248`..`m256` intact (the model predicted 256). `overflow_text.m3u`: `188 / 188`, last rows
`long 180`..`long 188`, the final name cut short (`long 188 xxxxxxxxxxxxxxxx`), exactly the modelled count
(188) and the predicted mid-name cut. Playback of the first real track continued in each (00:05, 00:14,
00:10), no error, no garbled text, no audio issue reported.
**Reading:** the whole 13,312 B section works through its top end: the 256-entry index arrays are full
(`pl_order` and `pl_off` to their last element) and the 12,288-byte text buffer is full and cut at the
limit, with the same counts as the BRAM design's limits. The clipping toast, the marker rows 50/100/150/200
of `large240` and the final re-pick of the 5-track list were not photographed.
**Verdict:** rows 3-4 pass. Still open: row 1 (a bitstream without the window must refuse without side
effects) and a probe-free HUD-free long soak; then decide whether to make `TAU_PL_SDRAM` the product default.

### A-109 — no-window fail-safe test prepared (fault-injection ROM; true no-window RBF still needed)

**Date:** 2026-09-20
**Evidence:** host (build, package); no Pocket result.
**Finding:** matrix row 1 as written cannot be run with an existing RBF. Firmware refuses to start on any
RBF whose `R_VERSION` differs from `EXPECT_VERSION` (rev 23, A-080), painting a fixed pattern and
stopping before the playlist code runs. Every rev-23 build so far (A-080..A-102) has
`TAU_PHASE2_WINDOW`; the only macro-off RBFs (dist product, `work/diagnostics/sdram/fpga`) are rev 22.
So a genuine no-window test needs a new **rev-23 macro-off** Quartus build (no `TAU_PHASE2_*` macros,
about 50 min on the VM); not launched.
**Prepared now:** fault-injection variant `TAU_PL_SDRAM_FAULT=1` (`fw/build.sh player-sdram-pl-fault`): the
window preflight reads a word the mailbox never wrote, so it fails on a healthy bitstream and exercises the
feature-off path (`PL_ERR_SDRAM`, toast `NO SDRAM PLAYLIST`, playlist off, player otherwise alive). ROM
152,440 B, SHA-256 `00b17a682dd9f09f4c9afcdfe4310b200cfd01711759e324cb493c97603fb66e`; packaged with the
seed-4 RBF as `alfatreze.TAU_PLSDRAMF` / `tau_plsdramf` (`package_sdram_stress.py --playlist-sdram --fault`),
bundle `work/diagnostics/playlist-sdram-fault/pocket`; NOT installed. The normal SDRAM ROM
(`63e605e9...`) and the product ROM (`b365dc2a...`) are unchanged.
**What it proves and what it does not:** proves the failure branch (no store issued, toast, single-file
playback, no crash, no MMIO side effect from the branch itself) on hardware. It does not prove the
premise that on a window-less bitstream the preflight's read of `0xA0100000` is harmless (that read hits the
MMIO decode); that needs the rev-23 macro-off RBF.

**A-109 installation (host, 2026-09-20):** `alfatreze.TAU_PLSDRAMF` / `tau_plsdramf` installed on the card beside
the other cores; all 14 bundle files SHA-256-identical (ROM `00b17a68...b66e`, bit-reversed seed-4 RBF
`2e9aaf0e...3cd7`). Assets: `playlist.m3u` (5 lines) and one real track
(`TAU A-102 stress/02 320CBR 48k stereo.mp3`) so single-file playback can be tried while the playlist is refused.
Indexes backed up to `work/diagnostics/playlist-sdram-fault/pocket-cache-backup-2026-09-20/System/` and cleared.
Result pending. Expected: toast `NO SDRAM PLAYLIST`, no playlist overlay, track playable via Load > Audio file.

### A-110 — rev-23 macro-off (no window) RBF build launched

**Date:** 2026-09-20
**Evidence:** VM launch only; result pending.
Staged `/home/taualpha/tau-local/nowin-a110-20260920` from the A-101 seed-4 snapshot minus its last three qsf
lines (comment, `TAU_PHASE2_WINDOW=1`, `SEED 4`): no `TAU_PHASE2_*` macros, default seed, same rev-23 RTL;
`make check-fpga` ok; `make fpga` launched 12:13 WEST (log `quartus-a110.log`). Purpose: matrix row 1, the
`TAU_PL_SDRAM` ROM (`63e605e9...`) on a bitstream where the alias decodes to MMIO must refuse without side
effects. Timing on this build is irrelevant to the product; it is a test vehicle.

### A-111 — fault-injection core on Pocket: playlist refused, player alive (A-109 result)

**Date:** 2026-09-20
**Evidence:** Pocket (4 screenshots, card clock 11:41-11:42, copies in
`work/diagnostics/playlist-sdram-fault/screenshots/`).
**Observed:** (1) cold boot lands on the idle "Getting started" screen, i.e. the boot-time `pl_load()` produced no
playlist; (2) after the user chose Load Playlist the `LOADING PLAYLIST` indicator appeared (over the splash, then the
CRT loading screen) and ended without a playlist; (3) after Load MP3 the 48 kHz track played normally
(`PLAYING`, 00:09 / 11:13, cover, spectrum) and a Select tap showed `NO PLAYLIST LOADED` (the `pl_count == 0` path);
no crash, no garbled state, no MMIO side effect visible, and playback worked with the playlist off.
**Reading:** the feature-off branch behaves as designed on hardware: the window check fails on a healthy
bitstream, `pl_count` stays 0, the UI handles it, and single-file playback is unaffected.
**Limit:** the `NO SDRAM PLAYLIST` toast itself was not photographed (it is brief and the screenshots came after it),
so its exact wording on screen is unconfirmed; the `NO PLAYLIST LOADED` message seen is a different, existing toast.
Still open: the true no-window RBF (A-110, building).

**A-110 result (Quartus, 2026-09-20):** the rev-23 macro-off build finished Successful (0 errors, 346 warnings;
it took 2h20m wall clock, far longer than the 50-55 min of earlier builds, cause not investigated, other
idle VM sessions or host load are possible), 300/308 RAM blocks, all reported slacks positive (setup
+1.029 ns and up, hold +0.313 ns and up). Raw RBF SHA-256
`743f9fdd349b1cfae11d98492b578ffac95cd9839bc60bd5190f192a8d34b1e6`, copied to
`work/diagnostics/nowindow-a110/ap_core.rbf`. Packaged with the normal SDRAM-playlist ROM
(`63e605e9...029fe`, unchanged) by `package_sdram_stress.py --playlist-sdram --nowin` as core
`alfatreze.TAU_PLSDRAMN` / platform `tau_plsdramn` (bit-reversed RBF `260164d1...03af`, verified against
the raw file), bundle `work/diagnostics/playlist-sdram-nowin/pocket`; NOT installed on the card.
Expected on Pocket: the ROM passes the rev-23 interlock, the window preflight fails, toast
`NO SDRAM PLAYLIST`, playlist off, single-file playback works, and nothing else misbehaves.

**A-110 installation (host, 2026-09-20):** `alfatreze.TAU_PLSDRAMN` / `tau_plsdramn` installed on the card beside the
other cores (nothing removed); all 14 bundle files SHA-256-identical (ROM `63e605e9...029fe`, bit-reversed
no-window RBF `260164d1...03af`). Assets: `playlist.m3u` (5 lines) and one real track
(`TAU A-102 stress/02 320CBR 48k stereo.mp3`). Indexes backed up to
`work/diagnostics/playlist-sdram-nowin/pocket-cache-backup-2026-09-20/System/` and cleared. Result pending.

### A-112 — SDRAM-playlist ROM on a no-window RBF (Pocket pass) and a correction about the top-edge strip

**Date:** 2026-09-20
**Evidence:** Pocket (3 screenshots, card clock 14:09, `work/diagnostics/playlist-sdram-nowin/screenshots/`) and RTL
code-review of `src/fpga/core/core_game.vh`.
**Row 1 result:** `TAU Playlist No Window` (`63e605e9...` ROM on the A-110 rev-23 macro-off RBF) booted past the
interlock to the idle "Getting started" screen (no playlist), `LOADING PLAYLIST` appeared after Load Playlist and
ended without one, and a loaded track then played normally (`PLAYING`, 00:08 / 11:13, cover, spectrum). No crash,
no garbled screen, no visible side effect of the preflight's read of `0xA0100000` on a bitstream where the
alias decodes to MMIO. So the fail-safe premise holds on hardware; the `NO SDRAM PLAYLIST` toast itself was again not
photographed. The user reported no problem beyond "tested".
**Correction (important):** in all earlier window-build screenshots (A-102 sessions, A-104, A-106, A-111) a
red/green strip runs along the top edge. A-104 called it "a fixed UI element". It is not: `core_game.vh` (around
lines 463-500 and 591-594) instantiates `tau_sdram_cpu_window_probe` and overlays 49 eight-pixel cells on the top
scanlines whenever **`TAU_PHASE2_WINDOW` is defined**; the probe macros only select the return-path mode. The
strip is absent in the three no-window screenshots. Consequently the A-101 "product-candidate RTL, no probe
macros" still contains the diagnostic overlay (and its probe logic), and A-102 was wrong to say the strip was
confined to the A-093 RBF. The A-101 timing/RAM-block numbers, the A-102..A-108 memory results and the
window's function are unaffected; what is not a product build is the presentation (visible strip) and the exact
netlist.
**Next:** split the overlay and the probe instance under their own macro (for example `TAU_PHASE2_PROBE_OVERLAY`),
keep `TAU_PHASE2_WINDOW` for the window alone, add the macro to the existing probe builds' recipes, run
`make test-rtl`, then re-run the multi-seed fit and re-verify (soak, coverage, contention, playlist) on the
resulting RBF, since removing the probe changes placement and timing. Not started; needs the owner's decision.

### A-113 — split the diagnostic probe/overlay from the CPU-window macro (RTL; simulation + synthesis only)

**Date:** 2026-09-20
**Evidence:** code-review, simulation (`make test-rtl` passes, 0 failures), Quartus Analysis & Synthesis (VM); no fit, no Pocket.
**Check first:** every use of `TAU_PHASE2_WINDOW` in the tree: `core_game.vh` (mp3_soc window enable; SDRAM debug
port hookup; probe instance plus overlay; video-out mux), `sdram_fb.sv` (six blocks, all debug ports/registers of the
probe), `Makefile` (controller-probe testbench). Only the first is the window itself; everything else feeds the
probe/overlay. No other module depends on the probe, and the wires that only the probe consumed are unconditional
and were already removed by synthesis in macro-off builds.
**Change:** new opt-in macro `TAU_PHASE2_PROBE` now guards the controller debug taps in `sdram_fb.sv` and the
debug-port hookup, probe instance and overlay/video mux in `core_game.vh`. `TAU_PHASE2_WINDOW` keeps only the
`mp3_soc` window enable. `TAU_PHASE2_MUX_PROBE/_ADAPTER_PROBE/_RETURN_PROBE` remain return-path selectors and now
require `TAU_PHASE2_PROBE`. The controller-probe simulation in the Makefile builds with both macros.
**Consequence for old recipes:** a build that used `TAU_PHASE2_WINDOW=1` plus a `*_PROBE` selector (A-080, A-093 and
the other probe RBFs) needs `TAU_PHASE2_PROBE=1` added to reproduce the same bitstream; the bare-window build
(A-101) changes: it loses the probe and the strip.
**Synthesis check** (`quartus_map`, tree with only `TAU_PHASE2_WINDOW=1`, stage `map-a113-20260920` on the VM):
successful, 0 errors, 5m16s. The probe entity is read from source but not instantiated (no instance in the report).
Total registers: 7,470, against 7,688 for the A-101 seed-4 map (218 fewer, the probe) and 7,311 for the no-window
A-110 build (the window itself costs about 159); block memory bits unchanged (2,380,928).
**Not done:** no place-and-route, no timing, no Pocket. The probe-free window RBF must be built on several seeds
and every A-093..A-108 gate re-run on it before it replaces the A-101 seed-4 RBF as the product candidate.

### A-114 — probe-free window RBF, multi-seed fit launched

**Date:** 2026-09-20
**Evidence:** VM launch only; results pending.
Committed the A-113 split (`e67f93a` RTL, `bf56f20` docs/tools). Staged
`/home/taualpha/tau-local/probefree-a114-s1-20260920` and `...-s2-20260920` from the A-110 tree with the
committed `src/fpga` and `Makefile` overlaid, and `TAU_PHASE2_WINDOW=1` plus `SEED 1` / `SEED 2` appended to
`ap_core.qsf` (no probe macros). `make check-fpga` ok; both `make fpga` runs launched 15:05 WEST in parallel
(logs `quartus-a114-s1.log`, `quartus-a114-s2.log`). Seeds 3-4 only if timing is tight. Acceptance: 0 errors, all
slacks positive with hold at least as good as A-101 (+0.105 ns worst), 300/308 RAM blocks or fewer; then repackage the
window stress core and re-run soak, coverage, contention and the playlist tests on the chosen RBF.

### A-115 — in-app settings UI (grouped), firmware built and packaged (not installed)

**Date:** 2026-09-20
**Evidence:** host (build, link map, snapshot fixtures, `make test-host`); no Pocket run.
**Decisions (owner):** grouped home + three pages; opened with **Start** ("we'll remove the stop function from
there"; settings may later become part of a main menu). Hold-Select was rejected once found to be the existing
album-art toggle.
**Change** (behind `TAU_SETTINGS_UI`, default 0): new `fw/settingsui.inc`, table-driven. Home lists Appearance,
Audio, Playback; Appearance = Colour, Meter (named), Album art on/off, Screen blank; Audio = EQ, Volume; Playback =
Repeat, Shuffle, Resume, Speed (Normal/1.2x). Up/Down move, A/Right open a group or change a value, Left/Right change,
B back (closes at the home), Start closes. Every change applies immediately through the same variables and side effects
as the direct shortcuts, which stay (X, Y, Select+L/R/Down, hold-Select art, hold-A speed, Up/Down volume). Nothing new
is written to the persisted settings words; Screen blank stays non-persisted (as Select+Down). Hooks in `player.c`:
`UI_OVERLAY_UP` (= playlist overlay or settings) gates the title/meter/status repaint exactly as the playlist
overlay does, draw in the repaint tail and main loop, input consumed early in `poll_input()`. In this build **Start no
longer stops playback** (B still repositions to the start, A pauses); Select+Start debug/stress combos are untouched.
Deferred: Advanced, reset, gallery/preview screens, EQ curve preview, playback-speed preview.
**Build:** `fw/build.sh player-settings` (defines `TAU_SETTINGS_UI=1 TAU_PL_SDRAM=1`: the settings alone do NOT link on
BRAM, the SDRAM playlist is required; the linker refuses with "firmware image collides"). ROM 156,816 B, SHA-256
`9e252117...7e40`, heap gap 11,984 B (SDRAM-playlist build 16,368 B), i.e. the four-page settings cost about 4.4 KiB, about
1.5x the flat prototype of A-096 (2,996 B). Product ROM unchanged (`b365dc2a...`), SDRAM-playlist ROM unchanged
(`63e605e9...`). Packaged with the A-101 seed-4 RBF as `alfatreze.TAU_SETTINGS` / `tau_settings`
(`package_sdram_stress.py --playlist-sdram --settings`); bundle `work/diagnostics/settings-ui/pocket`; NOT installed.
**Snapshots (AGENTS.md):** four new named fixtures `settings-home`, `settings-appearance`, `settings-audio`,
`settings-playback` (labels, row counts and column geometry parsed from `fw/settingsui.inc`), registered in
`ui_snapshot_renderer.py`, `check_ui_snapshot_renderer.py` and `visual_review.py`; rendered to `work/previews/` and
inspected (first render clipped `SCREEN BLANK` and `OSCILLOSCOPE`; column widths fixed in the firmware and fixture).
`check_ui_snapshot_renderer` now emits 37 fixtures.
**Risks to test on the Pocket:** (1) the Pocket screenshot combination is seen by the core and earlier stopped playback
(KB-031); if it includes Start, a screenshot will now open the settings screen; (2) closing must repaint the player
without artefacts (`pl_ui_restore` path, as the playlist); (3) meter and cover behaviour after changing them from the
menu; (4) audio while paging and changing values (EQ, speed, volume), listen for dropouts; (5) shuffle change with a
playlist loaded keeps the current track.

**A-115 installation (host, 2026-09-20):** `alfatreze.TAU_SETTINGS` / `tau_settings` installed on the card; all 14 bundle
files SHA-256-identical (ROM `9e252117...7e40`, bit-reversed seed-4 RBF `2e9aaf0e...3cd7`). Media copied (not moved; the
base TAU is untouched) from `Assets/tau/common` into `Assets/tau_settings/common`: the `Nausicaa OST` folder and
`playlist.m3u`, diffed identical. Removed as finished/superseded test builds (each backed up first and diffed identical
under `work/diagnostics/settings-ui/card-removed-2026-09-20/`, including Cores, Assets, Platforms and Settings):
`TAU_PLSDRAM`, `TAU_PLSDRAMF`, `TAU_PLSDRAMN`. Left in place: base `TAU`, the window stress core `TAU_SDRAM_WSTRESS`
(needed for the gate re-runs) and the older Phase 1 `TAU_SDRAM_STRESS` / `TAU_SDRAM_DIAG`, plus stale `Settings/`
folders of long-removed probe cores. Incident: the first attempt aborted when the card dropped off the Mac (nothing
deleted); the second removed the three cores and then failed on an unquoted path before installing; the third, with quoted
paths, completed. Catalog indexes backed up to `work/diagnostics/settings-ui/pocket-cache-backup-2026-09-20/System/` and
cleared. Result pending.

### A-116 — full-screen overlays, choice lists, colour swatches and meter placeholders (firmware built, not installed)

**Date:** 2026-09-20
**Evidence:** host (build, link map, snapshot fixtures, `make test-host`, `overlay_preview.py`); no Pocket run. A-115 was
reported working on the Pocket by the user ("everything worked").
**Request (owner):** playlist and settings full screen with more visual space; Colour as a submenu list with a circle per
option and the selected one marked; Meter the same with a small preview per meter (grey rectangle placeholder); Equalizer as a
submenu with the active preset marked and A selecting a new choice; "this should be the default behaviour".
**Changes:**
1. **Full-screen overlays.** New geometry (`PL_UI_*`: panel inset 8 px, 12 rows of 22 px, 36 px hint area) and a shared
   `ov_frame()` (border, rounded panel, accent title, optional right-hand counter, hairline, dim hint line). Playlist gets a
   `n / total` counter and an `A PLAY   B BACK` hint. Because nothing of the player may show through, **the three drawing
   primitives (`fb_rect`, `fb_copy_span`, `fb_char`) are now no-ops while an overlay is up** (`FB_HELD()`), except when the
   overlay itself paints (`ov_draw`); playback continues and the existing close path (`pl_ui_restore`: chrome repaint plus
   invalidation of meter, clock, progress, info) redraws everything. Toasts raised while an overlay is up are not shown.
   This changes the **standard build too** (playlist overlay).
2. **Choice lists as the default for multi-option settings** (`fw/settingsui.inc`, rewritten table-driven): Colour, Meter,
   Equalizer, Repeat and Screen blank open a list page; Up/Down move the highlight, **A selects** (applies at once, the mark
   moves), B goes back. Colour rows are swatches (a circle in each theme colour, white ring on the active one); Meter rows are a
   radio circle plus a 56x32 grey placeholder thumbnail plus the name (11 meters, 6 visible, scrolls); the others use a radio
   circle with a dot on the active option. Two-state settings (Album art, Shuffle, Resume, Speed) and Volume stay inline (A or
   Left/Right).
3. **Snapshots.** `playlist-browser` and the four settings fixtures were rewritten to the new layout, and five new fixtures added:
   `settings-colour`, `settings-meter`, `settings-eq`, `settings-repeat`, `settings-blank` (geometry, row tables and names are read
   from `fw/player.c`, `fw/settingsui.inc`, `fw/eq_curve.h`). 42 fixtures; rendered to `work/previews/` and inspected.
**Builds (ROM SHA-256 prefixes):** product `661c5936...` (151,252 B), SDRAM-playlist `02fa0499...` (151,600 B), settings
`78cb7582...` (152,680 B; heap gap 16,112 B here, the previous settings ROM had 11,984 B: the rewrite is smaller). These replace
`b365dc2a`, `63e605e9` and `9e252117`; the earlier Pocket evidence (A-104..A-115) belongs to the old ROMs. Settings bundle
`work/diagnostics/settings-ui/pocket` repackaged with the seed-4 RBF; NOT installed.
**Risks for the Pocket:** (1) the primitive gate could hide a legitimate draw made while an overlay is up (art or meter not
redrawn after close, stale clock/progress/toast); (2) close must repaint the player cleanly for both overlays; (3) full-panel
redraw cost on every cursor move (12 rows of circles): check for visible flicker and audio dropouts while paging; (4) scroll
handling and highlight on the 11-row meter list; (5) selecting Colour/Meter/EQ/Repeat/Blank applies at once (accent changes
while the list is open); (6) screenshot combination still to be checked for Start.

**A-116 installation (host, 2026-09-20):** replaced `Assets/tau_settings/common/tau.rom` on the card with the A-116 build (SHA-256
`78cb7582...50faa5e`); the previous A-115 ROM (`9e252117...`) and the catalog indexes are backed up in
`work/diagnostics/settings-ui/rom-replaced-a116/`, the indexes were cleared. RBF, JSON, base media and every other core untouched;
all 14 bundle files SHA-256-identical, media diffed identical. Result pending.

### A-117 — A-116 on Pocket (works), one overlay-conflict bug and its fix

**Date:** 2026-09-20
**Evidence:** Pocket (12 screenshots from the A-116 session, card clock 15:51-15:52, copies in
`work/diagnostics/settings-ui/screenshots-a116/`; plus one older A-115-layout shot) and the user's report: everything worked, no
tearing, no audio problem heard.
**Seen:** full-screen playlist (`PLAYLIST 2 / 13`, 12 rows, hint line, `>` on the playing row, cursor row highlighted), settings
home, Appearance (COLOUR BLUSH, METER VU, ALBUM ART OFF, SCREEN BLANK NEVER), Audio (EQUALIZER BASS, VOLUME 100%), Repeat list
(radio dot on ALL), Colour list (swatch per theme colour, white ring on the active BLUSH, dark ring on the cursor row); accent
follows the selection as it changes. Screenshots taken while settings was open did not close or re-open it, so the Pocket's
screenshot combination does not toggle Start. The red/green strip at the top is still there: this card carries the A-101 seed-4 RBF
(probe included); the A-113 probe-free RBF is still being fitted.
**Bug (user-reported and visible in screenshots 1-2):** opening settings while the playlist overlay is open left `pl_ui_open` set;
the playlist's selected-row marquee redraws that row through the `ov_draw` gate, so the playing track name overwrote the first
settings row. **Fix:** `set_input()` closes the playlist when settings opens (one overlay at a time; closing settings returns to
the player). Select cannot reopen the playlist over settings because `set_input()` consumes the falling edge. Settings ROM
`7f81d6f1...` (152,692 B), bundle `work/diagnostics/settings-ui/pocket` repackaged; product ROM (`661c5936...`) and SDRAM-playlist
ROM (`02fa0499...`) unchanged. Not yet on the card.

### A-118 — Diagnostics group, Phase 1: Info page (firmware built and packaged, not installed)

**Date:** 2026-09-20
**Evidence:** host (build, link map, snapshot fixtures, `make test-host`); no Pocket run.
**Scope (owner: "start phase 1 and install the overlap fix"):** a **Diagnostics** group on the settings home, compiled only with
`TAU_DIAG_MENU` (default 0; the `player-settings` developer target defines it), containing one page, **Info**, of eleven read-only
live values: firmware version, FPGA rev (`R_VERSION`), SDRAM window state (untested/OK/FAILED) and cycles for one window read
(measured in `pl_sdram_prove()`, new `pl_win_rd`), free RAM (heap gap), playlist track count, list-clipped flag, current track
(format, kbps, rate), underrun count since boot (`pcm_under_n`), draw-engine stall in ms, and the last load timings
(head/size/art/total). Rows only repaint once a second (`set_info_tick()`; the panel is drawn once), B goes back. It includes the
overlap fix (opening settings closes the playlist).
**Save report deferred:** the product's persist register file has 16 words of which 12 are used (`SW_N`), so the 16-word,
31-bit record of A-091 does not fit; a compact 4-word summary or an RTL/register widening is needed and is a Phase 2 decision.
The Pocket already takes screenshots, so no firmware screenshot is attempted (architecture doc: no runtime image export).
**Builds:** product ROM unchanged (`b365dc2a` line replaced by A-116's `661c5936...`, 151,252 B, still the same); SDRAM-playlist ROM
changed by the `pl_win_rd` measurement: `82fdb70c...` (151,616 B); settings ROM `d0b5a32b...` (156,252 B, heap gap 12,512 B, about
11.5 KiB above the linker minimum). Bundle repackaged with the seed-4 RBF. New fixtures `settings-diagnostics` and `settings-info`
(44 total), rendered and inspected. `make test-host` passes.
**Not on the card yet** (card unmounted at build time).

### A-119 — release-style build includes Info; "Diagnostic Build" defined; settings ROM with Info installed

**Date:** 2026-09-20
**Owner decision:** the release-style build combines the standard settings with the read-only Info page (small, useful for support);
the developer build is named **Diagnostic Build** and will carry the on-demand tests, stress pump and soak.
**Change:** `TAU_DIAG_MENU` renamed `TAU_DIAG_INFO` (Diagnostics group + Info page); `TAU_DIAG_TESTS` reserved for the Diagnostic
Build (no code behind it yet). `fw/build.sh`: `player-settings` = settings + SDRAM playlist + Info, minimum heap gap 8 KiB enforced
at build time; new `player-diagnostic` = the same plus `TAU_DIAG_TESTS`, minimum 4 KiB (both print `heap gap: N B`). Packager
`--diagnostic`: core `alfatreze.TAU_DIAGNOSTIC` / platform `tau_diagnostic`, title "TAU Diagnostic Build", bundle
`work/diagnostics/diagnostic-build/pocket` (not installed). Until Phase 2 exists the two ROMs are byte-identical
(`d0b5a32bb7309e2a...`, 156,252 B, heap gap 12,512 B); product ROM (`b365dc2a` lineage, 151,252 B) and SDRAM-playlist ROM
(`82fdb70c...`) unchanged from A-118. `make test-host` passes.
**Card (host):** `Assets/tau_settings/common/tau.rom` on the card replaced with `d0b5a32b...` (A-118: overlap fix + Info page); the
previous A-116 ROM (`78cb7582...`) and the indexes are backed up in `work/diagnostics/settings-ui/rom-replaced-a118/`, indexes cleared;
all 14 bundle files SHA-256-identical, base media untouched. The Diagnostic Build was NOT installed. Result pending.

**A-114 result (Quartus, 2026-09-20):** both fits finished Successful (0 errors, 343 warnings; 50:00 and 52:18 wall clock,
started 15:05 WEST, finished about 15:55). Probe-free window RBF, 300/308 RAM blocks, 7,768 registers (the A-101 seed-4 map had
7,688 registers in synthesis, so this is the fitted count and not comparable to the map figure). No negative slack anywhere.
| Seed | Worst setup | Worst hold | Raw RBF SHA-256 |
|---|---|---|---|
| 1 | +0.158 ns | +0.124 ns | `5a1d75c39828f232859a737345a65831b679e20f4301214a556cedf9747057f2` |
| 2 | +0.664 ns | +0.124 ns | `551e5a7600fbf5c5e93a3d1f513a4b71c5603e3d890d26fa72dfcbfd4343718b` |
Against A-101 (setup +0.39..+0.52, hold +0.105..+0.123): both hold slacks are equal to or slightly better than the best A-101 seed,
seed 2 has clearly the best setup margin, seed 1's setup is thin. **Seed 2 is the candidate** (acceptance from A-114: no negative
slack, hold at least +0.105 ns, at most 300 RAM blocks; equal hold, so setup breaks the tie). Copies in
`work/diagnostics/sdram-probefree-a114/s1|s2/ap_core.rbf`. Seeds 3 and 4 not needed. Still open: repackage the window-stress core
and the playlist/settings cores with this RBF and repeat soak, coverage, contention and playlist gates on Pocket; the red/green top
strip must be absent on it (the probe is gone).

**A-114 candidate installed (host, 2026-09-20):** the probe-free seed-2 RBF (raw `551e5a76...718b`, bit-reversed `cb15310a...a3c9`,
reversal re-checked against the raw file) replaced the seed-4 RBF (`2e9aaf0e...`) in **both** `alfatreze.TAU_SDRAM_WSTRESS`
(stress ROM unchanged, `55384a55...`) and `alfatreze.TAU_SETTINGS` (ROM unchanged, `d0b5a32b...`). All 14 files of each bundle
SHA-256-identical on the card; the old RBFs and the catalog indexes are backed up in
`work/diagnostics/sdram-probefree-a114/card-replaced/`, indexes cleared. Media and every other core untouched. Result pending.
Checks to run: (1) top-edge red/green strip must be absent in both cores; (2) stress core: repeat the A-102 protocol (tracks x R0-R3,
E/L/M/S/K counters) on this RBF; (3) settings core: the earlier settings/playlist/Info checks; (4) then soak (A-097) and coverage
(A-100) probes need packaging with this RBF (not built yet).

### A-120 — Info page on Pocket (A-118 ROM, old seed-4 RBF): works

**Date:** 2026-09-20
**Evidence:** Pocket (1 screenshot, `work/diagnostics/sdram-probefree-a114/screenshots/20260920_161516.png`, card clock 16:15) and the
core's persist file (same minute). The card clock runs behind the host clock, and this shot predates the seed-2 RBF install, so it was
taken with the A-118 settings ROM on the **A-101 seed-4 RBF** (the red/green top strip is still present, as expected; the probe-free RBF
has not been run yet). The user's remark: the Info page and overlap fix were tested; no problems reported.
**Info page:** FIRMWARE 0.1.0; FPGA REV 4D503317 (matches `EXPECT_VERSION`); SDRAM WINDOW OK; WINDOW READ 286 CYC; FREE RAM 12,512 B
(equals the build's heap gap); PLAYLIST 13 TRACKS, LIST CLIPPED NO; TRACK MP3 64K 44.1K; UNDERRUNS 1; DRAW STALL 8 MS; LOAD MS
383/0/2587/3017 (head/size/art/total).
**Reading:** all eleven fields render and carry plausible values, and the ones we can cross-check agree with the build. Notes:
(1) WINDOW READ is one cold read right after the mailbox write, so it includes SDRAM arbitration (286 cycles, inside the known 360-cycle
worst case); the typical cost is about 50 cycles (A-094). A better figure would be the minimum of several reads; (2) one underrun edge
since boot is consistent with the start-of-track restart artefact (KB-031), not a sign of a problem; (3) the load breakdown shows
album-art decode as the slow part (2.6 s of a 3.0 s load for this track); (4) the 8 ms draw stall is cumulative since boot.
The persisted settings file was written on Quit, so persistence still works with the new pages.
**Not yet run on Pocket:** the seed-2 probe-free RBF in `TAU_SETTINGS` and `TAU_SDRAM_WSTRESS` (installed after this session).

### A-121 — window stress on the probe-free seed-2 RBF: no logged failure, top strip gone, user reports errors and poorer audio (open)

**Date:** 2026-09-20
**Evidence:** Pocket (10 screenshots, card clock 16:46-16:50, `work/diagnostics/sdram-probefree-a114/screenshots/`) plus the user's report:
"windows stress seemed to have errors and poorer audio quality", tested mostly on tracks 1 and 2 (one screenshot each of tracks 3 and 4
at the start). Settings core: UI fine (user).
**Confirmed good:** the red/green top-edge strip is **absent** in every frame, so the probe is out of the bitstream; the playlist/settings
UI is fine. The core loaded and played.
**HUD readings (stress ROM `55384a55`, burst pump; E early underruns, L late, M worst window access, S draw stall ms, K k ops/s):**
| Time | Speed | Track | Level | E | L | M | S | K |
|---|---|---|---|---:|---:|---:|---:|---:|
| 46:23 | **1.2x** | 1 | R3 | 21 | 0 | 372 | 1 | 1.4 |
| 47:08 | **1.2x** | 2 | R3 | 45 | 0 | 372 | 1 | 1.4 |
| 47:30 | **1.2x** | 3 | R3 | 50 | 0 | 372 | 1 | 1.4 |
| 47:55 | **1.2x** | 4 | R3 | 56 | 0 | 372 | 1 | 4.7 |
| 48:45 | 1.0x | 1 | R1 | 12 | 0 | 371 | 0 | 4.2 |
| 49:08 | 1.0x | 1 | R2 | 28 | 0 | 373 | 0 | 15.5 |
| 49:28 | 1.0x | 1 | R0 | 40 | 0 | 373 | 0 | 0 |
| 49:45 | 1.0x | 2 | R0 | 50 | 0 | 373 | 0 | 0 |
| 50:07 | 1.0x | 2 | R1 | 14 | 0 | 371 | 0 | 3.0 |
| 50:27 | 1.0x | 3 | R2 | 26 | 0 | 372 | 0 | 13.4 |
**Reading:** no `FAIL`, no `SDRAM MISMATCH` or timeout text, no late underrun (`L` 0 in all ten), `M` 371-373 (the idle bound), `S`
0 at 1.0x. Two differences from the earlier A-102 runs: (1) the **1.2x speed indicator is lit in the first four frames**
(the hold-A speed gesture, or the settings toggle, was on) and only there does `S` read 1 ms and the pump reach just 1.4k ops/s at R3,
i.e. the decoder was using most of the CPU; that alone would sound different (pitch/tempo) and leaves little idle time; (2) the
early-underrun counter `E` is higher than before (up to 56 against 4-13), which fits many stops, seeks and restarts but cannot be
separated from a real change without a like-for-like A/B. Screenshots stop playback (KB-031), so they add restarts.
**Not established:** what the user perceived as "errors" (a message, a glitch, audible dropouts) and where; the frames show none.
Whether the RBF changed audio quality is unproven either way. Suggested discriminators: the same track and level at 1.0x by ear on the
old seed-4 RBF and on seed 2 (A/B, swap `bitstream.rbf_r` only); a longer, screenshot-free run per level; the Info page underrun
count on the settings core over a full album.

### A-122 — 1.2x hold gesture removed where the settings menu exists

**Date:** 2026-09-20
**Owner decision:** after A-121 (the user's stress session had 1.2x on without noticing, and reads the `E` counter as errors: it is
early underruns after a restart, not a failure indicator; failures show as `FAIL n`) - "remove 1.2x from the long press and keep in
settings only".
**Change:** in `fw/player.c` the hold-A speed toggle is compiled only when `TAU_SETTINGS_UI` is 0. In builds with the settings menu a
long press of A is an ordinary press (pause on release); Speed stays in Settings > Playback (Normal / 1.2x). The standard build has no
settings menu yet, so it keeps the gesture until the settings ship (it would otherwise lose the only way to change speed). Product ROM
unchanged (`661c5936...`), SDRAM-playlist ROM unchanged (`82fdb70c...`); settings and Diagnostic Build ROM `448a49dc...` (155,468 B, heap gap
13,312 B; the removed block shrank it by about 0.8 KiB). Bundles repackaged with the seed-2 RBF (`TAU_SETTINGS`, `TAU_DIAGNOSTIC`); NOT installed.
`make test-host` passes. The A/B of old versus new RBF suggested in A-121 is postponed by the user (judged user error).

### B-003 — PSRAM plan review after later SDRAM/UI/skill updates (docs only)

**Date:** 2026-09-20

**Decision/change:** Reviewed everything since B-002 (A-100..A-122 in the A series, `ARCHITECTURE_ROADMAP.md`,
the skill's drift checks and its KB restructure) against the PSRAM plan. Updated
`docs/PSRAM_IMPLEMENTATION_PLAN.md`: audit-id policy (next free id), the
window/probe macro split, P5 candidates (playlist buffers already in SDRAM), the
scheduling note, and a new section 7 listing twelve considerations. Removed the stale
`--probe-a100` from B-001/B-002 (A-100 belongs to the SDRAM coverage probe).
**Skill checks:** `refresh.py docs` 0 changed; `refresh.py repos` openfpga-library
metadata moved, one repo unreachable, neither PSRAM-relevant; `kb.py validate` 0
problems. The KB now has publishable and git-ignored local entries; KB-029 (PSRAM
datasheet) is in `local-entries/`, unchanged.
**Not changed:** no RTL, firmware, package, card or VM action.

**A-122 installation (host, 2026-09-20):** `Assets/tau_settings/common/tau.rom` on the card replaced with the 1.2x-hold-free build
(`448a49dc...`, 155,468 B); previous ROM (`d0b5a32b...`, A-118) and the catalog indexes backed up in
`work/diagnostics/settings-ui/rom-replaced-a122/`, indexes cleared. All 14 bundle files SHA-256-identical (seed-2 RBF unchanged,
base media and every other core untouched). The Diagnostic Build is not on the card. Result pending: a long press of A must now only
pause/resume, and Speed must still work from Settings > Playback.

### A-124 — soak and coverage gates repackaged on the probe-free seed-2 RBF (host only)

**Date:** 2026-09-20
**Evidence:** host (build, package); the user reported the A-122 settings ROM tested fine on the Pocket (long-press A no longer changes
speed; user-reported, no screenshots read).
**Change:** `tools/package_sdram_cpu_diagnostic.py` gained `--rbf PATH --rbf-sha256 HASH` (audited override; output goes to a `-pf`
sibling of the profile's folder so the old bundles stay). The soak (A-097) and whole-window coverage (A-100) ROMs were rebuilt with
`fw/build.sh sdram-cpu-soak` / `sdram-cpu-full` and are **byte-identical** to the ones that passed on Pocket (soak `3a4cfbbd...0f0a`,
10,400 B; full `6a7567d1...044f`, 7,852 B), so only the bitstream differs. Packaged with the seed-2 RBF (raw `551e5a76...718b`,
bit-reversed `cb15310a...a3c9`):
`work/diagnostics/sdram-cpu-probe-a097-pf/pocket` (core `alfatreze.TAU_SDRAM_PRB97`, platform `tau_sdram_prb97`) and
`work/diagnostics/sdram-cpu-probe-a100-pf/pocket` (core `alfatreze.TAU_SDRAM_PRB100`, platform `tau_sdram_p100`). Not installed.
**Run plan (same acceptance as A-097/A-100):** coverage: run once (seconds to a minute), 52 address-line checks and three 1 MiB CRC
rounds with 0 failures, worst access about 360 cycles, draw stalls 0; soak: leave 30-60 min, expect 0 failures over hundreds of millions
of checks; Quit the core afterwards so APF writes `interact_persist.json`; decode with `tools/decode_tau_diag_log.py --interact --full`
(coverage) and `--interact --soak`.

**A-124 installation (host, 2026-09-20):** `alfatreze.TAU_SDRAM_PRB97` (soak, platform `tau_sdram_prb97`) and
`alfatreze.TAU_SDRAM_PRB100` (coverage, platform `tau_sdram_p100`) installed on the card beside the other cores, nothing removed. All 13 card
files of each bundle SHA-256-identical to the packaged copy (the `INSTALL.txt` and `SHA256SUMS.txt` in each bundle are documentation and
were deliberately not copied). ROMs `3a4cfbbd...` / `6a7567d1...`, bit-reversed seed-2 RBF `cb15310a...`. Catalog indexes backed up in
`work/diagnostics/sdram-cpu-probe-a097-pf/card-backup-2026-09-20/System/` and cleared. Results pending.

### A-125 — Diagnostic Build, Phase 2: on-demand tests (firmware built and packaged, not installed)

**Date:** 2026-09-20
**Evidence:** host (build, link map, fixture, `make test-host`); no Pocket run.
**Scope (owner: "do phase 2"):** `TAU_DIAG_TESTS` (Diagnostic Build only): **Diagnostics > Tests** page, A runs the highlighted test and its
result replaces the value in the row. Tests run in the menu, are short and blocking (well under a few ms), use the CPU window only at a
**scratch area at physical 8 MiB** (never the playlist buffers at 1 MiB+13 KiB, the framebuffer, or the card), and refuse with `NO WINDOW`
unless the window check of the SDRAM playlist passed.
- **WINDOW TEST:** 64 words written then read back-to-back (the A-093 bug returned the previous beat), a byte+byte+halfword sub-word merge
  check, and one distinct address per address line 2..25 (base 8 MiB plus each single bit; write all, then read all: aliasing shows as a
  mismatch). 89 checks; `PASS 89` or `FAIL n OF 89`.
- **READ CYCLES / WRITE CYCLES:** net cycles per window access (cycles() overhead subtracted), min/avg/max over 256; expect about 48-50
  average and a maximum near the known 360-370 bound.
- **PLAYLIST CHECK:** `pl_order` is a permutation of 0..count-1, every `pl_off` is inside the parsed text at a non-comment named entry and
  increasing, and the parsed text still hashes to what it did at load (hash and length are captured in `pl_load()` after parsing, Diagnostic
  Build only): `PASS <tracks>` or `FAIL <n>`.
- **CLEAR COUNTERS:** the Info page's UNDERRUNS and DRAW STALL then count from now (base offsets), so restart artefacts do not pollute a run.
**Dropped, with reason:** the planned **SD read speed** test. It needs a safe read path while the audio ring is in use, and the load timings
on the Info page already give the SD-bound numbers; it belongs with Phase 3 or with playback stopped. **Save report** stays deferred
(4 free persist words).
**Builds:** Diagnostic Build ROM `9580c8e9...` (158,356 B; heap gap 10,288 B, above its 4 KiB floor; the tests cost about 3 KiB).
Release-style settings ROM `bd6a7700...` (155,476 B, +8 B: the unreachable Tests page tables), heap gap 13,296 B; it differs from the ROM on
the card (`448a49dc...`) only by those tables. Product `661c5936...` and SDRAM-playlist `82fdb70c...` unchanged. Diagnostic bundle repackaged with
the seed-2 RBF (`work/diagnostics/diagnostic-build/pocket`); not installed. New fixture `settings-tests` (45 fixtures), rendered and inspected.
**To validate on Pocket:** WINDOW TEST must read `PASS 89`; READ/WRITE CYCLES near 48/50/360-370; PLAYLIST CHECK `PASS <tracks>` with a playlist
loaded (and `NO PLAYLIST` without); CLEAR COUNTERS then zeroes the Info counters. Compare against the standalone coverage/soak cores on the same RBF
before trusting the menu versions alone.

**A-125 installation and card cleanup (host, 2026-09-20):** `alfatreze.TAU_DIAGNOSTIC` / `tau_diagnostic` ("TAU Diagnostic Build") installed on the card:
14 bundle files SHA-256-identical (ROM `9580c8e9...`, seed-2 RBF `cb15310a...`), media (the `Nausicaa OST` folder and `playlist.m3u`)
copied, not moved, from the base TAU and diffed identical. Removed as superseded Phase 1 builds, each backed up and diffed identical first under
`work/diagnostics/diagnostic-build/card-removed-2026-09-20/` (Cores, Assets, Platforms, Settings): `TAU_SDRAM_STRESS` (Phase 1 mailbox stress, replaced by the
window stress core) and `TAU_SDRAM_DIAG` (Phase 1 mailbox diagnostic, replaced by the CPU-window probes). Kept: base `TAU`, `TAU_SETTINGS`, `TAU_SDRAM_WSTRESS`,
`TAU_SDRAM_PRB97` and `TAU_SDRAM_PRB100` (gate runs pending on the seed-2 RBF); the latter three are to be retired once Phase 3 of the Diagnostic Build
reproduces them. Six alfatreze cores remain. Catalog indexes backed up and cleared. Result pending.

### A-126 — Diagnostic Build Phase 2 tests on Pocket: all pass

**Date:** 2026-09-20
**Evidence:** Pocket (1 screenshot, `work/diagnostics/diagnostic-build/screenshots/20260920_173026.png`, card clock 17:30; the user reported the quick
tests done). Diagnostic Build (`TAU_DIAGNOSTIC`, ROM `9580c8e9...`, probe-free seed-2 RBF).
**Result (Tests page, all five rows run):** WINDOW TEST `PASS 89` (all 89 checks: back-to-back read-back, sub-word merge, address lines 2..25);
READ CYCLES `48/56/330` (min/avg/max, net); WRITE CYCLES `31/38/313`; PLAYLIST CHECK `PASS 13` (13 tracks: permutation, offsets and parsed-text hash);
CLEAR COUNTERS `DONE`. No error, no FAIL, no `NO WINDOW`.
**Reading:** on the probe-free RBF the window passes the same class of checks as the standalone matrix and coverage probes (A-093, A-100), and the in-menu
figures agree with the earlier probes: minimum 48 cycles (A-094: about 48-50), a maximum of 330 under the menu's own concurrent scanout (inside the 360-373
idle bound of A-094/A-100/A-102); the write average (38) is lower than the read average (56), as a posted write would be. The playlist in SDRAM passes its
integrity check after real use. The menu versions now agree with what the standalone gates established, which was the condition for trusting them.
**Not yet done:** the standalone gate cores on this RBF (coverage A-100, soak A-097) and the clean 1.0x stress run; the Diagnostic Build's Info and Tests
under longer playback; Phase 3 (stress pump, timed soak).

### A-127 — Diagnostic Build, Phase 3: in-menu stress pump and timed soak (firmware built and packaged, not installed)

**Date:** 2026-09-20
**Evidence:** host (build, link map, fixtures, `make test-host`); no Pocket run.
**Approach:** reuse the pump that produced the A-102 evidence instead of writing a new one. The Diagnostic Build now also compiles
`TAU_SDRAM_STRESS`, `TAU_SDRAM_STRESS_WINDOW` and `TAU_STRESS_HUD` (the same code and counters E/L/M/S/R/K/P as the standalone stress core),
driven from a menu instead of the Select+X chord (the chord is compiled out where the settings menu exists, so it cannot start the pump by
accident).
**Menu (Diagnostics > Stress):** `LEVEL` (choice list: OFF, R1 16k ops/s paced, R2 8-op bursts, R3 32-op bursts; A selects, applies at once),
`SOAK` (choice list: OFF, 5, 15, 30, 60 min) and `STATUS` (live page, rows repaint once a second): level, state (stopped / running / failed),
passes, operations, failures, early and late underruns, worst window access (cycles), draw stall (ms), achieved rate, and soak time left or
PASS/FAIL. The player screen also shows the familiar HUD line at the bottom while the pump exists. New `stress_set_level()` sets an absolute
level (starting from off zeroes the counters as `stress_toggle()` does; changing between running levels keeps them).
**Soak:** starts the pump (the chosen level, else R2) for N minutes beside normal playback, then stops it; the verdict is PASS when there was no
data mismatch or timeout, FAIL otherwise (a failure ends the soak at once). Late underruns are shown and counted but are not part of the verdict
(at R3 they can be CPU starvation, A-102). Changing the level by hand cancels a running soak.
**Safety change:** with the playlist in SDRAM the pump's region moved from physical 1-2 MiB (where the playlist buffers now live) to **2-3 MiB**
(`STRESS_BASE 0x00100000`, `STRESS_LAST 0x0017FFFE` in 16-bit word units); the Tests page scratch area stays at 8 MiB. The window preflight
(mailbox write, window read) still gates every start.
**Builds:** Diagnostic Build ROM `580bb638...` (164,164 B; **heap gap 4,384 B, only 288 B above its 4 KiB build floor**: Phase 3 cost about 5.8 KiB;
no room for more diagnostic code without first freeing memory or lowering the floor). Release-style settings ROM `bc26aeba...` (155,684 B, heap gap
13,088 B; it changed again only by the unreachable stress/soak table entries). Product `661c5936...` and SDRAM-playlist `82fdb70c...` unchanged. Bundle
`work/diagnostics/diagnostic-build/pocket` repackaged with the seed-2 RBF; not installed. Five new fixtures (`settings-stress`,
`settings-stress-level`, `settings-soak`, `settings-stress-status`; 49 in total), rendered and inspected.
**To validate on Pocket:** (1) Stress > Level R1 then R2 then R3 while a track plays: status shows RUNNING, operations grow, failures 0, worst access about
365-375, rate near 4k/15k/high; HUD line agrees; (2) playlist still intact afterwards (Tests > Playlist check `PASS 13`), which proves the region move; (3) a 5
minute soak ends with PASS and the pump stopped; (4) cancelling with OFF stops it; (5) compare the counters with the standalone stress core's on the same RBF
before retiring it.

### B-004 — PSRAM diagnostic core drafted: RTL, firmware, packager, decoder, tests (simulation only; not built or installed)

**Date:** 2026-09-20
**Evidence:** simulation and host tests only (`make test` exits 0, 25 PASSED suites); no Quartus, no card, no Pocket.
**Decision/change:** P2 of `docs/PSRAM_IMPLEMENTATION_PLAN.md`.
- **RTL:** `tau_psram_probe.sv` (mailbox MMIO 0x88-0xA8 + `tau_psram_async` + both CRAM pin groups) behind
  `TAU_PSRAM_PROBE`; `mp3_soc` gains an inert expansion-MMIO port (`xm_*`, read data only used inside the window);
  `core_game.vh` instantiates the probe under the macro; `core_top.v` keeps the idle tie-offs only when the macro is off.
  `CORE_VERSION` not bumped (additive, inert without the macro). Register map and the shared MMIO table:
  `docs/MMIO_ALLOCATION.md` (this also closes roadmap Phase B4's table).
- **Firmware:** `fw/psram_diag.c`, `fw/build.sh psram-diag` (8,080 B, SHA-256 `b43bdbba...ac4bc`) and `psram-diag-sim`
  (64-word fill). Per die: address lines, byte lanes, 1 MiB hash fill + CRC, anchors incl. last legal word, guard refusal;
  A = default timing, B = slow dials (+3/+3). Screen evidence and 16 `interact.json` words.
- **Packager:** `tools/package_psram_diagnostic.py`, no default RBF (`--rbf` + `--rbf-sha256` mandatory), identity
  `alfatreze.TAU_PSRAM` / `tau_psram`, ROM slot only. Exercised only with a throwaway RBF in the scratchpad (wrong hash refused).
- **Decoder:** `decode_tau_diag_log.py --interact --psram` recomputes each die's expected CRC from the deterministic fill.
- **Controller change from the review below:** per-chip DQ pad registers (mux after the register) and `T_ACC` 8 -> 9.
**Tests added (all in `make test`):** `test-rtl-psram-probe` (mailbox: ID, hold-off ignores REQ, all dies, lanes, LAST/RDATA
held, guard flags and CLR, dials, op counter; run also with `WATCHDOG=8` to prove TIMEOUT latches);
`test-rtl-psram-fw` (real VexRiscv running the ROM against `mp3_soc`, the probe and the strict chip model, run A then run B by
key press, both records decoded independently: 384 checks, 0 failures, 760/1520 ops, guard hit set, all four CRCs equal the
Python-recomputed values; the chip-side count reconciles: 752 chip ops per run = 760 minus 8 refused guard ops, two 16-bit
ops each); the same run with one injected bit flip on chip 1 die 0 word 10 is reported on CPU die 2 only, with a CRC mismatch
on die 2 only; `sim/test_psram_decode.py` (good record, bad magic/checksum, wrong CRC, missing guard hit, CE conflict,
timeouts); `tools/check_psram_idle.py` now also checks the macro-on branch (no double drivers, every CRAM pin connected).
Icarus cannot elaborate `mp3_soc` as written (use before declaration of the MMIO constants, already true at HEAD; Quartus
accepts it), so the test builds a sim copy with `sim/make_soc_sim.py`; the synthesised source is untouched.
**Finding (design review during P2):** the B-002 margins are edge-to-edge and ignore the FPGA's own output delay, trace and
input path (about 8-15 ns, an estimate; the SDC has no CRAM constraints, same as the SDRAM pins, so Quartus will not report
it). Under the safe `tAADV`-from-ADV#-rising reading, `T_ACC` = 8 left about 13 ns before those delays, i.e. roughly 0-5 ns
after. The chip model gained a pad-delay parameter (`T_IO`): with 15 ns, `T_ACC` = 9 passes and `T_ACC` = 8 fails (added as a
killed mutant). Default is now 9 (read 26 clocks, write 22 per 32-bit word in simulation).
**Not established:** anything about hardware. Quartus fit and timing, the I/O-cell packing of the pad registers, the real pad
delay, `tAADV` origin, defaults-only operation (BCR/RCR caption question) and board behaviour are all still open.
**Next (needs approval):** stage a fresh ext4 snapshot on the VM, append `TAU_PSRAM_PROBE=1` (optional `SEED n`) to
`src/fpga/ap_core.qsf`, `make check-fpga`, detached `make fpga`; two seeds fit the 4 vCPUs (about 50-55 min). Require 0
timing failures, RAM blocks 300/308, and a look at the CRAM pin timing in the report. Then hash-lock the RBF here, package,
review, and only then install (cache backup, SHA-256 verify) and run the P3 matrix.


**A-127 installation (host, 2026-09-20):** `Assets/tau_diagnostic/common/tau.rom` on the card replaced with the Phase 3 build (`580bb638...`, 164,164 B);
previous ROM (`9580c8e9...`, Phase 2) and the catalog indexes backed up in `work/diagnostics/diagnostic-build/rom-replaced-a127/`, indexes cleared.
All 14 bundle files SHA-256-identical (seed-2 RBF unchanged), media and every other core untouched.
**Cleanup decision:** no core was removed this time. The standalone soak (`TAU_SDRAM_PRB97`), coverage (`TAU_SDRAM_PRB100`) and window-stress
(`TAU_SDRAM_WSTRESS`) cores are still the only validated reference for the gates on the seed-2 RBF (their runs are pending) and the menu versions of
soak and stress are unproven on hardware; the 1 MiB CRC-under-drawing part of coverage has no menu equivalent yet. They are to be removed once the gates have
passed and the Diagnostic Build's stress/soak agree with them. `TAU` (base), `TAU_SETTINGS` (release-style) and `TAU_DIAGNOSTIC` stay. Result pending.

### B-005 — PSRAM diagnostic RBF: two-seed Quartus builds launched (result pending)

**Date:** 2026-09-20
**Evidence:** VM stage and launch only; no result yet.
**Approval:** the owner approved the launch in chat ("go ahead and launch the Quartus build") after B-004.
**Change to the tree:** `src/fpga/ap_core.qsf` now lists `core/tau_psram_async.sv` and `core/tau_psram_probe.sv` (unused,
so harmless, when `TAU_PSRAM_PROBE` is off; `tau_psram_bus.sv` is deliberately not listed until P4).
**Stage:** two fresh ext4 snapshots streamed from the local tree (255 files, source only; no toolchain, work, .git, .claude,
docs/vendor or Quartus db), `/home/taualpha/tau-local/psram-probe-b004-s1-20260920` and `...-s2-20260920`. Source hashes of all
45 `src/` files were compared with the local tree: identical except `ap_core.qsf`, which has the intended appended lines
`VERILOG_MACRO "TAU_PSRAM_PROBE=1"` and `SEED 1` / `SEED 2` (on top of the existing `USE_SDRAM=1`; no `TAU_PHASE2_*` macro,
so this is the legacy SDRAM path plus the PSRAM probe). `make check-fpga` ok on both.
**Launch:** 2026-09-20 18:22 WEST, detached (`setsid nohup make fpga`), logs `quartus-b004-s1.log` and `quartus-b004-s2.log`
in each stage. The VM was idle (load 0.00; only the harmless stale A-067 shell). Two parallel builds fit the 4 vCPUs (about
50-55 min each, A-114).
**Acceptance (pre-set, same as B-004):** "Successful", 0 timing failures (setup and hold, all corners), RAM blocks stay 300/308,
no new M10K, and the CRAM pin timing read from the report (the SDC has no CRAM constraints, so Quartus will not flag it; look
at the I/O register packing of `dq0_q`/`dq1_q` and the output flops instead). Pick a seed by the larger worst-case slack.
**Next:** on success copy the RBF to `work/diagnostics/psram-diag/fpga`, record its SHA-256 here, package with
`tools/package_psram_diagnostic.py --rbf ... --rbf-sha256 ...`. Installing on the card is a separate step (needs approval).

### B-006 — PSRAM diagnostic RBF: both seeds fit and meet timing; CRAM pad registers NOT packed into I/O cells (bundle packaged, not installed)

**Date:** 2026-09-20
**Evidence:** Quartus on the VM (two seeds, `TAU_PSRAM_PROBE=1` on top of `USE_SDRAM=1`, no `TAU_PHASE2_*`); host packaging. No Pocket run.
**Result (B-005 acceptance):** both builds finished "Successful", 0 errors, 321 warnings, 52m16s (seed 1) and 51m16s (seed 2).
Synthesis (6m) already showed the probe instantiated at `u_psram`; block memory bits 2,380,928 and DSP blocks 11 are unchanged, so the
probe added no RAM.
| | Seed 1 | Seed 2 |
|---|---:|---:|
| ALMs | 6,238 / 18,480 | 6,247 / 18,480 |
| Registers | 8,136 | 8,085 |
| RAM blocks | 300 / 308 | 300 / 308 |
| DSP blocks | 11 / 66 | 11 / 66 |
| Negative-slack entries | 0 | 0 |
| Worst setup (Slow 0C) | +0.385 ns | **+0.642 ns** |
| Worst hold (Fast 0C, clk_sys group) | +0.033 ns | **+0.068 ns** |
| Worst min pulse width | +0.833 ns | +0.833 ns |
| Raw RBF SHA-256 | `9c3a2660...daf2f` | `142cd354...9de8` |
Seed 2 chosen by the pre-set rule (larger worst-case slack on both). Compared with the SDRAM candidates (A-114: setup +0.158/+0.664,
hold +0.124) the hold margin is thinner (+0.068), all still positive. Registers rose about 700 against the no-window build (7,311 at A-110);
mailbox, controller and monitor account for it.
**Finding (the check set in B-004): the CRAM pad registers were not packed into I/O cells.** The fitter's pin table for seed 2 shows
`Input Register = no`, `Output Register = no`, `Output Enable Register = no` for every `cram*_dq` and for the control outputs
(`ce0_n`, `ce1_n`, `oe_n`, `we_n`, `adv_n`, `lb_n`, `a[..]`); `cram*_dq` has combinational fan-out 1 and registered fan-out 0. With no
timing constraints on those pins and no `FAST_*_REGISTER` assignments, the flops sit in the fabric, so the pad delay is set by routing and
is neither reported nor controlled. The B-004 read-sample default (`T_ACC` = 9, about 30 ns before pad delays) was chosen with that in mind,
but the real delay is unknown and could be larger than the 8-15 ns estimate.
**Consequence and options (not yet decided):** (a) use this bundle for P3 as is; the slow dial (+3/+3) and default give two operating points,
a pass says the margin is adequate, a failure could be timing skew or a real fault; (b) rebuild with `FAST_OUTPUT_REGISTER`,
`FAST_INPUT_REGISTER` and `FAST_OUTPUT_ENABLE_REGISTER` on the CRAM pins (2 qsf lines per group, no RTL change, another 50 min), which makes
the pad delay short and repeatable and is what a product path would use. Recommendation: (b) before P3, since P3 is the evidence P4 rests on.
**Other warnings from the PSRAM sources:** three 32-to-8-bit truncations of small constants in `tau_psram_async.sv` (lines 93-96),
harmless; the four expected `cram*_clk`/`cre` stuck-at-ground notes (async mode).
**Artifacts:** `work/diagnostics/psram-diag/fpga/ap_core.rbf` (seed 2), `fpga-s1/ap_core.rbf`, `reports-s2/` (fit/sta/map reports);
bundle `work/diagnostics/psram-diag/pocket` built with `tools/package_psram_diagnostic.py --rbf-sha256 142cd354...9de8 --rom-sha256 b43bdbba...ac4bc`;
`SHA256SUMS.txt`: packaged `bitstream.rbf_r` `ec68b436c3cecddc0de31c07f04fc01a5006891db53804a1c22b5043f9265ad2`, ROM `b43bdbba...` (8,080 B), and
the reversal was checked independently (`rbf_r` equals the bit-reversed RBF, 1,833,572 bytes). NOT installed on the card.
**Next (needs a decision/approval):** choose (a) or (b); then install (cache backup, SHA-256 verify) and run the P3 matrix (5 cold, 5 warm,
plus slow runs); quit the core before removing the card, decode with `--interact --psram`.

### B-007 — PSRAM diagnostic RBF rebuild with I/O-cell register packing (launched, result pending)

**Date:** 2026-09-20
**Evidence:** simulation, VM stage and launch only; no result yet.
**Approval:** the owner chose option (b) of B-006 in chat ("rebuild with the fast I/O register assignments").
**Why:** B-006 found that no CRAM register was packed into an I/O cell (Input/Output/Output-Enable Register = no), leaving the pad delay
to fabric routing.
**RTL change (needed for packing, not only the qsf):** a register can be packed into an I/O cell only if it drives nothing but its pad.
Two monitors read the pin registers back, which would have blocked packing: the CE-conflict / OE-WE-clash check in
`tau_psram_async.sv` and the WAIT sampler in `tau_psram_probe.sv`. The controller now monitors the same next-pin values with its own
register (`mon_bad`) and exports separate taps (`oe_act`, `sel_chip`); the probe samples WAIT from those. The pin registers keep exactly
one load. The monitor sees what the pins will do one clock later, instead of the pins themselves. A new controller-test step forces
`mon_bad_d` and checks the sticky flag latches and clears (the monitor was previously never exercised). `make test` exits 0 with all
PSRAM suites, the real-CPU firmware sim, its injected fault, and the six mutants killed.
**Build config:** `tools/psram_probe_qsf_append.txt` (new, in the repo) is appended to the STAGED `ap_core.qsf` only:
`TAU_PSRAM_PROBE=1` plus `FAST_OUTPUT_REGISTER` on `cram{0,1}_dq[*]`, `a[*]`, `adv_n`, `ce0_n`, `ce1_n`, `oe_n`, `we_n`, `ub_n`, `lb_n`,
`FAST_INPUT_REGISTER` and `FAST_OUTPUT_ENABLE_REGISTER` on `cram{0,1}_dq[*]` (22 lines), then `SEED 1` / `SEED 2`. Product builds are unchanged.
**Stage:** `/home/taualpha/tau-local/psram-probe-b007-s1-20260920` and `...-s2-...` (256 files); all `src/` files identical to the local
tree except `ap_core.qsf`; `make check-fpga` ok. VM idle at launch (load 0.01, no Quartus stages, 16 GB free).
**Launch:** 2026-09-20 19:38 WEST, detached, logs `quartus-b007-s1.log` / `-s2.log`.
**Acceptance:** "Successful", 0 timing failures, RAM blocks 300/308; and now the packing itself: `Input Register`, `Output Register`
and `Output Enable Register` = yes for `cram*_dq`, and `Output Register` = yes for the control outputs and `a[..]`. If any read "no", record
which and why before doing anything else. Compare slacks with B-006 (seed 2: setup +0.642, hold +0.068).

### B-008 — B-007 result: I/O packing partly worked (reads packed, write data and address not); both seeds meet timing; bundle repackaged, not installed

**Date:** 2026-09-20
**Evidence:** Quartus fit reports for two seeds; host packaging. No Pocket run. (Wall-clock build time 1h51m in the log includes about an hour
where the VM was suspended; VM uptime shows about 50 min of real work, in line with B-006.)
**Timing and resources (0 errors, no negative slack, both seeds):**
| | Seed 1 | Seed 2 |
|---|---:|---:|
| ALMs | 6,240 | 6,236 |
| Registers | 8,169 | 8,130 |
| RAM blocks / DSP | 300 / 308, 11 | 300 / 308, 11 |
| Worst setup (Slow 0C) | +0.482 ns | **+1.012 ns** |
| Worst hold (Fast 0C) | +0.083 ns | **+0.121 ns** |
| Raw RBF SHA-256 | `27e588b3...c44a` | `8e9d9c16...873d` |
Seed 2 chosen (larger slack on both). Better than the B-006 seed 2 (+0.642 / +0.068) and close to the SDRAM candidates' hold (+0.124).
**Packing (the acceptance item), identical on both seeds:**
- `cram*_dq` (32 pins): `Input Register` **yes**, `Output Enable Register` **yes**, `Output Register` **no**.
- Control outputs `adv_n`, `ce0_n`, `ce1_n`, `oe_n`, `we_n`, `ub_n`, `lb_n` (14 pins): `Output Register` **yes**.
- `cram*_a[21:16]` (12 pins): `Output Register` **no**. (`cram*_clk` and `cram*_cre` are constants; not applicable.)
- Cause, from 44 fitter warnings (176279): `dq_out0/1[15:0]` and `cram0/1_a[5:0]` "cannot simultaneously use clear and load signals". An I/O-cell
  register takes one synchronous control; those registers have both a reset/idle clear and a data mux.
**Reading:** the read path, which is the timing-critical one (tAADV/tOE), now has its DQ input flop and its OE#/ADV#/CE# outputs in I/O cells.
The unpacked paths are write data and the address phase, whose datasheet margins are at least 28 ns (tAVS 5 ns vs 33 ns of ADV# low; tDW 20 ns
vs at least 50 ns), so a few ns of extra routing there is not a concern for P3. Decision recorded: do NOT spend another build on it now.
**Known fix, deferred to the next RTL change (P4):** make `dq_out` and `cram*_a` plain registers (`dq_out <= dq_val;`, `cram_a <= addr_hi;`, no
reset and no zero-mask; the address and data pins are don't-care while CE# is high and DQ is released), then check `Output Register = yes`.
**Artifacts:** `work/diagnostics/psram-diag/fpga/ap_core.rbf` (B-007 seed 2), `fpga-b007-s1/`, `reports-b007-s2/`; B-006 material moved to
`fpga-b006-s1|s2/`, `reports-b006-s2/`, `pocket-b006-unpacked/` (superseded, do not install). New bundle `work/diagnostics/psram-diag/pocket`:
raw RBF `8e9d9c1653cc630a83038d9308033288eeba1c885832d22465a5f5621332873d`, packaged `bitstream.rbf_r`
`1d64bcfa573723dcbc2872bda6bdd3088875fe2912cf2b52eae3ffc3f1e2fec6` (checked equal to the bit-reversed RBF, 1,832,700 bytes), ROM
`b43bdbbae7b3688f08f09675a1db23954c69211fb4a4c88276aff4cc112ac4bc`. Hash-locked via `--rbf-sha256` / `--rom-sha256`. NOT installed.
**Next (needs approval):** install on the Pocket card (cache backup, SHA-256 verify, indexes cleared) and run the P3 matrix.

### B-009 — PSRAM diagnostic installed on the Pocket card (P3 pending)

**Date:** 2026-09-20
**Evidence:** host (card install and per-file SHA-256 verification). No Pocket run yet.
**Approval:** the owner asked for the install and the P3 run in chat ("install it on the card and run P3").
**Install (additive, nothing removed):** volume `Pock` (110 GB free). Backed up the five catalog indexes (`core_viewby_platform`, `corelist_cache`,
`cores_cache`, `platform_viewby_category`, `platforms_cache`) to `work/diagnostics/psram-diag/pocket-cache-backup-2026-09-20/System/` and
verified them identical to the card. Copied `Cores/alfatreze.TAU_PSRAM`, `Assets/tau_psram`, `Platforms/tau_psram.json` and
`Platforms/_images/tau_psram.bin` from the hash-locked B-008 bundle; deleted the five indexes so the Pocket rebuilds them. macOS added
`._*` metadata files; I removed only those I had created in the new paths. The other cores (TAU, TAU_SETTINGS, TAU_DIAGNOSTIC, the SDRAM
diagnostics, third-party cores) were not touched. Verification: 13 of 13 bundle files SHA-256-identical on the card, file list equal to the
bundle, bitstream `1d64bcfa...` and ROM `b43bdbba...` as recorded in B-008. Card unmounted after `sync`.
**P3 protocol given to the owner:** launch "TAU PSRAM Diagnostic" from Media Players; the ROM runs once automatically (A = default timing, B =
slow dials +3/+3). Five cold starts (Pocket powered on from off) and five warm starts (core quit and relaunched), each with one default run and one
slow run; photograph the screen after each run (a phone photo, not the Pocket screenshot combination, which the core may see as button presses,
KB-031); QUIT the core to the menu before removing the card so APF writes the last record to
`Settings/alfatreze.TAU_PSRAM/Interact/_core/interact_persist.json` (only the last quit persists, so read the card after the first cold pair and after
the last; earlier runs are evidenced by the photographs). Decode with `tools/decode_tau_diag_log.py --interact --psram <that file>`.
**Predictions (before the run, from simulation and the fill definition):**
- Screen: `PASS`, `FAIL 0`, `TO 0 CE 0 GHIT 1`; the `W` bits (WAIT seen low/high) are unknown and informational.
- Per-die CRC (1 MiB fill = 2^18 words per die): D0 `833D7446`, D1 `4BF5A918`, D2 `ECE394E1`, D3 `8B21EF3F`, on default and slow runs alike.
- CHECKS `1048704` (262,176 per die), OPS `2097400` for the first run after power-up (the counter is cumulative; the slow run adds the same again).
- A run takes on the order of seconds (about 2.1 M controller ops at roughly 26-60 clocks each).
- Not predicted: whether defaults-only operation works on real silicon (BCR/RCR question), or whether the read-sample margin is adequate under real pad delay.
**How to read a failure:** a data mismatch on one die with CRC differing points at that die; mismatches everywhere, or all reads returning one value,
point at read timing or the power-up state (compare the slow run: if slow passes and default fails, it is a read-margin problem); `TO 1` means the
controller never completed a request; `CE 1` is a controller bug; `GHIT 0` means the guard did not fire. Do not power off; photograph first.

### B-010 — PSRAM diagnostic on the Pocket, P3 start C1: PASS on default and slow timing, saved record agrees

**Date:** 2026-09-20
**Evidence:** Pocket (two phone photos of the screen, and the persisted `interact_persist.json` read from the card, decoded independently). Archived in
`work/diagnostics/psram-diag/p3/c1/`. The owner reports this start as C1 (cold); the cold/warm state is as reported, not independently verified.
**Screen, run 1 (default timing):** D0..D3 PASS 0, CRCs `833D7446` / `4BF5A918` / `ECE394E1` / `8B21EF3F`, `CHECKS 1048704 FAIL 0`,
`TO 0 CE 0 GHIT 1 W 01`, `LAST 0F7FFFFE RD C0DE0303`, `OPS 2097400`, verdict PASS.
**Screen, run 2 (slow +3/+3):** identical CRCs and checks, `OPS 4194800` (cumulative, exactly double), PASS.
**Saved record (after Quit):** decodes with a valid checksum to run 2, slow dials, verdict PASS, 1,048,704 checks, 0 failures, all four per-die CRCs equal the
value recomputed in Python from the deterministic fill, controller ops 4,194,800, no timeout, no CE conflict, guard hit set, `WAIT` seen high and never low
(status byte `0xA2`). Screen and record agree on every field.
**All predictions in B-009 held exactly:** PASS, the four CRCs, `CHECKS 1048704`, `OPS 2097400`, `TO 0 CE 0 GHIT 1`. `LAST 0F7FFFFE` is the last check of the run
(read of die 3's last legal word, offset 0x1FFFFE) and returned the expected anchor `C0DE0303`.
**Established (for this unit, at room temperature, one start):**
1. Defaults-only operation works: no register write was ever issued, and all four dies read and write correctly. The BCR/RCR question from the datasheet
   caption (B-002) is answered for this part in practice: async on power-up defaults is enough.
2. The read path at the shipped default (`T_ACC` = 9, B-008 packing) is correct over 1,048,704 checks per run and about 2.1 M controller operations per run, including
   both chips and both dies of each, byte lanes, all address lines, and a full 1 MiB hash-verified fill per die.
3. The guard word was refused with the sticky flag set and no data disturbed (no die failure, last legal word intact).
4. `WAIT` read high whenever it was sampled during an OE#-low phase and was never seen low: consistent with the datasheet's "ignore WAIT in async mode".
**Not established:** repeatability (1 start of 10), cold versus warm, whether the two default and slow passes differ in margin (both passed, so the slow dial says
nothing about how close the default is to failing; it only adds cycles), and the actual read-timing margin under real pad delay. Reads pass at 9; whether they
would at 8 or 7 is unknown and cannot be tested with this build (the dial only lengthens).
**Next:** starts C2-C5, W1-W5 per the P3 protocol in B-009 (W5 ends with a third, default run and a Quit), then the final card read. Any FAIL, TO 1, CE 1
or GHIT 0 stops the sequence.

### B-011 — PSRAM diagnostic P3 complete: 10 of 10 starts PASS on default and slow timing, zero errors; P3 exit gate met

**Date:** 2026-09-20
**Evidence:** Pocket. 19 phone photos of the result screen (archived `work/diagnostics/psram-diag/p3/photos/`, plus the two C1 photos in B-010) and the last persisted
`interact_persist.json` (`p3/final/`), decoded independently. Cold/warm labelling of the starts is as reported by the owner.
**Sequence (B-009 protocol):** C1 (B-010), then C2-C5 and W1-W5, each with an automatic default run and a slow (+3/+3) run; W5 ended with a third default run and a Quit.
**Screens:** all 19 new photos show PASS with `FAIL 0`, `TO 0 CE 0 GHIT 1 W 01`, `LAST 0F7FFFFE RD C0DE0303`, `CHECKS 1048704` and the four expected CRCs
(`833D7446`, `4BF5A918`, `ECE394E1`, `8B21EF3F`). Counts: 9 default-run screens (`RUN 1`, `OPS 2097400`), 9 slow-run screens (`RUN 2 SLOW`, `OPS 4194800`) and 1
`RUN 3 DEFAULT` screen (`OPS 6292200`), exactly the set expected for nine further starts plus the extra run. The `OPS` counter reading 2,097,400 at every first run shows the
counter, and so the core, was reloaded at the start of each session. Together with B-010, that is 10 starts, 20 runs plus one, no failure.
**Saved record (last Quit, after W5's run 3):** valid checksum, PASS, run 3, default timing, 1,048,704 checks, 0 failures, all four die CRCs equal the Python-recomputed
values, 6,292,200 controller ops (3 x 2,097,400), no timeout, no CE conflict, guard hit set, WAIT seen high only. It agrees with the W5 photo. With B-010's record
(slow run) there is one persisted record of each timing, both matching the screen.
**Card:** after the sequence, the 13 installed files were still byte-identical to the B-008 bundle. No new Pocket screenshots exist on the card (the owner used phone photos).
**P3 exit gate (evaluation plan): "10/10 complete Pocket runs with zero errors and matching persisted results": met.** It covers bounded diagnostic access only
(not cacheability, audio concurrency or product use), as the plan says.
**Limits of this evidence:** the photos carry no timestamps and the screens are identical, so I verified content and counts (9/9/1), not that each photo belongs to a
distinct start; cold versus warm is as reported. The slow dial only lengthens cycles, so 20 passes say nothing about how much read margin remains at the shipped default
(`T_ACC` = 9). One unit, room temperature, one bitstream (B-007 seed 2).
**Decision points from the plan:** no stop condition triggered (no CE overlap, no guard side effect, no stale response, no timing failure, no mismatch or timeout,
no regression). P4 (uncached CPU window) is now allowed by the plan; it needs its own approval and a new Quartus build.
**Suggested before P4 (owner to decide):** (1) a margin experiment: two parallel builds of this diagnostic with `T_ACC` = 8 and 7 (same seed), about 55 min, plus two
card runs, to learn how much timing slack exists and whether the product can run faster; (2) fold the deferred packing fix (plain `dq_out` / `cram_a` registers) into that
build or into P4's.

### B-012 — PSRAM read-timing margin experiment: two builds launched (read-sample index 7 and 6), result pending

**Date:** 2026-09-20
**Evidence:** simulation (model predictions), VM stage and launch only; no hardware result yet.
**Approval:** the owner chose the margin experiment first ("run the margin experiment first", after B-011).
**Question:** P3 passed at the shipped read-sample index `T_ACC` = 9 but the slow dial only lengthens reads, so it cannot show how much margin exists. The
experiment shortens the sample and finds where reads start to fail.
**Change from the option described in B-011:** two builds at index 7 and 6, not 7 and 8, plus finer firmware dials, because the dials reach further up:
key X = read +1, Y = read +2, B = +3/+3 (with the automatic default run that is four consecutive indices per build). Build 7 covers 7,8,9,10 and
build 6 covers 6,7,8,9, so 7, 8 and 9 are measured twice in different bitstreams (a check on build-to-build placement variation) and the boundary is
resolved to one clock (16.7 ns).
**RTL and firmware changes:** `tau_psram_probe.sv` takes the index from a macro `PSRAM_T_ACC` (default 9, so all other builds are unchanged) and returns it in
`PS_CFG[15:8]` (read-only); the ROM shows `IDX n` (= build index + read extra) and records the build index and read extra in the interact record (word 4
[23:16], word 1 [19:17]); the decoder reports `t_acc`, `read_extra_clocks` and `sample_index`. A decoder bug found by its unit test in passing: the verdict
treated any word-4 bits above bit 7 as a firmware timeout, which would have judged every real record FAIL once the index was stored there; fixed to test bit 8
only. Tests: mailbox test checks the readback; the firmware-in-the-loop sim now runs four modes and checks each record's mode and index (still with the
injected-fault run); `make test` exits 0, 25 suites.
**Build config:** `tools/psram_probe_qsf_append.txt` (probe macro plus the FAST_* register assignments, as B-007) then `PSRAM_T_ACC=7` / `=6` and `SEED 2` (the
seed used in B-007/B-008), staged at `/home/taualpha/tau-local/psram-probe-b012-t7-20260920` and `...-t6-...`; all `src/` files identical to the local tree
except `ap_core.qsf`; `make check-fpga` ok; VM idle at launch (load 0.06, no Quartus stages, 15 GB free). Launched 2026-09-20 22:37 WEST, detached.
The packager gained `--variant` (identities `alfatreze.TAU_PSRAM_T7` / `_T6`, platforms `tau_psram_t7` / `_t6`) and the X/Y mappings so both builds can sit on the card.
**Predictions (before hardware).** Datasheet-conservative model (tAADV taken from ADV# rising, tOE 20 ns), firmware-in-the-loop, with 0 and 10 ns of pad delay:
| Effective sample index | 6 | 7 | 8 | 9 |
|---|---|---|---|---|
| Model (0 or 10 ns pad delay) | FAIL | FAIL | PASS | PASS |
The model is the pessimistic reading. If tAADV is instead measured from ADV# falling (not stated in the datasheet, B-002 section 5) then index 7 passes and only
6 fails (tOE 20 ns cannot be met at 6: OE# falls one clock before the sample). So: FAIL at 6 is expected either way; FAIL or PASS at 7 tells which reading is right.
**How to read the result:**
- 7 FAIL, 8 PASS: the shipped 9 has exactly one clock (16.7 ns) of margin; keep 9 (or 8 only with more evidence).
- 7 PASS, 8 PASS: tAADV runs from ADV# falling or the chip is faster; 9 has two clocks (33 ns) of margin; 8 is a candidate default.
- 8 FAIL: margin at 9 is under one clock; raise the default to 10 before P4.
- 6 PASS: unexpectedly fast silicon or much shorter pad delay; note it and test 5 if wanted.
- Disagreement between build 7 and build 6 at the same index means placement matters and more seeds are needed.
Acceptance for the builds: "Successful", 0 timing failures, RAM blocks 300/308, DQ input and output-enable registers and the control outputs packed (as B-008).


### A-128 — gates on the probe-free seed-2 RBF: coverage PASS, soak PASS, in-menu soak PASS

**Date:** 2026-09-20
**Evidence:** Pocket (5 screenshots and two persist files, copies in `work/diagnostics/gates-a128/`, card clock 21:42-22:46); results decoded with
`tools/decode_tau_diag_log.py`. Bitstream: seed-2 probe-free RBF (raw `551e5a76...718b`); ROMs byte-identical to the ones that passed A-097/A-100.
**Coverage (A-100, `TAU_SDRAM_PRB100`): PASS.** 52 address-line checks, 0 failures; 3 CRC rounds of 1 MiB under concurrent drawing, 0 block mismatches,
block-0 CRC read = written `0xD7F900C4`; worst read 359 and write 350 cycles; draw-engine stall 0; 1,572 thousand window accesses. The persisted record
(after Quit) equals the screen (`ADDR LINES 52 F 0`, `CRC ROUNDS 3 BAD 0`, `MAX RD 359 WR 350`, `STALL 0`, `ACCESSES K 1572`). Same figures as A-100 on the
old RBF (worst 360/350).
**Soak (A-097, `TAU_SDRAM_PRB97`): PASS.** 1,920 s (32:00), 746,120 passes, **279,795,000 checks, 0 failures** (0 matrix failures, 0 timeouts, 0 random-
pattern failures), pass time 0.9-3.9 ms. Screenshots at 10:00 (233,440 passes, 87,540,000 checks), 20:24 (178,338,000 checks) and 31:54 (743,824 passes,
278,934,000 checks) all read `FAILS 0` and agree with the persisted record. A-097 on the old RBF: 30:15, 264,435,000 checks, 0 failures. In two of the
soak screenshots the small phase label under the bar (`ADDRESS ALIAS`, `BYTE ENABLES`) is caught half-redrawn; it is the probe screen's own label update
(same ROM as A-097), not a data error.
**In-menu soak (Diagnostic Build, Stress > Soak, music playing):** Stress status after the run: level OFF, state STOPPED, **SOAK PASS**, 112 passes,
29,447,688 operations, **failures 0**, early underruns 6, **late underruns 0**, worst access 373 cycles, draw stall 7 ms. 29.4 M operations is about 16.4k ops/s
over 30 minutes, consistent with the R1 rate (or R2 at a similar rate); the level and duration were not photographed. The playlist check after the soak was not
photographed either.
**Reading:** the probe-free seed-2 RBF passes the whole-window coverage and the 30-minute soak with the same figures as the probe-carrying seed-4 build, and
the menu's stress/soak agrees (0 failures, worst access 373, no late underruns during real playback). Together with A-121 (stress core, no failure, strip gone),
A-114 (timing) and A-122..A-126 (settings, Info, tests on it), this is the evidence needed to **adopt seed 2 as the product window RBF**. Still open: the
clean 1.0x A-102 protocol on the standalone stress core (four tracks x R0-R3) or an equivalent in-menu run; a saturating level; FLAC; temperature.

**A-128 addendum (user, 2026-09-20):** the in-menu soak was run at **R2** (unpaced 8-operation bursts) for its full duration beside playback; the average of about 16.4k operations
per second is the pump's achieved rate on the idle CPU time at that level, a little above the 13-15k seen at R2 in A-102 (different track, cover and load). The **playlist check
after the soak was not run** (forgotten), so the proof that the relocated pump (2-3 MiB) leaves the playlist buffers alone rests on the Phase 2 check before the soak (A-126) and on the
soak itself finishing with the playlist still loaded; a Tests > Playlist check on the same session is still owed (quick, and can be run any time the Diagnostic Build has been
running the pump).

**A-128 closure (user, 2026-09-20):** the owed check was run as directed (Stress > Level R2 with music playing for a minute or two, then OFF, then Tests > Playlist
check): **`PASS 13`**. The relocated pump (physical 2-3 MiB) leaves the playlist buffers (1 MiB+13 KiB) intact under real stress. User-reported, no screenshot.

**A-128 card cleanup (host, 2026-09-20):** the standalone soak (`alfatreze.TAU_SDRAM_PRB97`, platform `tau_sdram_prb97`) and coverage (`alfatreze.TAU_SDRAM_PRB100`, platform
`tau_sdram_p100`) cores were removed from the card after their gates passed and the Diagnostic Build's soak reproduced the soak result. Each was backed up first (Cores, Assets,
Platforms and the Settings folder with its persist record) and diffed identical under `work/diagnostics/gates-a128/card-removed/`; the results themselves are in
`work/diagnostics/gates-a128/`. Catalog indexes backed up and cleared. Remaining alfatreze cores: `TAU` (base), `TAU_SETTINGS`, `TAU_DIAGNOSTIC`, `TAU_SDRAM_WSTRESS` (kept until the
four-track A-102 protocol is repeated on the seed-2 RBF), and `TAU_PSRAM`, which appeared on the card from the PSRAM session and was not touched.

### B-013 — margin experiment builds (read-sample index 7 and 6) fit and meet timing; bundles packaged, not installed

**Date:** 2026-09-20
**Evidence:** Quartus fit and timing reports; host packaging. No Pocket run yet.
**Result (B-012 acceptance):** both builds "Successful", 0 errors, 0 negative-slack entries, 58m46s each, RAM blocks 300/308, DSP 11.
| | T_ACC = 7 | T_ACC = 6 |
|---|---:|---:|
| ALMs / registers | 6,237 / 8,151 | 6,244 / 8,130 |
| Worst setup (Slow 0C) | +0.693 ns | +0.719 ns |
| Worst hold (Fast 0C) | +0.100 ns | +0.108 ns |
| Raw RBF SHA-256 | `ae01ae5c...6b43` | `e363c5f7...d1a7` |
The synthesis reports show the controller parameter `T_ACC` = 7 and 6 respectively (9 in B-007). Packing is identical to B-008 in both: DQ `Input Register` and
`Output Enable Register` yes for 32 of 32, all 14 control outputs `Output Register` yes; DQ output data and `cram_a` still not packed (the known clear-and-load
case, deferred). Slacks are positive and close to the B-008 seed 2 (+1.012 / +0.121); placement differs between builds, so the read path was re-fitted, which
is exactly why the same effective index is measured in both builds.
**Bundles (hash-locked, ROM `9e1e65a4f4cafacf51a033e89ec48198f73923ae3832ddf645e4199e467f884b`, 8,404 B, with the X/Y modes and index display):**
`work/diagnostics/psram-diag/pocket-t7` (core `alfatreze.TAU_PSRAM_T7`, platform `tau_psram_t7`, packaged `bitstream.rbf_r` `3b9ec0f0...f323`) and
`pocket-t6` (`alfatreze.TAU_PSRAM_T6`, `tau_psram_t6`, `0471b0e0...f0cd`); both `rbf_r` files checked equal to the bit-reversed RBFs. Artifacts:
`fpga-b012-t7|t6/ap_core.rbf`, `reports-b012-t7|t6/`. The B-008 core (`alfatreze.TAU_PSRAM`, older ROM without X/Y) stays on the card untouched.
**Next (needs approval and the card in the Mac):** install both variants additively (index backup, SHA-256 verify, indexes cleared), then the card run below.
**Card run protocol (proposed):** for each of the two new cores, two starts (cold or warm, either): the automatic default run (index = build value), then X, then Y,
then B, photographing each result screen (four photos per start, 16 in all), Quit at the end of each start. Every screen shows `IDX n` (build index plus read
extra), so a photo identifies its own configuration. A FAIL screen shows the first failing test, word, expected and actual: photograph it fully. Predictions
are in B-012.


### A-129 — window stress on the probe-free seed-2 RBF, track 1 at R1/R2/R3 (Pocket PASS); stress core retired

**Date:** 2026-09-20
**Evidence:** Pocket (3 screenshots, `work/diagnostics/gates-a128/wstress-screenshots/`, card clock 23:07-23:14), standalone `TAU_SDRAM_WSTRESS` (burst-pump ROM `55384a55`, seed-2 RBF),
the shortened protocol agreed in place of the full A-102 grid: track 1 only (320 kbps, 44.1 kHz, 1400 px cover, the heaviest load), normal speed (no 1.2x indicator), no seeking.
| Time in track | Level | E | L | M | S | K (k ops/s) | Passes |
|---|---|---:|---:|---:|---:|---:|---:|
| 05:34 | R3 | 0 | 0 | 373 | 0 | 17.8 | 24 |
| 02:05 | R1 | 0 | 0 | 373 | 0 | 4.2 | 2 |
| 03:04 | R2 | 1 | 0 | 373 | 1 | 15.0 | 13 |
No `FAIL`, mismatch or timeout text; no late underrun (`L` 0); worst window access 373 cycles (the idle bound of A-094/A-100/A-102); draw stall 0 (1 ms cumulative in the R2 frame);
the R3 frame shows 24 passes (about 6.4 M operations) accumulated over roughly six minutes of uninterrupted playback. The user reported no audible problem (the earlier "poorer
audio" of A-121 was the 1.2x speed being on).
**Verdict:** PASS for the closing scope: real playback at the heaviest track and cover with the pump at R1, R2 and the saturating R3, on the probe-free seed-2 RBF, no failure. Tracks 2-4 and an
R0 baseline were not photographed; they were the lighter cases of the A-102 grid, and the 30-minute R2 in-menu soak (A-128) also passed.
**Retirement (host):** `TAU_SDRAM_WSTRESS` (platform `tau_sdram_wst`, with its test tracks) removed from the card after a verified backup (Cores, Assets, Platform files, Settings, all diffed
identical) in `work/diagnostics/gates-a128/card-removed-wstress/` (66 MB; the test music also exists in `work/test-music/`). Catalog indexes backed up and cleared. Remaining cores:
`TAU`, `TAU_SETTINGS`, `TAU_DIAGNOSTIC`, and `TAU_PSRAM` (PSRAM session, untouched). The Diagnostic Build carries the stress pump, soak and tests from now on.

### A-130 — release v0.2.0: settings + Info + SDRAM playlist on the probe-free seed-2 RBF (built and packaged in dist/, not installed)

**Date:** 2026-09-21
**Owner decisions:** version **0.2.0**; add a `release` target and keep `player` as the legacy build.
**Change:** `fw/build.sh release` builds the shipped product into `dist/`: `TAU_SETTINGS_UI=1 TAU_PL_SDRAM=1 TAU_DIAG_INFO=1` (no diagnostic tests, no stress), minimum
heap gap 8 KiB enforced. Version bumped in the three places the build cross-checks: `APP_VER "0.2.0"` (`fw/player.c`), `dist/Cores/alfatreze.TAU/core.json`
(`version 0.2.0`, `date_release 2026-09-21`) and `README.md` ("Current version **v0.2.0**"); `CHANGELOG.md` has a v0.2.0 entry. `package.py` gained an audited
`--rbf PATH --rbf-sha256 HASH` option; `python3 package.py --rbf work/diagnostics/sdram-probefree-a114/s2/ap_core.rbf --rbf-sha256 551e5a76...718b` wrote the
bit-reversed bitstream to `dist/Cores/alfatreze.TAU/bitstream.rbf_r` (`cb15310a...a3c9`, 1,828,936 B; verified equal to the reversed raw file).
**Artifacts:** `dist/Assets/tau/common/tau.rom` = release ROM `9b5d6575...` (155,684 B, 86.4% of the RAM budget, heap gap 13,088 B); `dist/Cores/alfatreze.TAU/bitstream.rbf_r` = seed-2
probe-free window RBF; `tools/check_tau_package.py` passes; `make test-host` passes (`make check` cannot run on this Mac: its build-tool probe reports the Quartus/toolchain
tools missing, as before). The release ROM is the settings-UI ROM with the version string 0.2.0, which is what ran on the Pocket in A-116..A-122 (plus the gesture change of A-122) and the
same window/playlist code that passed A-105..A-108, A-126, A-128 and A-129.
**Not done:** no card install (the base `TAU` core on the card is still the old 0.1.0 product), no full release regression on the packaged product, no version-bumped rebuild of the
`TAU_SETTINGS`/`TAU_DIAGNOSTIC` test bundles (their ROMs show 0.1.0 until rebuilt).
**Release checks to run on Pocket (base TAU after installation):** boot, playlist and settings screens, Info page (version 0.2.0, FPGA rev `4D503317`, window OK), playback of MP3 with and
without cover, seek, pause/resume, repeat/shuffle/resume across a Quit, a long-press of A (must only pause), Speed in Settings, colour and meter persistence, the large playlists (A-107 lists),
a couple of screenshots for the record.

**A-130 installation (host, 2026-09-21):** the release was installed as the base `alfatreze.TAU` core on the card: `Cores/alfatreze.TAU/bitstream.rbf_r` (`cb15310a...`, seed-2 probe-free), `core.json`
(`version 0.2.0`) and `Assets/tau/common/tau.rom` (`9b5d6575...`). The replaced 0.1.0 core (old RBF `9301546c...`, old ROM `e7643ea5...`), the core folder, and the base core's Settings
folder were backed up first in `work/diagnostics/release-v020/card-replaced/`; catalog indexes backed up and cleared. All other core files, the music (`Nausicaa OST`, `playlist.m3u`) and every other core
untouched. Verification: 12 of 13 compared dist files SHA-256-identical to the card; the one difference is `Platforms/tau.json`, which was **not** touched: the card still has the platform name
`TAU` + superscript alpha (installed earlier), `dist/` has plain `TAU` (the repo decided the Pocket cannot render the glyph, `check_tau_package.py`). Decide later whether to sync the card to dist.
Result pending: run the release checks listed above.

### A-131 — release v0.2.0 on Pocket (base TAU core): tested by the user

**Date:** 2026-09-21
**Evidence:** user report ("tested the release", no screenshots requested; no problem reported) plus the base core's persisted settings read from the card (Pocket clock 23:44, written on Quit):
volume 45, colour 4 (sky), repeat 0, shuffle 0, meter 8 (peak dots), EQ 0 (flat), resume 0. Colour, meter and volume are off their defaults (amber, bars, 65), so the settings saved by the new
menu/shortcuts reach the persist file as designed on the release build. Whether every value came back after a relaunch, and each item of the A-130 checklist, is the user's own confirmation, not
independently recorded.
**Status:** v0.2.0 is the installed base `TAU` product: probe-free seed-2 RBF, release ROM `9b5d6575...`, settings menu, full-screen playlist and Info page, the playlist in SDRAM, no 1.2x hold gesture.
The SDRAM implementation work (A-088..A-131) is complete for the CPU window and the first data move (playlist). Deliberately open: FLAC, other tracks under saturation, temperature, the artwork buffers
(blit engine versus cached window), Diagnostic Build items (save report), and rebuilding the `TAU_SETTINGS`/`TAU_DIAGNOSTIC` bundles with the 0.2.0 version string.

### A-132 — real meter previews in the menu and a ten-step speed list (firmware built and packaged, not installed)

**Date:** 2026-09-21
**Evidence:** host (build, link map, snapshot fixtures, `make test-host`); no Pocket run.
**Meter previews.** Found the Figma exports at `assets/ui/meter/` (11 files, one per meter): **baseline JPEG, 24-bit, already 56x32 and about 2 KB each**, not large (the
converter nevertheless area-averages any larger export down to 56x32). Raw RGB565 would need 39,424 B, far beyond the RAM headroom, so `tools/gen_meter_thumbs.py`
(decodes with macOS `sips`, or Pillow if present; deterministic k-means) writes `fw/meter_thumbs.h`: **one 16-colour RGB565 palette per image plus a raster run-length
stream (one byte per run: palette index << 4 | length-1)**, 5,883 B in total (5,507 run bytes, 352 B of palettes, 24 B offsets). Per-image palettes keep each preview's own
hues (the shared-palette attempt lost the magic eye's cyan and the spectrum's orange); a 12- or 8-colour palette would save only 0.3 or 0.8 KiB. The firmware fills a preview with its dominant
colour and paints the other runs as one-row rectangles (`set_draw_thumb()`), indexed by the `VIZ_*` value (static assert on the enum order). The snapshot fixture decodes the same header, so
`settings-meter` shows the real previews. `TAU_METER_THUMBS` is on in the release-style targets only; the Diagnostic Build keeps the grey placeholder (it has no room).
**Speed.** `speed_fast` (0/1, 1.2x) is replaced by `speed_idx` over ten rational factors: **0.85, 0.95, 1.00, 1.10, 1.25 (replacing 1.2), 1.30, 1.50, 1.75, 2.00, 2.50**
(`speed_num/speed_den`, no FPU); Settings > Playback > Speed is now a choice list of them (A selects, applies at once, not persisted), the time-row marker shows the active speed,
and where there is no settings menu the old hold-A gesture toggles 1.00x/1.25x. Pitch follows speed (pitch correction later, as agreed).
**Limits to expect (from the code's own notes, not yet measured):** (1) the decoder budget: the source comments record that 1.5x already breaks on 320 kbps MP3 and 2x cannot work at
any bitrate at 60 MHz, so 1.30x is marginal and **1.50x-2.50x will underrun** (stutter/silence) until decode is faster or frames are skipped; (2) `sound_i2s` zero-order-holds to a fixed
48 kHz, so whenever file rate x speed exceeds 48 kHz (44.1 kHz above 1.09x, 48 kHz above 1.00x) samples are dropped without filtering, which is audible; that is a likely reason the earlier
1.2x "sounded poorer" (A-121), and a proper resampler belongs with the pitch-correction work. Slower speeds (0.85, 0.95) ask less of the decoder.
**Builds:** release-style settings ROM `b80fe44b...` (162,232 B; **heap gap 6,544 B**, floor lowered from 8 KiB to 6 KiB for the release-style targets because of the previews; hard link minimum
1 KiB); Diagnostic Build `7b0acdd2...` (164,384 B; gap 4,176 B, 80 B above its floor); SDRAM-playlist `4cfc1662...` and legacy product `player` (151,316 B) changed only by the speed table.
`dist/` still holds the v0.2.0 release ROM (`9b5d6575...`); a new release would need this ROM, a version bump and a new install. Bundles `TAU_SETTINGS` and `TAU_DIAGNOSTIC` repackaged with the seed-2
RBF, not installed. Fixtures: 50 (new `settings-speed`; `settings-meter` shows the real previews).
**To validate on Pocket:** the meter list shows the eleven previews (colours right, no flicker while paging, audio fine); Playback > Speed lists the ten speeds, the marker appears next to the time, 0.85/0.95/1.10/1.25
play, and how far up the list playback stays clean (report where it starts to stutter).

**A-132 installation (host, 2026-09-21):** `TAU_SETTINGS` on the card now runs the meter-preview and speed-list build: ROM `Assets/tau_settings/common/tau.rom` = `b80fe44b...` (162,232 B) and
its `core.json` updated to `version 0.2.0` (it still said 0.1.0). The previous ROM (`448a49dc...`), the old `core.json` and the catalog indexes are backed up in `work/diagnostics/settings-ui/rom-replaced-a132/`,
indexes cleared. All 14 bundle files SHA-256-identical (seed-2 RBF unchanged); the base `TAU` (v0.2.0 release), `TAU_DIAGNOSTIC`, `TAU_PSRAM` and the media untouched. Result pending.

### A-133 — meter previews and the ten-speed list on Pocket (A-132 build)

**Date:** 2026-09-21
**Evidence:** Pocket (5 screenshots, `work/diagnostics/settings-ui/screenshots-a132/`, card clock 00:03-00:06) and the user's report. Saved colour 7 (blush), meter 4 (oscilloscope).
**Meter previews:** the list shows the real previews (bars, waterfall, L/R levels, phase scope, oscilloscope, VU, waveform, mirrored bars, peak dots, magic eye seen), correctly drawn and placed, with the radio marker
on the active meter, scrolling, and the highlight/accent following the chosen colour (lime, blush in the frames). **The previews do not follow the theme colour:** they are the Figma artwork's own fixed lime/green
(plus the cyan magic eye), converted as exported; nothing in the converter or firmware recolours them, so with a non-lime accent they look out of place. (User: "if so not visible".) Open design question.
**Speed:** the Speed list shows the ten speeds with the radio on the active one. User-reported audio: **1.25x sounded fine; clear problems from 1.30x** (above it not described), i.e. the
decoder budget limit predicted in A-132 lands between 1.25x and 1.30x, and 1.25x is the practical maximum until decode gets faster or a resampler exists. Slower speeds and 1.10x not
reported as a problem.

### A-134 — speed list trimmed to 0.85-1.25x; previews stay as designed (firmware built and packaged, not installed)

**Date:** 2026-09-21
**Owner decisions after A-133:** keep the meter previews as designed (fixed Figma colours, no theme tint); trim the speed list to the speeds that play cleanly, 0.85, 0.95, 1.00, 1.10 and 1.25x.
**Change:** `SET_SPEED_SHOWN 5` in `fw/settingsui.inc`: the Speed list (and its name array) offers only those five; `speed_num/speed_den/speed_txt` in `fw/player.c` keep all ten entries so 1.30-2.50x return by
raising the constant when a resampler or faster decode exists. No change to the previews (`fw/meter_thumbs.h`, `tools/gen_meter_thumbs.py` as in A-132). Fixture `settings-speed` shows five rows.
**Builds:** release-style settings ROM `36d1e37c...` (162,172 B, heap gap 6,608 B), Diagnostic Build `62640957...` (164,324 B, gap 4,224 B); product and SDRAM-playlist ROMs unchanged from A-132; `dist/` still the v0.2.0 release.
`make test-host` passes. Bundles `TAU_SETTINGS` and `TAU_DIAGNOSTIC` repackaged with the seed-2 RBF; not installed (the card currently runs the A-132 ROM `b80fe44b...` with the ten-speed list).

**A-134 installation (host, 2026-09-21):** `Assets/tau_settings/common/tau.rom` on the card replaced with the trimmed-speed build (`36d1e37c...`, 162,172 B); the A-132 ROM (`b80fe44b...`) and the catalog
indexes are backed up in `work/diagnostics/settings-ui/rom-replaced-a134/`, indexes cleared. All 14 bundle files SHA-256-identical (seed-2 RBF and `core.json` 0.2.0 unchanged); `TAU` (v0.2.0), `TAU_DIAGNOSTIC`,
`TAU_PSRAM` and the media untouched. Result pending: the Speed list should show only 0.85-1.25x; the meter previews unchanged.

### A-135 — release v0.2.1: meter previews and a 0.85-1.20x speed list (built and packaged in dist/, not installed)

**Date:** 2026-09-21
**Evidence:** user report after testing the A-134 build: **1.25x now micro-stuttered**, although 1.25x had sounded fine in the A-132 test; decision "drop back to 1.20 and proceed to release". Host checks: build, link map,
`check_tau_package`, `make test-host`, fixtures (50). No Pocket run of the release.
**Reading of the 1.25x result:** the decode budget at 60 MHz is marginal there (about 57 of 60 MHz at 320 kbps by the source's own Stage 0 figures), so whether it stutters depends on the material and on what else the
CPU is doing (cover decode, drawing); 1.25x was on the edge, not solidly clean. 1.20x is the value the project shipped before (55 MHz), with a little more margin.
**Changes:** speed table entry 4 is 6/5 (`1.20x`, was 5/4); the Speed list offers 0.85, 0.95, 1.00, 1.10 and **1.20x** (the table keeps 1.30-2.50x hidden); the legacy hold-A toggle (builds without a settings menu) toggles 1.00x/1.20x. Version
**0.2.1** in the three cross-checked places (`APP_VER`, `dist/Cores/alfatreze.TAU/core.json` with `date_release 2026-09-21`, `README.md`) plus a `CHANGELOG.md` entry (meter previews, speed list, the overlay fix from A-117).
**Artifacts:** `fw/build.sh release` -> `dist/Assets/tau/common/tau.rom` = `40b92cdf...` (162,172 B, 90.0% of the RAM budget, heap gap 6,608 B above the 6 KiB floor); `dist/Cores/alfatreze.TAU/bitstream.rbf_r` unchanged (the seed-2 probe-free RBF, `cb15310a...`).
Same ROM as `TAU_SETTINGS` (identical flags). Diagnostic Build `afca0152...` (gap 4,224 B). Test bundles repackaged with the seed-2 RBF.
**Not done:** no card install (the card runs v0.2.0 as the base `TAU` and the A-134 ROM in `TAU_SETTINGS`); no re-run of the full release checklist on v0.2.1.

**A-135 installation (host, 2026-09-21):** v0.2.1 installed as the base `alfatreze.TAU` core: `Assets/tau/common/tau.rom` = `40b92cdf...`, `Cores/alfatreze.TAU/core.json` = `version 0.2.1`; `bitstream.rbf_r` was already the seed-2 probe-free RBF and
compared identical, so it was not rewritten. `TAU_SETTINGS` (test build) was brought to the same ROM and version (its A-134 ROM `36d1e37c...` still offered 1.25x) so nothing on the card is stale. The replaced files (v0.2.0 ROM `9b5d6575...`, its
`core.json`, the settings core's A-134 ROM and `core.json`) and the catalog indexes are backed up in `work/diagnostics/release-v021/card-replaced/`, indexes cleared. Media, `TAU_DIAGNOSTIC` (still the A-127 Phase 3 ROM),
`TAU_PSRAM` and every other file untouched. Result pending: Info page shows 0.2.1; Speed list 0.85-1.20x; meter previews; a long-press of A only pauses.

### A-136 — meter previews regenerated from the grayscale exports (built and packaged, not installed)

**Date:** 2026-09-21
**Evidence:** host (converter run, fixture, build, package). The user re-exported the eleven Figma previews as **grayscale** (56x32 baseline JPEG, 1.5-2.3 KB each, timestamps 00:58); they replace the lime/green originals of A-132 in `assets/ui/meter/`.
**Change:** `python3 tools/gen_meter_thumbs.py` regenerated `fw/meter_thumbs.h`: 5,747 B (5,371 run bytes, 352 B palettes, 24 B offsets), 136 B less than before. Neutral grey previews sit comfortably on every accent colour, which is what the earlier
"do they follow the theme?" question was after; no draw-time tint was added. (The 16-colour per-image palettes are more than grey art needs: a grey-only palette of 8 levels would save about 0.5-1 KiB more if the heap gap ever matters.)
Fixture `settings-meter` (50 fixtures) shows the new previews; inspected. Release-style settings ROM `69d036da...` (162,036 B, heap gap 6,736 B); `TAU_SETTINGS` bundle repackaged with the seed-2 RBF; the Diagnostic Build has no previews.
`dist/` (v0.2.1) still carries the ROM built from the lime previews, and the card runs it: the grayscale previews reach the base `TAU` only with a rebuilt release (a v0.2.2, or the next version).

### A-136 addendum / A-137 — 8-level grey previews and release v0.2.2 (built, committed, installed)

**Date:** 2026-09-21
**Owner decisions:** apply the saving (an 8-level grey palette); release v0.2.2 and install it.
**Change (A-136 follow-up):** `tools/gen_meter_thumbs.py` now uses **8 colours per preview** (the exports are greyscale) and a **3-bit index + 5-bit run length** byte (runs of 1..32 pixels instead of 1..16, so flat areas need fewer
tokens); `fw/settingsui.inc` (`set_draw_thumb`) and the fixture decoder read the same layout. `fw/meter_thumbs.h`: **4,667 B** (4,467 run bytes, 176 B palettes, 24 B offsets), down from 5,747 B (grey 16-colour) and 5,883 B (lime),
about 1.2 KiB saved against the first version; quality checked on the contact sheet and the `settings-meter` fixture (magic eye and spectrum lose a little smoothness, everything is recognisable).
**Release v0.2.2:** `APP_VER`, `dist/Cores/alfatreze.TAU/core.json` (`0.2.2`, `date_release 2026-09-21`) and `README.md` bumped; `CHANGELOG.md` entry. `fw/build.sh release` -> `dist/Assets/tau/common/tau.rom` = `a8e76b78...`
(160,956 B, 89.3% of the RAM budget, **heap gap 7,824 B**, up from 6,608 B); `bitstream.rbf_r` unchanged (seed-2 probe-free RBF). `check_tau_package` and `make test-host` pass; the Diagnostic Build (no previews) gap is 4,224 B.
Same ROM as the `TAU_SETTINGS` test build.
