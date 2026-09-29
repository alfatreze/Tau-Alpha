# 720: phased spec and implementation plan

Status: **spec, nothing in it built yet** except T1 (the scanout doubler, B-410), which Phase 1 absorbs.
Branch `test/720`. Written 2026-09-29 (B-412) after a full review of `VIDEO_720_TEST_PLAN.md`, which stays the
background document (platform limits, bandwidth and resource arithmetic). This file is the one to implement from.

**Baseline for every build in this plan: the 192 KB stack with all current hardware features** -- the
`all6-combined` bundle (poly + wave + spectrum + beam + pipelined blend + 192 KB RAM + clk66 + H2), plus `TAU_LPC`
once `lpc-b372` closes. The 256 KB stack is not a target (owner decision, 2026-09-29).
Baseline resources: ~240/308 M10K, 17-18/66 DSP, ALMs not logged (~9k [EST] of 18,480). Timing is the tight part:
`all6-combined` closes hold by **+0.037 ns** (B-371).

**Correction (2026-09-29, `main`): `glyphbuf`'s MLAB fallback is already fixed (T2-00), just not on this branch
yet.** This branch forked from `main` before T2-00 landed, so the paragraph above and Group A3 below were written
against a `glyphbuf` that still falls back to M10K under `TAU_BLIT_BLEND` (~6,500 ALMs of registers instead of an
MLAB). That is fixed on `main` (uncommitted there until this same pass, now committed): one shared write port
(`gb_we`/`gb_addr`/`gb_data`) fanned out to two MLAB copies, fit-confirmed and hardware-confirmed. **Rebase or
merge `main` into `test/720` before Group A3's own fit** -- the MLAB-inference work A3 describes below is already
done upstream; redoing it here would be duplicate effort on the same bug, not a second fix.

Two test builds:
- **Phase 1** -- changes that help 400x360 now and prepare 720, plus 720 output of the *existing* 400x360 picture
  behind a Diagnostics switch. Default behaviour stays 400x360; everything is ABI-compatible, so current firmware
  still runs on it.
- **Phase 2** -- native 800x720 framebuffer, tested only through a diagnostic page, so it can be judged on its own.

## 1. Review of the existing plan: corrections and additions

| # | Finding | Effect on the plan |
|---|---|---|
| R1 | **360 timing can move to 37.5 MHz unchanged.** 1562 x 400 clocks = 624,800 per frame (60.02 Hz), line 41.65 us vs 41.67 us today, same 40-line vertical blank. | One pixel clock serves both modes, so the switch never needs PLL reconfiguration (KB-015). The 360 picture keeps today's line/frame timing and Helios beam behaviour. |
| R2 | **Scaler slot changes apply the next frame** (skill: end-of-line word, "effective next frame, last request wins"). T2 did not sequence this. | The slot word for the new mode must be sent during the frame *before* the geometry changes; the geometry changes at the frame wrap (section 2.3). |
| R2b | **Is an all-zero end-of-line word a slot-0 request?** The docs say "all-zero default = slot 0"; it is unclear whether that is the power-on default or a per-line request. | Treat it as a request: send the target slot on every line (section 2.3 B3). Correct either way. |
| R3 | **No recovery if 720 blanks the screen.** | The mode is never persisted, reset is always 360, and the Diagnostics switch auto-reverts after 10 s unless confirmed (monitor-style). |
| R4 | **Our HS/VS are multi-cycle** (HS 40 clocks, VS 4 lines); the docs say one-cycle pulses. It works on hardware today. | Keep the proven shape in Phase 1; one-cycle pulses become the first thing to try if 720 misbehaves (parameter, not default). |
| R5 | **Scanout fetches 512 words per row, 112 of them never shown.** | Fetch only the visible words: ~22% less scanout traffic now, and it is the difference between ~45% and ~35% SDRAM at native 720. |
| R6 | **"Fill-late counter" is too vague.** | Measure fill latency directly in clk_sdram (max latency + count over budget). The deadline per mode is a constant, so no cross-domain comparison is needed. |
| R7 | **Field widening can be ABI-compatible.** Unused high bits already exist: `R_FB_ADDR` bit 19, `R_FB_SIZE` bits 19:18, the stride field's bit 10. Today's firmware always writes them as 0. | Widening moves into Phase 1 with no interlock; old firmware on the new bitstream behaves the same. |
| R8 | **A capability register replaces interlocks.** | One read-only `VID_CAPS` word; firmware enables each feature only if its bit is set. Old bitstreams read 0. |
| R9 | **Stride 1024 = exactly one SDRAM page per line** (full-page bursts), but bursts must stay <= 512 words for refresh. | Phase 2 fills an 800-word line as two 400-word chunks from the same open page. |
| R10 | **Native 720 fill slack is ~1,460 cycles per line** [EST]; one long RECT row ahead of a fill uses most of it. | Phase 2 option: a third line buffer (prefetch two lines ahead, +1 M10K) doubles the deadline margin. Decide from Phase 1/2 latency measurements. |
| R11 | **Timing risk sits in the clk_sdram dispatch paths** (five retimings so far). | Each Phase 1 group has its own macro so a failing fit can be bisected, and any new address arithmetic is registered at command dequeue (the B-111 pattern), not added in dispatch. |
| R12 | **Screenshot and dock behaviour at 800x720 are unknown.** | Explicit hardware checks in both phases. |

## 2. Phase 1 -- cross-resolution build

Goal: one bitstream that is better at 400x360 today and already carries everything 720 needs except the native
framebuffer. The user-visible default is unchanged.

### 2.1 Group A -- scanout efficiency and health (macro `TAU_SCAN_A`)

- **A1 Fetch only visible words.** The fill burst ends at `H_ACT` (400) instead of `STRIDE` (512). Columns 400-511 of
  each row stay valid off-screen storage (probe cells, stash); they are simply not fetched for display.
- **A2 Fill latency.** In clk_sdram: cycles from a fill request edge to the fill's last word, tracked as
  `max_lat` (since last clear) and `over_cnt` (fills whose latency exceeded the per-mode budget: 360 = 4,000 cycles,
  720-doubled = 4,400 cycles; values from the line periods minus a margin). Both cross to clk_sys through the
  existing Gray-code CDCs (`tau_cdc_gray_ctr`, `tau_cdc_gray_bus`).
- **A3 Wider copy buffer.** `glyphbuf` 128 -> 256 words (`copy_cnt`/`wsrc_addr` index widths follow). On
  `all6-combined` it is already one M10K, and 256x16 still fits one block, so +0 M10K.
  **Corrected (2026-09-29): the "also worth one attempt at the MLAB inference regression" line above is
  now moot -- that's T2-00 (see the correction at the top of this file), already fixed on `main`, not
  something to redo here.** What A3 actually needs post-merge: `glyphbuf` is now `glyphbuf_a`/`glyphbuf_b`
  (T2-00's two MLAB copies behind one shared write port `gb_we`/`gb_addr`/`gb_data`) -- widen `gb_addr`
  and both arrays' declared size to 256, not a single renamed `glyphbuf` array. The one-write-port
  invariant T2-00 established must survive this widening unchanged (still exactly one `if (gb_we)` site).

MMIO (from the free 0x140-0x1FC range):

| Offset | Name | Access | Meaning |
|---|---|---|---|
| 0x140 | SCAN_LAT | R/W | R: `{over_cnt[15:0], max_lat[15:0]}`; any write clears both |
| 0x14C | VID_CAPS | R | bit 0 scan health (A), bit 1 wide copy buffer (A3), bit 2 video mode switch (B), bit 3 wide addressing (C), bit 4 framebuffer bases (C), bit 5 native 720 (Phase 2). 0 on older bitstreams |

### 2.2 Group C -- addressing prep (macro `TAU_FB_WIDE`)

- **C1 Widened command fields, ABI-compatible.** `cmd_addr` 19 -> 20 bits (`R_FB_ADDR[19:0]`); `cmd_w`/`cmd_h` 9 -> 10
  bits, with `w[9]` in `R_FB_SIZE[18]` and `h[9]` in `R_FB_SIZE[19]` (the old `{h[17:9], w[8:0]}` layout is
  unchanged); blit strides 10 -> 11 bits (stride field bit 10). `cmd_mem` 88 -> 91 bits, still 3 M10K.
- **C2 Framebuffer base registers.** `draw_base` (added to every FB-relative address, including fill) and
  `disp_base` (added to fill only, latched at vblank). H2 double buffering becomes a base swap; the existing
  `DBUF_*` registers keep working as a compatibility view (buffer 1 = `DBUF_BASE1`), so current firmware is
  unaffected. The base add is **registered at command dequeue**, not in dispatch.
- **C3 `scan_vc` 9 -> 10 bits** through the Gray CDC; firmware keeps reading the 360 numbering.

At 400x360 this group changes nothing visible. It exists so Phase 2 only adds the native mode, and it is the
group to drop first if a fit fails.

| Offset | Name | Access | Meaning |
|---|---|---|---|
| 0x150 | FB_DRAW_BASE | R/W | 25-bit word address added to FB-relative draw addresses (default 0) |
| 0x154 | FB_DISP_BASE | R/W | 25-bit word address scanned out; applied at the next vblank (default 0) |

**Open question, flagged 2026-09-29, resolve before implementing C2:** does `draw_base` cover the
BLIT-class opcodes' own sticky `SRC_BASE`/`DST_BASE` fields (`R_BLT_IDX` fields 0/2 -- used by
BLIT/CBLIT/SBLIT and B-405's Settings crossfade), or only the RUN/RECT/BAR/RRECT/CHAR/COPY path through
`cmd_addr`/`R_FB_ADDR`? This is not a wording nitpick -- it's the actual bug `TALOS2_REIMPLEMENTATION_PLAN.md`
flags ("H2 double buffering offsets only non-blit opcodes"), confirmed word-for-word in `docs/MMIO_ALLOCATION.md`'s
own `DBUF_CPU` row on `main`: "Every blit-mode opcode (BLIT/BAR/SBLIT/CBLIT/RRECT) already addresses through
the sticky `blt_*_base` fields and is unaffected [by this bit]." The RECT-class path already gets H2 coverage
today via the old 1-bit `R_DBUF_CPU` selector; C2 as written generalizes that (useful for Phase 2's own
relocatable native framebuffer either way), but if it does NOT also reach the blit sticky bases, it does
not close the flagged gap, it only replaces one working mechanism with a more general one. **Recommend:**
`draw_base` folds into the blit sticky-base computation too (so `fb_blit()`/`fb_cblit()`/`fb_sblit()`'s own
`SRC_BASE`/`DST_BASE` end up relative to `draw_base`, same as everything else), and C2's own testbench adds
a mutation case -- "a BLIT command ignores `draw_base`" -- that must be caught, the same discipline every
other Talos change in this codebase already uses. `TALOS2_REIMPLEMENTATION_PLAN.md` section 5.1 defers its
own "target surface" design to whichever of these two implementations lands first; make sure this one
actually earns that by covering blits.

### 2.3 Group B -- video mode switch (macro `TAU_VIDMODE`, replaces `TAU_VID720`)

- **B1 One pixel clock:** clk_vid / clk_vid_90 = 37.5 MHz (600/16), as T1.
- **B2 Two geometries, one timing generator**, selected by a registered mode bit that only changes at the frame
  wrap:

| Mode | Active | H_TOT x V_TOT | Rate | Fill cadence | `scan_vc` |
|---|---|---|---|---|---|
| 0: 360 (default) | 400x360 | 1562 x 400 | 60.02 Hz | 1 per line, 41.65 us | `vc` (as today) |
| 1: 720 doubled | 800x720 | 850 x 735 | 60.02 Hz | 1 per 2 lines, 45.3 us | `vc >> 1` |

  HS/VS keep today's multi-cycle shape (R4) under a `ONE_CYCLE_SYNC` parameter defaulting to 0.
- **B3 Switch sequencing.** clk_sys writes `VID_MODE`; a toggle crosses to clk_vid. At the next frame start the
  request becomes *pending*, and at the frame wrap after that the geometry switches.
  - **Slot word on every line.** Every line's end-of-line word (the RGB word right after DE falls) carries
    `{slot[10:0], 10'b0, 3'b000}` for the *target* slot: the pending mode's slot during the pending frame, the
    current mode's slot otherwise.
  - **Why every line.** The docs say an all-zero word means slot 0, so a line that sends zero may itself be a slot-0
    request. In mode 0 the word is all zeros, identical to today's output.
  - All other non-DE cycles stay 0, including the VS-pulse word (no frame feature bits).
  - **Hardware question (R2b):** confirm that a mode-1 frame with the slot word on every line holds slot 1 steadily.
- **B4 `video.json`** gains a second scaler mode: slot 0 = 400x360, slot 1 = 800x720, both 10:9. Slot 0 stays
  first, so reset and old firmware are always 360.
- **B5 Reset is always mode 0**, and the mode is never persisted.

| Offset | Name | Access | Meaning |
|---|---|---|---|
| 0x148 | VID_MODE | R/W | W bit 0: requested mode. R: bit 0 current, bit 1 pending, bits 31:16 count of completed switches |

### 2.4 Firmware (Phase 1)

- **Probe once at boot:** read `VID_CAPS`; every Phase 1 feature is gated on its bit.
- **Diagnostics > VIDEO MODE:** a 360 / 720 (doubled) choice, shown only when caps bit 2 is set, not persisted.
  Choosing 720 switches, then shows "720 ACTIVE - A KEEP / B REVERT (10)". No A within 10 s, or B, switches back.
  This state machine lives in a small header (`fw/vidmode.h`) with a host test for keep, timeout and cancel.
- **Info rows:** VIDEO (mode, `VID_MODE` switch count, frame rate from the existing frame counter) and SCANOUT
  (`max_lat`, `over_cnt`).
- **Check:** a new QR tag (`SR_T_SCAN`) with `max_lat`/`over_cnt` over the Check window. The blit-storm verdict also
  requires `over_cnt` delta 0.
- **Copies** use the 256-word buffer when caps bit 1 is set (chunk constant chosen at boot).
- **No change** to UI layout, draw coordinates or `helios.inc` constants.

### 2.5 Verification before any fit

- `sim/tb_mp3_fb_vid720.v` extended:
  - both modes (the 360 mode now at 1562x400);
  - a runtime switch 360 -> 720 -> 360 mid-run, checking the slot word on every line of the pending frame, the
    geometry change exactly at the wrap, and no bus-rule violation across the transition;
  - fill words per row = 400;
  - a stub delay that forces a late fill, so `over_cnt` increments.
- **Mutants that must fail:**
  - geometry switched mid-frame;
  - slot word missing;
  - fill still 512 words (checked via a new fill-length count);
  - latency counter not cleared.
- **Existing suites:**
  - `tb_mp3_fb`, `tb_blit_scene` / reference renderer and `tb_helios_dbuf` unchanged with all three macros on and
    off (C1/C2 compatibility);
  - a new blit-scene command using `w`/`h` > 511 and an address with bit 19 set (C1), checked against
    `blit_reference.py`.
- **Firmware:** `make test-host`, including the `vidmode.h` host test.

### 2.6 Fit, package and hardware test

1. **Fit:** `tools/vid720/phase1_qsf_append.txt` = `all6-combined` (+ `TAU_LPC` if closed) + `TAU_SCAN_A` +
   `TAU_FB_WIDE` + `TAU_VIDMODE`, seeds 1 and 2. If timing fails: bisect by dropping `TAU_FB_WIDE`, then
   `TAU_VIDMODE`.
2. **Package** the current diagnostic-profile firmware (RAM_192K, CLK66, the caps-gated code) with the two-slot
   `video.json`, copying the RBF directly (B-353).
3. **Hardware, 360 against alpha.15:**
   - identical picture; VBLANK ~60/s; BEAM moving;
   - STANDARD and FULL Check with 0 late underruns and `over_cnt` 0;
   - `max_lat` recorded (the first real number for the deadline margin);
   - screenshot 400x360.
4. **Hardware, 720 doubled** (Diagnostics switch), handheld and dock:
   - identical content, no shimmer, no lost edge rows or columns;
   - keep and revert both work, as does the 10 s timeout;
   - screenshot size recorded;
   - STANDARD Check in 720 with `over_cnt` 0;
   - battery drain over a fixed period in both modes.
5. **Exit:** all of the above pass. Phase 1 can then ship with the switch still Diagnostics-only.

### 2.7 Phase 1 cost [EST]

| Group | M10K | ALMs | DSP |
|---|---|---|---|
| A (visible-only fetch, latency, 256-word copy buffer) | 0 | ~80-150 | 0 |
| C (widening, bases, `scan_vc`) | 0 | ~250-450 | 0 |
| B (clock, two geometries, switch, slot word) | 0 | ~120-200 | 0 |
| **Phase 1 total** | **0** | **~450-800** | **0** |

## 3. Phase 2 -- native 800x720 (test build 2)

Goal: answer the real Phase H question -- can this system scan out and draw a true 800x720 frame while audio stays
L0 -- with nothing but a diagnostic page on top of Phase 1.

### 3.1 RTL (macro `TAU_NATIVE720`)

- **N1 Mode 2 "720 native"** in the Phase 1 timing generator: 800x720, 850 x 735, one fill per line, no doubling.
  `scan_vc` reports `vc` with 720 numbering (a separate `VID_MODE` read field). Firmware treats the beam as
  unavailable in this mode for now.
- **N2 Line buffer** 2 x 1024 x 16 (+2 M10K). Option N2b: three buffers (+3 M10K instead), prefetching two lines
  ahead, decided from the Phase 1 `max_lat` numbers.
- **N3 Fill = 800 words** as two 400-word chunks in the same SDRAM page (stride 1024, R9), from `disp_base` (C2).
- **N4 Slot 1 (800x720)** is used for both 720 modes; only the fetch and line-buffer read change.
- **N5 `VID_CAPS` bit 5** set.

### 3.2 Memory map for the test

- **Native framebuffer at byte 16 MiB** (word 0x800000), stride 1024. Written only through `FB_DRAW_BASE` while the
  test page runs.
- **Nothing else moves:** the 360 framebuffer, stash rows and CPU window stay where they are. The test switches
  `FB_DRAW_BASE`/`FB_DISP_BASE` on entry and restores them on exit.

### 3.3 Firmware (Phase 2)

- **Diagnostics > 720 NATIVE TEST**, shown only when caps bit 5 is set:
  1. switch to mode 2 (same keep/revert guard as Phase 1);
  2. draw test patterns with the existing opcodes on the widened fields (1 px checkerboard, colour bars, gradients,
     1x and 2x CHAR text, a full-width 800-word RECT);
  3. B returns to 360 and restores the bases.
- **Check profile "720 STORM"** (Diagnostic Build): blit storm plus playback in mode 2. Reports:
  - SDRAM busy %;
  - `max_lat` / `over_cnt`;
  - late underruns;
  - `audio_full`.
- **No UI port**: the player never runs in mode 2 in this phase.

### 3.4 Verification

- Testbench in mode 2:
  - pixel-exact against the stub at stride 1024 and base 0x800000;
  - fills of 800 words in two chunks;
  - mutant: single 800-word burst (must fail the chunk check).
- An SDRAM contention bench: a worst-case command stream (back-to-back 800-word RECT rows plus CPU-window traffic)
  against the stub with realistic latencies, which must keep `over_cnt` at 0. Run with N2 and N2b.
- Existing suites unchanged.

### 3.5 Fit, package and hardware test

1. **Fit:** Phase 1 bundle + `TAU_NATIVE720`, two seeds.
2. **Hardware:**
   - test patterns pixel-exact (checkerboard without moiré at 2x panel scale);
   - 720 STORM: late underruns 0, `over_cnt` 0, busy % recorded;
   - Phase 1 checks still pass in modes 0 and 1;
   - dock and screenshot checked.
3. **Exit:** measured busy % and fill-latency margin. These decide the route after Phase 2:
   - **A** -- doubled base plus a hi-res overlay plane;
   - **B** -- a native 720 UI;
   - **T4** -- an 8 bpp framebuffer, if bandwidth is the limit.

### 3.6 Phase 2 cost [EST]

| Item | M10K | ALMs | DSP |
|---|---|---|---|
| N1 mode 2 + N3 chunked fill + N4/N5 | 0 | ~100-200 | 0 |
| N2 line buffer (N2b) | +2 (+3) | ~10 | 0 |
| **Phase 2 total over Phase 1** | **+2 (+3)** | **~110-210** | **0** |

On the 192 KB baseline: ~240 -> 242-243 of 308 M10K; ALMs roughly +600-1,000 over today for both phases.

## 4. After Phase 2 (not scheduled)

- **Overlay plane** (+1 M10K, ~4% SDRAM): sharp text over the doubled base at 720, or menus over a live meter at 360.
- **Hi-res font and icon atlas** in SDRAM/PSRAM (`HELIOS_SPEC.md` 7.1).
- **8 bpp framebuffer** with a scanout CLUT (+1 M10K).
- **Native 720 UI**: this is the UI redesign itself.

## 5. Order of work

**Phase 1:**
1. A (sim + mutants)
2. C (sim + reference renderer)
3. B (sim + switch mutants)
4. Firmware (caps, Diagnostics switch, Info, Check tag, host tests)
5. Fit (two seeds)
6. Package
7. Hardware checklist 2.6

**Phase 2:**
1. N1-N5 (sim + contention bench)
2. Firmware test page and 720 STORM
3. Fit
4. Package
5. Hardware checklist 3.5

Each step is committed when its own tests pass. The existing `VID720` parameter and `TAU_VID720` macro are folded
into `TAU_VIDMODE` in step B; `make test-rtl-fb-vid720` is extended rather than replaced.
