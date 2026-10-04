# Session handoff, 2026-10-04 (later): all five meter packs, 4 KB scratch, PACKS_ONLY

Branch `meter-builder`, worktree `../tau-alpha-meter-builder`. **Pushed to origin; NOT merged to main.** Supersedes `SESSION_HANDOFF_2026-10-04_METER_PACKS_AND_HEADROOM.md` as the entry point (that one is still correct for the CPU budget, headroom and the first two packs). Firmware builds need the `toolchain` symlink and `RISCV_TOOLCHAIN_BIN` pointing at the main checkout's toolchain.

## State

| Area | Status |
|---|---|
| Winamp Scope pack (slot 2, ABI 4: `grad`, `fullscreen`, `bg_restore`, `bg_blend`, `scope_note`), `fw/winamp_scope.inc` shared with the built-in | **Hardware-confirmed** (`TAU_DEV_METER_18`, owner: all tests passed) |
| MASTER VU cleanup (`in->dt_ms`, host-measured `mtr_info_t` readout) and pack (slot 3, ABI 5: `info`, text services) | **Hardware-confirmed** (METER_18) |
| 4 KB meter scratch at 0x26800, five slots, Chladni pack (slot 4, ABI 6: mailbox, blit, sblit, fence, bases, yield, cpu, toast) | **Hardware-confirmed** (METER_18) |
| Info > METER PACKS shows all five (`FILE 5 LW BR SC VU CH <ms>MS`; code alone = OK, `code-` none, `codeE26` refused) | **Hardware-confirmed** |
| `PACKS_ONLY=1`: five meters not compiled in, Settings list built from the pack directory (`viz_list_rebuild`), +5.8 KB hot RAM, -21 KB cold image | **Hardware-confirmed** (METER_18 is a PACKS_ONLY build) |
| Pack fallback tests (`OFF E20`, `LW E26`, and now: list with the pack file missing, saved pack meter falls back to the first meter) | **Deferred to the end** (owner) |
| Audio stats checks (sine crest 1.4, mono +100, inverted -100) | Not done |
| Card-only meters (parameters, presets, thumbnails inside the pack), Omega side | Not started |

Card (ejected): `TAU`, `TAU_DIAGNOSTIC`, `TAU_DEV_91`, `TAU_DEV_BARCODE_04` (other sessions, untouched), `TAU_DEV_METER_14` (known-good, kept for comparison), **`TAU_DEV_METER_18`** (current). Bitstream: `work/diagnostics/audio-stats-fit/ap_core_s2.rbf` (sha256 `c345af0e...`). Package: rebuild the packs and bundle (`tools/pack_meter.py <meter> --out work/packs/<m>.tmpk` for layered_wave, winamp_bars, winamp_scope, vu_master, chladni; `tools/pack_bundle.py work/packs/tau-packs.bin <the five .tmpk>`), then `python3 tools/package_dev_build.py --meter NN --rbf work/diagnostics/audio-stats-fit/ap_core_s2.rbf --rbf-sha256 c345af0ee1614f4d51391536a7c21975511269731079c99f62e7dc4eb7a9b93c --build-flags RAM_192K=1,CLK66=1,SDRAM_BUSY=1,LPC_FW=1,PACKS=1,PACKS_ONLY=1 --packs work/packs/tau-packs.bin`. Install: `tools/install_dev_core.py <package> --carry-from <old> --remove <stale> --yes` (dry run first, ask before the write).

## Numbers worth remembering

- Heap gap, 192 KB, PACKS: diagnostic profile 2,320 B (floor 2,048 B), release 11,488 B (floor 6,144 B). With `PACKS_ONLY`: 8,112 B and 17,264 B; release cold image 109,736 to 88,232 B.
- Pack sizes: Layered Wave 12,072 B image + 2,424 B ring in the slot + 820 B state; Winamp Bars 1,868 B / 240 B; Winamp Scope 1,012 B / 136 B; MASTER VU 3,076 B / 28 B; Chladni 7,152 B / 2,744 B.
- ABI is 6; the fingerprint in `sim/test_meter_pack.py` is `9222db2e` (change both together).

## Next, in the order I would take them

1. **Card-only meters**: ship the parameter table, presets and thumbnail inside the pack (manifest section in the `.tmpk`), so a meter needs no firmware metadata. Today `meters_gen.h` still holds all of it and a pack only replaces the drawing. Needs a generic Configure page over a pack-supplied table (the existing one already walks `mtr_data_t`), persisted meter index for ids the firmware does not know, and a decision on thumbnails (CLUT blit data in the pack).
2. Scratch sizing improvements (logged in B-612): Chladni's 3 KB state to the PSRAM slot, shared transient buffers; measure Info METER COST first.
3. Omega side: library view, cost ceiling from the manifest budgets, pack writer.
4. Look changes per meter (owner approval), `dt_ms`-aware ballistics, the framework items in `docs/features/AUDIO_HEADROOM_AND_METER_THROTTLE.md`.
5. At the very end: fallback tests, audio-stats checks, merge to main (shared files: `fw/player.c`, `fw/settingsui.inc`, `tools/ui_snapshot_renderer.py`, `tools/heap_gap_baseline.json`, `CLAUDE.md`, `docs/MMIO_ALLOCATION.md`; rerun `make test-host`, `tools/check_cold_calls.py`, `tools/check_heap_gap.py`, `tools/check_packs_abi.py`). Main's Check tags 24-27 are taken; the next free one is 28.

## Traps met this session

- **Equal feature macros** in firmware and pack (KB-102). A trace that hashes every host call (KB-105) proves equality of calls, not timing.
- **Settings are restored before packs load** (`settings_load` runs before `packs_boot_load`): validate the saved meter against the static `viz_order`, then fall back after `viz_list_rebuild()`.
- A comment containing `meters/*/` closes a C comment (seen before, B-240); keep globs out of C comments.
- `sed -i` on macOS needs an empty suffix argument; use Python for edits.
- The Info METER PACKS row only showed two packs until this session (the Scope pack would have been invisible on METER_15).
- `check_cold_calls.py` reads whichever ELF was built last: its "ui_draw_chrome placed cold" warning appears for every 192 KB build and is not new.
- Do not run a build while checks run; `git checkout -- dist` after every default build or check.

## Where to read

`docs/AUDIT_TRAIL.md` B-610..B-614, `docs/features/meters/METER_PACKS.md` (sections for each pack, ABI history, PACKS_ONLY), earlier handoffs `SESSION_HANDOFF_2026-10-04_METER_PACKS_AND_HEADROOM.md`, `SESSION_HANDOFF_2026-10-03_METER_BUILDER.md`. Skill entries KB-098..KB-106 (local, `analogue-pocket-dev`; KB-105 host-table equality traces, KB-106 leaving built-ins out).
