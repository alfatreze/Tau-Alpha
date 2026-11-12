# External meters, phase P2: the firmware (plan)

Status 2026-11-12: **plan only, nothing built, waiting for the owner's approval.** Inputs: `EXTERNAL_METERS_SPEC.md` (the formats, decisions D-M22 and D-M23), the P0 result (B-1045: window swap works on a Pocket) and a read of the current code. Evidence labels: **code** (read in this tree), **P0** (measured on a Pocket), **to measure**.

## 1. What changes, in one paragraph
Today a meter is a **row of a 7-slot table**, a pack lives in a **fixed PSRAM slot** (8 slots of 64 KiB), and almost every per-meter fact is a hot-RAM array sized by the slot count. After P2 a meter is a **TOC entry** (up to 64, in PSRAM), only the **active** meter is loaded (one window, P0), identity is the **id hash**, and the only per-meter hot state is that of the active meter. Winamp Bars stays built in as the fallback (D-M22).

## 2. Where the slot assumption lives (code, 2026-11-12)
| Place | What it holds today | Becomes |
|---|---|---|
| `fw2/generated/meter_registry.h` | `METER_N` 1, `METER_SLOTS` 7, manifest ids, budgets | built-in Winamp Bars only; the rest comes from the TOC |
| `fw2/ui/meters.c` (565 lines) | `meter_table[]`, `meter_count`, `pre[7]`, `want_id[7]`, `custom[7]`, `user_n[7]`, `budget[7]`, `pack_strike[8]`, `pack_slot[8]` (288 B), `tune_meter[2]`, the dynamic registration `dyn_register` | the active meter's state (one struct), the TOC reader, the window loader |
| `fw2/stream/packfile.c`, `meters_packs_load_file` (called from `main_player.c:50`) | reads the core's `tau-packs.bin` (data slot 9) | reads the user bin (slot 10) then the core's, v1 or v2 |
| `fw2/cores/meter_pack_core.h` | `MTR_PACK_SLOTS` 8, slot addresses, `mpk_install`, `mpkb_*` | window loader on top of the same proven `mpk_install` |
| `fw2/cores/menupages_core.h`, `settings_view.c` | the METER list: static names `mg_choices[V_METER].opt[i]`, 7 rows, `MLIST_VIS` 7, previews by row | a scrolling list of up to 64 rows from the TOC (names, previews, state) |
| `fw2/ui/nowplaying.c`, `meters_next` | X walks rows | X walks visible TOC rows in TOC order |
| `fw2/ui/appearance.c:13-16` | boot restore loops `METER_SLOTS` with `SF_MPID`/`SF_MPRE`, then `meters_tune_apply`, then `meters_select(sv[SF_METER])` | restore by id hash, commit rule (section 4) |
| `fw2/cores/settings_core.h`, `tune_core.h` | `SF_METER`, 7 `SF_MPRE`, 7 `SF_MPID`, tuning record with a 5-bit meter index | `persist_layout` 2 (SPEC section 5) |
| `fw2/art/mpreset_host.c` | preset folder `tau-presets/<decimal manifest index>/` | folder = first 8 hex digits of the id |
| `tools/persist_layout.py`, `sim/test_persist_layout.py`, golden vectors | Omega's reference of the layout | new layout, new vectors (Omega needs them before it builds) |
| the WebAssembly twins | hash `fw2/ui/meter.h` | rebuilt once when `meter.h` changes (D-M15; Homebrew llvm + lld) |

**Hot RAM (code):** the per-row arrays above add up to about **740 B** (`pack_slot` 288, `sh` 320 is needed for the active meter anyway), so replacing them by one active-meter struct should *free* hot RAM. The Diagnostic Build had 96 B above its 1,024-byte floor before the watchdog work (the normal build 2,752 B); every step must keep the floor (rule 5 of `PROJECT_RULES.md`: drop features, never lower the floor).

## 3. Steps
Each step has a host test that fails first, then passes; each ends in a commit. Steps marked (Pocket) end with a `TAU DEV` core the owner tests. **No step needs a new FPGA fit**: slot 10 is JSON and the data-slot table, not RTL (the shipped bitstream `wdog-w3` stays, `CORE_VERSION 4D50331B`); the one thing to confirm is in P2.0.

**P2.0 Spikes and decisions (small).**
1. Confirm that adding data slot 10 (user bin, `deferload`, not core-specific) needs no bitstream change (slot 9, `tau-packs.bin`, was added the same way; check `tools/tau_data_slots.py`, `tau_compat.py`, `install_dev_core.py`).
2. Measure the hot-RAM budget with the active-meter struct on paper against the 740 B and the floor.
3. Fix the ids of our seven meters (128 random bits each, made once, recorded in `meters/<key>/meter.json` and the collection repository).
4. Decide where the TOC copy lives in PSRAM (the existing `.psram_state` region, free of hot RAM cost) and how it is read (word reads through the data alias, as `desc_rd` does for descriptors).

**P2.1 Bin v2 and the reference tools (host only).**
- `tools/meter_bin.py` (new): compose a v2 bin from packages, parse v1 and v2, the 64-meter and 4,456,448-byte limits, the TOC layout of SPEC section 2; the independent reference, like `tau_packs_info.py`.
- `fw2/cores/meterbin_core.h` (new, pure, host-tested against the Python reference with random bins, damaged bins, truncations): header and end CRC, TOC reading, `id_hash` uniqueness, ABI and `tau_min` checks, refusal codes (`MTOC ERR ...`), v1 mapped to a TOC.
- Golden vectors and the packages they came from (`docs/research/meter_bin/`), which Omega asked for (O021/1).
**Exit:** host green; the vectors can be handed to Omega.

**P2.2 Window loader and lazy loading.**
- `meters_activate(toc_index)`: read the pack from the bin in 1 KB pieces (the path P0 used), CRC and ABI check, write through the uncached alias, `fence.i`, zero the pstate, run under `wd_run`; refusal or a strike falls back to Winamp Bars and marks the entry.
- Remove the 8-slot install; one window (0x24900000, 64 KiB incl. pstate) and the 4 KB scratch.
- Load at selection (X, the Settings list, boot restore); never inside a tick; the audio stream unchanged (P0 measured no dropout; the swap runs between pictures).
- Packs relinked for the window (`build_packs.py` already has `build_window`; the shipped packs become window-linked, ids from P2.0).
**Exit (Pocket, DEV 921):** the shipped seven meters selected from a v2 bin, no dropout across 100 swaps, the `MWIN` rows (load cycles, last error) match P0's.

**P2.3 `persist_layout` 2 and the commit rule (can run in parallel with P2.1).**
- New layout (SPEC section 5): selected `id_hash` (31 bits) and its preset, tunings keyed by hash and `params_rev`; the seven `MPRE`/`MPID` fields freed.
- `tools/persist_layout.py` updated first (the reference), then `settings_core.h`/`tune_core.h`, `sim/test_persist_layout.py` and `sim/test_p2_settings*.py` with 500 random register files as before; the migration from layout 1 (the seven shipped ids, tunings that still match kept, the rest dropped), resumable like the layout-1 migration.
- **The commit rule (safe boot):** the persisted selection (the register-file word the Pocket saves on Quit) is written only after the new meter has run cleanly for a few seconds; a meter that hangs or is struck in that time never reaches the saved word, so the next boot starts on the last clean meter without a crash flag (which would need a value to survive a reset: unverified). A newly installed meter is never selected automatically.
**Exit:** host green; golden vectors and staged legacy captures for Omega (their item 7); (Pocket) the migration of a real DEV 185-style legacy file, as B-845.

**P2.4 The registry rewrite.**
- Replace the per-row arrays by the active-meter struct plus the two tuning-pool owners; `meter_table` keeps only Winamp Bars; `meters_present/def/list_*` read the TOC; budgets and strikes belong to the active meter (strikes reset on selection, as a swap is a new start).
- Preset files: folder by `id_hash` (8 hex digits), decimal-folder fallback for the seven shipped meters (decided in P2.0 with Omega's move on composing).
- Info/Report rows registered as diagnostic features (`fw2/diag_features.json`, never a bare `#if`): `MTOC` (state, count, error), `MWIN` (loads, load cycles, max, last id, errors), `MSEL` (committed or pending).
**Exit:** all existing meter host tests green on the new registry (`test_p2_meter_*`, `test_p2_pack_*`, `test_p2_mpreset*`, `test_p2_tune`), plus new ones for refusal paths (bad CRC, ABI above the firmware's, `tau_min`, origin not the window, oversize, duplicate hash, a hang, three strikes).

**P2.5 One order and the UI.**
- Now Playing X and the Settings METER list walk the visible TOC rows in TOC order (the `sel_index` leftover deleted); the list scrolls (up to 64), draws names and previews from the TOC and its preview blocks, greys an entry that needs a newer TAU or crashed last time; Configure uses the loaded meter's descriptor.
- `menupages_core.h` loses the static meter names (they come from the TOC); `settings_view.c` reads rows a window at a time.
**Exit (Pocket, DEV 922):** both screens show the same order for 7 and for a synthesized 64-meter bin; the list scrolls; a removed meter in the saved selection falls back cleanly.

**P2.6 Release plumbing (overlaps the plan's P3).**
- `tools/meter_package.py` (package the seven shipped meters, verify a package: pins, limits of SPEC section 1), the build composes the shipped `tau-packs.bin` as a **v2** bin; `tau_data_slots.py` adds slot 10; `tau-compat.json` gains `packs_abi_minor`, `persist_layout` 2 and `tau-meters.bin` as a user file; `install_dev_core.py`, `check_tau_package.py`, `tau_compat.py` and their tests change together (rule 4).
- Rebuild the WebAssembly twins once (`meter.h` changes) and the lab drop if its inputs change.
**Exit:** `tools/run_host_tests.py` green (and the stale `BASELINE.json` refreshed); a release rehearsal like `tau-alpha-rehearsal` of the v2 package.

**P2.7 Hardware ladder (the owner, on a Pocket).** In this order: (a) a v1 `tau-packs.bin` still runs (compatibility); (b) the v2 bin with the seven meters (P2.2, P2.5); (c) a synthesized 64-meter bin (list scroll, TOC read time at boot, the window size of the largest meter); (d) damaged packs (CRC, ABI, hang pack, overrun pack): fallback, strikes, no stuck state; (e) the persist migration (P2.3); (f) the commit rule with the hang pack (the next boot is on the last clean meter); (g) 200 swaps with music playing and the dropout counters read before and after (P0 had 22); (h) **a hostile test pack on the host simulator first** (can it write the card through the bridge?) so D-M23's open point is closed with a fact.

## 4. Order and what can run in parallel
P2.0 first. Then **P2.1 and P2.3 are independent** (different files, different tools) and can run as two sessions in separate worktrees; **P2.2 needs P2.1**; **P2.4 needs P2.2 and P2.3**; P2.5 needs P2.4; P2.6 can start when P2.1 has its vectors and finishes after P2.5; P2.7 follows each Pocket exit. Omega's read-side work needs only the P2.1 vectors; its writer needs P2.2, as agreed in message 024.

## 5. Risks (to measure, not assume)
| Risk | Why it matters | Mitigation |
|---|---|---|
| **Hot RAM** | the Diagnostic Build is near its floor | the new struct replaces about 740 B of arrays; measure at each step; cold code (`P2_COLD_TEXT`) for the TOC and loader, state in `.psram_state` |
| **TOC reads at boot and for the list** | 16 KB TOC plus previews in PSRAM, read through the data alias | read the header and TOC once into PSRAM; the list reads only the rows it shows; measure boot time |
| **Load latency at selection** | P0: 22 ms typical, 97 ms worst | acceptable at selection; show a short "loading" frame only above a threshold; never load inside a tick |
| **First tick after a swap** | instruction cache cold (4 KiB direct-mapped) | one forced repaint (P0 did it); measure the first drawn tick |
| **Largest pack and pstate** | Layered Wave 15.7 KB + 2.4 KB; the window is 64 KiB | refuse above the window by the TOC before reading; confirm with real packs |
| **Persist migration** | a real card must not lose its settings or boot-loop | resumable design as layout 1; staged legacy file test (B-845 method); Omega treats layout 2 as read only until its mirror ships |
| **Commit rule vs the Pocket's save timing** | the saved word is whatever the register file holds at Quit | measure with the hang pack (P2.7 f); the rule needs no state that survives a reset |
| **WebAssembly twins** | `meter.h` is hashed by every twin | one deliberate rebuild at P2.6 (or keep new declarations out of `meter.h` as in B-1046) |
| **Pocket Sync or pupdate overwriting `tau-meters.bin`** | it lives in `common` like the library | not shipped in any release zip, listed as a user file in `tau-compat.json`; checked in P2.6 |
| **A hostile meter** | native code, no memory protection | D-M23 states it; P2.7 (h) tests the card-write question on the host first |

## 6. What the owner decides before P2.0 starts
1. **Approve the plan and the order** (or change it). 2. **Parallel sessions:** P2.1 and P2.3 as two sessions in worktrees, or one after the other (parallel is faster; one at a time touches fewer files at once). 3. **A `TAU DEV` number sequence:** the next free number is 921 (P2.2), then 922 (P2.5), more as needed; old test cores removed on each install as usual. 4. **Rebuilding the WebAssembly twins** once at P2.6 is accepted. 5. **Our seven meters' ids** are generated by me in P2.0 and shown to you before they are written.
