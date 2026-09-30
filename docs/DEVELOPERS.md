# For core developers

Working on this core, or building something similar on Cyclone V with Quartus (an openFPGA blit/GPU engine, a soft CPU, a tight block-RAM
budget)? This page has the practical side (building, testing, fitting, installing, the repository map, how to contribute) and the lessons
the project paid for. Full detail and evidence for everything below is in [AUDIT_TRAIL.md](AUDIT_TRAIL.md); the system itself is described
in [TECHNICAL_SPEC.md](TECHNICAL_SPEC.md).

Contents: [Repository map](#repository-map) · [Building](#building) · [Testing](#testing) · [The Quartus fit workflow](#the-quartus-fit-workflow) ·
[Installing on a card](#installing-on-a-card) · [Evidence discipline and the audit trail](#evidence-discipline-and-the-audit-trail) ·
[Issues faced, and what fixed them](#issues-faced-and-what-fixed-them) · [Techniques and approaches found useful](#techniques-and-approaches-found-useful) ·
[The developer skill](#the-developer-skill-this-project-maintains) · [Contributing](#contributing)

## Repository map

| Folder or file | What it holds |
|---|---|
| `src/fpga/core/` | Tau's RTL: the SoC (`mp3_soc.v`), draw engine (`mp3_fb.sv`), PCM FIFO, EQ, hardware meter and MP3 blocks, PSRAM and SDRAM glue, CDC blocks |
| `src/fpga/apf/` | The Analogue Pocket Framework files |
| `src/fpga/rtl/` | The generated VexRiscv CPU and the SDRAM controller |
| `src/fpga/ap_core.qsf`, `.qpf` | The Quartus project (fit settings and assignments) |
| `fw/` | Firmware: `player.c` plus `.inc` and header modules, `link.ld`, `build.sh`, generated meter and theme headers |
| `third_party/` | Vendored components with their own licences (Helix, picojpeg, the Inter font) |
| `meters/` | One `meter.json` manifest per meter; `tools/gen_meters.py` turns them into firmware tables |
| `themes/` | Theme sources (`tau.json`, `ocean.json`, user examples) |
| `tools/` | Host tools: sync, library, packaging, install, decoders, generators, VM fit runner, qsf macro bundles, previews (`tools/meters/`, `tools/lab/`) |
| `sim/` | Icarus testbenches and Python/C host tests |
| `dist/` | The built release payload (Cores, Platforms, Assets) |
| `release/` | Release zips (local; published through GitHub releases) |
| `docs/` | All documentation (see [docs/README.md](README.md)) |
| `assets/` | Branding and UI artwork sources |
| `archive/` | Retired code kept for reference (the cassette meter) |
| `toolchain/` | Vendored RISC-V toolchain and isolation experiments (large; not needed to read the code) |
| `work/` | Local scratch: diagnostics builds, card backups, previews (not part of the release) |
| `CHANGELOG.md`, `NOTICE.md`, `PROJECT.md`, `ROADMAP.md`, `AGENTS.md`, `CLAUDE.md` | Release history, provenance, milestone ledger, roadmap, working agreements |

## Building

Toolchain check: `make check` (or `check-firmware`, `check-fpga`). The firmware uses `riscv-none-elf-gcc` (a vendored copy is looked for under
`toolchain/`, or set `RISCV_TOOLCHAIN_BIN`).

```bash
bash fw/build.sh release                          # the shipped firmware -> dist/Assets/tau/common/tau.rom (+ tau-cold.bin)
bash fw/build.sh player-library-diagnostic        # the Diagnostic Build's firmware
bash fw/build.sh player-library-diagnostic-profile  # the same plus per-stage decode profiling
bash fw/build.sh psram-diag                       # PSRAM diagnostic firmware (needs a matching bitstream)
bash fw/build.sh psram-diag-sim                   # the same for simulation
make firmware                                     # = build.sh release
make fpga                                         # full Quartus compile (very long; see the fit workflow)
make package                                      # package.py: the dist/ payload
python3 tools/make_release.py                     # both release zips (TAU and TAU_DIAGNOSTIC) with checks and SHA256SUMS
```

`build.sh` accepts overrides such as `POLY_FW=1` (MP3 window unit firmware), `SDRAM_BUSY=1`, `ART_TIMG`, `G4`, `PICOJPEG_COLD`, and `RAM_192K=1`
(link for the 192 KB RAM build). It enforces a minimum heap gap and refuses to build a target that does not fit. The link map and the cold/hot rules are in
[TECHNICAL_SPEC.md](TECHNICAL_SPEC.md) section 4 and [PHASE_G_SPEC.md](PHASE_G_SPEC.md). Note that hot code may call cold code only through a `COLD_READY()` gate;
`tools/check_cold_calls.py` checks this.

## Testing

```bash
make test-host   # Python and C host tests (firmware logic in an RV32 emulator, library index, cold image, QR, themes, meters, decoders, packaging, install script, audit-trail format)
make test-rtl    # Icarus RTL testbenches and mutation tests (draw engine and its reference renderer, EQ, PCM FIFO, SDRAM, PSRAM, CDC blocks, hardware meters, MP3 window unit, RAM)
make test        # both
make rtl-lint    # Verilator lint
```

Two habits worth copying: every test that guards a fix must **fail against the old behaviour** (mutation variants are kept for the draw engine, MP3 window unit and PSRAM), and a
test that cannot catch its own mutant is not a test (a zero-delay simulator cannot reproduce Gray-code metastability, so that counter's mutation test was dropped rather than kept as false comfort).
Every UI state change also needs a named framebuffer snapshot ([AGENTS.md](../AGENTS.md); `tools/ui_snapshot_renderer.py`).

## The Quartus fit workflow

Timing on this design is close (the RAM budget is 304 of 308 M10K blocks in the current bitstream), so a fit is an experiment with a written prediction, not a formality.

1. Put the macros for the build in a qsf append file (`tools/*_qsf_append.txt`; the current shipped set is the `blit_g3_poly` family plus the gamma text table).
2. Launch on the build VM: `python3 tools/vm_fit.py launch NAME --append tools/<bundle>.txt --seed 1 --seed 2`.
3. `python3 tools/vm_fit.py status NAME` shows each seed's state, resources and worst slack per corner; `collect NAME --seed N` copies the RBF and prints its SHA-256.
4. A result is only trusted when both seeds close (all four corners positive) or the difference is explained; pick the seed with the best margin.
5. Pair the bitstream only with firmware built for the macros it was fitted with (a fit missing the product macros once produced a core that looked broken but was working as designed).

Details: [FPGA_BUILD.md](FPGA_BUILD.md) (baseline, resource tables, VM setup), [JTAG_DEBUG_ACCESS.md](JTAG_DEBUG_ACCESS.md) (USB-Blaster, SignalTap and in-system probes, both proven on this project).

## Installing on a card

Use `tools/install_dev_core.py` (dry run by default, `--yes` to write): verified backup, copy with SHA-256 check, media carry-over with a rebuilt library index, removal of superseded
cores, deletion of the Pocket's five catalog caches, junk cleanup, eject. Do not hand-type the procedure; every skipped step in the past produced a plausible but wrong diagnosis.
See [CARD_INSTALL_PROCEDURE.md](CARD_INSTALL_PROCEDURE.md) and [guide/MEDIA_AND_TOOLS.md](guide/MEDIA_AND_TOOLS.md).

## Evidence discipline and the audit trail

- Every result, hardware or not, is a numbered entry in [AUDIT_TRAIL.md](AUDIT_TRAIL.md) (series `A-NNN` for the SDRAM/UI/firmware phase, `B-NNN` after that; `tools/check_audit_trail.py`
  checks the format in `make test-host`). Log the evidence label with it: hardware-confirmed, simulation-verified, host-verified or design only.
- Predictions are written down before a run. A wrong prediction is recorded and corrected, not deleted.
- [ROADMAP.md](../ROADMAP.md) is the single ordered "what next" list ([docs/ROADMAP.md](ROADMAP.md) is the detailed internal one); [CURRENT_STATUS.md](CURRENT_STATUS.md) says what is true now;
  [DECISIONS.md](DECISIONS.md) records assessed and rejected options so they are not re-litigated.
- Two projects touch this repository's docs (this one and the Tau Omega companion): never share literal files, only reference documented interfaces
  ([CROSS_PROJECT_INTERFACE.md](CROSS_PROJECT_INTERFACE.md)).

## Issues faced, and what fixed them

- **A new DSP-heavy feature broke timing on logic that had nothing to do with it.** The blit engine's alpha-blend pipeline did not just cost DSP blocks: it changed placement congestion around
  *unrelated* existing arithmetic, pushing an address adder Quartus had mapped onto a DSP block into a real setup violation. The failing path was traced with `report_timing -detail full_path`, not assumed:
  the obvious suspect (the new blend logic) had nothing to do with the actual violating path. **Lesson: do not trust which feature "must" be causing a timing failure. Trace the real path before fixing anything.**
- **Removing the DSP-heavy feature fixed the failure, then exposed a second, unrelated one, and a third.** Each fix only revealed the next-worst pre-existing marginal path; the device had several genuinely tight
  paths, not one bug. **Fix, applied to several different paths (BAR, SBLIT, CHAR, then the rounded rectangle):** retime the arithmetic to compute off the *same raw memory read* a downstream register already uses,
  on the *same* clock edge, instead of chaining combinational logic after that register. Same function, zero functional change, verified bit for bit against the existing tests each time.
- **A plausible-sounding explanation for a timing fix turned out to be wrong.** ("Removing feature X fixed the violation because it reduced logic feeding the same write port" was false; the real mechanism was
  DSP-block placement congestion, confirmed by re-running `report_timing` against the original failing build and finding no trace of the suspected logic.) **Lesson: verify a fix's mechanism, not just its result.**
- **Update: the rounded rectangle and the alpha blend are now both closed.** The hardware rounded rectangle (`OP_RRECT`) had a -2.366 ns violation in a corner-sequencer chain; the same retiming fixed it (B-231) and the
  combined fit with the RAM shrink closed on both seeds (B-235). The alpha blend then failed again when re-enabled, until its read-modify-write was rebuilt as a three-stage pipeline (capture, blend, write);
  **that pipelined blend closes timing (fit `blend-pipe-b327`, 2026-09-27, setup min +0.755 ns on seed 1; B-326, B-327) and ships in v0.6.0-alpha.1**, driving the Settings menu cross-fade (B-405).
  So the earlier "shelved because it cannot close" was true for the single-cycle version only.
- **The Fitter's own effort setting can silently cap how hard it tries.** `FITTER_EFFORT` at `AUTO FIT` (this project's current setting in `ap_core.qsf`) stops optimizing once it estimates "good enough" and skips optimizations that affect
  timing, to save compile time. Worth checking before assuming a design is at its real timing limit.
- **Synthesis-stage resource reports do not reveal physical memory packing.** A font-ROM repack expected to reduce M10K use measured identical declared bits at synthesis by construction; only a real Fitter run showed the physical count,
  and it showed no improvement. A negative result kept on record. Do not trust a synthesis-only report to answer a packing question.
- **A RAM-inference pattern-matcher can be pickier than it looks.** Splitting one memory into two power-of-two regions (to work around an odd total size Quartus refused to infer as block RAM) kept failing the same way after removing
  every suspect in turn (a local address wire, a module boundary, a `generate` block). The real cause: every failing version read the result through a ternary selecting between two *different* multi-array concatenations in one
  statement, unlike every successfully inferred RAM in the file, which reads exactly one array unconditionally. Fix: give each region its own plain registered read and mux the *already-registered* values afterward.
- **A linker script's `DEFINED()` can silently do nothing in the wrong context.** Making a memory region's size conditional via `LENGTH = DEFINED(SYM) ? A : B` inside a `MEMORY` block had no effect in this toolchain: both settings linked
  an identical image with no error. Caught only by checking symbol values with `nm`. `DEFINED()` in an ordinary symbol assignment is the well-supported form. **Lesson: verify the actual linked addresses.**
- **A "shared read port" fear can be a timing question in disguise.** Two draw-engine opcodes reading the same lookup table looked like a contention risk. In a design that dispatches one command at a time they can never read it at once;
  the real cost of a second address source is the extra fan-in into the shared read-address expression, a timing-margin question the retiming technique already answers.
- **Raw CPU pointers into the wrong SDRAM range hang the core.** A long investigation of a hanging test page ended at three functions that used raw pointers based at `0xA0000000`, which hung
  unconditionally on every firmware and bitstream combination, while pointers based at the SDRAM window's start (`0xA0100000`) worked every time. The fix routed the affected accesses through the
  proven window base or the SDRAM mailbox (AUDIT_TRAIL B-192, B-193).
- **A bitstream fitted without the product macros looks broken.** A whole "timing closed" series had been built without the macros the firmware needs, so fail-safes correctly degraded and the core looked dead. Always check the macros against the
  firmware's needs before installing (AUDIT_TRAIL B-130).
- **The RAM shrink is fit-proven but the firmware does not fit yet.** The 192 KB RAM RTL closed timing (B-235) and would release 64 blocks; the firmware image is still several KB too large, so the 256 KB bitstream ships
  ([RAM_SHRINK_192K_PLAN.md](RAM_SHRINK_192K_PLAN.md)).

## Techniques and approaches found useful

| Approach | What it solves | Confidence |
|---|---|---|
| Retime a combinational chain off the *raw* memory read instead of the registered value downstream | A register-to-register timing path running through non-trivial logic (compares, subtracts, muxes) fed from a BRAM/MLAB output | Applied and fit-proven on this project several times |
| Split a read-modify-write into capture / compute / write stages with a drain before the row is released | A single-cycle path through a memory read, a DSP and a write mux | Fit-proven (pipelined blend, B-327) |
| Per-instance `DSP_BLOCK_BALANCING` (`set_instance_assignment -name DSP_BLOCK_BALANCING "LOGIC ELEMENTS" -to <instance>`) | Forcing one adder off DSP-block mapping without disturbing real multiplies elsewhere | Documented by Intel; not yet tried on this project |
| `FITTER_EFFORT STANDARD FIT` instead of `AUTO FIT` | Recovering timing margin the Fitter leaves unclaimed to save compile time, at the cost of a build that can run 2x or more longer | Documented by Intel; not yet tried on this project |
| Quartus Rapid Recompile | Cutting iteration time (about 65% on average per Intel's figures) for a small isolated RTL change | Documented by Intel; not yet tried on this project |
| Gray-coded counters and buses for crossing clock domains; a plain synchroniser for a single level | The standard, simulation-provable way to move multi-bit values across domains | Built and unit-tested (`tau_cdc_gray_ctr.sv`, `tau_cdc_gray_bus.sv`, `tau_cdc_sync1.sv`) |
| Known-answer probes for any hardware feature, with software fallback | An older bitstream or a failed unit must degrade, not crash | Used for the draw engine, cold code, TIM1 and the MP3 window unit |
| Golden model plus real-code harness (firmware and JS ports run against the same vectors) | Proving generated or ported code equals the original | 35,562 meter draw commands compared one by one |
| Two seeds for every timing claim; evidence-graded knowledge base (community-reported / docs-verified / hardware-validated) | Not trusting one lucky fit, and being explicit about how far to trust a claim | In active use |

## The developer skill this project maintains

Development on this core uses an **[Analogue Pocket / openFPGA development skill](https://github.com/alfatreze/analogue-pocket-dev-skill)** for Claude Code: a reference covering
`core.json`/`data.json`/`interact.json`, the BRIDGE bus and host/target commands, save persistence, Chip32, SD packaging, JTAG/SignalTap debugging, and an evidence-graded community
knowledge base built from the official developer docs, the open-fpga repos and community cores. It is public and MIT-licensed; findings like the ones above get folded into it as they are
found, so a future session (or another developer) does not have to rediscover them.

## Contributing

Open an issue or pull request at <https://github.com/alfatreze/Tau-Alpha>. Before a change:

1. Read [CURRENT_STATUS.md](CURRENT_STATUS.md) and [ROADMAP.md](ROADMAP.md) (what is true, what is next) and the relevant spec listed in [docs/README.md](README.md).
2. Keep RTL changes behind a macro with a hardware probe and a software fallback; add a testbench and, where a bug is being fixed, a variant that fails without the fix.
3. Run `make test` before you push; a timing claim needs two fitted seeds.
4. Log the result in the audit trail with its evidence label, and keep licences straight: never copy GPL code into the MIT tree ([ATTRIBUTIONS.md](ATTRIBUTIONS.md)).
5. Commit only your own files (documents in this repository are edited by more than one session; stage exact hunks).
