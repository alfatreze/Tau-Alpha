> **Correction (DEV 196, `docs/research/TALOS_T3B_RESULT_2026-10-27.md`): the Waterfall's engine time is its two full-grid scroll copies, not `CBLIT`; the burst CBLIT of T3b did not shorten it (712 k to 661 k cycles). The conclusions about command issue, the column list and the command ring stand.**

# Talos T4 gate: result of the per-meter measurement on a Pocket (TAU DEV 195, 2026-10-27 00:08 to 00:20)

Source: 14 Report shots (`Memories/Screenshots/20261027_000828` .. `001956`; one, `001113`, is not a grid), all decoded with `tools/decode_tau_suite.py --grid --table`. Firmware `0.6.1-dev.195+c4319ade`, the `stats-b821` bitstream (no T3 block, so `GFX Q OVF` and `GFX SCAN LATE` read 0 because the registers do not exist, which is not evidence). Method: `docs/research/TALOS_T4_MEASUREMENT_PLAN.md`. Order (owner): for each meter the small (window) shot first, then the fullscreen shot; the pairs are 000828/000930 Bars, 001020/001120 Scope, 001206/001253 Chladni, 001341/001427 Master VU, 001536/001622 Waterfall, 001712/001804 Sound Map, 001907/001956 Layered Wave. Each value is the meter's own moving average at the moment of its shot (the last few ticks before it). Averages include ticks that drew nothing (the scope returns early while a capture runs; a paused tick is cheap), so a value can read low when a Report was taken right after pausing; the "most commands" column is the picture's real size.

| Meter | commands / tick, window / fullscreen (max) | CPU cycles / tick, window / fullscreen | of which in fences | issue cost (commands x 132) as a share | stall |
|---|---|---|---|---|---|
| Winamp Bars | 0.4 / 0.6 (max 32) | 13.8 k / 13.9 k | 0 | under 1% | 0 |
| Winamp Scope | 0.4 / 0.4 (max 75) | 8.2 k / 8.9 k | 0.05 k / 0.9 k | under 1% | 0 |
| Chladni | 3.9 / 4.9 (max 15) | 243 k / 250 k | 97 k / 130 k (40% / 52%) | 0.2% | 0 |
| Master VU | 3.3 / 3.4 (max 8) | 16.1 k / 16.1 k | 0 | 3% | 0 |
| Waterfall | 20 / 69 (max 370) | 95 k / **712 k** | 73 k / **678 k (77% / 95%)** | 3% / 1.3% | 0 |
| Sound Map | 0.7 / 0.4 (max 147) | 71 k / 177 k | 0 | under 1% | 0 |
| Layered Wave | 2.2 / 11 (max 304 / 414) | 48 k / 117 k | 0 | 0.6% / 1.2% | 0 |

Seen outside the pairs (the rows of a meter not on screen keep their last value): Winamp Bars at **17.6 commands and 23.7 k cycles** and Winamp Scope at **39 commands, 103 k cycles, 71 k of them in fences (69%)** appear frozen in the Reports taken after their own, so they are live values from somewhere between the pairs (most likely the fullscreen picture drawn while playing, before the pause for the shot); and Layered Wave once read **1,748 k cycles (26 ms) at 35 commands and no fence**, between the Master VU and the Waterfall shots, in a state the pair does not show (a heavier preset or a full-resolution setting; to be reproduced before it is quoted).

## What it says
1. **Command issue is never the limit.** The largest issue share is 9% (Bars fullscreen); for the meters that cost the most it is 1% or less. A primitive that cuts the number of commands (the column list, and by the same arithmetic a command ring) would change nothing measurable. **The column list is not worth building.** The queue never filled (`stall` 0 everywhere).
2. **Fence waits are the second cost, and they are engine time.** Waterfall fullscreen spends 678 k of 712 k cycles (10 ms of every tick) waiting for the engine; the Scope's live fullscreen picture 71 k of 103 k; Chladni 97 k to 130 k. The Waterfall's draw is `gfx_scroll_*` copies plus `gfx_cblit` (a colour-mapped blit), the Scope's is the blended trail blit: `CBLIT` and `SBLIT` do one SDRAM transaction per pixel (`TALOS2_PLAN_REVIEW_2026-10-26_EXHAUSTIVE.md` R1: about 20 cycles per pixel, a 10x overhead against the burst copy). **This is the plan's in-place fix (T3b: burst-gather `SBLIT`/`CBLIT`) and the measured case for it.** A hardware fence (T3) shortens only the round trip of the wait, not the engine's work; not waiting at all (a flip without the fence spin, the asynchronous present) would let the CPU do something else for those 10 ms.
3. **The rest is CPU arithmetic, not graphics:** Layered Wave has no fence at all (48 k / 117 k in the pair, 1.75 M once, unreproduced): its cost is the model computed in firmware. Chladni's non-fence part (about 120 k) and Sound Map (71 k window, 177 k fullscreen, under one command a tick) are the same kind. No graphics primitive changes these; they are firmware or (for the Layered Wave model) a hardware helper question for another day.
4. Nothing late: `UNDER RING` stayed 0 in every Report, but these Reports were read while paused as asked, so this says nothing about audio under load.

## Decision proposed (owner)
Replace T4 (column list) by **T3b: burst-gather `CBLIT` and `SBLIT` in the engine** (RTL, behind the existing differential test, so the picture stays identical), with the Waterfall fullscreen tick (712 k cycles, 678 k in the fence) as the number to move, and re-measure with these same rows. The T3 block is already merged and rides the same fit.
