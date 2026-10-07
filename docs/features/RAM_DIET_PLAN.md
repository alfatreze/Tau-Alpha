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

## Done on this branch (2026-10-07)
- Guard measures the shipped link. **Correction:** the profile build's shortfall was overstated by 2 KB (a stale stack constant in `fw/build.sh`'s over-limit message); it was about 1 KB over, not 3.4 KB.
- Tempo is in the normal release (`fw/build.sh` default, `TEMPO=0` removes it): release 8,960 B free (floor 6,144 B).
- Tempo state diet, behind switches so it can be A/B tested on a Pocket: `TEMPO_SLICE=1` (output hop produced and pushed in 64-sample slices, -1,792 B) and `TEMPO_RING=512` (stage-2 ring, -1,024 B; the largest span ever needed is 320 entries). Release 11,776 B free, Diagnostic 4,416 B, **the profile build links again (1,760 B)**. Host tests: sliced output == whole-hop output for every slice size and rate, 512 ring == 1024 ring, 11 mutants killed (`sim/test_tempo_slice.py`, `sim/test_tempo_funnel.py -DTEMPO_SLICE=1`). Not run on a Pocket.
- QR encoder archived (`archive/qr_encoder/`, tag `archive/qr-encoder`): about 4.4 KB cold code, 0.2 KB hot RAM.
- Diagnostic features register and `DIAG_DROP` / `FEATURES_ON` (phase 1, `docs/features/DIAG_FEATURES.md`).
- All 17 Diagnostic-Build pieces are switchable at build time (`DIAG_DROP=<ids>`, `DIAG_PRESET=perf|slim|release-like`, `FEATURES_ON=<id>`); measured per feature in `tools/diag_cost.json` (`docs/features/DIAG_FEATURES.md`). Diagnostic Build free hot RAM: 1,600 B default, 4,416 B `perf`, 7,568 B `slim`, 8,944 B with everything dropped (the release has 8,960 B, so the Diagnostic Build costs 16 B beyond its features). `stress` is the only large hot cost (2.9 KB); `cymo_toggle` is the only one that changes the default audio path. Default builds hash identical to before every conversion.
- Mode-overlay: proposal and host prototype only (`docs/features/MODE_OVERLAY_PROPOSAL.md`); recommended against for 2-3 KB.

## Pocket test of the tempo diet
Build both from the same tree and compare HEADROOM and UNDERRUNS at 1.00-1.75x on a mono speech MP3 and a stereo MP3: A = default, B = `TEMPO_SLICE=1 TEMPO_RING=512` (`python3 tools/package_dev_build.py --variant tempo --build-flags RAM_192K=1,CLK66=1,SDRAM_BUSY=1[,TEMPO_SLICE=1,TEMPO_RING=512] ...` with the bitstream that matches the firmware; the Halcyon fit is still pending, so no package was made). Expected: identical sound; the sliced path calls the meter hook 8 times per hop instead of once.

## Remaining trade-offs (owner decision)
- A: the three-region 224 KB RAM is a real RTL project, see the discussion in the log; not proposed.
- Next conversions if more is needed: `counters` (hot hooks), `rate_toggles`, then `sweeps` / `blit_test` / `tests_page` (cold).
