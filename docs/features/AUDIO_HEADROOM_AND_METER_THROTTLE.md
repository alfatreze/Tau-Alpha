# Audio headroom: the meter CPU budget and the FLAC decode speed-up (hand-over for the audio-engine work)

Written 2026-10-03 on branch `meter-builder` (worktree `tau-alpha-meter-builder`, NOT merged to main). It collects what a hardware session on a hard FLAC found, what was built on
this branch in response (`TAU_DEV_METER_06` .. `09`), what is measured and what is only estimated, and what is proposed next. It is meant to be read next to the audio-engine
plan (`docs/features/CYMO_AUDIO_ENGINE.md`, C0a headroom, C7 tempo): everything here either gives that work more CPU headroom or measures it.

Evidence labels used throughout: **MEASURED** (read from a Pocket screenshot or a host test), **ESTIMATE** (modelled or inferred), **INFERENCE** (a conclusion drawn from measured facts, not itself measured).

## 1. The problem that started it

A 415 kbps, 48 kHz FLAC ("Tau Test", FLAC 415K 48.0K) clicked when the Winamp Bars meter ran fullscreen. The Cymo resampler was ruled out first (Info `CYMO RESAMP READY` on a 48 kHz file:
the main-line 44.1 kHz guard works; `meter-builder` branched before it, so older branch builds had the resampler engaged by the manual toggle, which was the first suspect and is now merged away).

Measured on a Pocket, same file, stall counts are `UNDERRUNS n ALL m` (m = every FIFO stall since the track started, B-546):

| Build / state | Stalls (ALL) | Idle (HEADROOM) | Note |
|---|---|---|---|
| METER_05, fullscreen Bars | 201 | I0/0, WS 0.8x | |
| METER_06, fullscreen Bars (after the first throttle) | 149 | I3/0, WS 0.8x | |
| DEV_76 (main), meter = Layered Wave | 255 | I0/0, WS 0.8x | main stalls too: not a branch regression, not the heap |
| METER_07, normal screen, ~60 s, 354 kbps variant of the file | 329-366 | I1/0, WS 0.8x | |
| METER_08, normal screen, ~60 s, 415 kbps | 0 | I4/0 | thin cap active (10 draws/s) |
| METER_08, fullscreen, ~60 s, 415 kbps | 32 (0.5/s) | I15/0, WS 0.8x | UI total about 0.2-7 percent of wall time |
| METER_06, menu open (meter hidden), 415 kbps file | 0 | (not counted: HEADROOM only counts plain playback) | run length not recorded |

Reading (**INFERENCE**): this file sits at the decoder's limit on every build (worst second projects only 0.8x), so any extra CPU use in the UI shows up as stalls. The heap gap
(reduced on the `METER` builds) is not a factor: DEV_76 had 1.2 KB more free RAM and the same stalls.

### A wrong figure that was in the notes, corrected

For two builds the "fullscreen Bars costs 9.8 ms per draw" number was used to tune the throttle and even written into a source comment. It was wrong: Info > `METER DRAW` times only the full-repaint
meters (Chladni, Layered Wave), not Bars. Measured Bars cost with the new Info > `METER COST` row (METER_07 onward) is **0.27-0.31 ms per draw, 0.7-3 ms worst** (**MEASURED**). The comment in
`fw/player.c` has been corrected. Chladni really is heavy: **12.5 ms last, 17.5 ms worst** (**MEASURED**, V14).

## 2. The meter framework change: a mandatory CPU budget (built, hardware-tested METER_06..08)

Rule, now at the top of `fw/meter.h`: **no meter is ever called directly. The host calls `helios_meter()` (`fw/player.c`), which applies the budget before the meter's tick runs.**
A tick may therefore be skipped. It must keep all its state in its own statics, take elapsed time from `in->dt_ms` (which carries the time of skipped calls, clamped to `MP_MAX_DT_MS` = 80 ms),
and honour `in->force` (a forced repaint is never skipped).

What the gate does (`fw/meter_policy.h`, pure and host-tested in `sim/test_meter_policy.py`):

1. **Duty cap** (`mp_gate`): after a draw that cost C cycles, the next may not start before `C * 100 / duty` cycles. `HM_DUTY_PCT` 40, `HM_DUTY_LOW_PCT` 20 when the last measured second left under
   20 percent idle (`hr.last`, the same figure as Info > HEADROOM).
2. **FIFO cover** (`mp_fifo_covers`): a draw may start only if the audio FIFO holds at least the draw's cost in audio entries plus `MP_FIFO_MARGIN` (64). The cost used is the **recent worst**
   (`hm_cost_w`, decays 1/16 per draw), not the last. The older `meter_afford()` yield alone triggers below about 7 ms of audio, shorter than a heavy draw.
3. **Starvation cap**: the FIFO gate alone can hold a meter off for at most `MP_STARVE_MS` (300 ms): a frozen meter is the worse failure.
4. **Thin-headroom rule**: when idle is under `HM_THIN_IDLE_PCT` (5), every meter is held to `HM_THIN_MS` (100 ms) between draws (about 10 draws a second). Info shows a `T` flag.
5. All **eight older meters** (Scroll, LED, Dots, Water, VU, Wave, Scope, Bars) now go through `helios_meter()` (they were called directly and bypassed any throttle).

Instrumentation added so this can be judged from a photo (Info page):

- **METER COST** `V<id> L<last>/<worst>US F<fifo skips> D<duty skips> B<sdram busy permille>`: per meter, reset when the meter changes. V ids: 12 = Winamp Bars, 14 = Chladni, 16 = Layered Wave.
- **UI COST** `L<last>/<worst>US P<share>/<worst share>` (+ `T`): the whole per-frame UI pass, share of wall time in permille over windows of 64 calls (about 1.7 s).
- **UI PARTS** (METER_09, not yet run) `C<ms> F<ms> L<ms> P<ms>MS`: worst time of the main pass, the whole fullscreen pass, the fullscreen label and the progress bar. F minus L and P is the figure.

Measured effect (**MEASURED**): fullscreen on the hard file went from 149-255 stalls to 32 in about 60 s; the normal screen to 0. Bars stayed visibly smooth to the owner on METER_07/08 (not yet judged under the 10 Hz thin cap on a
heavy file).

## 3. Review findings (two read-only reviews, code only, nothing run unless stated)

### 3.1 Core functions (embedded-audio review)

| Sev | Finding | Status |
|---|---|---|
| HIGH | The yield threshold was shorter than one draw (about 7.1 ms of audio at 48 kHz vs a draw that can exceed that) | **Built**: cost-aware FIFO gate (section 2) |
| HIGH | FLAC channel 0 pushes nothing: for a 4096 block about 30 ms (**ESTIMATE**, from the B-363 channel-0 share) without a FIFO write; the 2048-entry FIFO (42.7 ms) is the only cover | **Open**. A UI tick fired just before channel 1 starts streaming can land in that gap. Fix options: no draw in the last part of channel 1, or a channel-0 reserve in the gate |
| HIGH | Off-screen composed meters call `fb_fence()` (up to 4 times, 50 ms each, plus one in the present): the CPU busy-waits for the engine and that time is charged to the decoder, to `mp_drew_cost` and to CPU LOAD | **Open**; needs the Talos idle bit (T2-1, RTL) |
| MED | Draws are timer-driven, not idle-driven; the FIFO-full wait in `cymo_push` only runs `poll_input` and the refill pump | Open (idea: run the meter tick from that wait when due) |
| MED | CPU LOAD over-counts: only the FIFO-full wait counts as idle; fences, mailbox waits and delay loops count as load (so 92-97 percent reads are not interpretable) | Open (show an engine-wait counter or count it as idle) |
| MED | `poll_input()` can start full-screen work (fullscreen toggle, library enter, Configure page) inside the FIFO wait; a Settings crossfade is about 100 ms, longer than the 42.7 ms FIFO | Open (set a flag, run from the main loop behind the gate) |
| LOW | `meters_feed` re-scans every sample for peaks although `R_WAVE_PK` already tracks them, and builds scope and wave data even when neither meter is shown (about 0.5-1 percent CPU, **ESTIMATE**) | Open |
| LOW | the 64-bit divide in `meter_policy.h` | **Done** (32-bit form) |

`fb_wait()` itself is not a problem (**INFERENCE from the RTL**): it spins on `R_FB_GO` bit 0, which is the command FIFO's "nearly full" flag (255 of 256 entries), so it almost never blocks.

### 3.2 Meter code vs the framework

Efficiency findings:

- **Winamp Bars repainted the whole column** of every changed band (323 rows in fullscreen). **Done (METER_07)**: only the rows between the old and new height are painted, plus the peak marker change; a forced repaint,
  new geometry, accent or ground colour repaints everything. Proven pixel-identical to a full repaint every frame (`tools/meters/preview/test.js`, all presets, dark and light) and in the C-vs-JS golden frames
  (`sim/test_meter_golden.py`).
- Fullscreen forced a full repaint whenever the CPU% label changed width (99 to 100 percent). **Done**: fixed-width plate.
- `blit_probe_ensure()`/`BLIT_READY()` were evaluated per band. **Done** (hoisted).
- Open, from the review: fullscreen Scope draws 3 rects for each of 64 columns (about 192 commands) and divides by 100 per column; Chladni has a fixed 128-cycle spin per mailbox word plus two fences a frame; Scope's `ui_bg_blend` fences
  every frame and does debug mailbox reads each frame (should be Diagnostic-only and at most once a second); Layered Wave does 64-bit math per call.

Framework-adherence violations still open (**not fixed**): `dt_ms` is the constant 26 (not measured) and `vu_master.inc` hard-codes 26; Chladni keeps its own clock and reads `ui_cpu_pct()` itself; several meters
clear the shared `wviz_force` flag (leaks between contexts); `viz_led_tick` writes `spec_lvl[]` while paused; `viz_bars_tick` reads `bars_layout` and calls the blit probe; `mtr_ease` and the peak fall move a fixed step per call and ignore `dt_ms`, so
a skipped draw slows a bar instead of keeping it on time (needs dt scaling and an update of the golden twins); `vu_master.inc` draws with `fb_rect` not `fig_rect` (unclipped; only matters if VU becomes fullscreen-capable).

## 4. The FLAC decode speed-up (measured on the host, NOT built into the firmware yet)

Goal: the decoder is the real consumer on this file. Where do the software cycles go, and what can be cut?

Method (`tools/lab/flacperf/`): the real `fw/flac.c` compiled with the firmware's own `-Os` line and run on the host RV32 simulator (`tools/rv32sim.py`, with a per-PC profiler and a model of the hardware LPC unit taking N = 2*order+4
clocks, the figure from `tau_flac_lpc.sv`, N = 20 at order 8). The simulator counts instructions (**MEASURED**); cycles are **ESTIMATED** with a VexRiscv-Full model (static branch prediction, 3-cycle mispredict or jalr, 1-cycle load-use stall, MMIO load +5, store +2, all cache hits).
Two workloads, 12 frames each, 4608-sample blocks: w1 = 48 kHz 16-bit mid/side and left/side, LPC order 6-8, 7.9 bits per sample; w2 = 44.1 kHz right/side, LPC 2-12. Per channel-sample.

Current split with the hardware LPC (w1): **222.9 instructions, about 350 cycles per sample**:

| Part | Instructions (MEASURED) | Cycles (ESTIMATE) |
|---|---|---|
| Rice residual (unary + bits + zigzag) | 139.4 (62.6%) | 200.0 (57.1%) |
| Bit-reader refill | 10.7 | 13.3 (3.8%) |
| Hardware LPC glue (write, poll, read) | 32.5 | 67.9 (19.4%) |
| Stereo output (EMIT: decorrelate, `to16`, sink call) | 29.8 | 50.3 (14.4%) |
| Sink (model of `player.c`) | 10.1 | 18.2 (5.2%) |

Under `-Os` every 64-bit shift is a libgcc call (`__lshrdi3`, `__ashldi3`) and so is `__clzdi2`: together **38.4 instructions, about 56 cycles per sample** on the 64-bit bit reservoir; `to16()` is not inlined (10 instructions, about 23 cycles).
For reference, software LPC costs 319.7 instructions (about 434 cycles) per sample, so the hardware unit already saves a lot.

Candidates (cycle change vs current; every variant produced **identical audio** on 55,296 stereo pairs per workload, and a forced hardware failure at write 3,000 and 77,778 still matched through the software fallback):

| Candidate | Cycles | Code size |
|---|---|---|
| C1 fused Rice decoder, 32-bit left-aligned window, inline `clz`, small-quotient checks first | **-45.1% (w1), -46.3% (w2)**; 118.1 instructions | +0.2 KB |
| C2 per-stereo-mode `EMIT` loops with inline `to16` | -6.8%; with a 16-bit-output version -9.0% | +3.6 KB / +7.4 KB |
| C3 overlap the hardware LPC call with the next residual decode | +2.3% (N=10), -0.9% (N=20), -6.7% (N=36), -11.9% (N=60); on top of C1+C2: -5.7% at N=20 | +0.9 KB |
| C1+C2+C3 at N=20 | **-56.3%** (152.9 cycles, 100 instructions) | **+17.6 KB** |
| Leaner (C1+C3, C2 without the 16-bit variant) | -53.4% | +10.4 KB |
| Side finding: today's `flac.c` at `-O2` instead of `-Os`, nothing else | -23% (269.9 cycles) | +8.7 KB |

Projected real-time load at 48 kHz stereo (**ESTIMATE**): about 50% now, about 22% with all three. With C1 alone the arithmetic suggests roughly 28% (extrapolated, not separately measured).

Caveats: the sink model leaves out `meters_feed` and the UI; MMIO latency is a guess that drives the LPC-glue and C3 numbers; only two workloads, no 24-bit files; real hardware has cache misses the model ignores, so the saving will be somewhat smaller; C2's growth may not fit the 192 KB link (heap gap is already near its floor on the diagnostic build); C3 would put the LPC register addresses inside `flac.c`, breaking its no-MMIO separation (pass them in with `-D` if used).

## 5. Recommended order (proposal, nothing below this line is built)

1. **C1 only**, as a drop-in replacement of the Rice loop in `fw/flac.c` (`rice_next`/`unary`/`bits` and the batch `residual`). Verify bit-exact against the existing FLAC vectors (`sim/test_flac*.py`, `tools/flac_ref.py`, `tools/flac_verify.py`, the LPC redirect harness) including: escape partitions, long unary runs, zero-length partitions, wasted bits, 24-bit, mono and the four stereo modes, truncated streams. Re-measure with `tools/lab/flacperf`.
2. Run on a Pocket against the same hard file with the same protocol as section 6; expect `HEADROOM` idle to rise and the stall count to fall independently of any meter.
3. Only then consider C3 (needs the unit latency measured, not guessed) and `-O2` for `flac.c` alone (+8.7 KB: needs the 192 KB link accounting). Skip C2 unless the heap budget changes.
4. Close the open framework items in the order: channel-0 reserve in the gate; `dt_ms`-aware ballistics (with the golden twins updated); `poll_input` deferring page changes; the Talos idle bit to remove the fence busy-waits; CPU LOAD accounting.

Interaction with the audio-engine plan: the same decode headroom is what the C7 tempo stretcher and any speed above 1.0x consume (`fw/headroom.h`: `WS` projects the maximum speed from the worst idle second, with `HR_WSOLA_EST_PCT` = 16 percent as a fixed extra cost, itself an
estimate). Freeing about half of the per-sample FLAC cycles is the cheapest way to make that budget real.

## 6. Test protocol (what to photograph)

Same track, same length, always from the start of the track (the stall counter is cumulative): 60 seconds each in three states -- normal screen, fullscreen, menu open -- then Settings > Diagnostics > Info, page 1 (`UNDERRUNS`) and the page with
`HEADROOM`, `METER COST`, `UI COST`, `UI PARTS`. Say what was heard in each state. Do not scroll the Info page for long before reading (each scroll step is a full redraw and adds load).

## 7. Where things are

- Branch `meter-builder` (worktree `../tau-alpha-meter-builder`): commits `41a7ff2` (first throttle), `4fa805d` (METER_07), `5c416aa` (METER_08), `89c369a` and `0ed1c07` (METER_09). Main was merged in twice. **Not merged to main.** Merge notes: shared files are
  `fw/player.c`, `fw/settingsui.inc` (Info row numbering: HEADROOM 33, METER COST 34, UI COST 35, UI PARTS 36, AUDIO STATS 37, METER PACKS 38), `tools/ui_snapshot_renderer.py` (sample row list must match), `tools/heap_gap_baseline.json`.
- Test cores are named `TAU DEV METER NN`; build flags used: `RAM_192K=1,CLK66=1,SDRAM_BUSY=1,LPC_FW=1,PACKS=1,HEAP_MIN_OVERRIDE=3584`. **`HEAP_MIN_OVERRIDE` was lowered from the 4,096 B floor for throwaway builds only (3,840, then 3,584); the METER_09 gap is 3,664 B.** Never use it for a release.
- A hot-RAM trap met on the way: a static function stops being inlined once it has two callers and then costs its full size in hot RAM. `meter_afford()` grew 248 B that way and was marked `COLD_FN3` (all its callers are cold code).
- FLAC measurement harness: `tools/lab/flacperf/` (`prof_sim.py`, `run.py`, `classify.py`, `build.sh`, `harness.c`, `glue.c`, `mkblob.py`, `stats.py`, `flac_opt.c`, `opt_rice.inc`, `opt_subframe.inc`, the two test blobs). Some scripts contain absolute paths from the
  session that created them; adjust before reuse. `flac_opt.c` is a measurement variant, not drop-in firmware.
- Reviews and numbers in sections 1-2 come from Pocket screenshots; section 3 from two code reviews that did not run anything; section 4 from the host simulator.
