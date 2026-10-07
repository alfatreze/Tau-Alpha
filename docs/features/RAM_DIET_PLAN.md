# RAM diet plan (branch `ram-diet`, worktree `../tau-alpha-ram-diet`)

Measured 2026-10-07 on the shipped link (192 KB, CLK66, SDRAM_BUSY) from commit 31f03bf; `tools/check_heap_gap.py` now tracks exactly these:

| Build | Free (heap gap) | Floor |
|---|---|---|
| release | 15,504 B | 6,144 |
| release + tempo (ships in release too) | 8,752 B | 6,144 |
| Diagnostic Build (tempo on) | 1,328 B | 1,024 (lowered from 6,144 -> 2,048 -> 1,024) |
| Diagnostic profile | does not link, 3,376 B over | |

Rule: tempo is a live feature in both builds (only the 1.75x+ click fix is parked). Floors are not lowered again: a build that falls short sheds hot bytes instead.

## Owner constraints (2026-10-07)
- Keep an eye on performance: no change may cost CPU on the decode/tempo paths without a measurement.
- The MP3 ring stays at 24 KB.
- Tempo ships in release and the Diagnostic Build; only the 1.75x+ click fix is parked.

## Assessment after reading the code
1. DONE: the guard measures the shipped link.
2. DROPPED: art working set shares `pcm` (already PSRAM since B-567).
3. DROPPED (performance): tempo state to PSRAM. `pend` and `out_l/out_r` are written and read once per hop, 512 words each way per 2 KB: about 2,000 PSRAM-window accesses per 512-sample hop at 30-48 cycles = 60-100 k cycles per 10.7 ms, i.e. an estimated +8-12 % CPU on top of the stretcher's 26-31 %, for 4 KB. Tempo is already CPU-limited (idle 0 % at 1.75x), so this trades away tempo speed. Shrinking instead needs a streaming overlap-add (push the hop in slices) which touches the bit-exact core, twin and goldens; mono could halve it at run time but the buffers are static.
4. DROPPED (design rule): `PolyphaseStereo` to cold code. It is the mandatory stereo window whenever the hardware unit is absent, fails, or mid-track times out, and the firmware's rule is that playback works without the cold image (every cold entry is gated on `cold_code_ok`; docs/TEST_SCRIPT_B071.md fail-safe). In cold code that fallback would also run at ~31 cycles per instruction word, not real time.
5. EXCLUDED by the owner: ring 24 KB -> 20 KB.
6. OPEN, no CPU cost: meter scratch overlay (about 2 KB; golden-frame tests exist; re-init hook per meter).
7. Not worth it: stack 6 -> 5 KB (1 KB, worst case near 4.5 KB), tag buffer (many DMA users), `xmp3_IntensityProcMPEG2` etc. (same cold-gating rule as 4), the flat tail of 200-900 B hot functions.

Conclusion: with the ring excluded, the performance-neutral list is exhausted except the meter scratch overlay (~2 KB). Closing the diagnostic gap (needs ~1.7 KB more for the profile build, and margin for new features) therefore needs either that overlay plus a decision on one trade-off below, or an RTL step.

## Trade-offs left (owner decision)
- A: spend a few M10K blocks to give the CPU RAM back (about 7 blocks per 8 KB; needs a fit cycle; no CPU cost).
- B: accept the tempo-to-PSRAM CPU cost (4 KB, est. +8-12 % CPU).
- C: make the profile build drop a feature (it is a developer build) instead of fitting.
