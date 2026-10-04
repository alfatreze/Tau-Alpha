# Session handoff, 2026-10-04: meter packs, audio headroom, Settings meter rows

Branch `meter-builder`, worktree `../tau-alpha-meter-builder` (never the shared checkout). **Pushed to origin up to `1b9bb5b`; NOT merged to main.** Main was merged into the branch on 2026-10-04 (FLAC Rice fast path, CLUT fixes, art overlay). Needs a `toolchain` symlink in the worktree and `RISCV_TOOLCHAIN_BIN` pointing at the main checkout's toolchain for firmware builds. Test cores are named `TAU DEV METER NN`.

## State

| Area | Status |
|---|---|
| Meter CPU budget in `helios_meter()` (duty cap, cost-aware FIFO gate, 300 ms starvation cap, 10 draws/s thin-headroom cap, all eight older meters routed through it), Winamp Bars delta repaint, Info `METER COST`/`UI COST`/`UI PARTS` | **Done, hardware-confirmed** (`METER_06..10`; hard FLAC: `UNDERRUNS 1 ALL 1`, idle 45 percent with main's Rice fast path) |
| Stress pump overlapped the H2 back buffer (screen noise in the longer Check) | **Fixed** (`fw/stress_defs.inc`), one clean 15-minute FULL Check; the event was 1 in 4, so not proven (B-605) |
| Layered Wave pack froze (data at the fetch-only instruction alias) | **Fixed and confirmed on a Pocket** (B-606) |
| Winamp Bars pack (slot 1), ABI 2/3, per-slot params, stats words, `LW_STATS` guard | **Done, hardware-confirmed** (`METER_12..14`) |
| Settings: METER on the main page, gear on the current meter when the row is selected, X configures, list X + gears, Appearance rows removed | **Done, owner-confirmed** (`METER_14`: "menu changes ok", minor visual artefacts to fix later, unspecified) |
| Pack fallback tests (`OFF E20`, `LW E26`) | **Deferred to the end of the whole feature work** (owner) |
| Audio stats checks (sine crest 1.4, mono +100, inverted -100) | Not done |

Card (ejected): `TAU`, `TAU_DIAGNOSTIC`, `TAU_DEV_80`, `83`, `84`, `85` (main line, untouched), `TAU_DEV_METER_01`, `03` (stale, remove on the next install), **`TAU_DEV_METER_14`** (current). Bitstream for all METER cores: `work/diagnostics/audio-stats-fit/ap_core_s2.rbf` (sha256 `c345af0e...`, audio-stats fit seed 2). Package command: `python3 tools/package_dev_build.py --meter NN --rbf work/diagnostics/audio-stats-fit/ap_core_s2.rbf --rbf-sha256 c345af0ee1614f4d51391536a7c21975511269731079c99f62e7dc4eb7a9b93c --build-flags RAM_192K=1,CLK66=1,SDRAM_BUSY=1,LPC_FW=1,PACKS=1 --packs work/packs/tau-packs.bin`; rebuild the packs and bundle first (`tools/pack_meter.py layered_wave|winamp_bars --out work/packs/<m>.tmpk`, `tools/pack_bundle.py work/packs/tau-packs.bin work/packs/layered_wave.tmpk work/packs/winamp_bars.tmpk`). Install with `tools/install_dev_core.py ... --carry-from <old> --remove <old> --yes` (dry run first).

## Next, in the order I would take them

1. **Scope pack** (slot 2): needs wave data and the blend path in the host table (ABI bump), same tests as Bars.
2. **VU Master**: remove its framework violations first (hard-coded 26 ms, reads decoder globals and `R_SDR_BUSY` directly), then pack it.
3. **Chladni**: decide the scratch size (3,072 B state vs the 1 KB scratch) or move its plane to PSRAM; heaviest host table.
4. A Settings **meter list built from the pack directory**, and the Omega side (library view, cost ceiling from the manifest budgets, pack writer).
5. **Look changes** needing owner approval per meter; the open framework items from the review (`dt_ms`-aware ballistics, `poll_input` page changes, Talos idle bit, `CPU LOAD` accounting): `docs/features/AUDIO_HEADROOM_AND_METER_THROTTLE.md`.
6. At the very end: the pack fallback tests; the audio-stats checks; merge to main (shared files: `fw/player.c`, `fw/settingsui.inc`, `tools/ui_snapshot_renderer.py`, `tools/heap_gap_baseline.json`, `CLAUDE.md`, `docs/MMIO_ALLOCATION.md`; rerun `make test-host`, `tools/check_cold_calls.py`, `tools/check_heap_gap.py`, `tools/check_packs_abi.py`).

## Traps met this session

- A **data load from the PSRAM instruction alias hangs the CPU**; link a pack's `.rodata`/`.pstate` at the data alias (KB-098). The host simulator cannot see this.
- **Same feature macros in firmware and pack** (`LW_STATS` changes the cost guard): host equality tests pass with both sides wrong (KB-102).
- **A second caller un-inlines a static function** and costs its size in hot RAM (`meter_afford`, 248 B); diff `nm -S` between builds (KB-101).
- **Do not run a build while checks run**: `fw/fw.elf` and `dist/` are shared outputs; `git checkout -- dist` after every default build or check. Wait for `check_heap_gap.py` before packaging. Edits made while a check runs invalidate it.
- The merge of main once left conflict markers in `CLAUDE.md` (fixed in a later commit; the older pushed commits still contain them). Resolve conflicts in `CLAUDE.md` by hand, keep both sides' log lines.
- `HEAP_MIN_OVERRIDE` was a temporary test-build escape (removed); main's diagnostic floor is now 2 KiB.
- A host cycle model understates the real core about 1.9x (KB-100). The decode-profile fields `t_pct`/`c1_pct` no longer mean CPU use after the Rice fast path (they include time blocked on the FIFO); use Info `HEADROOM`.
- Info page rows: `HEADROOM` 33, `METER COST` 34, `UI COST` 35, `UI PARTS` 36, `AUDIO STATS` 37, `METER PACKS` 38 (the renderer fixture's sample list must match).

## Where to read

`docs/AUDIT_TRAIL.md` B-600..B-609 (this session), `docs/features/meters/METER_PACKS.md`, `docs/features/AUDIO_HEADROOM_AND_METER_THROTTLE.md`, `docs/features/FLAC_RICE_DECODER_SPEC.md` (main built it, B-561/B-562), earlier handoff `SESSION_HANDOFF_2026-10-03_METER_BUILDER.md`. Skill entries KB-098..KB-102 (local, `analogue-pocket-dev`).
