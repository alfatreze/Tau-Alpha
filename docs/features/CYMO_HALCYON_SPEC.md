# Halcyon: the Cymo EQ (macro-control layer over six biquad stages) (design, 2026-10-07)

> **Name (D-H01, owner 2026-10-07): the EQ is called Halcyon.** This file was written as "Sound Shaping" and keeps its old file name; code identifiers keep theirs (`tools/lab/halcyon_model.py`, `sim/test_halcyon_model.py`) until a rename task renames them to `halcyon_*`. The engine module stays `eq_biquad.v` ("the Halcyon engine"). The legacy 8-preset EQUALIZER choice stays as is until Halcyon replaces it.

> **Owner decisions 2026-10-07 (section 11) supersede sections 3-5 wherever they differ: SIX stages (not five), presets rebuilt from the controls as configurable data, recall by gain-dip first, de-esser firmware-only. Host model: `tools/lab/halcyon_model.py`, tests: `sim/test_halcyon_model.py` (in `make test-host`).**

**Status: DESIGN, with a host prototype of the stage mapping. Nothing built in RTL or firmware.** Owner request: replace the 10-band graphic-EQ plan (C5 option B in `CYMO_AUDIO_ENGINE.md` section 8) with an MSEB-inspired mode, judged together with the gain stage (`CYMO_GAIN_STAGE_DESIGN.md`), the soft clipper and quantiser (`CYMO_OUTPUT_STAGE_SPEC.md`), the 16-bit I2S A/B, and the DSP/ALM budget. Naming: HiBy's MSEB is a proprietary PEQ plus dynamic processing with no public algorithm, so this is **not** an MSEB clone and should not carry the name; the working name here is "Halcyon".

## 1. Verdict

Good idea, and cheaper than the graphic EQ it replaces, because **the six perceptual controls map onto the five biquad stages the EQ already has**. No 10-band engine, no new datapath, no extra DSP. What it needs is the already-planned option D (a writable coefficient store instead of the fixed preset ROM), a small firmware layer that turns six slider positions into five stage gains and a preamp, and the soft clipper that the loudness-matched preamp design requires anyway.

## 2. Facts from the repo (checked, not assumed)

- The EQ is 5 biquads per channel, sequential, one time-multiplexed engine: **116 clocks busy per sample (9.2% of 1,250 at 60 MHz, 8.4% of 1,388 at 66.7 MHz)**, measured by `make test-rtl-eq-cycles`. Coefficients are 18 bits, state 36 bits (18.1 dB spare), accumulator 58 bits; preset 0 is a literal bypass; all eight presets match the Python model exactly.
- The five bands today: low shelf 80 Hz Q0.7, bells at 250 Hz, 1 kHz and 4 kHz (Q1), high shelf 12 kHz Q0.7 (`tools/gen_eq_coeffs.py`). The coefficient ROM (`crom`) and preamp ROM (`prom`) are declared `romstyle = "logic"` and the 40 state words `ramstyle = "logic"`, from the days when M10K was at 97%; the EQ is 1,659 ALMs, the largest single user. The shipped fit now has 52 M10K and about 5,270 ALMs free (B-606).
- **The preamp is pink-weighted loudness matching, not peak matching** (`loudness_preamp_db`): by design a preset can overshoot full scale after its preamp (worst preset: +3.55 dB, TREBLE at 16 kHz) and the saturation stage absorbs it. Correction to my own C3 review (finding H): with the EQ on, the soft clipper is not a robustness extra, it is what makes loudness-matched voicings safe; its reach is narrow only with the EQ off.
- `interact.json` has 14 variables, all persistent, against the Pocket's hard 16-entry UI cap (KB-079, B-455/B-456): **two free entries**. A declared slider `max` clamps what is persisted (the ReplayGain lesson, B-600/B-605).
- The spectrum bank (hardware, 16 half-octave bands, a 1,024-sample window about 23 ms) already measures band levels in RTL.

## 3. The mapping (host prototype)

Cascade magnitudes multiply, so **stage responses add exactly in dB** (checked with the real coefficient generator: worst difference 0.0000 dB over the 1/24-octave grid). That is what makes macros composable without a joint solve: each macro contributes dB to one or two fixed stages; the stage gain is the clamped sum.

| Stage (fixed centre, Q) | Kind | Controls feeding it |
|---|---|---|
| S1 100 Hz, Q0.7 | low shelf | Bass weight (+), Warmth (+) |
| S2 200 Hz, Q1.0 | bell | Punch (+), a little Warmth |
| S3 2 kHz, Q0.8 | broad bell | Vocal presence (+/-) |
| S4 6.5 kHz, Q1.5 | bell, dip only | Sibilance (0 to -6 dB) |
| S5 10 kHz, Q0.7 | high shelf | Air (+), Clarity (the negative side of Warmth/Clarity), minus a little for Warmth |

Controls: Warmth to Clarity (one slider, -5..+5), Bass, Punch, Vocal, Sibilance, Air; each a position with a tuning table to dB, per-stage clamp +-9 dB. Prototype worst cases with the real design equations and a pink-weighted auto-preamp: all macros at +5 gives stage gains +9 / +7.5 / +5 / -5 / +2.5 dB, peak +8.96 dB, preamp -5.37 dB, **overshoot after the preamp +3.59 dB**, the same order as today's worst preset (+3.55 dB), so the existing 18-bit input range (+-4 full scale) and the planned soft clipper cover it. The numbers are illustrative of the structure, not the final voicing (that is tuned by ear, with a lab page like the meter labs, then frozen into tables).

## 4. Hardware changes

1. **Writable coefficient store (option D).** Replace `crom`/`prom` with a small RAM (MLAB or one M10K) written through a sticky index+data port (the `R_LPC_COEF_*` convention), holding the 25 coefficients and the preamp of a live "shaping" bank next to the eight fixed presets. Updates go to a **shadow bank** and commit atomically at a sample boundary (no torn coefficient set; a mutant that commits mid-sample must be caught). Preset 0 stays the bit-exact bypass, and "all macros zero" simply selects it.
2. **ALMs go down, not up.** Moving the coefficient tables and the 40 state words out of logic into RAM is what the 1,659-ALM EQ needs anyway; a synthesis-only run measures it (an estimate of 600-1,000 ALMs back, to be measured).
3. **DSP and clocks:** no extra DSP and no extra clocks (still 5 stages x 2 channels, 116 clocks of 1,388). A 7- or 8-stage cascade stays under 200 clocks if ever wanted.
4. **Gain stage (separate design):** the auto-preamp is folded into the gain stage's target (volume x ReplayGain x preamp), so a macro change that moves the preamp ramps over the same 5 ms and never steps the level; the EQ's own preamp multiply is set to unity in shaping mode. This is the C5 merge `CYMO_GAIN_STAGE_DESIGN.md` section 8 anticipated.
5. **Soft clipper** right after the EQ (`CYMO_OUTPUT_STAGE_SPEC.md`), then the single dither in the final quantiser. The "existing limiter work" mentioned in the research notes is the soft clipper **design**, not built; a look-ahead limiter was explicitly not chosen.

## 5. Firmware layer

- Tables (per stage, gain steps of 0.5 dB, -9..+9 dB: 37 steps x 5 coefficients x 18 bit, about 17 kb) generated offline by `tools/gen_eq_coeffs.py` into a data section (`tau-assets.bin`, a new `EQCO` section) so Tau Omega can ship retuned voicings and headphone/speaker corrections without a firmware rebuild; a compiled-in default for the case the file is absent.
- On a control change: stage gains = clamped sums; composite response from a per-stage response table (dB at 32 log-spaced points, exact because dB add), pink-weighted preamp in integers; write 25 coefficients to the shadow bank and commit. About 30 writes, only on change, ramped in <= 0.5 dB steps every 20 ms so a big move is a short smooth sweep. No trigonometry on the CPU.
- **Click-free:** small coefficient steps on a preserved state are inaudible for a biquad cascade; the loopback tool's step/click detector (`cymo_loopback.py track`) is the gate. For a preset recall (a large jump) the fallback is a short gain dip through the gain stage ramp (mute, swap, unmute: about 10 ms) rather than doubling the MAC work with a dual-bank crossfade (17% of the clock budget, only worth it if the dip is audible).
- **Persistence:** one new `interact.json` variable packing the six controls (6 x 5 bits = 30 bits, declared `max` 2^31-1; the guard test `sim/test_persist_ranges.py` already refuses a range smaller than the packed value). That uses 15 of the 16 UI entries.
- **UI:** Settings > AUDIO > SOUND (six sliders and RESET) plus the existing EQUALIZER choice kept as the "styles" list (the eight presets). A change in the page applies live; Info shows the active stage gains and preamp.

## 6. What the research notes got right and what I would change

- Right: no exact MSEB clone is possible; macros over a PEQ are the useful part; defer dynamics, widening, FFT, convolution, reverb, ML; order the work as gain/output stage and the 16-bit decision first, then the EQ, then macros, then one dynamic feature behind a flag.
- Change: the notes plan the macros over a **10-band** graphic EQ. Five retuned stages already give every named control with one shaped stage each, at lower cost; ten bands add clocks, ROM and tuning for no named control. Keep 10 bands only if a power-user graphic page is wanted later (the writable store supports it).
- Change: the notes say "use the existing limiter work"; it does not exist yet (soft clipper design only, section 4).
- Add: the preamp must be computed from the **composite** response (exact, as above), and the gain-stage fold is what keeps preamp moves click-free.

## 7. Deferred dynamics: a cheap path for the first one

A de-esser is the right first dynamic feature because **S4 is already a bell with gain-indexed coefficients** and the spectrum bank already measures the 4-8 kHz band energy. A firmware-only prototype can read the 5-8 kHz band means each window (about 23 ms) and move S4's gain toward the dip when the band exceeds a threshold relative to the broadband level, through the same ramped coefficient updates. Limits: 23 ms detection lag and window-rate steps (sibilants last 50-150 ms, so it can work, but it is a de-esser of events, not a fast compressor); no RTL, no new DSP; behind a flag, with the normal gates (model, loopback and listening pass). Dynamic bass/punch and adaptive anything else need real envelope followers and attack/release in RTL and stay deferred.

## 8. Verification

Host: extend `tools/eq_model.py` with the gain-indexed table build and tests: each table entry equals `design(kind, f0, q, g)` quantised and stable; the table is monotone in gain; the composite response equals the cascade response and the dB-additivity property; macro-to-gain mapping is bounded; after the preamp and the clipper nothing exceeds full scale; all-zero equals today's FLAT bit for bit. RTL: coefficient-update testbench mid-stream equals the model sample for sample, with mutants (commit mid-sample, state cleared on update, wrong bank, preamp not applied). Fit: two seeds, ALM change measured. Hardware: loopback step/click on slider sweeps and on preset recalls, response sweep per macro (the loopback `analyze` sweep mode), ear tests, and the ALM and timing reports against the B-606 ledger.

## 9. Order

1. F2 A/B (fit `audio16-b602` running) and the gain stage G0-G4. 2. C5-lite: writable coefficient store, `EQCO` section, firmware macro layer, soft clipper, the six controls. 3. Tuning lab page (HTML, like the meter labs) to voice the macros by ear and freeze the tables. 4. Optional: the de-esser prototype behind a flag. 5. Park: dynamic bass/punch, widening, crossfeed, FFT, convolution, reverb, ML.

## 10. Decisions for the owner

1. Five retuned stages (recommended) or a full 10-band engine under the macros?
2. The six controls and their names as listed, or a different set? (Sibilance dip-only, Warmth to Clarity as one bipolar slider.)
3. Keep the eight fixed presets as "styles" next to the sliders, or fold them into macro tuples?
4. Preset recall: gain-dip (recommended) or dual-bank crossfade if the dip is audible?
5. De-esser prototype wanted soon, or parked after the basic layer is proven?

## 11. Owner decisions (2026-10-07) and the resulting design

**D-S01: six stages, not five and not ten.** The sixth stage is a separate **punch/definition bell at 3.5 kHz (Q1.2)**, so every control owns a stage of its own and only the Warmth-to-Clarity tilt is shared. Stage map: S1 low shelf 100 Hz Q0.7 (Bass), S2 bell 220 Hz Q1.0 (warm body / low-mid mud), S3 bell 1.8 kHz Q0.8 (Vocal), S4 bell 3.5 kHz Q1.2 (Punch), S5 bell 6.5 kHz Q1.5 (Sibilance, dip only), S6 high shelf 10 kHz Q0.7 (Air, and the clear side of the tilt). Cost, from the verified engine: **139 clocks per sample, 10% of 1,388 at 66.7 MHz** (116 for five), 48 state words, no extra DSP; gain-indexed table 37 steps x 5 coefficients x 6 stages x 18 bits = 19,980 bits (two M10K or MLAB). Both stage count and table size are parameters, so a seventh stage or the 10-band page stays a data change. Versus ten stages: about 40% less clock use and table, and no stage without a name. Versus five: Punch no longer has to share a bell with the low mids, which is what makes it controllable.

**D-S02: the presets are rebuilt from the ground up, as data.** The old eight (BASS/ROCK/POP/JAZZ/TREBLE/CLASSICAL/VOCAL and FLAT) are retired as voicings; a preset is now a name plus the six control positions, so it is exactly "slider positions you can see and change". Method: each preset is derived from the perceptual control it exercises and a known listening purpose (equal-loudness trend for LOW VOLUME, the intelligibility and plosive/sibilance bands for SPEECH, the usual 200-400 Hz "mud", 2-4 kHz presence and 5-8 kHz sibilance regions), then checked with the real coefficient generator for stability, coefficient range, composite peak and the auto-preamp. First defaults (macro tuple [warmth, bass, vocal, punch, sibilance dip, air] -> stage gains in dB, peak, preamp, overshoot after the preamp), from `halcyon_model.py`:

| Preset | Tuple | Stage gains (S1..S6) | Peak | Preamp | Overshoot | Purpose |
|---|---|---|---|---|---|---|
| FLAT | 0 0 0 0 0 0 | all 0 | 0.0 | 0.0 | 0.0 | reference; bit-exact bypass |
| WARM | +3 +1 0 0 0 0 | +2.5 +2.5 0 0 0 -1.5 | +3.1 | -1.1 | +2.0 | fuller body, softer top |
| CLEAR | -3 0 0 +1 0 +1 | -1.5 -2.5 0 +1 0 +2.5 | +2.5 | 0.0 | +2.5 | less mud, more definition and air |
| BASS | +1 +4 0 +1 0 0 | +4.5 +1 0 +1 0 -0.5 | +3.8 | -1.5 | +2.4 | small-driver bass weight |
| VOCAL | 0 -1 +3 0 +1 0 | -1 0 +2.5 0 -1 0 | +2.5 | -0.2 | +2.2 | speech band presence, less boom |
| SPEECH | -1 -3 +3 +1 +2 0 | -3.5 -1 +2.5 +1 -2 +0.5 | +2.7 | 0.0 | +2.7 | audiobooks and podcasts: rumble and plosives down, intelligibility up, sibilance down |
| LOW VOLUME | +1 +5 0 0 +1 +2 | +5.5 +1 0 0 -1 +1.5 | +5.4 | -1.9 | +3.4 | equal-loudness compensation at low level |
| SMOOTH | 0 0 0 -2 +2 -2 | 0 0 0 -2 -2 -2 | 0.0 | 0.0 | 0.0 | fatigue reduction for bright or harsh recordings |

Findings that shaped the set: (a) all eight are stable with coefficients inside the 18-bit range, worst overshoot +3.4 dB (LOW VOLUME), inside the +12 dB input range and absorbed by the soft clipper; (b) a pink-weighted loudness-matching preamp turns **positive** on cut-heavy presets (SMOOTH +0.6 dB before the cap), which only eats headroom, so the preamp is **attenuate-only** (capped at 0 dB), the same discipline as ReplayGain, at the price of those presets sounding up to about half a dB quieter than FLAT; (c) LOW VOLUME is a static stand-in for a volume-dependent loudness contour (plan option F: gains that follow the volume position, a cheap follow-on once the gain stage exists); (d) the lists are first voicings: the final numbers are chosen by ear on the lab page and by loopback sweeps through the Pocket's real output, never from the model alone. A "headphone natural" preset (a Harman-style target) needs measurement of the actual headphone chain and is not guessed here.

**Configurability: three layers, and the one hard limit.**
1. **Built-in defaults:** the table above, compiled into the firmware and checked equal to the model by a host test. Never overwritten.
2. **Tau Omega:** a `PRST` section in `tau-assets.bin` (the same CRC-checked TAUA container as the themes) with up to 16 named presets (name + six positions) and, for advanced use, an `EQST` section with the stage centres, Qs and the macro-to-dB step table; Omega edits them with a live response-curve preview (the same maths as this model), validates ranges, and can restore defaults by rewriting the section. A missing or invalid section means the built-ins (fail-safe, as for themes).
3. **On the Pocket (Settings > AUDIO > SOUND):** pick a preset (its tuple loads into the six sliders), move any slider (the preset reads CUSTOM), **SAVE** to a user slot ("MY SOUND"), **RESET** (back to the selected preset's own tuple) and **RESTORE DEFAULTS** (ignore Omega's override and use the built-ins; one persisted bit).
**The hard limit (and the decision it forces):** the Pocket shows at most 16 `interact.json` entries and 14 are already used (KB-079, B-455/B-456), so only **two** more persisted values fit today. Packed as 4 bits per control (11 positions), six controls are 24 bits: one entry holds the current six positions plus the preset index and the restore-defaults bit, the other holds MY SOUND. Editing a *named* preset in place on the Pocket and keeping it across power-off would need more entries (retire the legacy playlist ids 20-23, which only the Diagnostic Build's Check summary still uses) or a write-back to a data slot (rejected in this project's history: B-216, the A-088 to A-091 flush results). Open decision D-S05 below.

**D-S03: preset recall uses the gain dip first** (ramp down through the gain stage, swap coefficients, ramp up; cover about the 23 ms the slowest filter needs to settle), measured with the loopback step/click detector; the dual-bank crossfade is built only if the dip is audible. Slider moves need neither (ramped 0.5 dB steps).

**D-S04: de-esser is firmware-only for now:** S5's gain follows the 5-8 kHz band means from the hardware spectrum bank (about 23 ms window) through the same ramped coefficient updates, behind a flag, only after the basic layer is hardware-proven. No RTL, no new DSP.

**D-S05 (open): how many presets can the user edit and keep on the Pocket?** (a) the current positions plus ONE user slot (MY SOUND) now, everything else edited in Omega (zero reclaimed ids, recommended start); (b) retire ids 20-23 to get up to four more persisted values, e.g. four user slots; (c) data-slot write-back (not recommended). Omega-edited presets are unaffected by the limit (the core only reads the file).

**Build order (unchanged shape):** F2 A/B and the gain stage first; then the writable coefficient store (six stages), the `PRST`/`EQST` data sections with a Tau Omega editor, the firmware SOUND page and tables, the soft clipper; tuning lab page in parallel; the firmware de-esser flagged last.

> **Errata and additions 2026-10-07 (`CYMO_AUDIO_STACK_REVIEW.md`):** (F1) the 18-bit Q2.16 coefficients are too coarse for the 100 Hz and 220 Hz stages (up to 0.61 dB response error, uneven and non-monotonic 0.5 dB steps on the Bass slider, measured with `tools/lab/eq_coeff_precision.py`); the store should use 24-bit Q2.22 coefficients with a 64-bit accumulator, so the 19,980-bit table figure above becomes about 26,600 bits (three M10K). (F9) add a fixed seventh high-pass stage (35-40 Hz) for subsonic protection. (F7) a per-variant `interact.json` frees ids 20-23 in the release core, widening D-S05. (F3) the recall dip clears the EQ state during the mute.

> **Errata 2026-10-07 (`CYMO_DSP_REVIEW.md`):** the 0.70 shown for the two shelf stages is the cookbook shelf SLOPE S, not Q (S <= 1 is the monotonic range; every tool that edits a shelf must clamp S to (0, 1]). Bilinear cramping costs up to 1.14 dB at the 10 kHz shelf and 0.79 dB at the 6.5 kHz bell; an offline analog-matched table generator removes it at no hardware cost. An 18-bit trapezoidal SVF stage is the documented fallback if 24-bit coefficients prove too costly. Presets may later carry a raw biquad list (AutoEQ / Equalizer APO import in Tau Omega). Provenance: `docs/PROVENANCE.md`.

## Design updates 2026-10-07 (headphone review, B-616/B-623; parallel plan A7)
These supersede the earlier text where they differ. Source: `CYMO_HEADPHONE_PLAN.md` sections 5, 11, 13 and the recording plan's gates.
1. **Fixed infrasonic stage at 15-20 Hz, not 35-40 Hz.** Second-order high-pass, Q 0.7, always on: DC and infrasound protection only. Real music lives at 30-40 Hz on headphones and in-ears, and the core cannot tell speaker from headphone. A 35-40 Hz high-pass is part of the SPEECH preset only.
2. **Engine sized for 17 stages:** 10 correction + 6 Halcyon tone + 1 infrasonic (typical AutoEQ profiles carry 10 filters). About 200 of the 1,250 free clocks per sample [EST]; state of 17 stages x 4 words x 2 channels x 36 bits is about 4.9 kb. **State and the writable coefficient store go to MLAB/M10K, not registers** (the register-based EQ costs 1,659 ALMs for 5 stages): this is a prerequisite of the Halcyon RTL, to be measured by a synthesis-only run (parallel plan B2). Tau Omega may reduce a 10-filter profile to fewer stages by an offline fit.
3. **Coefficients 24-bit (Q2.22) for every stage** (`TAU_EQ_COEF24`; the 18-bit format cannot hold a 40 Hz +24 dB low shelf, `sim/test_halcyon_generator.py`), generated with the analog-matched design (`gen_eq_coeffs.design_stage`), validated after quantisation at the build's real width (`quantise_checked`).
4. **Preamp policy:** tone presets keep the loudness-matched (pink-weighted) preamp as an option; **the default is peak-safe (never clip)**, and correction presets are always peak-safe (the maximum boost sets a negative preamp, shown to the user as headroom). The preamp belongs in the gain stage target (before the filters) once the 24-bit EQ input exists; until then it stays inside the EQ. Gated: the clipper/limiter ceiling (G-ISP) and the quantiser scope (G-FLOOR).
5. **Correction presets** are raw biquad lists (`halcyon_model.raw_preset`), applied up to about 10 kHz by default, with a bass trim; nothing is shipped (measurement data licences): Tau Omega imports the user's own `ParametricEQ.txt`. Gated on G-LOAD (output impedance and load behaviour).
6. The firmware menu title `SOUND` becomes `HALCYON` when the page is built.

### Finding while preparing the MLAB experiment (B-627, parallel plan B2): why the EQ state is in registers today and what the rewrite must change
`eq_biquad.v` declares its per-stage state `(* ramstyle = "logic" *) reg [SW-1:0] st [0:NBQ*4-1]` and the coefficient and preamp ROMs `romstyle = "logic"`, and three properties of the current code, not only the attribute, keep it out of RAM: (1) **four asynchronous reads at once** (`x1 x2 y1 y2 = st[sbase + 0..3]` feed the multiplier operand mux), (2) **a reset loop writing every state word** (`for (i...) st[i] <= 0` inside `if (rst)`): a RAM cannot be reset in one cycle, so inference fails and the array becomes registers (the same class as KB-069's 'uninferred RAM' fallback), (3) the coefficient ROM is addressed combinationally. Simply changing the attribute to MLAB would therefore not work. The rewrite needs: a **sequential read sequence** (one registered read per MAC; the multiplier already runs two clocks per MAC, so four state reads fit in the existing budget at about 200 of 1,250 clocks), **no reset on the state array** with an explicit clearing sweep (a state-clear pulse is already planned for preset recall, D-S03, so the same mechanism clears on reset), and the coefficient store as a registered-read MLAB written through the sticky index+data port. MLAB reads can be asynchronous but there is only one read port per array; four state words per stage in one array therefore means either sequential reads or four arrays. Expected saving is large because the register form costs about 1,659 ALMs for 5 stages and 17 stages would be about 3x that; the true figure must come from a synthesis-only run of the rewritten module (not of the current one, which would just reproduce the register cost). The experiment is part of the Halcyon RTL step, after the quantiser scope is known (G-FLOOR).
