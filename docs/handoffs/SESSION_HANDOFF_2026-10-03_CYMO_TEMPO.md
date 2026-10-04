# Session handoff, 2026-10-03 (late): Cymo hand-off confirmed, Cymo audiobook tempo built into the player

Read this first, then `docs/CURRENT_STATUS.md` and `docs/ROADMAP.md`. Audit ids B-529 to B-558 in `docs/AUDIT_TRAIL.md`. `main` is pushed to origin (`d3d1801`); working tree clean.

## State in one paragraph

The Cymo resampler's hand-off fix (elastic queue, B-527) is hardware-confirmed (SINAD 54.5 dB ON against 10.8 dB OFF, no pitch difference, no added noise). Since then the work was the audiobook feature: pitch-preserving tempo (WSOLA) with optional pause shortening. A float prototype, a fixed-point C core (three-stage search, then a streaming v2), an independent integer twin and a bit-exact test exist; the core is measured at 5.4 M instructions per second of output on rv32im. **Tempo is wired into the MP3 loop (T2) and installed as `alfatreze.TAU_DEV_79`, but it has never run on a Pocket. The next step is the owner's hardware test of DEV 79.**

## On the card

`TAU`, `TAU_DIAGNOSTIC` (v0.6.0-alpha.1 release cores), `TAU_DEV_79` (mine: release-style build with tempo, B-527 seed 1 bitstream, media + library carried, speech MP3 and FLAC included), and the owner's meter-worktree cores (`TAU_DEV_METER_01/03/08`, not mine, never touch them). DEV 70 and the METER numbers belong to the meter worktree: use the next free number for new builds.

## What DEV 79 is and how to test it

Release-style (no Diagnostics menu, no Check, no `ALL` stall counter) built with `TEMPO=1`: Settings > Playback > **TEMPO** (Off, 1.10, 1.25, 1.50, 1.75, 2.00x), MP3 only, exclusive with the varispeed Speed list, not persisted. Test: mono speech MP3 (Test Album track 11) at 1.25/1.50/2.00x (pitch kept, no buzzing joins); a stereo MP3 (MacCunn 128) at 1.25x; after 10 s of plain playback read Info > HEADROOM (`<tempo> I<latest>/<worst> O<io> WS<x.x>X`) and UNDERRUNS; seek, pause/resume, next track, back to Off. Known limits: every flush (seek, resume, track change) drops what the stretcher had staged (up to about 0.15 s); no soft reset on resume; FLAC excluded; no auto step-down guard; no pause shortening; the Diagnostic build cannot hold tempo (4.8 KB heap above its floor).

**Open unknown that decides a lot:** the stretcher runs from COLD code (PSRAM instruction alias) because the 192 KB link leaves only 13.9 KB of heap in the release build (6.1 KB floor; the design had assumed 52 KB from the default 256 KB link). Whether the instruction cache holds its loops is unmeasured; HEADROOM will show it. If idle is very low with tempo on, the cost per grain is far above the 5.4 M instructions measured under the simulator.

## Decisions made this session (owner)

- Pause setting: **Off (default), Small, Medium, High** (presets approved by ear: 3.0 / 6.2 / 9.8 % saved on the Twain clip).
- **No speed changes for FLAC** (`speed_eff()`); FLAC audiobooks doubtful, revisit "much later with proper files". Stereo FLAC music (MacCunn, 672-694 kbps) has no decode headroom even at 1.00x.
- **Speed list unlocked to 2.00x for MP3** (varispeed, all builds); 2.50x stays Diagnostic-only. Volume hold-to-repeat built (shared `fw/key_repeat.h`); Cymo resampler default ON in the Diagnostic Build, engaged only for a 44.1 kHz file at 1.00x (`cymo_guard_apply()`).
- Layered Wave and all meter work belong to the owner's separate worktree: not this session's.
- The example audiobook (Twain, LibriVox) is mono 44.1 kHz 128 kbps MP3; stereo audiobooks may exist.

## What was measured (all on DEV 7x, plain playback, worst second)

| File | 1.00x busy | Other |
|---|---|---|
| mono speech MP3 128k | 44% | idle 56% (B-539) |
| mono speech FLAC (hardware LPC) | 40% | 51% at 1.50x, 67% at 2.00x, no stalls up to 2.00x heard (B-545) |
| stereo MP3 128k (MacCunn) | 58% | 83% at 1.50x, no stalls (B-553) |
| stereo FLAC 354 kbps displayed (MacCunn) | idle 0-9% | thousands of FIFO-empty events (B-553) |

At 2.00x **varispeed** a FLAC stalled 516 times (23 ms FIFO at the 88.2 kHz drain), none at 1.00x (B-547/B-548): the reason tempo keeps the FIFO at the native rate. The Info UNDERRUNS row counts at most one stall per flush (sticky flag); `UNDERRUNS n ALL m` (Diagnostic Build) counts every FIFO-empty event (B-546, skill KB-090).

## Cymo state and open items

Resampler: fixed 147:160 (44.1 to 48 kHz), toggle in Diagnostics, default ON there (not in release-style builds). Tested chain: `tools/lab/cymo_loopback.py gen/analyze/track`, recordings in `../test music/Audio Lab/`. **Open: issue 023** (a rare random audio spike, not shown to be Cymo; plan = read S/D/UNDERRUNS when heard, long OFF/ON/48 kHz loopback recordings) and a note that after Cymo ON then OFF the noise profile seemed different (logged in 023, not measured). Parked per owner scope: C3-C6, C8, Bluetooth cart output.

## Tempo build order (docs/features/CYMO_TEMPO_INTEGRATION.md)

T0 measure (stereo headroom done, cold-code speed pending on DEV 79); **T1 core v2 done**; **T2 player path built, untested**; T3 pause shortening (delay line of about 1.2 s, causal noise-floor tracker, three presets: constants in `tools/lab/cymo_tempo_model.py` `PRESETS`); T4 settings persistence, Info row, tempo in the Diagnostic build (needs a stack/heap pass: the stretcher's step scratch is 1 KB on a 6 KB stack); T5 stereo ceiling (projection: stereo MP3 about 1.25x). Also owed: soft reset on resume, the headroom guard (auto step-down on stalls or low idle), `HR_WSOLA_EST_PCT` (12, an estimate) replaced by a measurement.

## Files that matter

`fw/wsola_core.h` (core v1 slab API, kept as reference, and v2 streaming `ws2_*`), `fw/wsola_tables.h` (generated Hann), `fw/tempo_core.h` (funnel: chunking, PSRAM staging ring at +0x600000, drain through `cymo_push`), `fw/player.c` / `fw/settingsui.inc` (hooks, `speed_eff()`, `tempo_*`, TEMPO row), `fw/pcm_push.h` + `cymo_push()` (the one audio push path), `fw/headroom.h` (HEADROOM, ur_all), `fw/key_repeat.h`; tests `sim/test_wsola.py`, `sim/test_tempo_funnel.py`, `sim/test_pcm_push.py`, `sim/test_headroom.py`, `sim/test_key_repeat.py` (all in `make test-host`); labs `tools/lab/cymo_tempo_model.py`, `wsola_fixed_ref.py` (integer twin + table generator), `wsola_search_lab.py`; rv32 harnesses `tools/host/wsola_harness.c`, `wsola2_harness.c` (instruction counts under `tools/rv32sim.py`).

## Procedures and traps

- Install with `tools/install_dev_core.py` (dry run first, `--yes` after approval; media of the carried-from core is no longer backed up, B-543); package with `tools/package_dev_build.py --number N [--variant tempo] --rbf work/diagnostics/cymo-feed-b527/ap_core_s1.rbf --rbf-sha256 195db766211c95ad4973c6c33b69494d222b18fd985f92a98d98f0c4f0f9db89 --build-flags RAM_192K=1,CLK66=1,SDRAM_BUSY=1,LPC_FW=1` (comma-separated, raw rbf only). The tempo variant needs RAM_192K=1 or the release target would write `dist/`.
- `tools/check_heap_gap.py` rebuilds with default flags and clobbers `dist/`: run `git checkout -- dist` afterwards. Its numbers are the 256 KB link; **size features against the 192 KB link** (skill KB-091).
- Screenshots on the card come from several cores (the owner also runs the meter worktree): check the Info rows (`AUDIO STATS` / `METER PACKS` = meter core) before reading a screenshot as DEV 7x.
- Sessions share one checkout with the meter worktree: `git branch --show-current` before commits.

## Resume sequence

1. Ask the owner for the DEV 79 results (or read the new screenshots on the card; the card must be mounted). 2. If tempo works: choose between the soft reset on resume, the headroom guard, persistence/Diagnostic-build fit, or pause shortening (T3). 3. If HEADROOM shows the stretcher is slow from cold code: measure cold versus hot cycles per grain, then decide (hot code needs about 6 KB more heap than the release link has: move something else cold, or shrink the state with PSRAM-resident ring and tail).
