# Session handoff 2026-10-16 (night): the new FLAC decoder (S8) is built and host-proven; its firmware wiring fits the link but has not run

Audit B-775..B-779 (`docs/AUDIT_TRAIL.md`). Supersedes `SESSION_HANDOFF_2026-10-16_PLAYER2_S4_S8.md` as the entry point (still correct for S0..S7, the traps and the build/install commands). **Everything is committed locally in the worktree `../tau-alpha-player2`, branch `player2`, nothing pushed.** Card tools need `--card /Volumes/Pock`.

## 1. State in one table
| Piece | State |
|---|---|
| Fit `undercnt-b768` (hardware underrun counter) | **closed, packaged as `TAU DEV 150`** (seed 2, all corners positive, worst hold +0.117 ns); NOT installed. The card still has DEV 137. DEV 150 = DEV 149 firmware + that bitstream: the build to test first. |
| FLAC decoder review | `docs/features/FLAC_DECODER_V2_REVIEW.md` (sections 1-14): audit of spec 8.2, revised spec, reuse feasibility, S8-0 measurements, independent review, S8-1c cycle comparison. Owner decisions 1-5 accepted (gate = no regression vs the current decoder; CRC-16 on; silent block + resync, give up after 8; dr_flac only as oracle; dither hook later). |
| S8-0 measurements | harness `tools/lab/flacperf` (README). Current decoder (Rice fast path + unit) 189-194 cycles per channel-sample at `-Os` = 25-29% of the CPU; dr_flac reuse closed (LGPL foxen out, dr_flac fails the pre-set gate); the governor never engages for FLAC any more. |
| New FLAC core `fw2/cores/flac2_core.h` | **built, host-proven.** Resumable bit reader + Rice, predictors (32-bit tier behind the per-subframe `sum|c| < 2^(32-b)` bound, 64-bit tier), CRC-8/16, STREAMINFO, frame header, frame decoder (buffered ch0 + streaming ch1, all subframe types, wasted bits, stereo, mono), recovery (finder, timeline, silence, give-up after 8, false-sync bounds), open/metadata walker, seek table + search, optional hardware LPC unit tier with fault fallback. |
| Track driver `fw2/cores/flac2_track_core.h` | open (picture skipped by repositioning), bridge over ring wraps, staged/commit events, seek landing on the sample, silent tail, IO failure. Verified through the REAL stream reader + a card model. |
| Audio ring | `aout_put_staged / aout_commit / aout_rollback` added (aout_put unchanged); host model scenario + 3 mutants. |
| Firmware wiring | `P2_FLAC2` path in `fw2/transport/mp3play.c` (+ `fw2/decode/flac2_unit.c/.h`, `fw2/build.sh` flag `FLAC2=1`). **Compiles and links under the real linker asserts** (`M3=1 FLAC2=1`, tau.rom 121,612 B, 1,120 B spare; the default config is unchanged at 157,408 B). **Never run on a Pocket; no host test exercises mp3play.c itself.** Default build still uses the old decoder. |
| Not done yet | (1) wire the real LPC unit into `flac2_unit.c` (`f2_hw_t` wrappers over `tau_lpc_hw_begin/sample`; `FLAC2=1` must also add `lpc_hw.c` and `-DTAU_LPC_FW=1`; `flac2_set_hw` after `flac2_init`; without it 24-bit costs about 345 vs 201 cycles per sample); (2) START-report / Info rows for the new counters (`mp.f2_*` exist in `mp_t`, nothing displays them); (3) package a test DEV with `FLAC2=1` and A/B it against DEV 150 on a Pocket; (4) independent review of the `mp3play.c` diff (the transport glue has no host test); (5) the `-O2` experiment on the old decoder (cheap, 12% of cycles for 4.4 KB, owner never answered); (6) FLAC tempo re-check (B-554 was decided before the Rice fast path); (7) S9: remove `fw/flac.c` only after hardware acceptance. |

## 2. The tests (all in `sim/`, quick variants wired into `make test-host`)
`test_p2_flac2.py` (Rice), `_pred` (predictors, UBSan), `_hdr` (CRC/STREAMINFO/headers, 3,872 bit flips), `_dec` (frame decoder, synthetic encoder `sim/flac2_enc.py`, 9 real files vs STREAMINFO MD5, unit tier, 300 corruptions), `_rec` (recovery), `_open` (66 real files vs a Python parse), `_seek` (1,296 seeks + real music), `_track` (driver through stream + ring), `test_p2_aout.py` (staged puts). Environment switches keep `make test-host` fast: `FLAC2_DEC_N`, `FLAC2_DEC_REAL=0`, `FLAC2_REC_N`, `FLAC2_SEEK_REAL=0`, `FLAC2_TRACK_QUICK=1`; the full runs take 10-30 minutes each. **Final state of the suites: all green** after the 32-bit narrowing and the forged-number change (2^31 / 2^20, so the recovery/seek mutants 19 and 31 are caught again): base, pred, hdr, open, dec (incl. the LPC-unit tier and 24-bit real music), rec, seek, track (quick), aout. The full (non-quick) track run with real music was last green before the narrowing and passed its quick variant after; rerun it once in full if time allows.

## 3. Findings worth keeping
* **Dr_flac vs ours at equal optimisation has the same instruction count**; the apparent cycle gap came from `-O2` vs `-Os` and from the uncalibrated MMIO constants. Calibrate the simulator with DEV 150's `MP DEC AVG` for a known file before trusting any cycle number.
* **The new core in pure software is cheaper than the old decoder with its unit for 16-bit music** (173/187 vs 189/194 at `-Os`); **24-bit needs the unit** (345 vs 201 without). The 32-bit tier passes 100% of real 16-bit predictors, 0% of 24-bit.
* **RAM**: the new unit is 15.7 KB at `-Os`; the first integration overflowed the link by 17 KB. Fixed by: own `-Os` unit, state overlaid on the MP3 decoder state (union `dv` in `mp3play.c`, `dec_live` flag, macro `DEC`), FLAC pass outlined, positions narrowed to 32 bits. Do not re-widen. Measure with a copy of `fw2/platform/link.ld` without the ASSERT (see KB-184).
* The stream reader needs a ring of several chunks (it reads only when 4,096 bytes are free): test caps >= 8,192.
* A frame error mid-stream is handled by staging: nothing of a failed frame is ever audible.

## 4. Traps
* Work in `../tau-alpha-player2`; `git branch --show-current` before every commit. `toolchain/` is a symlink to `../tau-alpha-boost/toolchain`.
* `mp3play.c`: never name a macro `dec` (the MP3 struct has a member `dec`); the variable is `DEC`. A FLAC track overwrites `dec` (union): every `MP3FreeDecoder` is guarded by `dec_live`.
* The test harnesses compile with `cc` (clang); the rv32 simulator profiler is `tools/lab/flacperf/prof_sim.py` (instruction counts exact, cycles an estimate).
* Do not install or push without being asked.

## 5. Next steps (in order)
1. (Optional) rerun the full `test_p2_flac2_track.py` once; everything else is green and committed.
2. Owner: approve installing `TAU DEV 150` (dry run first) and send one START screenshot (`decode_tau_suite.py --grid <shot> --table`): that also calibrates the cycle model.
3. Wire the real LPC unit into `flac2_unit.c` + build flags; add the Info/START rows for `mp.f2_*`; independent review of the `mp3play.c` diff; package `TAU DEV 151` with `FLAC2=1` for the A/B.
4. Gapless FLAC hardware acceptance, then S9 (retire `fw/flac.c`), then merge `player2` into `main` only when asked.
