# Talos engine v2: feature-cost map probes (2026-11-25)

Status: **COMPLETE at map level: ten configurations on the VM (Tau Alpha main twice, Tau Gallery's engine at 26d9b1c eight ways), `quartus_map` only, none fitted.** (A first version of this report was written before a read-only local clone of Gallery's bundle was available and covered only Tau Alpha; this version supersedes it.) Decisions behind this: D-T19 (the cost of each feature is measured before it is agreed) and D-T22 (map-only probes approved). Scoping: `TALOS_ENGINE_V2_GALLERY_FEATURES_SCOPING_2026-11-25.md` section 4, `TALOS_ENGINE_V2_SCOPING_2026-11-25.md`.

## 1. Method

* One standalone Quartus project per configuration, top = `tau_talos` itself (all ports are pins, so nothing is optimised away for lack of a consumer), device 5CEBA4F23C8, the global synthesis settings of `tools/sdram_probe/sdram_probe.qsf`. Sources: the repository's own `src/fpga/core/tau_talos.sv`, `tau_cdc_gray_ctr.sv`, `font_rom.v`, `font_ram.v` (staged flat). Parameters are set with `set_parameter` lines appended to the QSF, macros with `VERILOG_MACRO`.
* **`quartus_map` only** (analysis and synthesis): no fitter, no full compile. Stage `~/tau-local/probe-talos-feat-NAME` on the VM, one probe at a time, guards on load, free memory and other projects' fit jobs (`tools/probes/talos_feat/run_probe.py`).
* Reported from the map report: "Estimate of Logic utilization (ALMs needed)" (map prints `N/A` in its summary, the estimate is in the resource table), combinational ALUTs, registers, block memory bits, DSP blocks, M10K derived per RAM (see caveats), warnings.
* Quartus 25.1std Lite, the VM's own toolchain (same as every shipped fit).

Commands (from the repository root; each prints the summary and leaves `work/talos_feat/NAME/`):

```
python3 tools/probes/talos_feat/run_probe.py ta-default
python3 tools/probes/talos_feat/run_probe.py ta-shipped --param BLIT_BLEND_ENABLE=1 --param DBUF_ENABLE=1 --param FONT_RAM_ENABLE=1 --macro TAU_MLAB_MIGRATE --macro TAU_FONT_REPACK
python3 tools/probes/talos_feat/make_variants.py <gallery tau_talos.sv> work/gal/tau_talos_scratch.sv
# Gallery: --engine work/gal/tau_talos_gal.sv (as shipped) or the scratch copy, same --param/--macro trio as above, plus
#   --param VIEW_ENABLE=0|1 and --macro NO_CORNERS / NO_DSBLIT / NO_DOUBLER (scratch copy only)
python3 tools/probes/talos_feat/summarize.py
```

"Shipped" parameters: `core_game.vh` instantiates `tau_gfx` with `TAU_BLIT_BLEND_EN`, `TAU_DBUF_EN`, `TAU_FONT_RAM_EN`; the v0.7.5 release macros (`tools/release_075_qsf_append.txt`) define `TAU_BLIT`, `TAU_BLIT_BLEND`, `TAU_DBUF`, `TAU_FONT_RAM`, `TAU_MLAB_MIGRATE`, `TAU_FONT_REPACK`, so all three are 1. `FILL_WORDS` (400) and `CBLIT_BURST` (1) are the engine defaults and `core_game.vh` does not override them.

## 2. Results (Tau Alpha main engine, MEASURED at map level)

| Configuration | ALMs (est.) | ALUTs | Registers | Block bits | M10K (derived floor) | DSP | Latches | Map time |
|---|---|---|---|---|---|---|---|---|
| `ta-default`: main `tau_talos.sv`, every parameter at its default (blend, dbuf, font RAM off) | 1,317 | 1,920 | 1,121 | 125,440 | 15 (+2 auto) | 3 | 0 | 176 s |
| `ta-shipped`: BLEND=1 DBUF=1 FONT_RAM=1 + the two macros (what ships) | **1,489** | 2,120 | 1,219 | 221,696 | 24 (+1 auto) | **6** | 0 | 268 s |
| `gal-default`: Gallery's file as shipped, same trio and macros, VIEW_ENABLE=0 (CORNERS and DSBLIT are always built in it) | 1,766 | 2,545 | 1,260 | 221,696 | 24 (+1) | 10 | 0 | 255 s |
| `gal-view`: the same, VIEW_ENABLE=1 (the Gallery set) | **1,902** | 2,732 | 1,433 | 221,696 | 24 (+1) | **11** | 0 | 254 s |
| `sc-all`: scratch copy, nothing compiled out, VIEW_ENABLE=1 (control: must equal `gal-view`) | 1,902 | 2,732 | 1,433 | 221,696 | 24 (+1) | 11 | 0 | 247 s |
| `sc-nocorners`: minus OP_CORNERS | 1,760 | 2,596 | 1,433 | 221,696 | 24 (+1) | 11 | 0 | 239 s |
| `sc-nodsblit`: minus OP_DSBLIT | 1,772 | 2,498 | 1,392 | 221,696 | 24 (+1) | 7 | 0 | 245 s |
| `sc-noview`: minus viewer fill (VIEW_ENABLE=0) | 1,766 | 2,545 | 1,260 | 221,696 | 24 (+1) | 10 | 0 | 236 s |
| `sc-nodoubler`: minus the l/2 line select | 1,893 | 2,717 | 1,432 | 221,696 | 24 (+1) | 11 | 0 | 241 s |
| `sc-none`: all four compiled out (the "all off" file) | 1,481 | 2,125 | 1,219 | 221,696 | 24 (+1) | 6 | 0 | 232 s |

**Per-feature deltas against `sc-all` (map level, ALMs / ALUTs / registers / DSP):** OP_CORNERS 142 / 136 / 0 / 0; OP_DSBLIT 130 / 234 / 41 / **4**; viewer fill 136 / 187 / 173 / **1** (the doubler's l/2 select inside it: 9 ALMs, 15 ALUTs, 1 register); all four together 421 / 607 / 214 / 5. The three big features sum to 408 and the doubler adds 9: additive to within 4 ALMs, so there is no hidden interaction. **No feature adds block memory** in this file (221,696 bits in every row): the viewer's line buffer is not in the engine. **All-off check (D-T19 proof item 2):** `sc-none` has the same registers (1,219), DSP (6), block bits as `ta-shipped`; ALMs 1,481 against 1,489 (-8) and ALUTs 2,125 against 2,120 (+5): equal within synthesis noise at map level, not bit-identical, so the exact-equality form of the proof needs the rewritten engine's own all-off probe. Gallery's file has other small differences from main beyond the four features (mutation hooks, ports), which is where the -8 comes from.

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

* **TAU set (all off): costs nothing.** `sc-none` equals `ta-shipped` in registers, DSP and memory and is within 8 ALMs; the parameters do their job at map level. The budget (84 % ALMs, 274 to 281 of 308 M10K, 23 of 66 DSP) is untouched as long as nothing is on.
* **GALLERY set (all on) in the engine file: +421 ALMs (+28 % of the engine's 1,489, about +2.3 % of the device's 18,480), +607 ALUTs, +214 registers, +5 DSP, +0 M10K.** Gallery's DSP total goes from 23 to about 28 of 66 on this evidence (42 %; m1f reported 53 of 66 with everything else in it, the engine's own 11 are part of that). **DSBLIT is the surprise: 4 of the 5 DSP** (a 2 x 2 box average should not need multipliers: the scaled-extent function `dsb_ext` and the tap step logic are mapped onto DSP blocks; a rewrite can write them as shifts and adds and probably recover the 4 DSP, which is worth doing because DSP is not Gallery's tight resource but ALMs and LABs are).
* **Ranking of cost:** CORNERS 142 ALMs ~ viewer fill 136 ALMs (but the viewer fill carries 173 registers and the one DSP) ~ DSBLIT 130 ALMs (but 234 ALUTs and 4 DSP); the doubler select is negligible (9 ALMs).
* **Per-set verdict for fit planning:** the engine adds about 2 % of the device for Gallery and nothing for Tau; the real fit risk for Gallery stays the whole-core figures (91 % ALMs, 94 % M10K) and timing, which map level cannot judge.

## 6. Do the scoping's estimates hold?

* **ALMs: corrected downward.** Scoping +0.9 to 1.7 k for the Gallery set; measured +0.42 k at map level for the engine file (CORNERS + DSBLIT + viewer fill + doubler). Packing and physical synthesis move a fitted figure, but a factor of 2 to 4 is outside any plausible fit correction. The estimate is too high for the engine file; it may still hold for the viewer scan-out plus engine together (the scan-out is not in this file and was not probed).
* **M10K: not an engine cost.** Scoping +11 to 13; measured +0 in the engine file. The +13 of B-1005 is the viewer line buffer (8 by its own note), the landing FIFO and similar, in `tau_scanout_v` and the landing path. The engine file is not where to budget it; the scan-out second step (D-T22) must be probed on its own.
* **DSP: corrected upward.** Scoping +1; measured **+5** (+4 DSBLIT, +1 viewer fill). The B-1005 +1 DSP measured the viewer bundle only, and DSBLIT (a later Gallery 1.0 addition) was not in it.

## 7. Method notes on the scratch copy

* Gallery's `tau_talos.sv` (HEAD 26d9b1c, 2,057 lines) was copied from a read-only local clone to `work/gal/` (git-ignored); the leaf modules (`font_rom.v`, `font_ram.v`, `tau_cdc_gray_ctr.sv`) are byte-identical to Tau Alpha's. `tools/probes/talos_feat/make_variants.py GALLERY_FILE OUT.sv` builds the scratch copy: it wraps the `OP_CORNERS: begin ... end` and `OP_DSBLIT: begin ... end` dispatch arms in `` `ifndef NO_CORNERS `` / `` `ifndef NO_DSBLIT `` and turns the `src_line` select into `fill_line` under `` `ifdef NO_DOUBLER ``; nothing else changes. Gallery's text is not stored in this repository, only the patcher. The viewer fill is removed by the existing `VIEW_ENABLE` parameter. The control run `sc-all` equals `gal-view` in every figure, so the patch itself costs nothing.
* A removed arm leaves the registers it alone set (`dsb_*`, `q_dsb_out_*`, `vdbl_l`) assigned but never read; Quartus removes them (the warnings rise from 9 to 14 in `sc-none`), which is what the register deltas show.
* Parameters of all Gallery runs: BLIT_BLEND_ENABLE, DBUF_ENABLE and FONT_RAM_ENABLE = 1 with TAU_MLAB_MIGRATE and TAU_FONT_REPACK, as Gallery's `core_game.vh` passes them (it instantiates `tau_gfx` with `TAU_BLIT_BLEND_EN`, `TAU_DBUF_EN`, `TAU_FONT_RAM_EN`, `TAU_VIEW_EN`, the same macros as Tau Alpha plus `TAU_VIEW`); I did not check Gallery's own release macro list, so "blend, dbuf, font RAM on" is an assumption shared with Tau Alpha. `VIEW_ENABLE` is the only extra Gallery parameter.
* Run times: 232 to 268 s each, about 34 minutes for the eight Gallery runs, sequential, with another project's fit running on the VM the whole time (load 2.5 to 4).

## 8. Files and leftovers

* `tools/probes/talos_feat/`: `talos_feat.qpf`, `talos_feat.qsf`, `run_probe.py` (one configuration), `make_variants.py` (the scratch-copy patcher), `summarize.py` (table from the pulled reports). Pulled reports: `work/talos_feat/` (git-ignored).
* On the VM, left for the owner to clean (small), all under `~/tau-local/`: `probe-talos-feat-ta-default`, `-ta-shipped`, `-gal-default`, `-gal-view`, `-sc-all`, `-sc-nocorners`, `-sc-nodsblit`, `-sc-noview`, `-sc-nodoubler`, `-sc-none`.

