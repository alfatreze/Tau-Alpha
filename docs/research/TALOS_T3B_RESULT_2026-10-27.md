# T3 / T3b on a Pocket: TAU DEV 196 (the `t3-b898` bitstream), 2026-10-27 08:01 to 08:12

14 Report shots decoded (`Memories/Screenshots/20261027_080100` .. `081137`; `081053` is not a grid), firmware `0.6.1-dev.196+f6b99486`, bitstream `t3-b898` seed 1 (T3 status block, burst CBLIT; NOT the clean-room scanout or the 400-word fill). The first shot is a boot Report; then, per meter, the window shot followed by the fullscreen shot (owner's order).

## What was seen
* `GFX Q OVF` 0 and `GFX SCAN LATE` 0 in every Report, `GFX FENCE TMO` 0, `UNDER RING` 0. With the status block present on this bitstream, these are the first real zeros: the command queue never overflowed and no scanout line was late over the whole run (all seven meters, window and fullscreen). `HW UNDER EV` grew by about one per Report (each photograph costs a dropout, as recorded).
* **Not shown by the rows: whether the firmware used the hardware fence.** There is no row for the probe's result; a timeout would have raised `GFX FENCE TMO` (0) and fallen back silently otherwise. A `GFX HW FENCE` row is needed (the next firmware build).

## Per meter (window / fullscreen, own average at the shot; compare `TALOS_T4_MEASUREMENT_RESULT_2026-10-27.md`)
| Meter | commands / tick | CPU cycles / tick | in fences | DEV 195 (same columns) |
|---|---|---|---|---|
| Winamp Bars | 6.2 / 9.1 (max 32) | 16.3 k / 17.0 k | 0 | 13.8 k / 13.9 k |
| Winamp Scope | 0.8 / 0.4 (max 71) | 9.4 k / 8.1 k | 0.9 k / 0.3 k | 8.2 k / 8.9 k |
| Chladni | 3.0 / 4.3 (max 13) | 234 k / 251 k | 90 k / 127 k | 243 k / 250 k |
| Layered Wave | 0.4 / 7.3 (max 466) | 29.8 k / 87.6 k | 0 | 48 k / 117 k |
| Master VU | 0.7 / 3.9 (max 8) | 12.4 k / 16.3 k | 0 | 16.1 k / 16.1 k |
| Waterfall | 19.4 / 48.1 (max 87) | 89.5 k / **661 k** | 68.7 k / **634 k (96%)** | 95 k / 712 k |
| Sound Map | 0.4 (max 116) | 177 k (fullscreen shot) | 0 | 71 k / 177 k |

## The correction
**The burst CBLIT did not shorten the Waterfall: 712 k became 661 k (-7%), the fence share stayed at 95%.** My reading of DEV 195 ("the Waterfall's fence is `CBLIT`'s one-transaction-per-pixel path", B-896, B-897) was an inference from the names of the calls in the meter, not from its numbers, and it was wrong. Reading `fw2/ui/meter_waterfall_grid.c` and `fw2/gfx/scroll.c` with the data in hand: a Waterfall picture scrolls the grid by the engine's copy, and `gfx_scroll_down` copies the whole grid **twice** (grid to a temporary stash strip, then back, in 120-column chunks); the `CBLIT`s only paint the few new rows (`d`), and each palette load also fences. The two full-grid copies are the engine time. The burst CBLIT is still correct and 3.1 times faster per row in simulation, but this meter barely uses it.

## What would move the Waterfall (proposals, owner's call; none built)
1. **No double copy (firmware, host-testable, no RTL):** scroll with ONE pass by walking the rows from the bottom up one row per command (the copy direction that does not overwrite unread rows), or by keeping the grid as an indexed plane in a ring in the stash and showing it with two burst `CBLIT`s (the wrap), so that no scroll copy exists at all. Estimated engine time: one grid pass (about 170 k system cycles) or less, from 634 k.
2. **Negative-stride row copy in RTL** (a sticky stride field that can step backwards), so a downward scroll is a single in-place `BLIT`: a fit-bundle item, larger.
3. Not worth anything for this meter: a column list, a command ring, a hardware fence (the wait is the engine drawing, not the round trip).
