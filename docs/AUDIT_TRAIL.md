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
**Evidence:** **simulation | code-review** — the new focused mux-return test
captures `G-R-G` for an all-ones response at `wb_done`. Existing CPU-window,
CPU-return, adapter-return, bridge-mux, Wishbone-adapter, standalone Phase 2,
and composed-path simulations pass unchanged. Quartus and Pocket evidence are
pending and must not be inferred from these host simulations.
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
