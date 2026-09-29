# 720 test plan (branch `test/720`)

Status: **T1 built and simulation-verified, not fitted, not on hardware** (2026-09-29, B-374).

> **Implement from `VIDEO_720_PHASED_SPEC.md`** (B-376): Phase 1 (cross-resolution build, 720 switch in
> Diagnostics) and Phase 2 (native 720 test). This file stays the background analysis. Baseline for every build is
> the 192 KB stack with all current hardware features; the 256 KB rows below are informational only.
Scope of this branch: prove 800x720 output on the Pocket in small, reversible steps before any firmware or
memory-map work. 720 stays **last** on the roadmap (`docs/ROADMAP.md`, `ARCHITECTURE_ROADMAP.md` Phase H);
this branch only answers the questions that decide whether it is worth doing and what it will cost.

Sources: Analogue developer docs via the `analogue-pocket-dev` skill (`references/hardware-video-audio-input.md`,
`references/json-files.md`, KB-014, KB-015, KB-021, KB-027), `src/fpga/core/mp3_fb.sv`, `core_game.vh`,
`mf_pllbase_0002.v`, `fw/player.c`, `fw/helios.inc`, and this project's fit history in `docs/AUDIT_TRAIL.md`.
Figures marked **[EST]** are arithmetic, not measurements.

## 1. What the platform allows (docs-verified, skill)

| Limit | Value | Source |
|---|---|---|
| Video size | 16x16 to **800x720** | skill `hardware-video-audio-input.md` (Analogue "Bus communication") |
| Refresh | 47 to ~61 Hz | same |
| Pixel clock | 1 to **~50 MHz** | same |
| Pre-scaled height | Y max 720 | same |
| Panel | 1600x1440, so 800x720 is an exact 2x integer scale (400x360 is 4x) | `core_game.vh` PLL comment |
| Scaler slots | up to 8 in `video.json`; the core picks one **at runtime** with end-of-line bits (effective next frame) | skill `json-files.md` |
| PLL reconfiguration | unreliable on the 74.25 MHz source; ship fixed clocks | KB-015 (community-reported) |
| Bus rules | RGB 0 outside DE; HS at least 3 clocks after VS; VS never inside HS; >= 1 clock between HS and DE; a VS inside HS has wedged the scaler until a power cycle | skill + KB-014 |

800x720 is the ceiling. Nothing above it is possible, and 800 x 720 x 60 Hz needs at least ~36 MHz.

## 2. Clock plan

All four PLL outputs share one 600 MHz VCO (`mf_pllbase_0002.v`): clk_sys 60 (N=10) or 66.667 (N=9, TAU_CLK66),
clk_sdram 100 (N=6), clk_vid 12 (N=50). **600/16 = 37.5 MHz** is a clean divider on the same VCO, so the pixel
clock can change without touching the CPU or SDRAM clocks and without PLL reconfiguration:

- 850 x 735 clocks per frame = 624,750 -> **60.02 Hz**; 800x720 active, 50-clock horizontal and 15-line vertical blank.
- 90-degree output (`clk_vid_90`): 6667 ps.
- 37.5 MHz is 75% of the ~50 MHz limit. 600/12 = 50 MHz exists but sits on the limit; do not use it.

Trade-off (measure on hardware): vertical blanking shrinks from 40 lines x 41.7 us = 1.67 ms to
15 x 22.7 us = **0.34 ms**. Anything that relies on "draw in vblank" (Helios H2 flip, beam-gated meter draws)
gets less free time per frame. `V_TOT`/`H_TOT` are parameters: 833 x 750 (0.67 ms blank, 33-clock
horizontal blank) is the next option if 0.34 ms proves too short.

## 3. Staged test ladder

| Step | What | RTL | Firmware | Answers |
|---|---|---|---|---|
| **T1** (built) | 800x720 timing at 37.5 MHz, the unchanged 400x360 framebuffer **doubled at scanout** (each row fetched once, shown on 2 lines, each pixel twice) | `VID720` parameter in `mp3_fb.sv`, `TAU_VID720` macro, PLL | **none** (alpha.15 ROM reused as-is) | Does the Pocket and the dock accept our 800x720 stream? PLL, 37.5 MHz timing closure, scaler behaviour, power draw -- with zero change in SDRAM load |
| T2 | Same bitstream switches 360 <-> 720 at runtime: `video.json` declares both slots, timing generator picks active size, scaler slot via end-of-line bits | small | Settings toggle | Is a runtime switch safe (KB-014 wedge risk)? One bitstream instead of two |
| T3 | Native 800x720 framebuffer at its own SDRAM base, test-pattern page only (1 px checkerboard, gradients, 1x vs 2x text) + blit-storm Check | widening (section 5) | new diagnostic page, CORE_VERSION interlock | **The real Phase H gate:** measured SDRAM busy %, audio L0 under scanout + blit load, draw-throughput cost |
| T4 (option) | 8 bpp indexed framebuffer via a scanout CLUT | medium | palette-aware drawing | Halves 720 scanout and draw traffic |

Only T1 is built. Each step keeps the previous one's evidence meaningful: T1 changes nothing the firmware can see
(frame rate, framebuffer, `scan_vc` numbering and fill count are identical), so any hardware difference is the
video path alone.

## 4. T1 as built

- `mp3_fb.sv` `VID720` (default 0): output geometry `O_H_ACT/O_V_ACT/H_TOT/V_TOT/HOFF/VOFF` switch; framebuffer
  `H_ACT/V_ACT/STRIDE` do not. Fill is requested only before a row's first output line; the line buffer is read at
  `{vsub[1], hcsub[9:1]}`. **No new M10K** (still 2 blocks of line buffer), no new logic in `clk_sdram`.
- `scan_vc = vc[9:1]` in 720 mode: active lines 8..727 map to 4..363, exactly the 360-mode numbering
  `fw/helios.inc` (`HELIOS_VOFF 4`, `HELIOS_VACT 360`) expects; blanking reads 364..367.
- `mf_pllbase_0002.v`: outclk_1/2 37.5 MHz under `TAU_VID720`.
- `core_game.vh`: `TAU_VID720` -> `TAU_VID720_EN` -> `mp3_fb #(.VID720(...))`.
- `tools/vid720/video.json`: one scaler mode 800x720, aspect 10:9.
- `tools/vid720/t1_qsf_append.txt`: the `all6-combined` macro set (alpha.15's bitstream) + `TAU_VID720=1`, so the
  T1 bitstream pairs with **alpha.15's exact ROM and cold image** and differs from it only in the video path.

Verified (`make test-rtl-fb-vid720`, `sim/tb_mp3_fb_vid720.v`, four frames each at real clock ratios):

| Check | VID720=0 (shipped) | VID720=1 |
|---|---|---|
| Frame period | 200,000 clocks (500x400) | 624,750 clocks (850x735) |
| Active lines / pixels per line | 360 / 400 | 720 / 800 |
| Every pixel = the expected framebuffer word | pass | pass (row y>>1, column x>>1) |
| SDRAM fills per frame | 360 | **360** (unchanged load) |
| `scan_vc` at first active line / max | 4 / 399 | 4 / 367 |
| APF bus rules (RGB 0 outside DE, HS vs VS, HS/DE gap) | pass | pass |
| Checker mutant (expects undoubled columns) | -- | killed |

RTL mutants run once by hand (not in the Makefile, they need a sed-edited copy): "fetch on every line" (fails the
fill count) and "read without column doubling" (fails the pixel check) -- results in `docs/AUDIT_TRAIL.md` B-374.

Not verified: Quartus fit/timing (clk_vid at 37.5 MHz is a new constraint for the video pipeline and the
`clk_vid -> clk_sys` CDCs), and anything on the Pocket.

### T1 hardware test (when approved)

1. Fit: `python3 tools/vm_fit.py launch vid720-t1 --append tools/vid720/t1_qsf_append.txt --seed 1 --seed 2`.
   Expect RAM/DSP identical to `all6-combined`; watch the 37.5 MHz clk_vid domain and the hold slack, which was
   already razor-thin on `all6-combined` (+0.037 ns, B-371).
2. Package with alpha.15's ROM/cold image, the T1 RBF (copied directly, never re-reversed -- B-353) and
   `tools/vid720/video.json` in place of the core's `video.json`. Install additively with `tools/install_dev_core.py`.
3. On the Pocket, compare with alpha.15:
   - picture identical, no shimmer or dropped edge columns/rows; screenshot size is 800x720;
   - Info > VBLANK ~60/s; Info > BEAM moving; Diagnostics STANDARD Check: 0 late underruns, SDRAM busy %
     the same as alpha.15 (it must be -- same fill count);
   - dock output (1080p) and handheld;
   - battery drain over a fixed playback period (`docs/features/BATTERY_AND_POWER_PLAN.md`);
   - if video disappears across all cores, power-cycle the Pocket before debugging (KB-014).

## 5. Dependencies and risks for native 720 (T3 and beyond)

### 5.1 SDRAM bandwidth (the main risk)

SDRAM: 16-bit, 100 MHz, one port shared by scanout fill, all draw opcodes and the CPU SDRAM window.

| Mode | Words fetched per frame | Share of SDRAM cycles **[EST]** | Worst-case fill slack per line |
|---|---|---|---|
| Today (fill 512 per row) | 184,320 | ~11-12% | ~3,650 cycles (41.7 us line) |
| Today, fill only 400 active words | 144,000 | ~9% | ~3,760 |
| T1 (doubled) | 184,320 | ~11-12% (measured in sim: same fill count) | ~4,000 (one fill per 45.3 us) |
| T3, fill 1024 per row | 737,280 | ~45% | ~1,250 (22.7 us line) |
| T3, fill only 800 active words | 576,000 | **~35%** | ~1,460 |
| T3 + 8 bpp indexed (T4) | 288,000 | ~18% | ~1,860 |

Consequences:
- **Draw throughput.** A full-screen fill at 720 is 576,000 words (~5.8 ms of pure bursts), 4x today, with 65% instead
  of 88% of cycles left for it. Full-frame redraws get ~5x slower; partial invalidation
  (`HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md` item 6) and H2 double buffering stop being optional.
- **Fill deadline.** Every non-fill burst must finish inside the slack above. Today the longest is a RECT row (<= 511
  words). At 720 a RECT row can be 800 words (~810 cycles), still inside ~1,460, but only once per line: two
  back-to-back long bursts ahead of a fill would miss it. Needs a measured check (busy counter + a fill-late counter).
- **Audio.** The L0 (0 late underrun) record is the invariant. The blit-storm Check with `R_SDR_BUSY` is the
  existing tool for this; run it at T3.
- The "fill only active words" change is an independent win (~22% less scanout traffic at 360 today). Worth doing
  on `main` separately; it is not in this branch.

### 5.2 SDRAM memory map (hard blocker for T3)

At stride 1024 from base 0, a 720-line framebuffer covers words 0..737,279 (bytes 0..1.41 MiB) and collides with:
- the **CPU SDRAM window at byte 1 MiB** (`PL_SDRAM_BASE 0xA0100000`: playlist, Blit Test crumbs, sweep buffers);
- every **off-screen stash row** (rows 360..1023 at stride 512: art stash `ART_STASH_Y 360`, thumbnail stash,
  TIMG plane, Chladni plane at 984, probe row 1023);
- the **probe cells in columns 400..511** used by `blit_probe`/`rrect_probe` (visible at 800 wide);
- H2 buffer 1 (`DBUF_BASE1`, byte 2 MiB) would extend to ~3.4 MiB, over the diagnostic window (2-3 MiB).

Fix: give the framebuffer its own SDRAM base (e.g. byte 16 MiB) through one sticky origin register that fill, all
FB-relative opcodes and `dbuf_addr()` add, and re-place the stash rows. 64 MiB is plenty; the layout is the work.
Write the SDRAM map document first (`ARCHITECTURE_ROADMAP.md` Phase B item 3, still open).

### 5.3 Address and field widths (T3 RTL)

| Field | Today | 720 needs | Cost |
|---|---|---|---|
| `cmd_addr` / `R_FB_ADDR` | 19 bits (y*512+x) | 20 bits (y*1024+x) | +1 bit in `cmd_mem` |
| `cmd_w` / `cmd_h`, `R_FB_SIZE {h[17:9],w[8:0]}` | 9 bits (max 511) | 10 bits, repacked `{h[19:10],w[9:0]}` | ABI change: interlock |
| `blt_*_stride` | 10 bits (max 1023) | 11 bits (stride 1024) | sticky fields |
| `cmd_mem` | 88 bits x 256 = 3 M10K (120 bits capacity) | 91 bits | **still 3 M10K** |
| Line buffer | 2 x 512 x 16 = 2 M10K | 2 x 1024 x 16 = 4 M10K | **+2 M10K** |
| `scan_vc` + `tau_cdc_gray_bus W` | 9 bits | 10 bits | trivial |
| `glyphbuf` (COPY/BLIT row width) | 128 words | 256 for 2x-wide art/panels | MLAB, or more rows |
| geometry functions (`f_rrect_seg_addr`, BAR) | 9-bit w/h, 512-stride shifts | 10-bit, 1024 shifts | retime again (B-111 pattern) |

M10K budget: the full stack without the RAM shrink sits at 304/308, so +2 for the line buffer needs the 192 KB
RAM shrink bitstream (240/308). **Native 720 depends on the RAM shrink shipping.**

### 5.4 Firmware

- About 56 source lines in `fw/` use a literal 400/360/512 (grep, outside generated tables); `FB_W/FB_H/FB_STRIDE` exist but the UI layout is
  pixel-literal for 400x360 throughout. A native 720 UI is a UI redesign, not a constant change (it fits the parked
  UI/UX redesign, `ARCHITECTURE_ROADMAP.md`).
- Fonts: the 16 px ROM font renders half-size at 720. Options: 2x CHAR scale (exists, no visible gain) or a
  hi-res atlas font; at ~4x the bits of the current ROM (~10 M10K) it cannot live on-chip, so SDRAM/PSRAM + blit
  (the CJK-font pattern, `HELIOS_SPEC.md` 7.1).
- Interlock: a native-720 bitstream changes the command ABI, so it needs its own `CORE_VERSION` rev and a strict
  firmware check (the B-245 lesson). T1 does not: the firmware cannot tell T1 from alpha.15.
- CPU budget: 4x pixels per redraw also means 4x commands for software-drawn content (meters, Chladni plane
  writes through the mailbox).

### 5.5 Other risks

- **Timing closure.** The current stack closes with small margins (hold +0.037 ns on `all6-combined`). T1 adds a
  faster clk_vid; T3 widens adders on the `clk_sdram` dispatch paths that have needed retiming five times.
  Two seeds for every fit, as usual.
- **Scaler behaviour.** KB-014 reports that a bad VS/HS relation can wedge the scaler for every core until a power
  cycle. The testbench checks the rules, but only hardware proves it; T2's runtime switch is the riskiest step.
- **Power.** 3x pixel clock and (at T3) ~3x SDRAM activity: expect measurable battery cost; measure at T1.
- **Screenshots/dock.** Pocket screenshots and dock scaling should follow the declared 800x720; unverified.
- **Two-bitstream fallback.** If T2 fails, 720 needs a separate core or a second `cores[]` entry (core.json allows 8
  bitstreams, selected by instance JSON), because PLL reconfiguration is unreliable (KB-015).

## 6. Hardware features worth adding for 720

| Feature | Benefit | Cost | Verdict |
|---|---|---|---|
| **Scanout 2x doubler** (T1, built) | 720 output with zero bandwidth/firmware change; permanent fallback "720 container, 360 content" mode; lets hi-res be adopted screen by screen | none | built |
| **Runtime scaler-slot switch** (T2) | one bitstream serves 360 and 720, user setting instead of a second core; avoids PLL reconfig | small timing-generator mux | next |
| **Fill only active words** | -22% scanout traffic today, 45% -> 35% at 720 | trivial (`fill_cnt` end compare) | do on `main` |
| **Framebuffer origin register** | moves the framebuffer out of the CPU window/stash collision; also generalises H2 | one sticky 25-bit field + adders | required for T3 |
| **Fill-late / deadline-miss counter** | measures the real fill-deadline margin instead of estimating it | a counter + MMIO word | add with T3 |
| **8 bpp indexed framebuffer + scanout CLUT** | halves 720 scanout and draw traffic; theme roles already define a palette | 1 M10K CLUT copy in clk_vid, palette-aware drawing, loses free RGB565 gradients | option T4 |
| **Two-plane scanout: doubled 400x360 base + 800x720 2-bit text/overlay plane** | most of the visible 720 gain (sharp text, icons) at ~+4% bandwidth, UI geometry unchanged | overlay line buffer (+1 M10K, section 7), compositor, new drawing target | strongest long-term candidate, after T3 |
| **Hi-res font/icon atlas in SDRAM/PSRAM** | crisp text at 720 without on-chip font growth | offline tooling (exists for thumbnails) + blit path | with T3 or the overlay plane |
| Larger `glyphbuf` (256) | 2x-wide COPY/BLIT rows for 720 panels | MLAB | with T3 |
| PLL reconfiguration for mode switching | -- | KB-015: unreliable | rejected |
| Pixel clock 50 MHz | shorter porches not needed | on the ~50 MHz limit | rejected |

## 7. Resource budget (estimates, B-375)

Device 5CEBA4F23C8: **18,480 ALMs, 308 M10K, 66 DSP**, 4 PLL outputs in use. No pins, PLL outputs or clock
networks are added by anything below: T1 only retunes outclk_1/2. M10K counts are arithmetic on Cyclone V
native shapes (10 Kbit: 256x40, 1Kx10, 2Kx5 ...) and are firm; **ALM figures are [EST]** from the size of the
logic, not synthesized. A synthesis-only `quartus_map` (~5 min on the VM) would turn them into real numbers.

Baselines (last measured fits):

| Configuration | M10K | DSP | ALMs |
|---|---|---|---|
| 256 KB main RAM stack (gamma/blend-pipe: poly + wave + spec + beam + blend) | 304/308 | 17/66 | last measured 7,725 (B-246, before poly/wave/spec/blend) -> ~8-9k [EST] |
| 192 KB stack (`all6-combined` = above + RAM shrink + clk66 + H2), alpha.15 | ~240/308 (B-337 fit of the same RAM/blend set) | 17/66 (+1 with `TAU_LPC`) | ~8.5-9.5k [EST], not in the log |

Per feature:

| Feature | M10K | ALMs [EST] | DSP | Other |
|---|---|---|---|---|
| T1 scanout doubler (built) | 0 | ~30-60 | 0 | clk_vid 12 -> 37.5 MHz, 26.7 ns period, easy |
| T2 runtime 360/720 switch | 0 | ~80-150 | 0 | 1 MMIO slot; end-of-line scaler-slot word |
| Fill only active words | 0 | ~5 | 0 | -22% scanout traffic |
| Native 720 16 bpp framebuffer (T3): line buffer 2 x 1024 x 16 | **+2** (2 -> 4, 2Kx5 shape) | -- | 0 | -- |
| T3 field widening (addr 19->20, w/h 9->10, stride 10->11, `scan_vc` 10 bits) | 0 (`cmd_mem` 88->91 bits stays 3 blocks) | ~150-300 | 0 | all in the clk_sdram dispatch paths that needed retiming 5 times |
| Framebuffer origin register (25-bit, added to fill/draw/`dbuf_addr`) | 0 | ~40-80 | 0 | 1 MMIO slot |
| Fill-late counter (+ Gray CDC to clk_sys) | 0 | ~40-60 | 0 | 1 MMIO slot |
| `glyphbuf` 128 -> 256 | 0 (already falls back to 1 M10K on `all6-combined`, B-371; 256x16 still fits one) | ~0 (or 8 MLAB LABs if MLAB inference is fixed) | 0 | -- |
| H2 double buffering at 720 | 0 | ~10 | 0 | 2 x 1.41 MiB SDRAM |
| 8 bpp framebuffer + scanout CLUT (T4) | **+1** (CLUT copy read in clk_vid; line buffer stays at 2 because 800 px = 400 words) | ~50-100 scanout, ~300-800 palette-aware drawing | 0 | loses per-pixel AA/blend colours |
| Two-plane overlay (800x720 2 bpp over doubled base) | **+1** (2 x 800 x 2 bits = 3,200 bits) | ~150-300 fill/compositor + ~300-600 for a 2 bpp draw opcode | 0 | 2-3 MMIO slots, +~4% SDRAM |
| Hi-res font/icon atlas in SDRAM/PSRAM | 0 | ~0 (draws with existing CBLIT coverage ramp or blend) | 0 | SDRAM space, offline tooling |
| (rejected) 2x-resolution font ROM on-chip | ~40-60 | -- | 0 | would take most of the free M10K |

Totals (two alternative routes, plus everything):

| Route | M10K | ALMs [EST] | DSP |
|---|---|---|---|
| A: 720 output + overlay plane (T1, T2, active fill, fill-late, overlay, atlas) | +1 | +~650-1,200 | 0 |
| B: native 720 16 bpp (T1-T3, origin, fill-late, glyphbuf, H2, atlas) | +2 | +~350-650 | 0 |
| Everything (A + B + T4) | +4 | +~1,100-2,100 | 0 |

Against the budgets:
- **192 KB stack:** 240 -> 241-244 of 308 M10K, ~50% -> ~55-60% ALMs, DSP unchanged. Fits with room.
- **256 KB stack:** 304 -> 305-308. Route A fits (305), route B leaves 2 free (306), everything is exactly full (308)
  -- no margin for the fitter. **Native 720 in practice needs the 192 KB RAM shrink.**
- **DSP is not a constraint** for any 720 item (nothing multiplies per pixel; strides stay powers of two).
- **Capacity is not the real limit.** The limits are timing and bandwidth: `all6-combined` closes hold by
  +0.037 ns, the clk_sdram dispatch paths are where T3's widening lands, `glyphbuf`'s MLAB inference already
  regressed on that build, and native 720 takes ~35% of SDRAM cycles (section 5.1).
