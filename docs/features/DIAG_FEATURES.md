# Diagnostic-Build features, chosen at build time (ram-diet)

Everything that exists only in the Diagnostic Build used to hang off ONE switch, `TAU_DIAGNOSTIC` (about 90 uses), so nothing could be removed on its own and a feature incubating in the Diagnostic Build looked the same as a test. Now every piece has a name in one register, `fw/diag_features.json`, and a macro that defaults to `TAU_DIAGNOSTIC` (so the default Diagnostic Build is byte-identical to before; proved by hashing five configs after every conversion).

## Two kinds, kept apart
| Kind | Meaning | Macro | Lifecycle |
|---|---|---|---|
| **dx** diagnostic | measurement, test or reporting code with no user-facing function | `TAU_DX_<ID>` | stays in the Diagnostic Build, can be dropped for a smaller one |
| **fx** feature | a real capability still incubating behind the flag | `TAU_FX_<ID>` | **graduates** to the release (`FEATURES_ON=<id>`) or is dropped |

Classes (what dropping it does to the remaining measurements): **observer** (reads state only, never changes what is measured), **reporter** (turns readings into a report), **page** (does nothing unless its page is open), **option** (a setting that is off at every start), **load** (adds contention on purpose), **behavior** (changes the audio path by default).

## Choosing at build time
```bash
python3 tools/gen_diag_features.py --list                                  # the register
DIAG_DROP=blit_test,meter_trace bash fw/build.sh player-library-diagnostic  # drop by name (dependents come with it)
DIAG_PRESET=slim bash fw/build.sh player-library-diagnostic                 # a named set (below)
FEATURES_ON=meter_experimental bash fw/build.sh release                     # graduate an fx into the release
python3 tools/diag_cost.py                                                  # re-measure every row below (about 12 minutes)
```
Unknown ids, dropping something a kept feature depends on (`check` needs `load_stats`), turning a `dx` on outside the Diagnostic Build, and unknown presets are build errors, not silent no-ops. `make test-host` checks the register, the generated header, that the firmware only tests registered macros and that every registered macro is used (`sim/test_diag_features.py`).

## Measured cost of each feature
Diagnostic Build on the shipped 192 KB link, one feature dropped at a time (`tools/diag_cost.py`, stored in `tools/diag_cost.json`). Free hot RAM of the default build: 1,600 B.

| Feature | Kind / class | Hot RAM freed | Cold code freed | What it influences | Easily dropped? |
|---|---|---|---|---|---|
| `stress` | dx / load | +2944 B | 1.6 KB | adds SDRAM and CPU contention beside playback; it IS the test load for R1-R3 and the soak | unless you are testing contention / long-run stability |
| `load_stats` | dx / observer | +1008 B | 10.0 KB | a few cycle-counter reads per audio frame (negligible, but present): feeds the load fields of the Check report | yes, with Check |
| `check` | dx / reporter | +720 B | 10.0 KB | plays tracks and reads counters; changes nothing it measures. Its report is how most results leave the device | unless you need the Check report (profile builds report through it) |
| `blit_test` | dx / page | +720 B | 3.5 KB | drives the draw engine only while its page is open | yes |
| `meter_experimental` | fx / option | +496 B | 4.7 KB | adds experimental meter settings and presets; defaults unchanged | yes |
| `sweeps` | dx / page | +416 B | 2.8 KB | runs playback / meter changes only while a sweep is running | yes unless you need the sweep reports |
| `cymo_toggle` | fx / behavior | +272 B | 0.3 KB | ON BY DEFAULT: the 44.1 kHz hardware resampler is in the audio path, so dropping it changes the sound and the CPU load of 44.1 kHz tracks | NO if you test 44.1 kHz audio; it is the path under test |
| `pixel_grid_test` | dx / page | +272 B | 0.7 KB | draws a test pattern only while its page is open | yes |
| `meter_trace` | dx / observer | +240 B | 1.5 KB | records what each meter drew, one cheap hook per displayed frame | yes |
| `tests_page` | dx / page | +176 B | 1.8 KB | runs memory / cold-code tests only when started | yes |
| `underrun_log` | dx / observer | +160 B | 0.1 KB | counts FIFO stalls, no effect on playback | yes |
| `config_export` | dx / observer | +160 B | 0.3 KB | prints the Configure page values as a report | yes |
| `gap_timing` | dx / observer | +144 B | 0.3 KB | times the gap at a natural track end, no effect on playback | yes |
| `out16_toggle` | fx / option | +80 B | 0.1 KB | sends the full 16-bit I2S word only while switched on | yes unless you A/B the 16-bit slot |
| `accept_all_rates` | fx / option | +48 B | 0.0 KB | lets a 96 kHz file load for a decode reading; off at every start | yes unless you test above 48 kHz |
| `gain_toggle` | fx / option | +48 B | 0.3 KB | A/B of the hardware gain stage, only while switched | yes unless you A/B the gain stage |
| `all_speeds` | fx / option | +32 B | 0.1 KB | adds the 2.50x speed entry; off at every start | yes |

Reading it: `stress` is the only large hot cost (2.9 KB). `check` is mostly cold code (10 KB). Everything marked "yes" changes nothing the remaining tests measure. The one that does is `cymo_toggle`: the hardware resampler is ON by default in the Diagnostic Build, so a build without it plays 44.1 kHz tracks through the old path.

## Presets (`fw/diag_features.json` "presets")
| Build | Hot RAM free | Cold image |
|---|---|---|
| default Diagnostic Build | 1,600 B (+0) | 183.5 KB (+0.0) |
| `DIAG_PRESET=perf` | 4,416 B (+2,816) | 168.0 KB (-15.6) |
| `DIAG_PRESET=slim` | 7,568 B (+5,968) | 165.7 KB (-17.8) |
| ALL diagnostics (dx) | 8,048 B (+6,448) | 149.3 KB (-34.3) |
| ALL features (fx) | 2,512 B (+912) | 178.0 KB (-5.6) |
| `DIAG_PRESET=release-like` | 8,944 B (+7,344) | 143.7 KB (-39.9) |

- `perf`: keeps the load (`stress`), the report (`check`, `load_stats`), the counters and the audio-path toggles; drops pages and options.
- `slim`: keeps only Check, the load numbers, the underrun count and the Cymo path; the smallest build that still reports.
- `release-like`: everything off. It frees as much as the release has (8,944 B against 8,960 B), so the Diagnostic Build costs 16 B beyond its features.
With the tempo diet (`TEMPO_SLICE=1 TEMPO_RING=512`, +2.8 KB) every row gains that much on top.

## Adding a feature
1. Add it to `fw/diag_features.json` (id, kind, class, influences, safe_to_drop, depends) and run `python3 tools/gen_diag_features.py`.
2. Put its code, its menu row, its page-table row, its hooks and its Info-row text under `#if TAU_DX_<ID>` / `TAU_FX_<ID>`, with the release behaviour in the `#else` (a "-" in an Info row, the placeholder row stays so the row positions never move).
3. Prove the default is unchanged: hash `tau.rom` / `tau-cold.bin` of release, diagnostic and profile (256 KB link) and release and diagnostic (192 KB / CLK66 / SDRAM_BUSY) before and after.
4. `python3 tools/diag_cost.py`, `make test-host`.

## What is not covered
- "On demand" is build time. A diagnostics image loaded at runtime from a data slot would remove the second core, but the hot hooks in the playback loop would stay in RAM and the image would be one more file bound to the ROM layout. Not proposed.
- Info-page rows of the features stay as placeholders in every build (their text reads "-" or "OFF" when the feature is out), and the page-id enum keeps its numbers, so reports and saved settings never change layout.
- The decode sweep only exists in profile builds (`MP3_PROFILE || FLAC_PROFILE`) on top of `sweeps`.
