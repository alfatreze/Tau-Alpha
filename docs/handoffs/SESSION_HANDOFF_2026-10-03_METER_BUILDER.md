# Session handoff, 2026-10-03: the meter-builder branch

Branch `meter-builder`, worktree `../tau-alpha-meter-builder` (never the shared checkout; the main-line session works in `tau-alpha`). **Nothing here is merged to main or pushed.** Every test core from this branch is named **TAU DEV METER NN**
(`tools/package_dev_build.py --meter NN`), so it never collides with the main line's TAU_DEV_NN. Needs a `toolchain` symlink in the worktree (git-ignored) and `RISCV_TOOLCHAIN_BIN` pointing at the main checkout's toolchain for firmware builds.

## What exists (12 commits on top of `c7b2d14`, `make test-host` and `make test-rtl` pass)

| Area | What | Where |
|---|---|---|
| Measurements | per-meter cold/hot/PSRAM sizes, a gap audit of what each meter duplicates | `docs/features/meters/METER_COST_MEASUREMENT.md`, `METER_GAP_AUDIT.md`, `tools/meter_size_report.py` |
| Core steps 1-5 | `mtr_in_t` gains `paused`, envelope, `energy`, `silent` and the signal fields; `fw/meter_core.h` gains colour, geometry, cache and signal helpers; the older meters, Chladni and Layered Wave use them; frames identical | `fw/meter.h`, `fw/meter_core.h`, `fw/player.c` |
| Golden frames for the older eight meters | generated from the pre-change firmware, so steps 1-4 are proven frame-identical | `sim/test_legacy_meter_golden.py`, `sim/golden/` |
| Audio signals | RMS, stereo correlation, crest factor, spectral centroid, clip count; RMS/correlation/clips from a new hardware block | `src/fpga/core/tau_audio_stats.sv` (`TAU_STATS`, MMIO 0x180-0x18C), cost table `docs/features/meters/AUDIO_SIGNALS_COST.md` |
| JS twins | the newer core functions, with 1,200 golden vectors | `tools/meters/preview/tau_core.js`, `fixtures/core_vectors.json` |
| Manifest budgets | `symbols` and `budget` per modular meter; checker | `meters/*/meter.json`, `tools/meter_budget.py` |
| Meter packs | Layered Wave as a loadable pack (ABI-versioned, ROM-independent, working state in an on-chip scratch), loader with 13 refusal cases, bundle file, firmware side behind `PACKS=1` (scratch at fixed 0x27400, data slot 9, METER PACKS Info row) | `docs/features/meters/METER_PACKS.md`, `fw/meter_pack*.h`, `fw/meter_packs.inc`, `tools/pack_meter.py`, `tools/pack_bundle.py`, `tools/check_packs_abi.py` |

## State of the world

- **Card (`/Volumes/Pock`, ejected):** `alfatreze.TAU_DEV_METER_01` (PACKS firmware + Layered Wave pack on the cymo-feed bitstream) installed, media carried; also `TAU`, `TAU_DIAGNOSTIC`, and the main line's `TAU_DEV_71`. **Not yet booted.** First thing to read: Settings, Diagnostics, Info, last row METER PACKS (`FILE 1 LW OK <ms>MS` expected); Layered Wave must look as before.
- **Quartus fit `audio-stats-fit` (seeds 1 and 2, VM `tau-local/audio-stats-fit-s1/-s2`)** with `TAU_STATS=1` on the Cymo bundle, launched 14:05 local, was still in the Fitter's physical-synthesis stage at 15:27 (expected done about 15:55). `python3 tools/vm_fit.py status audio-stats-fit`, then `collect audio-stats-fit --seed N`. Check: both Successful, all four corners positive (setup and hold), new ALM total (never quoted before), DSP 21/66, RAM about 256/308. Then package as `TAU_DEV_METER_02` with the new RBF (`--rbf` raw file plus `--rbf-sha256`, `--build-flags RAM_192K=1,CLK66=1,SDRAM_BUSY=1,LPC_FW=1,PACKS=1`, `--packs work/packs/tau-packs.bin`) and read the new Info row AUDIO STATS on a Pocket (sine: crest 1.4; mono: correlation +100; inverted channel: -100).
- Everything about the new RTL is simulated only; nothing of the pack path has run on silicon.

## Open items, in the order I would take them

1. Collect the fit, package METER_02, install (use `tools/install_dev_core.py`, dry run first, never remove a core the owner is testing).
2. Boot METER_01 and METER_02, report the two Info rows; fallback tests (file removed: `OFF E20`; corrupted copy: `LW E26`) need card edits.
3. Pack sources for the other modular meters (Chladni, Winamp pair, VU Master) and a Settings meter list built from the pack directory; then the Omega side (library view, cost ceiling meter summing the manifest budgets).
4. Hot read-only tables in scratch (about 71,000 cycles a frame of PSRAM reads in Layered Wave, estimated, in the built-in meter too): read the real `LW COST` Info row first.
5. Look changes that need owner approval per meter (D-M07): unify ballistics, move Winamp Bars' layout, share the slow band averages; wire RMS/correlation/centroid into an actual meter.
6. Before merging to main: rebase onto main (shared files: `fw/player.c`, `fw/settingsui.inc`, `src/fpga/core/mp3_soc.v`, `core_game.vh`, `Makefile`, `CLAUDE.md`, `docs/MMIO_ALLOCATION.md`), rerun `make test`, and run `tools/check_packs_abi.py`.

## Traps met this session

- `fw/fw.elf` is clobbered by every build; copy it before the next one if you need it. Default builds write `dist/`: `git checkout -- dist` afterwards in this worktree.
- BSD `sed` rejects `\n` in replacements; use Python for multi-line edits.
- A literal `*/` inside a C comment (e.g. a path glob) closes it; `meters/*/meter.json` did it before.
- Renderer fixtures parse `fw/settingsui.inc` text: anything that exists only in a PACKS build must be inside `#if TAU_PACKS` so the renderer strips it.
- The skill (`~/.claude/skills/analogue-pocket-dev`, local entries KB-088 and KB-089) records the pack architecture and the ld empty-section trap.
