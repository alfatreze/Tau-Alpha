# One-pass scroll on a Pocket: TAU DEV 197 (2026-10-27 08:58 to 09:02)

5 Report shots (`Memories/Screenshots/20261027_085853`, `085940`, `090013`, `090117`, `090202`), decoded. Order (owner): Waterfall HORIZONTAL window and fullscreen, reboot and a boot Report, VERTICAL window and fullscreen. Firmware `0.6.1-dev.197+e7736c51` on the `t3-b898` bitstream (the same bitstream as DEV 196).

| Waterfall | commands / tick (avg, max) | CPU cycles / tick | in fences | queue stall |
|---|---|---|---|---|
| horizontal, window | 16.4 (30) | 89.9 k | 69.3 k | 0 |
| horizontal, fullscreen | 36.4 (75) | **226.8 k** | 203.0 k | 0 |
| boot (restored Waterfall) | 15.7 (17) | 89.5 k | 69.2 k | 0 |
| vertical, window | 186.8 (392) | 125.1 k | 68.8 k | 0 |
| vertical, fullscreen | 732.8 (1,305) | **450.4 k** | 282.0 k | 9.0 k |

Other rows, all five Reports: `GFX HW FENCE` = **2** (the hardware FENCE command is the fence in use: the T3 block works on the Pocket and the probe chose it), `GFX SCAN LATE` 0, `GFX Q OVF` 0, `GFX FENCE TMO` 0, `UNDER RING` 0.

## What it says
1. **The hardware fence and the status block work on a Pocket** (`GFX HW FENCE` 2, no timeout, no overflow, no late scanout line).
2. **Horizontal fullscreen: 661 k (DEV 196, two-copy scroll, same meter and the same command profile of about 48 commands a tick) to 227 k cycles: 2.9 times less.** The fence share fell from 634 k to 203 k. The old two-copy temporary was the cost, as read from the code. The window case did not move (90 k): its fence is a constant of about 69 k that is also there at boot (a fixed cost per tick, to be found).
3. **Vertical fullscreen is 450 k, twice the horizontal figure, with 733 commands a tick on average (1,305 at most).** The scroll is one row per command when d is one pixel (a downward move must be chunks of at most d rows), 4 column chunks x 300 rows = 1,200 commands. From the horizontal run (900 row-copies in 227 k cycles) and this one (1,200 row-copies in 450 k), each extra command costs the engine and the CPU about 180 engine cycles beyond its row copy; the queue filled at times (stall 9 k). **I have no DEV 196 vertical baseline**, so I cannot say what the old two-copy vertical tick cost; by the horizontal ratio it was probably 600 k or more, so the gain is likely about a quarter, not two thirds.
4. The per-command overhead is what is left. A downward move that is ONE command per column chunk (4 commands a tick whatever d is) would make vertical cost what horizontal costs (about 227 k): that needs the engine to copy rows bottom-up (a sticky "reverse" flag that subtracts the stride instead of adding it, in the BLIT/COPY row step: a small, additive RTL change, default off, the existing differential tests unchanged) and a firmware probe. It would ride the next fit.

## Proposed next step (owner's call)
RTL "reverse row order" for BLIT/COPY (sticky field, default 0): one command per column chunk for a downward scroll; host-proven like the other T3 items (`sim/tb_tau_gfx_move.v` extended), firmware uses it when the probe says present, falls back to the chunked scroll otherwise. Then the vertical Waterfall should land near the horizontal 227 k.
