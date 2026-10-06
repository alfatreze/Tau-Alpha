# Cymo C5 reconsidered: "Sound Shaping", a macro-control EQ layer (design, 2026-10-07)

**Status: DESIGN, with a host prototype of the stage mapping. Nothing built in RTL or firmware.** Owner request: replace the 10-band graphic-EQ plan (C5 option B in `CYMO_AUDIO_ENGINE.md` section 8) with an MSEB-inspired mode, judged together with the gain stage (`CYMO_GAIN_STAGE_DESIGN.md`), the soft clipper and quantiser (`CYMO_OUTPUT_STAGE_SPEC.md`), the 16-bit I2S A/B, and the DSP/ALM budget. Naming: HiBy's MSEB is a proprietary PEQ plus dynamic processing with no public algorithm, so this is **not** an MSEB clone and should not carry the name; the working name here is "Sound Shaping".

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
