# Meter framework gap audit (2026-10-03)

Read-only audit of `fw/player.c` (`viz_*_tick`, `wviz_*_tick`), `fw/chladni.inc`, `fw/layered_wave.inc`, `fw/vu_master.inc`,
`fw/meter.h`, `fw/meter_core.h` against `docs/features/meters/METER_MODULE_SPEC.md` sections 3, 12 and 13. Source reading only; no build, no test run.

## Correction to METER_COST_MEASUREMENT.md

The 8 older meters are **already separate functions** taking `mtr_in_t` (`viz_scroll/led/dots/water/vu/wave/phase/bars_tick`, Helios review item 5).
They are `static` without a cold attribute, so the compiler inlines them into `ui_draw_dynamic_cold` (4,684 B), which is why they showed as one function.
They are separable at source level; what still ties them together is shared file-scope state (below).

## What is shared today

`mtr_in_t` (input snapshot, rect, theme roles, `force`), `meter_core.h` (`mtr_ease`, `mtr_peak_step`, `mtr_band_target`, `mtr_delta`),
`helios_meter()` (draw entry, throttle, compose and present), the manifest generator, the capability registry. Users of the core:
Winamp Bars (all four), VU Master (`mtr_ease`, `mtr_peak_step`). Nobody else.

## Duplication found, per meter

| Meter | Own logic that the spec says belongs in the core |
|---|---|
| **Bars** (`viz_bars_tick`) | Column geometry `x = x0 + i*w/N`, gap and lit width; floor of 2 px; peak-hold marker from `wave_pk[]` (fall in the pre-dispatch step, `player.c` ~4934: `wave_pk--`); colour ramp `ui_mix(UI_TRACK, accent, i+1, N)`; own redraw cache (`wave_drawn`/`wave_pk_drawn`) instead of `mtr_delta`; own paused dimming |
| **Dots** | Same column geometry; same `wave_pk` fall; own skip-if-unchanged cache; same colour ramp |
| **Waterfall / Scroll** | Peak-to-height scaling `peak*h/32768` with clamp (twice, slightly different); COPY-scroll idiom (the planned `blit_shift` capability, today hand-written twice); loudness-to-colour ramp |
| **Spectrum (LED)** | Level-to-rows mapping, own `spec_drawn[]` cache and a `0xFF` repaint sentinel (the exact pattern that broke in B-234); own 3-stop colour ladder (`ui_mix` twice) instead of an `ok/warn/danger` helper; own paused-zeroing of `spec_lvl` |
| **VU needles** | Own attack/decay (`VU_ATT`/`VU_DEC`, linear) instead of `mtr_ease`; own face cache (`vu_face`, `vu_face_w`) |
| **Oscilloscope / Phase scope** | Waveform scaling `wave*ey/SCOPE_UNIT` with clamp, min 2 px line, bounds clipping (written twice, with different off-by-one margins); colour ramps; `paused` early-out |
| **Winamp Bars/Scope** | Already on the core, except the scope's own smoothing and trail |
| **VU Master** | On the core; its own segment table and zone colours (specific, correct to keep) |
| **Chladni** | Own palette literals (`0x3800`, `0xC2C1`, ...) for the fixed presets and `ui_mix` for the theme one; own onset detector `chl_detect` (the spec's `mtr_onset`, not yet promoted because no second user); own slew; no `mtr_*` calls |
| **Layered Wave** | Own colour mixer `lw_mix`, own role-to-colour switch (`th_role[TR_*]`), own easing and cost guard; only `mtr_psram_ready` from the framework |

Cross-cutting duplicates (counts from `ui_mix` and field reads, approximate):
1. **Colour:** 20 `ui_mix` calls in `player.c` plus 2 in Chladni, with a private `lw_mix` in Layered Wave. No `mtr_ramp` or `mtr_ladder`.
2. **Column geometry and clip:** the `x = x0 + i*w/N` / gap pattern appears in Bars, Dots and Winamp Bars with different gap handling.
3. **Peak/level shaping:** the `wave[]`/`wave_pk[]` envelope history is built in `ui_draw_dynamic_cold` before dispatch, outside `mtr_in_t`, so Bars/Dots/Scroll depend on a global that a pack could not see. `mtr_in_t` has `spec`, `wave`, `peak*` but no envelope history.
4. **Redraw caches:** four hand-rolled variants (`wave_drawn`, `spec_drawn`, `vu_face`, Chladni's own), only Winamp uses `mtr_delta`.
5. **Paused handling:** every meter reads the global `paused` itself; the host does not tell it.
6. **Fall and ballistics:** four different rules (Bars: pk-1 per tick; VU: linear ATT/DEC; Winamp/VU Master: `mtr_ease`; Layered Wave: own).
7. **Signals:** `silence`, `energy`, `onset`, `stereo balance` do not exist as shared functions; Chladni and Layered Wave each learn their own.
8. **Settings and cost:** the manifest drives parameters for the newer meters; the old 8 have no parameters and no `cost_class` guard. Cost fields exist but `hot_bytes`, `cold_bytes`, `state_bytes` do not.

## Gaps against the intended model (core owns data, API and cross-definitions; a meter owns only drawing)

| Intent | Status |
|---|---|
| Core reads peak, volume, spectrum, wave and publishes them | Done for spec, wave, peak L/R. Missing: envelope history, paused, silence/energy/onset, stereo balance, headroom as a parameter |
| New features proposed, analysed, tested, then built | Process documented (spec section 12) and registry exists. Enforced for engine primitives only, not for core helpers |
| Meters draw only | Newer meters yes. Bars/Dots/Waterfall/LED also own caching, ballistics, colour and geometry |
| Refresh in Helios | `helios_meter` owns throttle/compose/present. Per-meter redraw caches and `force` handling are still in the meters |
| Settings and cross-definitions from the core | Manifest and generic Configure page for 5 meters; old 8 have none |

## Proposed order (each step behaviour-neutral, proven by golden frames or an approved look change)

1. **Extend `mtr_in_t`** with `paused`, envelope history (`env`, `env_pk`) and silence/energy. Removes the pre-dispatch globals and the per-meter `paused` reads. Pure plumbing, ROM-neutral except size.
2. **Colour tranche in the core:** `mtr_ramp`, `mtr_ladder`, `mtr_role` (replaces `lw_mix`, `lw_role`, the 22 `ui_mix` sites). One tested implementation; Chladni's fixed palettes stay as data.
3. **Geometry tranche:** `mtr_cols(x0, w, n, gap, i, &x, &lit)`, `mtr_scale(v, h, max)`, `mtr_clip`. Replaces three copies of the column math and two scope scalers.
4. **Cache tranche:** move Bars/Dots/LED/Scroll to `mtr_delta`; retire the `0xFF` sentinels.
5. **Signals tranche:** promote `chl_detect` to `mtr_onset` only when a second meter asks (rule D-M06); add `mtr_silence`.
6. **Ballistics unification** (Bars `pk--`, VU linear) is a **look change**: needs owner approval per meter (D-M07). Propose, do not slip in.
7. **Manifest cost fields** (`hot_bytes`, `cold_bytes`, `state_bytes`, per-active `cost_class`) from real `nm` sizes, then the pack work.

Steps 1 to 4 are ROM-size-neutral or smaller and byte-identical in frames; they are the prerequisite for any removable-meter pack format, because a pack
can only be small if the ROM-resident core carries the shared parts.

## Status: steps 1 and 2 done (2026-10-03, uncommitted in the `meter-builder` worktree)

- **Step 1.** `mtr_in_t` gained `paused`, `env` and `env_pk` (the rolling envelope and its peak-hold markers, `UI_WAVE_N` entries). `mtr_build()` fills them. Bars, Dots, the scope pair, Waterfall,
  Scroll, Spectrum, VU, Winamp Bars/Scope and Layered Wave now read `in->paused` instead of the player global; Bars and Dots read `in->env` and `in->env_pk`. The envelope is still *produced*
  in `ui_meter_redraw` by the host (a meter never writes it). Silence/energy were not added: no meter consumes them yet (rule D-M06).
- **Step 2.** `fw/meter_core.h` gained `mtr_ramp` (the arithmetic `ui_mix` always used, now the single shared copy; `ui_mix` is a thin wrapper kept for non-meter UI), `mtr_mix256` (Layered Wave's mixer,
  moved, same rounding) and `mtr_ladder` (the three-stop ok/warn/danger ramp the spectrum LEDs drew by hand). Eleven `ui_mix` sites in the older meters, two in Chladni, Layered Wave's `lw_mix`
  and the LED ladder now use them. `sim/test_meter_core.py` compares all three with independent references over 20,000 random cases.
- **Proof.** `make test-host` passes (golden frames C against JS for Winamp, Layered Wave, VU Master, Chladni unchanged). `release` heap gap on the default (256 KB) build is 52,896 B,
  identical to the committed baseline; the 192 KB build gains 128 B (`mtr_build` is now cold) and the diagnostic profile gains 128 B. The older eight meters have no golden-frame test, so for them the
  evidence is the unchanged hot-code size and the straight substitution of identical arithmetic, not a frame diff.
- **Not done:** JS twins of the new colour functions (`tau_core.js`, spec rule 4), and the older eight meters are still not covered by golden frames.
- `tools/check_heap_gap.py` reports "dropped" against its stored baseline (53,232 B) even at the committed HEAD (52,896 B); that baseline is stale and was not touched here.

## Status: step 3 done (geometry tranche, 2026-10-03, uncommitted)

`fw/meter_core.h` gained `mtr_col_span`, `mtr_col_cw`, `mtr_scale_u`, `mtr_scale_s` and `mtr_in_box` (always-inline, same integer rounding as the inline code they replace).
Used by: Bars, Dots, the old waveform column loop (3 copies of the column and gap maths), Oscilloscope and Winamp Scope (the signed waveform scale and clamp), Waterfall and Scroll
(the peak-to-height scale and clamp) and the Phase scope's first clip test. `sim/test_meter_core.py` checks all five over 20,000 random cases; `make test-host` passes; heap gaps are byte-identical
to step 2 on the default, 192 KB release and diagnostic builds.

Deliberately left as they are, because they are not the same arithmetic: Winamp Bars' fixed pitch `colw + gap` layout, the Phase scope's second clip test (a different margin), and Spectrum's
level-to-rows mapping. Unifying those would change frames, so each needs an approved look change (D-M07).

## Status: golden frames for the older eight meters (2026-10-03, uncommitted)

`sim/test_legacy_meter_golden.py` cuts the real `viz_*_tick()` functions out of `fw/player.c`, runs them on the host over a fixed pseudo-random trace (80 frames, a paused stretch, a silent stretch, a forced repaint, two box
widths, both Bars layouts = 18 scenarios, 91,765 draw commands) and hashes every command in order; hashes live in `sim/golden/legacy_meters.json`. The golden was generated from the firmware as committed at `HEAD`
(`--write HEAD`, before steps 1 to 3), and the working tree matches it exactly, so steps 1 to 3 are now proven frame-identical for these meters, not just size-identical. `--selftest` mutates the source and confirms the
comparison fails; both run in `make test-host`. Regenerate only for an approved look change.
The harness feeds both the old inputs (globals) and the new ones (`in->paused`, `in->env`), so it measures the firmware on either side of a contract change.

## Status: step 4 done (redraw caches, 2026-10-03, uncommitted)

- Bars, Dots and the spectrum LEDs now use the shared cache: `mtr_delta` (two values) and the new `mtr_delta1` (one value) in `fw/meter_core.h`. The six hand-written reset loops (player.c x5, settingsui.inc x1) became
  `ui_meter_caches_invalidate()` / `mtr_invalidate()`, with `MTR_STALE` (0xFF) defined once in the core. Waterfall and Scroll have no cache (they move the picture with one COPY), so there was nothing to migrate.
- Proof: the 18 legacy golden scenarios are still byte-identical, `make test-host` passes, `sim/test_meter_core.py` covers `mtr_delta1` and `mtr_invalidate` (including "a stale cell redraws without force"). 192 KB release and the diagnostic profile
  gain 288 B of heap gap each; the default 256 KB release is 64 B lower than before step 4 (52,832 B against 52,896 B).
- **Sentinels are consolidated, not retired.** A stale cache is still signalled by writing 0xFF into the cells, because the hosts that invalidate (Helios `HELIOS_INV_SPEC`/`HELIOS_INV_WAVE`, pause/resume, menu close) do so independently of
  `ui_wave_force`: spec-only and wave-only invalidations exist, so passing `in->force` instead would redraw more than today. Removing the sentinel entirely means making every invalidation a per-meter `force` (the `helios_view_t` invalidate
  mask), which is a host change with its own risk and is not part of this step. The golden harness does not exercise the host reset paths (it resets the arrays itself), so those are covered by straight substitution only.
- Spectrum's `repaint = (spec_drawn[0] == 0xFF)` is still the way it learns a full repaint is due.

## Status: step 5 reviewed, nothing promoted (signals tranche, 2026-10-03)

Checked every candidate in the spec's signals group against the actual meters before moving anything (rule D-M06: a function enters the core when a second meter needs it):

| Candidate | Users today | Verdict |
|---|---|---|
| `mtr_onset` (`chl_detect`, spectral-flux trigger with refractory time) | Chladni only | Stays in `chladni_core.h` |
| `mtr_energy` (`chl_energy`, mean band level) | Chladni only (Layered Wave's `lw_learn` sums squared levels with a bias, a different quantity) | Stays |
| `mtr_silence` | none: meters test `paused` or an amplitude of 0 inline, and the host already zeroes the spectrum when paused | Not needed |
| Slow per-band average | Chladni `chl_update` (asymmetric slew limited by up/down rates on `lvl^2 >> 4`) and Layered Wave `lw_learn` (symmetric first-order, `(lvl^2 - e)/48`) | Two users, **but not the same arithmetic**. Sharing a primitive would change the frames of one of them, so it is a look change (D-M07), not a refactor |

No code changed. The trigger to revisit: a third meter wanting onset, or an owner-approved decision to unify the two slow averages (then the primitive would be a rate-limited first-order filter with separate up and down rates, with Layered Wave's EMA as the symmetric special case and a golden-frame regeneration for that meter).

## Status: signals added to the core (owner decision, 2026-10-03, uncommitted)

Owner direction: the core offers several ways to measure the audio and each meter uses the ones it needs, instead of waiting for a second user (this supersedes the step 5 hold; the table above stays as the record of why each meter needs what it does).

- **What the meters use them for.** Chladni: slow band weights choose the figure's vibration modes, mean energy sets how fast it morphs, the onset trigger changes scene and surges the line width. Layered Wave: a slow per-band power average places the layer boundaries at equal shares of energy in its ENERGY split.
- **Library functions in `fw/meter_core.h`** (the meter owns the small state array and passes its own rates, so each keeps its exact response): `mtr_energy`, `mtr_silent`, `mtr_slew_pow` (rate-limited, Chladni's weights), `mtr_ema_pow` (symmetric first-order, Layered Wave's energy), `mtr_onset_flux` (spectral-rise trigger). Both smoothing flavours exist side by side, so no look changes.
- **Host values in `mtr_in_t`:** `energy` (0..255) and `silent`, computed once per frame in `mtr_build()`. They are two trivial loops over 16 bands, so they are always computed rather than gated by a manifest `requires` list; a gated list only pays off for expensive signals (RMS, correlation, centroid). No meter reads these two fields yet.
- **Moved:** Chladni's `chl_energy`, the mode-weight loop of `chl_update` and the non-tonal branch of `chl_detect` now call the core; Layered Wave's `lw_learn` calls `mtr_ema_pow`. The tonal trigger stays in Chladni (an argmax-hold rule only that meter uses).
- **Proof:** `sim/test_meter_core.py` compares all five against Python transcriptions of the original arithmetic over 3,000 random cases (including digital silence and quiet input); the Chladni core, module and params tests, the Layered Wave golden frames (1,929,343 commands) and the legacy golden frames all still pass; heap gaps are identical to step 4 on every build.
- **Next signals** (each needs a cost): RMS, stereo correlation, crest factor, spectral centroid, clip count (`AUDIO_METERING_RESEARCH.md`); RMS and correlation are the ones worth a hardware block.

## Status: step 7 done (manifest budgets, 2026-10-03)

The five modular meters (Layered Wave, Chladni, VU Master, Winamp Bars, Winamp Scope) now declare `symbols` (name prefixes that belong to them) and a `budget` of byte ceilings
(`cold`, `hot_rom`, `hot_ram`, `psram_state`) in `meters/*/meter.json`; `tools/gen_meters.py` validates both. `tools/meter_budget.py --elf fw/fw.elf` measures the real footprint from the firmware and fails if any meter is over
its ceiling (sim/test_meter_budget.py proves it can fail, including a renamed-symbol guard). Ceilings were set about 12% above the measured 192 KB release footprint:

| Meter | cold B | hot ROM B | hot RAM B | PSRAM state B |
|---|---|---|---|---|
| Layered Wave | 10,608 / 11,904 | 12 / 64 | 810 / 960 | 2,424 / 2,560 |
| Chladni | 6,532 / 7,360 | 583 / 704 | 2,729 / 3,072 | 0 |
| VU Master | 2,440 / 2,752 | 8 / 64 | 18 / 64 | 0 |
| Winamp Bars | 1,156 / 1,344 | 0 / 64 | 144 / 192 | 0 |
| Winamp Scope | 1,296 / 1,472 | 0 / 64 | 546 / 640 | 0 |

(measured / ceiling). Attribution is by symbol-name prefix, so unnamed statics and compiler tables are not counted; the eight older meters are inlined into `ui_draw_dynamic_cold` and are not listed. Run it after a firmware build;
it is not part of `make test-host` because that does not build the firmware ELF. These per-meter numbers are what an Omega meter-library cost ceiling would sum.
