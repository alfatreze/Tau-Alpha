# Session handoff 2026-10-16 (later): Player 2 now plays music: S4 to S8a built, S4 hardware-confirmed, the rest host-proven only

Audit B-762..B-774 (`docs/AUDIT_TRAIL.md`). Supersedes `SESSION_HANDOFF_2026-10-16_PLAYER2.md` as the entry point (that one is still correct for S0..S3 and the traps). **All work is committed locally in the worktree `../tau-alpha-player2`, branch `player2` (head `878451a`+), NOT pushed.** Card tools need `--card /Volumes/Pock`. `toolchain/` in the worktree is a symlink to `../tau-alpha-boost/toolchain`.

## 1. State in one table
| Piece | State |
|---|---|
| S4 stream reader + queue + slot driver + library load + list walker | **Hardware-confirmed (B-763/B-764, DEV 134/135):** 8 tracks to the end with exact sizes, 89 MB, 0 failures, 2.4 MB/s flat out, replay hashes identical, stale completions dropped |
| S6 MP3 transport, seek, gain (volume x ReplayGain), polyphase hardware window, tempo, FLAC (interim inherited decoder + hardware LPC), settings table, Now Playing, library browser, spectrum meter, cover art (TIM1), Settings menu, hold-to-repeat, skip silence | **Built and host-proven where the logic is pure; NOT run on a Pocket.** `TAU DEV 136/137` (MP3 only) ran: gapless continue, ACCT ERR 0, UNDER RING 0, DRY 0; the owner heard "good, a very slight click on track change" (fade added in 138) |
| S1 | `0x008A` is already handled in RTL (`R_RELOAD`, the spec was wrong); CAPS register dropped; **hardware underrun counter built** in `pcm_fifo.v` (MMIO 0x19C, `sim/tb_pcm_under.v`, 3 mutants) and **fit `undercnt-b768` running on the VM** |
| S5 settings | table + persistence built (volume w0, repeat w2, ReplayGain bits 5:4 of w17); published only when nothing streams |
| Still to do | the new FLAC decoder (S8 proper, replaces the inherited one), FLAC seek, EQ page (Halcyon), gapless hardware acceptance, S9 retire, Omega writes `tau-skip.bin` |

## 2. What to install and how
**`TAU DEV 149` supersedes 142..148 and is the build to test.** It is the first complete player: A play, B pause, Y seek (step in Settings), X shuffle, Left/Right track, Up/Down album jump, L1/R1 volume (hold repeats), Select+L1/R1 tempo, Select+Y ReplayGain, Select+X repeat, **Select+A library**, **Select+Up Settings**, Select+B Info, START = pixel-grid report (any other key returns; one screenshot, `python3 tools/decode_tau_suite.py --grid <shot.png> --table`). Package: `python3 tools/package_dev_build.py --number NN --variant player2 --rbf work/diagnostics/pcmring-b745/ap_core_s1.rbf --rbf-sha256 c7423a54d55cbbd89181fa27a12c9fd6804e6630be552543eb4ec3b66b50249e --build-flags PLAYER2=1,M3=1` (the bitstream is the fitted `pcmring-b745` seed 1; `M3=1` is the player build, `POLY=0`/`FLAC=0`/`LPC=0` switch units off for an A/B). Install: `python3 tools/install_dev_core.py work/diagnostics/tau-dev-NN/pocket --card /Volumes/Pock --remove "alfatreze.TAU DEV <old>" --backup-dir "$PWD/work/card-backups/<name>" [--yes]` (dry run first; the owner approves card writes). **The card currently has TAU DEV 137 on it** (and the older cores); 138..149 were never installed.

**What the first test should look at (START report):** `ACCT ERR`/`ACCT BAD` 0, `UNDER RING`/`DRY` 0, `MP ERRORS` ~0, `MP GAPLESS` counting on a same-rate album, `MP DEC AVG` (us per block, a block lasts 26,122 us), `MP POLY SLOTS/BAD/TMO`, `FLAC REFUSED/ERR/WAITS` (WAITS must not grow while playing), `LPC SAMPLES/TMO`, `ART LOADED/FAILED/ERR/CLUT`, `SET STORES/MENU`, `STACK PEAK` (the stack was cut to 5 KB), `FREE`. A/B the polyphase with a `POLY=0` build. Covers need `tau-art` sidecars (the TIM1 folders already on the card have them).

## 3. The VM fit
`undercnt-b768` (2 seeds, launched 02:35 WEST, expected 04:15-04:25) = the shipped bundle `blit_g3_poly_blend_ram192_clk66_dbuf_lpc_cymo_audio16_gain_boost_halcyon_wide_ring_qsf_append.txt` + the underrun counter. Check `python3 tools/vm_fit.py status undercnt-b768`; `collect undercnt-b768 --seed N` gives `work/diagnostics/undercnt-b768/ap_core.rbf` (**raw, for `--rbf`; never pass an `.rbf_r`**). Pick the seed by the usual rule, package as the next DEV with `--rbf ... --rbf-sha256 ...`; the new rows `HW UNDER EV/TK` and `CAP UNDER` then read real values. `make test-rtl` passed (rc=0) before the launch.

## 4. Findings worth keeping
* **RAM is the constant constraint** of the 192 KB link (image + static buffers must stay under `_tag_start`; the linker asserts 1 KB free). Recovered so far by: report buffer in the idle tag zone, tempo output streamed in 128-pair blocks, `TEMPO_SLICE=1`, Helix window 1,536, stack 5 KB, every non-hot file `-Os` (`#pragma GCC optimize ("Os")` or the `SMALL` attribute), and the FLAC decoder as its own `-Os` unit (`fw2/decode/flac_unit.c`, -4.4 KB). About 8 KB free at DEV 148/149. Hot paths (Helix, the tempo stretcher, the decode pump) stay `-O2`.
* **MP3 seeking** (host-proven bit-exact, `sim/test_p2_mp3.py`): a Xing TOC is 8-bit (span/256, about 0.4% of the file): true VBR seeks are only that accurate; CBR (the LAME `Info` tag, or no tag) uses the constant frame size and aims half a frame EARLY; restart 5 frames before the target and drop the warm-up; **a sync word is accepted only if the next header sits where the first one says the frame ends** (false syncs inside audio data decoded garbage after a mid-frame landing).
* The Helix arena (24 KB) holds ONE decoder: free the old one on every track change (a skip without it made `MP3InitDecoder` fail and the track never end, DEV 136). FLAC's 20 KB channel buffer comes from the same arena.
* One known-answer palette blit decodes BOTH the mailbox half order and the CLUT write skew of a bitstream (`fw2/art/art.c`).
* APF saving the persist file drops the slot's fragment cache: publish settings only while nothing streams.

## 5. Decisions and ideas recorded
* Skip silence: Omega detects, one `tau-skip.bin` per album folder; contract `docs/features/SKIP_SILENCE_SPEC.md` (Omega still to implement; thresholds are my suggested defaults, untuned). MP3 only for now.
* Player 2 modules and rule: `tools/check_deps.py` (added `art`); ui reaches data only through transport.
* VBR sample-exact seek needs a frame table from Omega (add to the Omega sync list with skip silence).

## 6. Next steps (in order)
1. **Test DEV 149 on a Pocket** (owner), read one START report; fix what it shows. Highest risk: the cover load (probe + blocking mailbox stream), the 5 KB stack with FLAC+tempo, the polyphase unit in the new pump, FLAC gating (`FLAC_NEED`), settings persistence.
2. Collect `undercnt-b768`, package, confirm `HW UNDER` on hardware.
3. Gapless hardware acceptance (tone album, EVO 4 recording, phase <2 degrees, level <0.5 dB).
4. S8 proper: the new FLAC decoder with partition-sized steps and seek (then FLAC skip silence); EQ page (Halcyon); S9 retire the old player; merge `player2` into `main` only when the owner asks; push only when asked.

## 7. Traps
* Work in `../tau-alpha-player2`; the primary checkout is a stale detached HEAD. `git branch --show-current` before every commit.
* `/tmp/build_na.sh` style no-assert builds are throwaway; `fw2/build.sh` is the real one (M3=1 for the player). Keep `make test-host` green (it builds fw2 and runs all `sim/test_p2_*.py`; the MP3 tests skip when `../test music` is absent).
* The 32-bit cycle counter wraps every 64 s (use `monoclock_core.h`); `LP KB/S` in the old list walker is only right under 64 s.
