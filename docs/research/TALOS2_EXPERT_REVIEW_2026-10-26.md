# Talos 2: expert review of the plan and features, and the naming of `mp3_fb.sv` (2026-10-26)

Reviewer stance: senior 2D-graphics and Cyclone V / Analogue Pocket engineer reading `docs/features/TALOS2_REIMPLEMENTATION_PLAN.md`, `docs/TALOS.md`, `src/fpga/core/mp3_fb.sv` (header), the TALOS and HarpMudd research and the provenance data. Evidence labels: [DOC] stated in a project document, [READ] read in the source today, [HYP] my engineering judgement, not measured here, to be confirmed before anyone relies on it. Nothing here was simulated or fitted.

## 1. Verdict
The target architecture (front end lowering commands to row jobs, a small back end, in-queue control and a fence) is sound and is what a competent blitter looks like. **The plan as written is not ready to build**, for five reasons: it is justified by correctness hazards that are cheaper to fix alone, it has no measured performance case, it leaves the HarpMudd-lineage scanout untouched while claiming to retire HarpMudd code, parts of it describe the engine as it was before the writable font memory, and it has no new capability that users would see. Fix those and it becomes a good release item.

## 2. What the plan gets right
* One write port per buffer, enforced by a lint check: the class of bug that cost five fixes (B-111, B-114, B-157, B-231, B-327) [DOC].
* Row jobs as the only interface between the halves; every stage registered; scanout keeps absolute priority and waits at most one burst.
* In-queue `SET_FIELD` / `CLUT_WR` / `RC_WR` / `FENCE` instead of execute-time sticky state: removes real hazards (a CLUT reload overtaking a queued draw) [DOC].
* A DQM mask instead of a destination read for keyed blits; a gather burst for scaled blits instead of one SDRAM transaction per pixel [DOC].
* Compatibility contract (register map, opcodes 0-8, same pixels, probe-gated) and a reference renderer diff.
* MLAB sizing is fine [HYP]: a 128 x 17 buffer is about 4 MLABs, a few tens of ALMs, so two of them and a small job FIFO are cheap.

## 3. Findings, most important first
**F1. No measured case.** The plan's baseline is B-146's SDRAM-busy of 15.8% [DOC]. At that load the SDRAM is not the bottleneck; scanout alone is about 8.6 M words/s of a 100 MHz 16-bit port [HYP, from 400 x 360 x 60]. What limits meters and UI is likely the CPU side: MMIO writes per command on a 66 MHz softcore that is also decoding audio, and time spent waiting on the FIFO or the fence. **Before any RTL:** instrument the existing engine and firmware (per frame: commands issued, CPU cycles spent issuing, FIFO-full stalls, fence wait, engine busy cycles split into SDRAM-busy and idle). If CPU issue cost dominates, the best feature is a command list the engine fetches itself (see F8), not a new row pipeline.

**F2. Most of the correctness fixes do not need the rewrite.** In-queue control opcodes and the idle bit/fence register (plan P2) work on today's engine and are already their own phase [DOC]. The 9-bit bar count can be a sticky field. Clipping is done once in the firmware command builder (`fw2/gfx/gfx.c`) today for free. The width limit and page-crossing hazard are not hit by any current caller (stride 512) [DOC]. So P3's real, unique gains are throughput for scaled and palette blits and removing the row-buffer serialisation; F1 decides whether those matter.

**F3. The scanout is HarpMudd code and the plan keeps it.** `mp3_fb.sv` is ONE module of 1,846 lines: its header says the scanout half is adapted from HarpMudd's `pocket_vector_fb.sv` and the engine sits beside it [READ]. The plan's section 4 keeps "the scanout path (line buffer, video timing, `painted` blanking, `scan_vc`, vblank toggle, H2 flip logic) as they are" [DOC]. The provenance data counts 415 of 971 source lines of that file as HarpMudd-origin and 402 of 470 of `sdram_fb.sv` [DOC, 2026-10-18]. **So a Talos 2 engine rewrite alone does not retire the file's HarpMudd origin** (my earlier estimate that it removes about 415 lines was too generous). Retiring it means a fresh scanout (line buffer, CDC of the fill request, HOFF/VOFF/guard band, vblank/H2 flip) and a fresh SDRAM controller, each its own module with its own testbench, written from the contract and not edited out of the old text. Origin is a blame measure, not creative ownership; only genuinely new, validated code lowers it.

**F4. The plan predates the writable font memory.** It describes `OP_CHAR` with "Inter, 16 x 16 cell, 4-bit coverage" from a ROM and a `FONT` source kind that reads "the font ROM" [DOC]. The shipped engine has `font_ram.v` (writable atlas, four `.tfnt` faces, Latin-1, `text_light`) [READ: `FONT_RAM_ENABLE` parameter and the mutation hooks]. The `FONT` source kind must be redefined on the font RAM, the gamma/`text_light` tables kept bit-exact, and glyph column clipping specified: the clip rectangle must be able to start inside a glyph (the plan's `aux` carries only the row index and Bresenham state) [HYP].

**F5. Design this for the glyph sources still to come.** Extended Latin and CJK (owner scope) need a glyph source that is not the on-chip atlas, most likely a PSRAM-streamed bitmap through a cache [DOC: FONT_SPEC, PHASE_F_SPEC]. The row-job abstraction makes that a new `src_kind` (GLYPH_PSRAM) rather than a new bus client in the engine's hottest loop, which FONT_SPEC rejected for timing risk. Reserve the src_kind encoding and a back-end SRC-stage slot now.

**F4b. Fence and idle crossing.** The plan says "two-flop synchroniser" for the idle bit and a firmware-chosen token register [DOC]. One bit through two flops is fine; a multi-bit token is not. Use a monotonic fence COUNTER crossed with the existing Gray-code counter (`tau_cdc_gray_ctr.sv` is already in the tree) and have firmware compare counts; it also removes the "arbitrary token" meaning.

**F6. Ping-pong buffers hide pixel work, not SDRAM turnaround.** Source reads and destination writes still alternate on one port [DOC, section 5.3]; the cost of a copy row is dominated by activate/precharge and read-to-write turnaround, not by the CLUT or blend stage, which are already pipelined [HYP]. Two cheap ideas to test before choosing the structure: (a) place draw targets and blit sources (stash planes, atlases) in different SDRAM banks at zero RTL cost: the controller already takes the bank from the upper two address bits [READ: `sdram_fb.sv`], whether it keeps rows open per bank or precharges all must be read first [HYP]; (b) open-row tracking to skip a precharge when consecutive rows are in the same page. Bank interleaving was assessed and rejected in the review as too complex [DOC]; address placement alone is not the same thing.

**F7. The refresh change is global and rated too lightly.** "Refresh before a read only when the read could outlast the refresh interval" and "make the mid-burst refresh branch safe" change every SDRAM client including the CPU window and scanout [DOC, section 7]. It buys little for 128-word bursts and a lot for single-word CPU reads. Treat it as its own commit, own testbench, A-094 before and after, as the plan already says, and **do not bundle it with the engine rewrite in one fit**; ship it first or last.

**F8. Missing features that have real demand.**
1. **Column-list / strip primitive.** The hardware wave and scope path has been compiled out since B-302 because 256 columns cost about 21 times a normal meter (roadmap item 2, a still-open row) [DOC]. One command that draws N `(x, y0, y1)` entries from a small RAM the CPU fills would revive it and also serves the Waterfall columns and Layered Wave. This is the feature that justifies a "Talos 2".
2. **Command ring.** A ring in SDRAM/PSRAM that the engine fetches (a display list) with a kick register: removes per-command MMIO cost, gives the `graphics/talos` API of the architecture plan real transactions, and pairs with the fence counter. Prove its value with F1's numbers first.
3. **B13 gradient fill** (cost was found bigger than scoped [DOC]; much cheaper as a row-job `src_kind` GRADIENT than as a special case).
4. Keep B19 (flip) out: it hits the timing-critical write network [DOC]. B10 (RLE) has little value [DOC].
5. Optional: pattern/tiled fill (a `src_kind` of one repeated row) for backgrounds.

**F9. Verification is good but should be differential and adversarial.** Beyond per-opcode pixel parity: random command streams (opcode, geometry, clip, sticky changes, fence points, FIFO pressure) run on Talos 1 and Talos 2 testbenches and compared pixel for pixel (differential fuzz); an SDRAM model that checks tRCD/tRP/tRFC and refresh interval; a scanout-underflow assertion under a blit storm; assertions that every buffer has one write site; and mutation tests rewired, as planned. Keep the old engine behind `TAU_TALOS1` until the new one is hardware-confirmed [DOC], then delete it in the same release that removes the origin lines.

**F10. Budget numbers are stale.** The plan's constraints (17,285 ALMs, 240 M10K in `mp3_fb`'s combined build) predate the 12 M10K font memory and the statistics block [DOC]. Re-measure the free ALM, M10K and DSP from the `stats-b821` fit report before promising "0 extra M10K".

## 4. Naming: `mp3_fb.sv` is the wrong name
`mp3_fb` means "the MP3 player's framebuffer", from the project's first life. The module is the SDRAM framebuffer scanout AND the 2D draw engine; nothing in it is about MP3 (the MP3 hardware is `tau_mp3_poly.sv`). It is the only major block still named `mp3_*` besides `mp3_soc.v` (the whole SoC, equally misnamed). The repo's convention is `tau_*` for Tau's own modules and the plan already names `talos_front.sv`/`talos_back.sv`.

Proposed names (one rename, done inside the v0.7 RTL fit bundle, never as a separate fit because instance names reach the SDC and can move timing):
| Now | Proposed | Notes |
|---|---|---|
| `mp3_fb.sv` (module `mp3_fb`) | `tau_gfx.sv` (top: scanout + Talos + SDRAM client wiring) | split before renaming, see below |
| engine inside it | `tau_talos.sv`, later `tau_talos_front.sv`, `tau_talos_back.sv` | Talos stays the engine's name |
| scanout inside it | `tau_scanout.sv` | the HarpMudd-lineage part, its own module and testbench: also the first step of F3 |
| `src/fpga/rtl/mem/sdram_fb.sv` | `tau_sdram_ctrl.sv` | vendored-mixed today |
| `mp3_soc.v` | `tau_soc.v` | the SoC |
| `sim/tb_mp3_fb.v` and the `*_blit_g3_*qsf_append*` files | follow | `tools/*qsf_append*`, `ap_core.qsf` line 784, mutation Makefile targets (`make test-rtl-fb-mutation`) |
Mechanics: `git mv` (history and blame-based origin tracking keep working, the measure tool uses copy-aware blame); one commit that only renames, host tests green, then the fit. Splitting the file first (no behaviour change, differential test against the old module) is itself the safest first RTL step and enables everything else.

## 5. Revised phase plan (proposal)
| Phase | Content | Fit |
|---|---|---|
| T0 | Measure (F1): per-frame command, CPU-issue and engine counters on the shipped build; re-measure free resources (F10) | no |
| T1 | Firmware: stacked-bar split (P0), clipping in `gfx.c`, firmware-side page/width guards | no |
| T2 | Split `mp3_fb.sv` into `tau_gfx` / `tau_scanout` / `tau_talos` with a differential test, then the renames | v0.7 RTL bundle |
| T3 | P2: in-queue control opcodes, idle bit, fence COUNTER (Gray), 9-bit bar; firmware `gfx_fence()` | v0.7 RTL bundle |
| T4 | Column-list primitive (F8.1), with the lab twin and the wave/scope path re-enabled | own fit or the bundle if it stays small |
| T5 | Row-job engine (P3) only if T0 shows the engine itself is the bottleneck; fresh scanout and SDRAM controller as separate modules; `TAU_TALOS1` kept until hardware-confirmed | own fit, then old code deleted |
| T6 | Command ring, gradient and pattern `src_kind`s, glyph `src_kind` for Extended/CJK | after T5 |
**Release fit:** T2 + T3 (+ T4 if small) in the v0.7 bundle; T5 and later are not v0.7 unless the owner holds the release.

## 6. Open questions for the owner
1. Scope of "remove HarpMudd code": engine only, or engine + scanout + SDRAM controller (F3)? The honest cost of the second is three new, verified modules.
2. Approve the renames (section 4), in the v0.7 RTL bundle?
3. Is the column-list primitive (revives the hardware scope) the headline Talos feature, ahead of the row-job rewrite?
