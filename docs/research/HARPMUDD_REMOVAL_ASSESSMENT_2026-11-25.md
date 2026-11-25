# HarpMudd-origin code: assessment, plan and removal preparation (Phase 0, 2026-11-25)

**Evidence label:** HOST-ONLY. Source measurement on `main` at `3b8ff895` plus a read of the live readers of each file. Nothing was built for a Pocket, fitted, or installed. Method: `docs/research/provenance-2026-10-12/measure.py` (nonblank, non-comment lines; origin = last textual origin by copy-aware blame `-w -M -C -C`). **A blame figure is not creative ownership or a legal conclusion.** Files: `docs/research/provenance-2026-11-25/` (`files.csv`, `summary.json`, `areas.md`, `harpmudd_manifest.csv`, `doc_inventory.csv`, and the scripts that make them).

## 1. Fresh measurement and comparison with 2026-10-18

| Area | 2026-10-18 lines | HarpMudd | % | 2026-11-25 lines | HarpMudd | % | delta HarpMudd |
|---|---:|---:|---:|---:|---:|---:|---:|
| fw/ headers read by fw2 | 2,817 | 0 | 0.0% | 2,340 | 0 | 0.0% | +0 |
| fw/ rest (old player) | 13,791 | 3,758 | 27.2% | 13,316 | 3,758 | 28.2% | +0 |
| fw2 (Player 2 firmware) | 12,752 | 20 | 0.2% | 19,936 | 13 | 0.1% | -7 |
| other (root, packaging, assets) | 609 | 77 | 12.6% | 4,219 | 86 | 2.0% | +9 |
| sdk | 0 | 0 | - | 11,659 | 38 | 0.3% | +38 |
| sim | 22,452 | 4,411 | 19.6% | 42,453 | 5,284 | 12.4% | +873 |
| src/fpga/apf (Analogue framework) | 1,020 | 1,020 | 100.0% | 1,020 | 1,020 | 100.0% | +0 |
| src/fpga/core | 24,281 | 6,411 | 26.4% | 27,643 | 6,387 | 23.1% | -24 |
| src/fpga/rtl (vendored CPU, sdram) | 6,911 | 6,843 | 99.0% | 6,441 | 6,441 | 100.0% | -402 |
| third_party | 7,173 | 6,963 | 97.1% | 7,173 | 6,963 | 97.1% | +0 |
| tools | 20,199 | 4,382 | 21.7% | 25,826 | 4,254 | 16.5% | -128 |

First-party (firmware, rtl, test, tooling) 2026-10-18: 77,261 lines, 13,996 HarpMudd-origin (18.1%)

First-party (firmware, rtl, test, tooling) 2026-11-25: 125,414 lines, 14,757 HarpMudd-origin (11.8%)

Per-module `src/fpga/core` figures: `provenance-2026-11-25/areas.md`.


How to read it:
- The 10-18 column is branch `archive-old-fw` (`4c0dcc2`), the 11-25 column is `main` (`3b8ff895`). Same tool, same buckets (the "first-party" total is firmware, RTL, tests, tooling and framework, as in the 10-18 report).
- **The share fell (18.1 % to 11.8 %) mostly because the denominator grew** (77,261 to 125,414 lines: Player 2 firmware, the SDK, many new tests). **The absolute count rose by 761 lines (13,996 to 14,757).** The cause shows in the `sim` row (+873): the code deleted from `src/fpga` after D-T07 and D-T13 now lives in `sim/oracle/` (470 lines) and `sim/legacy_sdram/` (400 lines) as simulation-only references. It was moved, not removed, and still counts.
- `src/fpga/rtl` is now only `VexRiscv_Full.v` (Charles Papon's generated CPU), counted 100 % because HarpMudd imported it; `sdram_fb.sv` is gone (-402).
- `fw/ rest` is unchanged (3,758): the old player is still in the tree. `fw2` has 13 inherited lines (12 are register macros in `platform/mmio.h`).
- What ships: `fw2` (13 lines), the shared FPGA design (`src/fpga/core`, about 1,300 first-party-lineage lines, section 2), and vendored third-party code (Helix MP3, picojpeg, VexRiscv, Analogue APF, agg23 loader/I2S/FIFO, generated font and PLL files). Third-party lines are blamed on HarpMudd because he imported them; they are not his authorship.
- Not comparable with the 2026-10-12 28 % figure (scope: firmware + RTL only). The 10-18 first-party 18.1 % is the like-for-like predecessor.

## 2. What HarpMudd-origin code remains, module by module

Full list (131 files with at least 10 inherited lines or 25 %): `harpmudd_manifest.csv` (path, lines, HarpMudd %, verdict, shipped, status, readers or blocker, replacement, proof). Verdict rules are in `build_manifest.py`: THIRD-PARTY (vendored or generated, not HarpMudd's work), ORIGINAL-HARPMUDD (>= 70 %), MIXED (15-70 %), TAU-OWN (< 15 %). HarpMudd-origin lines by verdict: THIRD-PARTY 22,564; MIXED 5,715; ORIGINAL-HARPMUDD 5,523; TAU-OWN 121. The MIXED and ORIGINAL lines are what could be rewritten; most are tools, tests and the old player.

### Ships in the release (first-party lineage)
| File | HarpMudd lines / total | What it does | Replacement | Proof needed |
|---|---|---|---|---|
| `src/fpga/core/tau_talos.sv` | 360 / 985 (37 %) | the 2D draw engine (the engine half of the old `mp3_fb`) | **none**; D-T12 parked the rewrite (no measured need) | contract, new module, differential fuzz against `sim/oracle/tau_talos1_oracle.sv`, mutants, fit, Pocket |
| `src/fpga/core/tau_soc.v` | 282 / 1,238 (23 %) | SoC: CPU bus, MMIO register file, wiring (renamed from `mp3_soc`) | none; not in D-T01 | contract from `MMIO_ALLOCATION.md`, bench per block, fit, full Pocket regression |
| `src/fpga/core/core_game.vh` | 211 / 759 (28 %) | clocks, bridge, top-level wiring | none | as above |
| `src/fpga/core/core_top.v` | 238 / 249 (96 %) | the APF shell top (Analogue template, via HarpMudd) | none; scaffold of Analogue's framework | owner and licence view first: it is Analogue's template |
| `src/fpga/core/tgt_cmd.v` | 76 / 83 (92 %) | target-command CDC/protocol | none | contract, `tb_tgt_cmd` differential, Pocket |
| `src/fpga/core/pcm_fifo.v` | 63 / 89 (71 %) | audio sample FIFO | none | contract, `tb_pcm_fifo` and `tb_pcm_decay`, Pocket audio |
| `src/fpga/core/tau_gfx.sv` | 19 / 184 (10 %) | graphics top wrapper | rewrite with the engine | goes with the engine |
| `src/fpga/core/core_constraints.sdc` | 8 / 24 | timing constraints | keep; do not discard (10-12 report) | fit |
| `fw2/platform/mmio.h` (+3 SDK copies) | 12 / 105 | register address macros | trivial rewrite | host build |
| generated `font_rom.v` (3,052), `font_ram.v` (1,211) | blame on generated data | font ROM and RAM from Inter, Tamzen, Space Grotesk | generators: `gen_font_atlases.py` is Tau's, `gen_font_rom.py` is HarpMudd lineage | the `font_ram.v` match is most likely copy detection on repeated hex rows: **verify** |

Done and replaced (no HarpMudd text left in what ships): scanout `tau_scanout.sv` (1 line; D-T07, differential bench and 12 mutants, Pocket DEV 198), SDRAM PHY and schedulers (`tau_sdram_phy.sv`, `tau_sdram_sched_a.sv`; D-T09/D-T13, Pocket DEV 200, soak 46 passes), the old fixed EQ (Halcyon), the old player in `fw2/`.

Third party that ships (attribution, not removal): Helix MP3 (RealNetworks, RPSL), picojpeg (public domain), VexRiscv (MIT as stated by the project), Analogue APF files (Analogue licence), agg23 `data_loader`/`sound_i2s`/`sync_fifo` (MIT), Quartus-generated PLLs.

### Does not ship, still in the tree
- **Old player `fw/`**: 3,758 HarpMudd lines of 13,316 (`player.c` 3,148 of 5,637; `art.inc` 275; `playlist.inc` 141; `settings.inc` 59; `build.sh` 54; `flac.h` 52). Blockers (verified today; the 10-18 report counted six, there are now more): 14 tools read `fw/player.c` or `fw/*.inc` (`gen_themes`, `gen_meters`, `gen_meter_thumbs`, `gen_vu_segment_table`, `theme_preview`, `ui_snapshot_renderer`, `check_cold_calls`, `check_fw_bitstream_pair`, `fit_manifest`, `library_check`, `make_release` legacy path, `tau_version`, `meter_cost_estimate`, `player2/globals_map.py`) and 8 tests (`test_meter_core`, `test_splash_asset`, `test_theme`, `test_rc_lut`, `test_clut_contract`, `test_ram192k`, `test_clk66`, `test_fw_pair_check`); `fw2/` also includes about 21 `fw/` headers (0 HarpMudd lines). Smallest change: point each reader at its `fw2/` equivalent or archive the test (per file in `harpmudd_manifest.csv` and `fw_blockers.json`).
- **Simulation references**: `sim/oracle/tau_scanout1_oracle.sv` (55 lines; simulation-only, never synthesised: the black-box reference of `make test-rtl-scanout-diff`), `sim/oracle/tau_talos1_oracle.sv` (415; reference of `tb_tau_gfx_diff`), `sim/legacy_sdram/tau_sdram_ctrl.sv` (400; agg23 lineage, MIT; reference of two SDRAM benches).
- **Tools, host harnesses, test infrastructure**: about 60 files, mostly 90-100 % HarpMudd text (`rv32sim.py`, `flac_verify.py`, `flac_ref.py`, `flac_make_test.py`, `library_check.py`, `gen_font_rom.py`, `gen_text_gamma.py`, `gen_eq_coeffs.py`, `tools/host/*_harness.c`, `sim/common/{start.S,link.ld,sysio.c}`, `sim/fw_*`). Live readers are in the manifest. 18 files with no live reader were moved (section 4).

## 3. The D-T01 order against reality

| D-T01 step | Status | Evidence |
|---|---|---|
| 1. Scanout | **DONE, Pocket-confirmed.** New `tau_scanout.sv` written clean-room from `TALOS_SCANOUT_CONTRACT.md`; the old module is the simulation-only oracle | D-T07, DEV 198, `PROVENANCE.md` |
| 2. Engine (Talos) | **NOT DONE.** `tau_talos.sv` still holds 360 inherited lines. T4/T5/T6 were parked by D-T12 because the T0/T4 measurements showed no need | D-T12 |
| 3. SDRAM controller | **DONE, Pocket-confirmed** (shared PHY plus scheduler; the old controller was deleted from `src/fpga`) | D-T13, DEV 200 |
| Deletion of old code after Pocket confirmation | Scanout oracle and `legacy_sdram` meet the gate and are still in `sim/`; the engine oracle must stay | owner approval pending |
| Beyond D-T01 | `tau_soc.v`, `core_game.vh`, `tgt_cmd.v`, `pcm_fifo.v` and the old `fw/` player are covered by no decision | |

Honest reading: for the graphics path two of three steps are done; the engine and the SoC/integration layer are the remaining shipped first-party-lineage code (about 1,300 lines). The old `fw/` player is the largest block by lines (3,758) but does not ship.

## 4. Move-aside done on this branch (JOB A)
- `legacy-harpmudd/` (README: what, why, how to restore). **18 files moved with `git mv`**: `tools/{art_scale_model,card_snapshot,flac_bitreader_check,flac_framepos_check,flac_hdrcrc_check,flac_lpc_capture,flac_probe,flac_resync_check,font_preview,gradient_preview,idle_preview,push_card.ps1,splash_preview,strip_xing,xing_check}` and `sim/{tb_reference,tb_picorv32,tb_vexriscv}.v`; about 3,100 HarpMudd-origin lines. Criterion: nothing in the shipped build, `fw2/build.sh`, `make test-host`, `make release` or any generator reads them (grep over tracked non-doc files; the three previews and the flac checks only name each other). A mention in a comment of the old player (`fw/player.c`, `fw/art.inc`) was treated as no reader.
- Result: `fw2/build.sh` with DIAG=1 and DIAG=0 builds and the `tau.rom` is **byte-identical** to the pre-move build (SHA-256 starts `1bf6bdef` and `79640729`). Host tests: see the audit entry B-1158 for what ran.
- Everything else with a live reader stays; its blocker is in the manifest column `blocker_or_readers_or_whose`.

## 5. Phased plan
Every phase ends with an audit entry and the host suite green. Parallel means: can run in its own worktree.

| Phase | Work | Tests first | Exit gate | Pocket / VM | Parallel | Effort (honest) | OWNER decision |
|---|---|---|---|---|---|---|---|
| **1: retire the old `fw/` player** (-3,758 lines, no RTL) | repoint the 14 tools and 8 tests at `fw2/` sources (palette, meter tables, diag register, splash constants), archive `test_meter_core` and `test_splash_asset`, move `fw/` rest and the legacy release path to `legacy-harpmudd/` | each generator `--check` red against a deliberately wrong `fw2/` source, then green | `make test-host` green; `fw2` ROM and the release zip SHA-256 identical before and after | none (fw/ does not ship) | yes | 2-4 sessions | approve archiving the test coverage of old-player code |
| **2: sim reference cleanup** (-870 lines) | delete `sim/oracle/tau_scanout1_oracle.sv` with `test-rtl-scanout-diff` and `tb_tau_scanout_diff.v`; delete `sim/legacy_sdram/` with the two SDRAM benches (or freeze their results as data) | none new | `make test-rtl` green without them; D-T01 evidence cited | already confirmed (DEV 198 and 200) | yes | 0.5-1 session | **deletion approval (D-T01, D-T13)**; keep the scanout diff as a regression test with a frozen trace instead? |
| **3: small RTL, one fit bundle** (`tgt_cmd` 76, `pcm_fifo` 63, `tau_gfx` 19, `mmio.h` 12; about -170) | per module: contract written by the session that read the old file; clean-room module by another agent; differential bench and at least 6 mutants (the scanout method) | the benches, red against mutants | cycle-exact differential; lint clean; no latch; ALM/M10K unchanged | **1 fit, 2 seeds (about 1h45 each, 3h30 if serial; D-T04 style), Pocket audio and target-command run** | modules in parallel, one fit | 3-5 sessions | is 170 lines worth a fit? can ride a fit that happens anyway |
| **4: SoC and integration** (`tau_soc.v` 282, `core_game.vh` 211) | contract from `MMIO_ALLOCATION.md` and the firmware's register use; new `tau_soc` by block (CPU bus, MMIO map, wiring) | register-map conformance bench; `sim/fw_*` firmware sims unchanged | all `test-rtl` green, two seeds timing closed, Pocket full regression (audio, video, save, watchdog) | 2 fits and a full Pocket regression; instance names reach the SDC: no rename inside a fit | partly | 6-10 sessions, high risk | **worth it? it touches every client** (D-T02 only renamed it) |
| **5: Talos engine** (360 lines, oracle 415) | row-job engine per `TALOS2_REIMPLEMENTATION_PLAN.md`; contract = opcodes 0-8, register map, `tools/host/blit_reference.py` | differential fuzz (random command streams, pixel exact) against `tau_talos1_oracle.sv`; scanout-underflow assertion under a blit storm; mutants | pixel parity, ALM/M10K within budget, two seeds, Pocket Blit Test, all meters, 30 min soak | 2 fits and Pocket | no (needs phase 3's `tau_gfx`) | 8-12 sessions, high risk | **D-T12 parked it; reopening is a decision** |
| **6: tools and test infrastructure** (about 3,000 ORIGINAL lines: `rv32sim.py`, flac tools, `tools/host` harnesses, `sim/common` start/link, `sim/fw_*`) | retire where the `sdk/` runner or `fw2` tests supersede (`rv32run.c`), otherwise rewrite from the spec | parity runs old vs new on the same corpus | no live reader left | none | yes | 4-6 sessions | **rewrite or retire, per tool** |
| **Not rewritten** | Helix, picojpeg, VexRiscv, Analogue APF (`core_top.v` template, bridge), agg23 loader/I2S/FIFO, Quartus IP | | attribution wording accurate | | | | **licence view** (R7) |

Total: phases 1 and 2 are cheap and decisive for the number (about -4,600 lines, no fit). Phases 3-5 together are about 17-27 sessions and 4-5 fits (about 8 VM hours) for about 1,300 lines: the benefit is provenance, not behaviour. Rough projection: after phases 1, 2 and 6 the first-party HarpMudd-origin total would be about 8,000 lines of about 120,000 (7 %); phases 3-5 would take about 1,300 more, leaving third-party, framework and generated files. (Estimates, not measured.)

## 6. Expert review checkpoints
| # | When | Reviewer (not the author) | Receives | Must answer |
|---|---|---|---|---|
| R1 | before any rewrite | an independent agent plus the owner | the written contract, the register map, the reference model; **not** the old code | Is behaviour fully specified (ports, cycle timing, reset, error cases)? What can a clean-room author not infer? |
| R2 | authorship | the coordinator | the author's brief and its report of files read | Was the author given only the contract? The scanout record is honour-based: add a technical separation, for example an author worktree that does not contain the old file |
| R3 | after the new code, before a fit | an agent that wrote neither | new code, contract, bench | Does the bench kill its mutants? Any behaviour the contract does not state? |
| R4 | similarity audit, per replacement | a script plus a reviewer | new file and old file **after** writing | Normalised token and line similarity, copy-aware blame of the new file (`measure.py` on the branch) below a stated threshold; every similar run explained as dictated by the contract |
| R5 | RTL and timing before the fit | a Cyclone V reviewer agent | RTL, SDC, assignments, A-094 evidence | One-writer RAM inference, CDC, registered outputs, slack at both corners; D-T06: one unit only, so margins must be at least the old ones |
| R6 | pre-deletion | the owner with a reviewer | Pocket result, the diff of what is deleted, grep proof of no reader | D-T01 gate: Pocket-confirmed? every reader gone? rollback tag? |
| R7 | licence and attribution | **the owner, and a lawyer if he wishes** | `NOTICE.md`, `ATTRIBUTIONS.md`, `PROVENANCE.md`, the manifest | **Flagged, not decided here:** what "HarpMudd-origin" means for the MIT licence (HarpMudd's MIT notice is in `LICENSE`); whether notices stay when files are replaced; whether blame-origin on vendored files needs different wording; whether the honour-based clean-room claim for the scanout is acceptable |

## 7. Removal checklist (nothing deleted, nothing applied)
| Item | State | Replacement proof | Pocket per D-T01 | Owner approval |
|---|---|---|---|---|
| `legacy-harpmudd/` (18 files) | moved, no live reader | n/a (unused) | not needed (never shipped, never built) | needed to delete |
| `sim/oracle/tau_scanout1_oracle.sv`, `sim/tb_tau_scanout_diff.v`, Makefile target | in tree; blocked by `test-rtl-scanout-diff` | `tau_scanout.sv`, 12 mutants | **met** (DEV 198) | **needed** |
| `sim/legacy_sdram/` | in tree; blocked by `sim/sdram_baseline/run_baseline.sh`, `sim/sdram_sched_a/run_gfx.sh`, `sim/tb_mp3_fb.v`, `sim/test_check_qsf_files.py` | PHY and scheduler, A-094, soak | **met** (DEV 200) | **needed**, plus what to do with the baseline benches |
| `sim/oracle/tau_talos1_oracle.sv` | keep | none: engine not replaced | **not met** | n/a |
| `fw/` rest (13,316 lines) | blocked by 14 tools and 8 tests | `fw2/` | met (Player 2 ships) | needed |
| shipped RTL files | not started | none | not met | needed |

**Ready-to-apply final removal commit:** `docs/research/provenance-2026-11-25/removal.patch` (NOT applied). It deletes `legacy-harpmudd/`, the scanout oracle, its diff bench and its Makefile target. It does not touch `legacy_sdram`, the engine oracle or `fw/`. Apply only after the owner approves, then run `make test-rtl`.

## 8. What is missing before work starts
1. **Licence and attribution analysis** (R7): nobody has decided what "HarpMudd-origin" means for the licence; `NOTICE.md` and `ATTRIBUTIONS.md` still describe `sim/legacy_sdram` as before; `PROVENANCE.md` says the oracle "is to be deleted".
2. **A target definition**: "0 %" on the blame metric is not reachable (third-party and framework files are blamed on HarpMudd because he imported them) and says little. **Proposed target (Claude, 2026-11-25, for the owner to accept or change): no HarpMudd-authored lines in anything that ships.** In scope: the old `fw/` player, the first-party lineage in `src/fpga/core` (about 1,300 lines, including `tau_talos.sv` and `tau_soc.v`), and the tools and harnesses. Out of scope as removal (attribution only, listed in `NOTICE.md`/`ATTRIBUTIONS.md`): Helix MP3, picojpeg, VexRiscv, the Analogue APF files including the `core_top.v` template, agg23 loader/I2S/FIFO, Quartus-generated files. Simulation references (`sim/oracle`, `sim/legacy_sdram`) do not ship, so they do not count against the target but are deleted once their gate is met. Two measures, because the old player and the tools do not ship: **(A) ships:** manifest rows with verdict ORIGINAL-HARPMUDD or MIXED and `shipped = yes` equal zero. Baseline at 2026-11-25: 7 files, 3,427 lines (`tau_talos.sv`, `tau_soc.v`, `core_game.vh`, `tgt_cmd.v`, `pcm_fifo.v`, `core_constraints.sdc`, and `core_top.v`, which is Analogue's template and should be re-classified THIRD-PARTY, leaving 6 files). **(B) repo:** the old `fw/` player and the HarpMudd-text tools and harnesses are out of the live tree (moved to `legacy-harpmudd/` or archived, then deleted after the gates). Re-run `measure.py` and the manifest after each phase. Whether a given file counts as HarpMudd-authored is the licence question (item 1), not decided here.
3. **Clean-room evidence** for the scanout is honour-based (its record says so); a technical separation is missing.
4. **Release impact**: phases 1, 2 and 6 do not change the release zip; 3-5 change the bitstream (new `CORE_VERSION`, matched firmware, Omega `tau-compat.json` and persist layout checks).
5. **Omega/interop**: Omega already links five dead doc paths (`docs/DOC_MAP.md`); a bitstream change must be announced through `docs/interop`.
6. **Fit budget**: free ALM, M10K and DSP were last measured at T0 (2026-10-27); re-measure before phases 3-5; two seeds each (about 1h45 per fit).
7. **Engine test coverage**: the differential fuzz generator proposed in the Talos review is not built (only the oracle bench `tb_tau_gfx_diff` exists).
8. **Measurement caveats**: generated and vendored files inflate the share; `font_ram.v` (1,211) needs a look; there is no function-level view for RTL.
9. **D-T01 against D-T12**: the engine step is both in scope and parked.

## 9. Owner decisions needed
1. The metric and scope of "remove HarpMudd code" (8.2): **proposed answer: no HarpMudd-authored lines in anything that ships (third-party code stays, with attribution)**.
2. Approve phases 1 and 2 (no fit, no Pocket) and which deletions.
3. Reopen the engine (phase 5) and the SoC (phase 4), or stop at the current state (D-T12).
4. Rewrite or retire each old tool and harness (phase 6).
5. The licence and attribution review and who does it (R7).
6. The canonical copy of the diverged document pairs, and whether to add stubs for the five dead paths Omega links (`DOC_MAP.md`).
