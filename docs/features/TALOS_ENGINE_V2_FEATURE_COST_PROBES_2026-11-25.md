# Talos engine v2: feature-cost map probes (2026-11-25)

Status: **PARTIAL. The Tau Alpha half is measured (two configurations, VM, map only). The Gallery half was NOT run: the Gallery archive bundle lives on the external volume `MONSTER`, which is not mounted on this Mac, so Gallery's `tau_talos.sv` was never readable.** Nothing about Gallery's feature costs is measured here; the tooling is ready and the missing runs are listed in section 7. Decisions behind this: D-T19 (the cost of each feature is measured before it is agreed) and D-T22 (map-only probes approved). Scoping: `TALOS_ENGINE_V2_GALLERY_FEATURES_SCOPING_2026-11-25.md` section 4, `TALOS_ENGINE_V2_SCOPING_2026-11-25.md`.

## 1. Method

* One standalone Quartus project per configuration, top = `tau_talos` itself (all ports are pins, so nothing is optimised away for lack of a consumer), device 5CEBA4F23C8, the global synthesis settings of `tools/sdram_probe/sdram_probe.qsf`. Sources: the repository's own `src/fpga/core/tau_talos.sv`, `tau_cdc_gray_ctr.sv`, `font_rom.v`, `font_ram.v` (staged flat). Parameters are set with `set_parameter` lines appended to the QSF, macros with `VERILOG_MACRO`.
* **`quartus_map` only** (analysis and synthesis): no fitter, no full compile. Stage `~/tau-local/probe-talos-feat-NAME` on the VM, one probe at a time, guards on load, free memory and other projects' fit jobs (`tools/probes/talos_feat/run_probe.py`).
* Reported from the map report: "Estimate of Logic utilization (ALMs needed)" (map prints `N/A` in its summary, the estimate is in the resource table), combinational ALUTs, registers, block memory bits, DSP blocks, M10K derived per RAM (see caveats), warnings.
* Quartus 25.1std Lite, the VM's own toolchain (same as every shipped fit).

Commands (from the repository root; each prints the summary and leaves `work/talos_feat/NAME/`):

```
python3 tools/probes/talos_feat/run_probe.py ta-default
python3 tools/probes/talos_feat/run_probe.py ta-shipped --param BLIT_BLEND_ENABLE=1 --param DBUF_ENABLE=1 --param FONT_RAM_ENABLE=1 --macro TAU_MLAB_MIGRATE --macro TAU_FONT_REPACK
python3 tools/probes/talos_feat/summarize.py
```

"Shipped" parameters: `core_game.vh` instantiates `tau_gfx` with `TAU_BLIT_BLEND_EN`, `TAU_DBUF_EN`, `TAU_FONT_RAM_EN`; the v0.7.5 release macros (`tools/release_075_qsf_append.txt`) define `TAU_BLIT`, `TAU_BLIT_BLEND`, `TAU_DBUF`, `TAU_FONT_RAM`, `TAU_MLAB_MIGRATE`, `TAU_FONT_REPACK`, so all three are 1. `FILL_WORDS` (400) and `CBLIT_BURST` (1) are the engine defaults and `core_game.vh` does not override them.

## 2. Results (Tau Alpha main engine, MEASURED at map level)

| Configuration | ALMs (est.) | ALUTs | Registers | Block bits | M10K (derived floor) | DSP | Latches | Map time |
|---|---|---|---|---|---|---|---|---|
| `ta-default`: main `tau_talos.sv`, every parameter at its default (blend, dbuf, font RAM off) | 1,317 | 1,920 | 1,121 | 125,440 | 15 (+2 auto) | 3 | 0 | 176 s |
| `ta-shipped`: BLEND=1 DBUF=1 FONT_RAM=1 + the two macros (what ships) | **1,489** | 2,120 | 1,219 | 221,696 | 24 (+1 auto) | **6** | 0 | 268 s |

Warnings that matter: none new. Two "unintended latch behaviour" warnings on `blend_ch` locals (task variables `dsp` and `sum`, default configuration only) and the usual stuck-at output pins (`p0_byte_en`, upper length bits, `fill_seen` constant at map level); no inferred latch, no dropped feature logic beyond what the parameters switch off. The 268 s run overlapped another project's fit on the VM (load 3 to 4), the 176 s run overlapped that project's map.

## 3. Calibration against the fitted figures

The fitted per-entity figure is in `docs/research/TALOS_T0_BASELINE_2026-10-26.md` (B-891 era, `stats-b821-s1`): `mp3_fb:u_fb` (scanout + engine + command FIFO + font RAM + CLUT) **1,532 ALMs, 38 M10K, 6 DSP, 238,080 block bits**.

* **Block bits match exactly:** `ta-shipped` 221,696 + the scanout line buffer (1,024 x 16 = 16,384) = 238,080. So the same memories are present.
* **DSP matches exactly:** 6 (3 in the default configuration, the blend adds 3).
* **ALMs:** the engine alone at map level is 1,489; the fitted entity adds the scanout (the T0 note puts it at roughly 100 to 150 ALMs) and the two row-buffer copies. 1,489 + scanout is within about 5 % of 1,532 either way: the map estimate and the fit agree to that order for this entity, so the baseline map number is a usable reference. This is one pair of numbers, not a general law.
* **M10K does NOT reconcile with my derived column:** fitted 38, derived 24 + 1 + 2 (line buffer) + 1 (CLUT). The block bits agree, so the gap is how the fitter shapes the 6,112 x 32 font RAM (about 32 blocks implied, against 21 for the cheapest aspect ratio). The "derived" column is therefore a FLOOR; use block bits for relative comparison and the fitted 38 for budgeting. No per-feature M10K claim should rest on the derived column.

## 4. What the map numbers support, and what they do not

Supported: the relative cost of a feature (the difference of two map runs of the same file on the same settings, ALUT/register/bit/DSP counts), and the check that "all off" equals "no feature code" (D-T19 proof item 2).
Not supported: the final fitted ALM count (no packing, no physical synthesis, no LAB legalisation: the real ceiling is LABs at 93 %), timing (worst setup +0.655 ns on the SDRAM capture path, hold +0.078 ns), routing success (Gallery's m1f seed 1 died in routing), M10K block counts (see above). Any map-level delta is approximate and should be read as a ranking, not an invoice.

## 5. What it means for the sets and the budget

* **TAU set (all off): unchanged by anything measured here.** The probe confirms the reference: the engine as shipped is 1,489 ALMs at map level, 6 DSP, 221,696 block bits. The budget (84 % ALMs, 274 to 281 of 308 M10K, 23 of 66 DSP) is for the whole core; no feature that is off may move any of these, and the D-T19 proof (all-off map equal to the pre-feature file) still has to be run on the rewritten engine with exactly this recipe.
* **GALLERY set: not measured.** Scale only, from the baseline: the scoping's +0.9 to 1.7 k ALMs for the Gallery set would be +60 to +115 % of this engine's whole 1,489; +1 DSP would take the engine from 6 to 7. That sizing is the reason the probes matter, but it is not a result.

## 6. Do the scoping's estimates hold?

**Unknown, not corrected.** No Gallery engine file was readable, so the estimates (+0.9 to 1.7 k ALM, +11 to 13 M10K, +1 DSP) are neither confirmed nor refuted. One thing is already visible without Gallery: the +11 to 13 M10K measured for the viewer bundle (B-1005) is mostly the viewer line buffer and landing FIFO (8 + 2 M10K by its own note), which live in `tau_scanout_v`/the viewer path and not in `tau_talos.sv`; a probe of the engine file alone will show only the engine's share of it. Plan the engine-file probe and a separate scan-out probe if the viewer M10K is to be attributed.

## 7. Still to run (needs `MONSTER` mounted; about 30 minutes of VM time)

1. Clone the bundle read-only into a new empty directory under `work/`, check out `26d9b1c`, read how `core_game.vh` there sets `BLIT_BLEND_ENABLE` and its other `tau_gfx` parameters (the Gallery engine has `VIEW_ENABLE`, which main's does not).
2. Probes: `gal-default` (VIEW_ENABLE=0), `gal-view` (VIEW_ENABLE=1), both with the parameters Gallery uses; a baseline repeat of `ta-shipped` with identical settings is already above (like for like: same QSF, same VM, same toolchain).
3. Scratch-copy variants (not committed): in a copy of Gallery's file wrap each feature in a macro added to the copy only (`FEAT_NO_CORNERS` around the OP_CORNERS arm and its registers, `FEAT_NO_DSBLIT` around the OP_DSBLIT arm and the scaled-blit reuse, `FEAT_NO_VIEWFILL` around the viewer fill states and idle arm, `FEAT_NO_DOUBLER` for the doubler line select if it lives in this file); run `run_probe.py NAME --engine <scratch copy> --macro FEAT_NO_CORNERS ...` for: all on, minus CORNERS, minus DSBLIT, minus viewer fill, minus doubler, none (close to `ta-shipped`). The patch is described, not stored, because it would copy Gallery text into this tree; the deltas are the ranking.
4. Fill the table of section 2 and replace section 6 with a verdict.

## 8. Files and leftovers

* `tools/probes/talos_feat/`: `talos_feat.qpf`, `talos_feat.qsf`, `run_probe.py` (one configuration), `summarize.py` (table from the pulled reports). Pulled reports: `work/talos_feat/` (git-ignored).
* On the VM, left for the owner to clean (small): `~/tau-local/probe-talos-feat-ta-default`, `~/tau-local/probe-talos-feat-ta-shipped`.
