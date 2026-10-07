# Diagnostic-Build features on demand (ram-diet, phase 1)

Everything that exists only in the Diagnostic Build used to hang off ONE switch, `TAU_DIAGNOSTIC` (about 90 uses), so nothing could be removed on its own and a feature incubating in the Diagnostic Build looked the same as a test. Phase 1 gives that code a register and a build switch.

## Two kinds, kept apart
| Kind | Meaning | Macro | Lifecycle |
|---|---|---|---|
| **dx** diagnostic | measurement, test or reporting code with no user-facing function (stress, Check, sweeps, Blit Test, counters, report pages) | `TAU_DX_<ID>` | stays in the Diagnostic Build; can be dropped for a smaller one |
| **fx** feature | a real capability still incubating behind the flag (experimental meter settings, rate / speed / resampler toggles) | `TAU_FX_<ID>` | **graduates** to the release (`FEATURES_ON=<id>`) or is dropped |

`fw/diag_features.json` is the one register (id, kind, title, depends, converted). `tools/gen_diag_features.py` writes `fw/diag_features.h`; every converted macro defaults to `TAU_DIAGNOSTIC`, so the default Diagnostic Build and the release are byte-identical (proved by hashing five configs before and after, see below). `make test-host` fails if the header is stale.

## Using it
```bash
python3 tools/gen_diag_features.py --list              # the register
DIAG_DROP=meter_experimental bash fw/build.sh player-library-diagnostic     # a Diagnostic Build without it
FEATURES_ON=meter_experimental bash fw/build.sh release                      # graduate an fx into the release
```
Unknown ids, ids not converted yet, dropping something a kept feature depends on, and turning a `dx` on outside the Diagnostic Build are build errors, not silent no-ops.

## Converted so far
`meter_experimental` (fx): the experimental meter parameters and presets (Layered Wave anti-aliasing, layer blend, EQ bells, CURVE, per-layer gains, [EX] presets). Its sources are the `experimental` flags in `meters/*/meter.json`, emitted by `tools/gen_meters.py`, and `fw/layered_wave.inc`.
Measured (192 KB link): dropping it from the Diagnostic Build frees about **0.5 KB hot RAM** and 10.5 KB of cold PSRAM code; putting it into the release costs 0.5 KB hot RAM (release 8,960 to 8,464 B free) and the cold code.
Identity proof: `tau.rom` and `tau-cold.bin` of release, diagnostic and profile (256 KB link) and release and diagnostic (192 KB / CLK66 / SDRAM_BUSY link) hash the same before and after the conversion.

### stress (dx) and check (dx), converted 2026-10-07
`stress` = the SDRAM stress pump, its 1 Hz HUD, levels R1-R3, the timed soak, the Stress group and Stress Status page, and the hooks in the playback loop (`stress_tick`, `stress_note_underrun` x2, the Select+Start HUD refresh, the HUD restore after a chrome repaint, `stress_frames_at_flush`). Without it Select+Start is the normal Start action, and Check reports its stress tests (R1-R3, SOAK) as not applicable instead of failing.
`check` = the Check runner and its page (`chk_*` region of `fw/suite.inc`, the CHECK row and page-table entry, `chk_tick`, `chk_loads`, the summary persistence in `settings.inc`). Decode Sweep, Meter Sweep, Meter Trace, Blit Test, the pixel grid test and the report plumbing stay (separate features, not converted yet).
Identity: default builds hash the same before and after both conversions (release, diagnostic and profile at 256 KB; release and diagnostic at the shipped 192 KB link).

Measured on the shipped 192 KB link (Diagnostic Build, free RAM before the tempo diet: 1,600 B):
| Build | Free hot RAM | Cold image |
|---|---|---|
| default | 1,600 B | 193.7 KB |
| `DIAG_DROP=stress` | 4,544 B (+2,944) | about the same |
| `DIAG_DROP=check` | 2,320 B (+720) | 177.7 KB (-16 KB) |
| `DIAG_DROP=stress,check` | 5,152 B (+3,552) | 176.7 KB |
| profile, `DIAG_DROP=stress` | 1,872 B (it does not link by default: -1,056 B) | |
So `stress` is where the hot RAM is; `check` is mostly cold code. Both can be combined with `TEMPO_SLICE=1 TEMPO_RING=512` (+2.8 KB more).

## Classified, not converted (the plan)
| Feature | Kind | Sites | Hot cost (est.) | Note |
|---|---|---|---|---|
| ~~`stress`~~ done | dx | `stress.inc`, `stress_defs.inc`, `dg_soak_*`, HUD, 4 hooks in `player.c`, the Stress and Stress Status pages | about 2-3.5 KB incl. hooks | Check's stress profile uses the pump, so `check` depends on it |
| ~~`check`~~ done | dx | `suite.inc` chk_* | cold mostly | reports its stress tests as N/A when `stress` is dropped |
| `sweeps`, `blit_test` | dx | `suite.inc` sw_/mw_/mt_/bt_ | cold | pages and their menu rows must disappear together (B-605 row-count bug class) |
| `tests_page` | dx | `settings_diag.inc`, `cold.inc` (the 12 KB `cold_big` blob) | cold | |
| `counters` | dx | `ur_note`, `gap_*`, `ld_*`, Info rows | small, spread over hot hooks | |
| `report_pages` | dx | `report.inc`, exports | cold | |
| `rate_toggles` | fx | ACCEPT ALL RATES, ALL SPEEDS 2.50x, Cymo and 16-bit toggles | small | candidates to graduate or drop |

## How to convert a feature (the recipe, in this order)
1. Set `"converted": true` in the register and run the generator (the macro now exists and equals `TAU_DIAGNOSTIC`).
2. Replace `#if TAU_DIAGNOSTIC` by `#if TAU_DX_<ID>` / `TAU_FX_<ID>` at that feature's sites only; give any host harness that includes the file `#include "diag_features.h"`.
3. For menu rows and pages: the row, its handler and its page enum must be inside the same `#if`; run the UI snapshot tests.
4. Prove the default is unchanged: build release, diagnostic and profile (and the 192 KB variants) before and after and compare `tau.rom` / `tau-cold.bin` hashes.
5. Measure with `DIAG_DROP=<id>`, record the hot and cold cost here, run `make test-host`.

## Open question (not built)
"On demand" here means selected at build time. A runtime-loaded diagnostics image (the Diagnostic code in a separate PSRAM image fetched when Diagnostics opens, one core for everyone) would remove the second core, but the hot hooks in the playback loop would still be in RAM and the image would be one more thing bound to the ROM layout. Not proposed until the build-time version has proved its worth.
