# Talos T4 gate: result of the per-meter measurement on a Pocket (TAU DEV 195, 2026-10-27 00:08 to 00:20)

Source: 14 Report shots (`Memories/Screenshots/20261027_000828` .. `001956`; one, `001113`, is not a grid), all decoded with `tools/decode_tau_suite.py --grid --table`. Firmware `0.6.1-dev.195+c4319ade`, the `stats-b821` bitstream (no T3 block, so `GFX Q OVF` and `GFX SCAN LATE` read 0 because the registers do not exist, which is not evidence). Method: `docs/research/TALOS_T4_MEASUREMENT_PLAN.md`. Caveat: the mode (window or fullscreen) of each Report was not noted; a meter's row holds its last-used mode, so where one meter shows two values below they are two modes (the larger is the fullscreen one for Bars, Scope and Waterfall: commands rise with it). Averages include ticks that drew nothing (the scope returns early while a capture runs), so the "most commands" column is the picture's real size.

| Meter | commands / tick (avg, max) | CPU cycles / tick | of which in fences | issue cost (commands x 132) as a share | stall |
|---|---|---|---|---|---|
| Winamp Bars | 0.4 to 16 (max 32) | 13.8 k to 23.4 k | 0 | 0.4% to 9% | 0 |
| Winamp Scope | 0.4 to 39 (max 75) | 8.1 k to 103 k | 0.6 k to 71 k (69% fullscreen) | 5% | 0 |
| Chladni | 3.4 (max 15) | 210 k to 250 k | 84 k to 130 k (40 to 52%) | 0.2% | 0 |
| Layered Wave | 2.2 to 35 (max 112 to 414, a forced repaint 1,413) | 48 k to **1,748 k** (26 ms) | 0 | 0.3% | 0 |
| Master VU | 1.4 to 3.4 (max 8) | 16 k | 0 | 1% | 0 |
| Waterfall | 21 to 69 (max 370) | 101 k to **712 k** | 79 k to **678 k (95% fullscreen)** | 1% | 0 |
| Sound Map | 0.4 to 15 (max 147) | 71 k to 177 k | 0 | 3% | 0 |

## What it says
1. **Command issue is never the limit.** The largest issue share is 9% (Bars fullscreen); for the meters that cost the most it is 1% or less. A primitive that cuts the number of commands (the column list, and by the same arithmetic a command ring) would change nothing measurable. **The column list is not worth building.** The queue never filled (`stall` 0 everywhere).
2. **Fence waits are the second cost, and they are engine time.** Waterfall fullscreen spends 678 k of 712 k cycles (10 ms of every tick) waiting for the engine; Scope fullscreen 71 k of 103 k; Chladni 84 k to 130 k. The Waterfall's draw is `gfx_scroll_*` copies plus `gfx_cblit` (a colour-mapped blit), the Scope's is the blended trail blit: `CBLIT` and `SBLIT` do one SDRAM transaction per pixel (`TALOS2_PLAN_REVIEW_2026-10-26_EXHAUSTIVE.md` R1: about 20 cycles per pixel, a 10x overhead against the burst copy). **This is the plan's in-place fix (T3b: burst-gather `SBLIT`/`CBLIT`) and the measured case for it.** A hardware fence (T3) shortens only the round trip of the wait, not the engine's work; not waiting at all (a flip without the fence spin, the asynchronous present) would let the CPU do something else for those 10 ms.
3. **The rest is CPU arithmetic, not graphics:** Layered Wave at 1.75 M cycles a tick (26 ms, about 35 commands) is the heaviest meter on the Pocket and has no fence at all: its cost is the model computed in firmware. Chladni's non-fence part (about 126 k) and Sound Map (71 to 177 k) are the same kind. No graphics primitive changes these; they are firmware or (for the Layered Wave model) a hardware helper question for another day.
4. Nothing late: `UNDER RING` stayed 0 in every Report, but these Reports were read while paused as asked, so this says nothing about audio under load.

## Decision proposed (owner)
Replace T4 (column list) by **T3b: burst-gather `CBLIT` and `SBLIT` in the engine** (RTL, behind the existing differential test, so the picture stays identical), with the Waterfall fullscreen tick (712 k cycles, 678 k in the fence) as the number to move, and re-measure with these same rows. The T3 block is already merged and rides the same fit.
