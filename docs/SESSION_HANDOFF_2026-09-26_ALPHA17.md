# Session handoff, 2026-09-26: 0.5.0-alpha.17 (read this first)

Supersedes `docs/SESSION_HANDOFF_2026-09-25_PHASE_F_CONTINUED.md` for the state below. Full detail per step is in
`docs/AUDIT_TRAIL.md` (B-248 .. B-281; note B-269..B-274 are the other session's Chladni/meter-module work).

## 1. What is on the Pocket card right now
Cores: `TAU` and `TAU_DIAGNOSTIC` (both v0.4.0 release, untouched) and **`TAU_0_5_0_A_17`** (test build).
Alpha.17 = bitstream `beam-b267` seed 1 (rbf_r `c5e3264a...`; frame counter + hardware spectrum bank + beam position + blit engine +
B11 rounded-rect, timing closed on all four corners, RAM 299/308) + the firmware built before the cassette removal. **The
cassette-removal firmware and the cold-Chladni firmware are NOT on the card except as noted: alpha.17 already has Chladni cold and
fullscreen; the cassette removal (B-281) is newer than the install, so the next package is alpha.18.**

## 2. Not yet verified on hardware (the first run of alpha.17 decides these)
| Item | What to look at | If it fails |
|---|---|---|
| Hardware spectrum bank (B-263) | Info > SPECTRUM = `HW W<n>` with n rising; spectrum meters behave as before | falls back to the software cascade automatically if the bitstream lacks it; if present but wrong, compare band levels with the software path |
| Beam position + Helios beam gating (B-267) | Info > BEAM `OK <n>% WAITED`; meters tear less | gate `helios_rows_safe` is host-tested; RTL is sim-tested only |
| Chladni meter (B-276, B-279) | Settings > Meter > Chladni; Info > CHLADNI (`OK [SWAP] n DRAWN m SKIP` or `UNAVAILABLE`) | mailbox-plane path is unproven on hardware (probe self-disables it and toasts); tile draw is host-verified pixel for pixel |
| Chladni cold-code performance | Diagnostic Build Check, cold-frame test with Chladni selected: **0 late underruns** | move the kernel back to hot (`CHL_COLD` -> empty in `fw/chladni.inc`), costs ~5 KB RAM |
| Fullscreen (Select+Y, Chladni only) | layout vs the Figma design: label position, bar spacing, colours (Figma MCP was refused, the layout came from the owner's images) | pure firmware: `fw/fullscreen.inc` |
| Select+X presets | Chladni Lattice/Shimmer, Winamp 5 presets, Bars up/mirrored | |
| Bars mirrored layout (2 `OP_BAR` per column) | looks like the old Mirrored Bars | flat bed per half; a vertical flip flag on `OP_BLIT` (B19) would be cleaner |
| Meter list order | Winamp Oscilloscope, Winamp Bars, Chladni, Bars, Waterfall, Phase Scope, Oscilloscope, VU, Waveform, Peak Dots, Spectrum | `viz_order[]` in `fw/player.c` |
| CPU load | Info > CPU LOAD, fullscreen label | idle share of the last second |

Already confirmed on hardware this session (owner): VBLANK frame counter reads about 60/S (B-266), meters keep moving after menus (the
meter-yield latch fix, B-260), scrolling Info page, layout fixes, Blit Test 24/24 PASS, Check PASS except the old `Track changes` failure.

## 3. Commit state
All of my work is committed locally (the last four commits: Chladni core, cassette archive, docs, and the Chladni/fullscreen/presets/meter-cleanup
feature commit `57e7c1c`); **those four are not pushed** (`origin/main` is at `9c3bb65`). Still untracked and NOT mine (the other session's
work): `docs/CHLADNI_METER_SPEC.md`, `docs/METER_MODULE_SPEC.md`, `docs/DECISIONS.md`, `docs/THEME_SPEC.md`, `tools/lab/`. `fw/chladni_core.h`
and its tests are theirs too but are committed because the firmware needs them to build. Also untracked by design: `work/`, `release/`,
`docs/vendor/`, `.claude/`, `UniClaudeProxy/`.

## 4. Decisions the owner made this session (do not re-litigate)
- Cold file missing = nothing works, accepted (no fallback UI in hot code). Stale build targets and dead code are removed; backups are
  git tags (`backup/pre-cleanup-2026-09-25`, `backup/pre-meter-removal-2026-09-26`, `archive/cassette-meter`).
- RAM shrink to 192 KB: **deferred** (`docs/RAM_SHRINK_192K_PLAN.md`; the release now needs about 24 KB more headroom to link at 192 KB,
  the 64 freed M10K blocks are headroom, not a requirement of any planned feature).
- Firmware modularization: planned, parked until the key features are done; the owner will define the architecture
  (`docs/FIRMWARE_MODULARIZATION_PLAN.md`, decisions in its section 5).
- Winamp meter configurator: parked, may be removed in favour of Tau Omega (B-266).
- Meters removed: Magic Eye, L/R Levels, Cassette (archived), Mirrored Bars merged into Bars as a layout. Enum slots are kept as
  `VIZ_RETIRED_*` (the setting persists an index).
- Card installs go through `tools/install_dev_core.py` only; VM fits through `tools/vm_fit.py`; test packages through
  `tools/package_dev_build.py --semver 0.5.0-alpha.N`. Change the script, its test and the doc together when the procedure changes.

## 5. Open items and ideas, in rough priority
1. Install/run alpha.17 (or an alpha.18 with the cassette removed) and read the table in section 2; screenshots of Info (scroll to the end),
   Chladni normal and fullscreen, and a cold-frame Check are the useful evidence.
2. Helios: convert more regions to beam gating (progress, clock, transport, toasts); H2 double buffering is held.
3. Meter work the owner asked about: Peak Dots as a Winamp Bars preset (needs a bars-off flag and a 2 px cap, B-278), Waveform mono/stereo
   presets (needs per-channel history, `peak_l/peak_r` exist), Chladni presets beyond Lattice/Shimmer (Sand Table, Gallery Plate,
   Triptych need other engines), a vertical-flip flag on `OP_BLIT` (B19).
4. Once the hardware spectrum bank is proven: delete the software cascade (~164 B RAM plus ~1 KB hot code in `meters_feed`).
5. The stopped-state playing bar from Figma node 190-779 is implemented only inside fullscreen; the normal screen's stopped variant is
   not designed yet.
6. Old items: `Track changes` Check failure (pre-existing), heap-peak instrumentation for Check, boot-restore mismatch (issue 021),
   stale renderer fixtures, docs mentioning removed targets.

## 6. Working tree and traps
- **Two sessions write in this repo.** Check `git status` for files you did not write before committing; never overwrite the other
  session's `chladni_core.h`, specs or lab. Audit ids: the other session used B-268..B-274; mine are B-248..B-267 and B-275..B-281 (B-268 was
  renamed B-275 to avoid a clash).
- Figma MCP returns "no edit access" for the owner's file; work from exported images (the owner supplies them).
- Do not write the sticky blit fields (`R_BLT_IDX/R_BLT_DATA`) from firmware: implicated in two hangs, never proven on hardware. The CPU
  window cannot reach the first MiB of SDRAM (KB-061).
- Timing lessons still apply (`docs/PHASE_F_SPEC.md` sections 10, 11): no long combinational chains; retime with registers; two seeds.
- Hot-to-cold calls need a `COLD_READY()` gate or state that implies it (`tools/check_cold_calls.py` lists them); `COLD_READY()` is not
  visible before `fw/cold.inc` is included, use `cold_code_ok` in earlier code.
- `chl_*` functions are folded into cold entry points with `flatten`; do not call them from hot code.

## 7. Tools and where things are
| Need | Command / file |
|---|---|
| Build | `bash fw/build.sh release` (default), `player-library-diagnostic`, `player-library-diagnostic-profile` (`SDRAM_BUSY=1` for the beam/counter bitstreams) |
| Tests | `make test-host`, `make test-rtl`, `make test` |
| Package a test core | `python3 tools/package_dev_build.py --semver 0.5.0-alpha.N --rbf work/diagnostics/beam-b267/ap_core_s1.rbf --rbf-sha256 <hash>` |
| Install | `python3 tools/install_dev_core.py <pkg> --carry-from <old core> --remove <old core>` (dry run first, `--yes` after approval) |
| Quartus fit | `python3 tools/vm_fit.py launch NAME --append tools/blit_g3_full_qsf_append.txt --seed 1 --seed 2`, then `status` and `collect` |
| Reading results off the card | `Memories/Screenshots`, decode QR with `tools/decode_tau_suite.py --qr`; persist files under `Settings/<core>/Interact/_core/` |
| Cassette archive | `archive/cassette_meter/` |
| Skill knowledge | `~/.claude/skills/analogue-pocket-dev` KB-060 (vsync counter), KB-061 (window below 1 MiB / mailbox), OQ-13 |
