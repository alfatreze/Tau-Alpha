# Meters on Player 2: audit of main and the plan (2026-10-21)

Branch `meters-p2` (worktree `../tau-alpha-meters-p2`, from `main` = `origin/main` = `f3974b5`). Written before any code change. Owner brief: revise the meter work for Player 2; keep the modular, systematic approach;
align with the Player 2 architecture (`docs/features/PLAYER_V2_SPEC.md`); remove any original HarpMudd code from the meters. Order of work is the owner's: this is a proposal with gates.

## 1. What changed on main since the meter work branched (2026-10-04)

`meter-builder` last merged main at `21f69ae` (2026-10-04). Since then main has **421 commits** (193 since 2026-10-12). Facts that matter for meters:

| Fact | Evidence |
|---|---|
| **Player 2 (`fw2/`) is the shipped player.** v0.6.0 Stable (2026-10-17) and v0.6.1 (2026-10-21) ship it; the old player is not built into any release. | `CHANGELOG.md`, `e6e978d`, `d2d1df8` |
| Player 2 has **five meters**, each its own file: Winamp Bars, Winamp Scope, Chladni, Layered Wave, VU Master (`fw2/ui/meter_*.c`), a hand-listed registry `meter_table[]` in `fw2/ui/meters.c`, generated preset tables (`tools/player2/gen_meter_presets.py`, `gen_lw_presets.py`), a Configure page (`ui/configure_view.c`), METR user presets (`tau-assets.bin`), saved tuning (persist 20-23, 29), compose into the idle buffer, Scope trail by blend. **The eight inherited HarpMudd meters were not ported.** | `fw2/ui/meters.c`, `meter.h`, handoff `SESSION_HANDOFF_2026-10-16_PLAYER2_METERS_H2.md` |
| Player 2 has its **own `mtr_in_t`** (`fw2/ui/meter.h`: spectrum, frame, dt, rect, bg, force, paused, peak_l/r, optional wave). It does not carry RMS, correlation, crest, centroid, clip, the envelope history or the info readout of `meter-builder`'s `fw/meter.h`. | `fw2/ui/meter.h` |
| UI load control is the **governor** (ring level: `NORMAL / SHED_UI / AUDIO_ONLY / PANIC`); `meters_tick(allowed)` is the only gate. `helios_meter()` and the duty-cap/FIFO-yield policy of `meter-builder` do not exist in Player 2. | spec 9.2, `fw2/ui/meters.c` |
| **Diagnostics are a generated counter registry** (`fw2/cores/counters_core.h`, `diagnostics/info_counters.c`): Info, reports and stress logs come from one table. There are **no meter counters** in it (no draw time, no draw count). | `fw2/diagnostics/info_counters.c` |
| The 192 KB link leaves **about 12 KB** of hot RAM free above the 1 KB floor (measured by `tools/check_heap_gap.py`; the first draft of this plan said 1.1 KB, which was the floor, not the free space). Meters hold **about 7 KB** of it: Chladni 2,745 B, Layered Wave 1,123 B (+2,424 B in PSRAM), VU/Bars/Scope 325 B and **`meters.o` 2,808 B, of which `mp[8][40]` (parameter shapes) is 2,560 B**. | `nm -S` of the Player 2 objects, this session |
| The cold image (`0xA4800000`, 1 MiB region, 222 KB used) holds all meter code; PSRAM below it is allocated (METR `0xA4550000`, Layered Wave ring `0xA4560000`, tempo `+0x600000`, PCM ring `+0x700000`). `meter-builder`'s pack slots (`0x24840000..`) **collide with the cold image region**. | `fw2/platform/link.ld`, spec 12.1 |
| The old player (`fw/player.c` and its `.inc` bodies) is **still in the tree only because six live checks read it** (themes, splash asset, meter core vs old `wviz_bars_tick`, preview build, `gen_meters`, diag-features). Archiving it is "step 2" of `docs/research/HARPMUDD_DISCONTINUATION_STATUS_2026-10-18.md`, not done. | that document |
| **HarpMudd lineage in meters (provenance 2026-10-18):** `fw2/` has **20 HarpMudd-origin lines of 12,752**; in meter files exactly **6** (`mix()` in `fw2/ui/meter_chladni.c`: RGB565 interpolation, the old `ui_mix`) and 1 in `fw2/cores/theme_core.h`. The rest is the old player: `ui_wave_*`, `vu_angle`, `spec_log`, `ui_grad_*`, `meters_feed` (236 lines in `fw/player.c`), and the whole of `tools/meter_preview.py` (76/76). The legacy `viz_*_tick` bodies are already 0 HarpMudd lines (rewritten into `mtr_in_t` form in B-390). | `provenance-2026-10-18/files.csv`, `firmware-functions.csv`, `git blame` |

## 2. What `meter-builder` holds (41 commits, pushed, never merged) and what to do with each piece

A direct merge conflicts in 20 files (`fw/settingsui.inc`, `suite.inc`, `player.c` neighbourhood, `mp3_soc.v`, `core_game.vh`, `link.ld` deleted on main, packagers, snapshots): it is Player 1 work. **Nothing is merged.** The branch stays as the record (tag it `archive/meter-builder-2026-10-04`); pieces are re-built on Player 2.

| Piece (branch) | Disposition on Player 2 |
|---|---|
| Meter CPU budget in `helios_meter()` (duty cap, cost-aware FIFO gate, starvation cap), `meter_policy.h`, Info METER COST / UI COST / UI PARTS | **Superseded by the governor** (ring level is a better signal than a FIFO gate). Keep the *measurement*: per-meter draw time and count as counters in the registry (M2). The Layered Wave stride guard stays in its module. |
| Winamp Bars delta repaint | Player 2's bars already skip unchanged bands (`mtr_delta`); re-measure before porting anything (M2). |
| Per-meter budgets in manifests (`budget`), `tools/meter_budget.py`, `tools/meter_size_report.py` | **Port** against the Player 2 objects (M2): budget = hot ROM, hot RAM, cold, PSRAM state per meter, checked in `make test-host`. |
| Meter packs (loadable meters from `tau-packs.bin`: `.tmpk` format, loader `meter_pack_core.h`, `tools/pack_meter.py`, `pack_bundle.py`, rv32sim equality tests, host table ABI 1-6) | **Re-base** on Player 2 (M3): the loader, header, CRC, bundle and rv32sim harness are player-independent and reused; the **host table is rewritten over `gfx_*`**, the slots move to free PSRAM above the cold region, the scratch is sized from the Player 2 meters. Value stated honestly: in Player 2 the RAM saving is about 1.5 KB (Chladni's state sets the scratch), not the 5.8 KB of Player 1; the real value is **loadable meters from Tau Omega**. Built-in meters stay the default (a card without the pack file must still have meters). |
| `PACKS_ONLY`, Settings list from the pack directory | Becomes the **registry**: `meter_table` entries come from the manifests (built in) and from installed packs, one mechanism (M1/M3). |
| `tau_audio_stats.sv` (RMS, correlation, crest, clip) + `mtr_in_t` fields + hardware fit `audio-stats-fit` | **Needs a fit on the current bitstream (CORE_VERSION rev 27 with the font RAM, ring, Halcyon)** and merges into `mp3_soc.v` conflicts. Held as M5; owner approves the VM fit. Not needed for M1-M4. |
| Legacy meter goldens (`sim/golden/legacy_meters.json`), `test_legacy_meter_golden.py`, `tools/meter_cost_estimate.py` for old functions | Tests of code that Player 2 does not have: **not ported**; the Player 2 picture tests (`test_p2_*`) are the standard. |
| Settings page changes (meter rows on the main page, gear icon) | Player 1 UI: **not ported**. Player 2 has its own Settings and Configure views. |
| Docs, KB entries (KB-098..KB-106), handoffs | Kept; the pack documents are updated for Player 2 in M4. |

## 3. Plan, stages and gates

Every stage ends with `bash fw2/build.sh`, `make test-host` (baseline today: green, `rc=0`), the **picture tests of the touched meters unchanged**, the heap-gap/size report for `fw2`, an audit entry (B-NNN) and a commit. Nothing is installed on a card and no VM fit is launched without the owner's approval.

### M1. One registry, one core location, no inherited lines in the meters (fw2 only, no behaviour change)
1. **Generated registry.** `tools/player2/gen_meter_registry.py` writes `fw2/generated/meter_registry.h` from `meters/<key>/meter.json` (the five selectable Player 2 meters): the extern declarations (`PRESET_API`), the `meter_table[]` rows, the manifest meter ids (`ids[8]` and `mids[8]` in `meters.c` today), preset counts and the `compose` flag. Adding a meter = a manifest plus one `meter_<key>.c`; no edit to `meters.c`. `--check` in `make test-host`.
2. **`mp[8][40]` (2,560 B) goes away**: parameter shapes are derived from the descriptors into one small buffer when a METR preset is read or a tuning packed (`meters.o` bss 2,808 to about 250 B, about **2.3 KB of hot RAM back**, doubling the free margin). Behaviour identical; `test_p2_metr`, `test_p2_tune`, `test_p2_mpre` must pass unchanged.
3. **HarpMudd line removal in meters:** Chladni's `mix()` is replaced by the shared `mtr_ramp` of `meter_core.h` (checked equal over the whole domain first; the Chladni picture test and `test_p2_chladni*` must not change); `fw2/cores/theme_core.h`'s one line is rewritten from its behavioural spec; `tools/meter_preview.py` is archived if nothing reads it.
4. **Meter cores move to `fw2/cores/`** as the spec's section 4 says (`meter_core.h`, `chladni_core.h`, `vu_master_core.h`, `vu_segment_table.h`, `layered_wave` body and tables), with their tests, generators and the preview/golden stack re-pointed. The old player keeps compiling through `-I fw2/cores` until it is archived (M6). The layer checker `tools/check_deps.py` / `check_meter_deps.py` is extended: **meters call `gfx_*`, `th_role`, `cores/*` only**.
5. Record the decision: the inherited meters are **retired** (D-M07 "legacy meters stay as they are" applied to Player 1 only).
Gate: byte-identical drawing for all five meters (picture tests and draw-command hashes before and after), `fw2` size not larger, free hot RAM +2 KB or more.

### M2. Meter counters, budgets and the size check (Player 2 form)
1. Counters in the registry (cold table rows, one writer): per meter `METER DRAW US` (last), `METER DRAW MAX`, `METER TICKS`, `METER SKIPPED` (governor), `COMPOSE US`; shown on Info, in the report, in the Check page. A meter's own counters (Chladni drawn/skipped, Layered Wave stride) join through the same table.
2. Manifest `budget` fields and `tools/meter_budget.py` over `fw2` objects (hot RAM, hot ROM, cold, PSRAM state); fails `make test-host` when a meter exceeds its declared budget. Budgets are set from the measured sizes (no invented numbers).
3. Fixture: host replay of the draw cost model (`sim/p2_engine_model.h`) gives a command count per frame for each meter and preset; stored and checked (the successor of `tools/meter_cost_estimate.py`).
Gate: the new counters read correctly in the host harness; baseline hash of the report layout updated knowingly (`docs/research/player2/BASELINE.json`).

### M3. Loadable meters on Player 2 (packs, ABI restarted)
1. **ABI v1 of Player 2** (`fw2/cores/meter_pack_core.h`, reused from the branch: header, CRC, slot/scratch rules, bundle): a pack exports one `meter_def_t`-shaped descriptor (tick, apply, preset set/name, parameter descriptors, `compose`) and reaches the firmware only through the host table (`gfx_rect`, `gfx_rect_stash`, blit/sblit/mailbox/fence, blend, theme roles, accent, cycles, PSRAM-ready). Pack code is the same `fw2/ui/meter_*.c` compiled with the host table behind macros (the `meter_pack_*.c` wrapper pattern of the branch), so there is **one source per meter**.
2. Memory: slots in free PSRAM above the cold region (`0x900000` upward, in `memmap`), a scratch in hot RAM sized by the largest pack (Chladni's 2.7 KB) and **released to the heap gap when no pack is installed**; `tools/check_packs_abi.py` ported (addresses must agree between the firmware ELF and a freshly built pack).
3. Boot: cold loader after `cold_boot_load` (needs cold code proven), each pack verified and installed independently; a refused or missing pack leaves the built-in meter (fail-safe, `OFF E<n>` shown on Info). Registry rows for installed packs are appended after the built-in rows, so a card without the file behaves exactly as today.
4. Tests: rv32sim equality of each pack against its built-in over the existing picture traces; refusal matrix; the five-pack bundle; mutants.
5. A DEV package for the owner (`TAU DEV NN`, player2 variant). **The fallback tests (file missing, corrupt) and the audio-stats checks stay deferred to the end of the whole meter work** (owner rule from 2026-10-04).
Gate: host-green, size/RAM within the M2 budgets, **no card install without approval**.

### M4. Documents and records
`METER_MODULE_SPEC.md` (Player 2 contract: `mtr_in_t`, registry, packs), `METER_PACKS.md` rewritten for Player 2, `DECISIONS.md` (retire the inherited meters; one registry; packs re-based), `PLAYER_V2_SPEC.md` section 11 corrected (it says meters take the old `fw/meter.h`), `docs/CURRENT_STATUS.md` and `ROADMAP.md` entries, `docs/AUDIT_TRAIL.md` (B-NNN per stage), a handoff, skill entries where a lesson was learnt, provenance re-measurement of the meter files.

### M5. New inputs from hardware (needs the owner): audio statistics
Re-base `tau_audio_stats.sv` on the current RTL (conflicts in `mp3_soc.v` / `core_game.vh` resolved by hand), add `rms_l/r`, `corr`, `crest`, `clip` to Player 2's `mtr_in_t` behind a capability bit, sim and mutation tests, then **one VM fit** (stated duration and finish time at launch). Optional; blocks nothing above.

### M6. (Optional, owner) Retire the old player
Finish discontinuation step 1-2: re-point `gen_themes`, the splash-asset test, the preview build, `gen_meters`, `gen_diag_features` at Player 2 sources; archive `fw/player.c`, its `.inc` bodies and the legacy release path. This removes the remaining 3,758 inherited firmware lines, including the old meter helpers. Large and touches release tooling; **not started unless asked**.

## 4. Risks and how each is covered

| Risk | Cover |
|---|---|
| A refactor changes a picture | Draw-command hash and picture test of every meter before/after each M1 step; no step merges unless equal. |
| Hot RAM growth from the loader/counters | Counters are cold table rows; loader is cold code; the M1 saving (2.3 KB) pays for M2/M3; `fw2` size report in every step. |
| Pack code runs from PSRAM data alias vs instruction alias (the Player 1 freeze, KB-098) | The branch's link script and rv32sim cannot see it: the DEV package carries a self-test row and the first run is the owner's. Linked `.rodata`/state at the data alias, code at the instruction alias, as proven there. |
| Slots overlap another PSRAM owner | Slots in `memmap` with the overlap test of spec 12.1; `link.ld` asserts keep the cold region at 1 MiB. |
| A card without the pack file loses meters | Built-in meters are the default; packs only add. |
| Persisted meter indices | The registry order is the manifest `index`; built-in rows never move; pack rows append. Saved index beyond the table = first meter (existing rule). |
| Concurrent sessions | Worktree `../tau-alpha-meters-p2`; `git branch --show-current` before every commit; nothing touches `main`. |

## 5. Decisions asked of the owner (defaults in bold)
1. Packs on Player 2 (M3): **yes, built-ins stay default** / no, registry only.
2. Audio statistics RTL and a VM fit (M5): **later, when M1-M4 are done** / now / never.
3. Retire the old player (M6): **separately, when asked** / with this work.


## 6. Results so far (2026-10-21, branch `meters-p2`, host-verified only, nothing on a Pocket)
- **M1 done:** one generator (`tools/player2/gen_meters_p2.py`) and `p2` manifest blocks drive the tables and the registry; `mp[8][40]` removed (2.2 KB hot RAM back); Chladni's inherited `mix()` replaced by `rc_ramp`; the meter cores moved into `fw2/cores`, `fw2/generated`, `fw2/ui`.
- **M2 done:** draw-cost counters in the registry (Info rows), per-meter budgets (`tools/player2/meter_budget.py`), the meter include rule in `check_deps.py`.
- **M1b finding and fix (Winamp Bars):** the old golden test compared the JS twins with the OLD player's `wviz_*` functions, so nothing compared the JS twin with what Player 2 ships. `sim/test_p2_meter_golden.py` now compiles `fw2/ui/meter_winamp_bars.c` and compares the pictures (pixel by pixel after every frame: the JS twin uses the hardware bar opcode, fw2 two rectangles; the pictures are equal for all five presets). The old test is removed.
- **M1b, Scope done:** the JS Scope twin is now the twin of fw2's hardware-capture scope (256-column captures, 64 drawn columns, smoothing, the trail as one lab blend command); `test_p2_meter_golden.py` compares it with `meter_winamp_scope.c` command for command (26,735 commands) on the wipe path; the blended trail stays checked against the engine model by `test_p2_scope_trail.py`.
- **M1b, Chladni open (parked):** it has no JS twin. A faithful twin means porting `chladni_core.h` (Q14 fixed-point mode synthesis, onset detection, the mailbox plane writes) to JS bit for bit: a large port whose value is the Omega preview of one meter. The C side is already checked by `test_p2_chladni*.py` and, as a pack, against itself by the rv32sim equality test. Do it when Omega needs the Chladni preview.
- **Packs (M3):** all five meters build as packs and are proven identical to the built-ins under rv32sim; per-meter fallback, the Info rows (`PACKS RUN`, `PACK ERR`, `PACK FILE`, `PACKS SEEN`) and the dispatch are host-tested (`test_p2_meter_pack_dispatch.py`, which found and fixed a real bug: before `meters_init` a pack slot read as running). See `METER_PACKS_PLAYER2.md`.
