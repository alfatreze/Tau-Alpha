# Cymo DSP review against four open-source references (2026-10-07)

**Scope.** Full review of Tau's DSP implementation (EQ engine, coefficient design, the planned gain stage, Halcyon, soft clipper, dither, dynamics plan) against [alsa-jamesdsp](https://github.com/faust93/alsa-jamesdsp), [FreeEQ8](https://github.com/GareBear99/FreeEQ8), [Equalizer314](https://github.com/bearinmindcat/Equalizer314) and [ZL Equalizer 2](https://github.com/ZL-Audio/ZLEqualizer). **Provenance and licence record: `docs/PROVENANCE.md` (read it first).** All four are copyleft (GPL-2.0, GPL-3.0, GPL-3.0, AGPL-3.0) and Tau is MIT, so they were read for ideas and behaviour only: no code, constants or tables were copied, and the new analysis scripts in `tools/lab/` are our own. Evidence tags: [MEASURED] run here with the project's models, [READ] seen in a reference's source, [EST] estimate.

## 1. What each reference is, and what is actually in it

| | Own DSP? | Filter design | Structure | Dynamics | Notable | Relevance to Tau |
|---|---|---|---|---|---|---|
| **alsa-jamesdsp** (GPL-2.0) | yes, C | `iirfilters.c` ported from a foobar2000 plug-in, itself from SoX | DF2 sections | `compressor.c` is MIT (sndfilter, from Chromium); `JLimiter.c` is an instant-attack peak follower, 60 ms release, -0.1 dB | **15-band arbitrary-response EQ built as an FIR (min or linear phase) and run by FFT convolution**; bass boost, crossfeed, convolver, reverb, headphone DDC filters | The FFT/FIR design does not fit our clock budget (about 1,388 clocks per sample), the limiter is cruder than our soft clipper, the compressor is a standard design |
| **FreeEQ8** (GPL-3.0) | yes, C++/JUCE | RBJ cookbook | TDF-II in 64-bit; a second SVF (Simper/Cytomic) variant | per-band envelope follower, tanh saturation | audit and changelog document real bugs: **NaN in shelf bands for slope above 1 (about 11% of the parameter space)**, and its own correction that RBJ and SVF have **identical steady-state response** (cramping is a bilinear-transform property); SVF advantages are low-frequency SNR and stable coefficient modulation | The bug and the SVF finding both apply directly (sections 3 and 4) |
| **Equalizer314** (GPL-3.0) | no (Android DynamicsProcessing API does the DSP); own biquads for display/import | RBJ plus an optional Vicanek matched bell | n/a | platform multiband compressor and limiter | AutoEQ and Equalizer APO profile import; one band model, several views that "sound identical" | AutoEQ import is the most valuable idea for Tau Omega (section 6) |
| **ZL Equalizer 2** (AGPL-3.0) | yes, C++/JUCE | analog-matched "ideal" filters, filters beyond Nyquist | six filter structures incl. SVF and TDF; **parallel dynamic filters** | per-band side-chain, log-domain attack/release follower, threshold/knee gain computer | fitted-estimator gain compensation, EQ match by optimisation, K-weighted LUFS matching | The dynamic-band architecture and the analog-matched design problem (sections 3 and 5) |

## 2. Where Tau's DSP already stands, and where it is the same as the references

- RBJ cookbook biquads, fixed 48 kHz, exact cascade: same baseline as all four. Our engine is a bit-exact, model-verified fixed-point DF1 (36-bit state, round-to-nearest, clamp), 116 clocks per sample, which none of the plugins has an equivalent of (they run in double precision on a CPU).
- Loudness-compensated gain (our exact pink-weighted integration of the composite response against ZL's fitted estimator), attenuate-only, soft clip behind it. Ours is exact and cheaper; the references confirm the need (ZL ships it).
- Shelf-slope parametrisation: **our shelf stages use the cookbook's shelf-slope S, not Q** (the "0.70" on the two shelves is S = 0.70). The docs said Q; they are corrected here and in the spec. S <= 1 is the monotonic range.

## 3. New findings (measured here, not in any of the references)

### F1. Bilinear "cramping" costs up to 1.1 dB at the top stages, and it is free to remove [MEASURED, `tools/lab/eq_cramping.py`]
At 48 kHz the cookbook design strays from the analog prototype it imitates, by (worst dB, unquantised, +-9 dB): the 10 kHz high shelf 0.4 / **1.14** / 0.5 dB (to 8 kHz / 8-16 kHz / 16-20 kHz band), the 6.5 kHz bell 0.46 / **0.79** / 0.6, the 3.5 kHz bell 0.28, the 1.8 kHz bell 0.14, the two low stages 0.00. A least-squares refinement of the five coefficients against the analog prototype (starting from the cookbook values, a plain Levenberg-Marquardt fit, **no third-party code**) brings every stage to **0.02-0.06 dB** and stays stable [MEASURED]. Because Tau's coefficients are generated offline into tables, **a better design method costs nothing in hardware**. This addresses the same problem ZL ("ideal" filters), Equalizer314 (a Vicanek option) and FreeEQ8 (acknowledges it) address, with our own method.

### F2. SVF structure: far less coefficient-sensitive, quieter, but more multiplies [MEASURED, `eq_svf_precision.py`, `eq_structure_noise.py`]
The trapezoidal SVF (public equations, re-derived and verified against the cookbook biquad to 0.02 dB, the residual being the impulse-length limit of the check) with an **18-bit** coefficient word has coefficient-quantisation error of **0.002-0.009 dB** at the 100 Hz and 220 Hz stages, against **0.27-0.61 dB** for the DF1 biquad at 18 bits (the defect found in B-608/B-609) and 0.00-0.02 dB for DF1 at 24 bits. Rounding noise at 16 fractional state bits is negligible for both: at most 0.003 LSB RMS at the 16-bit output (DF1 24-bit 0.0007-0.0026, SVF 18-bit 0.00002-0.00013). Costs: 7 multiplies per stage instead of 5 (about 250 clocks for six stages against 139, still 18% of 1,388), different fractional scales for the coefficient groups, but only two state words per stage instead of four. This agrees with FreeEQ8's own note that the SVF's advantages are low-frequency precision and stable modulation, not a different response.

### F3. A parallel fixed-band-pass bank cannot replace the cascade [MEASURED, `eq_parallel_study.py`]
The ZL-style parallel form (`y = x + sum w_i * F_i(x)` with fixed unity-peak filters) would turn every control into one multiplier with no coefficient tables. Tested at the six Halcyon centres with least-squares weights: worst error against the cascade **0.4-1.9 dB (rms 0.15-0.55 dB) across the default presets, and 0.8-4.4 dB for single bands at +-9 dB** (the peaking filter's bandwidth depends on its gain, a fixed band-pass's does not). **Not recommended for the main EQ**; it remains a good fit for one dynamic band (section 5).

### F4. Parameter hardening: shelf slope and quantised stability [MEASURED, `sim/test_sound_shaping_model.py`]
FreeEQ8's audit found NaN in its shelf bands when the slope exceeded 1. Our generator uses the same cookbook form; within +-12 dB the radicand stays positive even at S = 4, but at larger gains an unclamped S above 1 raises (20 of 540 designs in our sweep), and **12 designs become unstable after 18-bit quantisation (all at 40 Hz with +-24 dB)**, none with 24-bit coefficients. New `clamp_slope()` and a sweep test; Tau Omega's editor and the firmware loader must clamp S to (0, 1] and check stability at the real coefficient width.

## 4. Review of the existing and planned DSP, with the references in mind

| Item | Verdict | Evidence and action |
|---|---|---|
| DF1 engine, 36-bit state, round-to-nearest | **Keep** | Rounding noise negligible [MEASURED]; the only weakness is coefficient width at low frequency |
| 24-bit Q2.22 coefficients (built, default off) | **Keep as the plan**, add a fallback | 0.016 dB worst, uniform steps [B-609]. Fallback if the synthesis-only count shows 24x36 costs too many DSP blocks or the 64-bit accumulate misses timing: the SVF with 18-bit coefficients (F2) |
| Offline analog-matched table generation | **Add** | F1: removes up to 1.1 dB, zero hardware cost |
| Soft clipper (static table) vs JamesDSP's peak-follower limiter | **Keep the clipper** | The follower limiter (instant attack, 60 ms release) pumps the whole mix and has state; ours is stateless, no latency. A fast follower could be a later safety net only |
| Pink-weighted preamp | **Keep** | Exact, and ZL's fitted estimator shows the need |
| Arbitrary-response FIR EQ (JamesDSP, ZL linear phase) | **Do not build** | Needs FFT convolution or hundreds of taps; a 128-tap FIR would take about 256 of 1,388 clocks for no gain over six biquads |
| Per-band saturation and oversampling (FreeEQ8) | **Not needed now** | Our only nonlinearity is the soft clipper at 48 kHz on rare peaks; oversampling it is a later quality option, not required |
| Dynamic EQ / de-esser | **Keep firmware-first, design the hardware path** | Section 5 |
| Presets as data, editable in Tau Omega | **Extend** | Section 6 |

## 5. Dynamics: how the references do it, and the cheapest hardware path for Tau

ZL and FreeEQ8 share the standard architecture: a side-chain detector (band-pass or the filter's own band), an attack/release envelope follower (one-pole, branching on rising or falling input, in the log domain for ZL), a threshold/knee gain computer, and the result drives the band's gain, either through a coefficient update per sample or block, or through a parallel path (`x + (g-1)*BP(x)`). The detector and gain-computer forms are textbook (Giannoulis, Massberg and Reiss, JAES 2012). For the Sibilance stage (S5) Tau can do the same inside the existing engine: one extra fixed band-pass stage as the detector (about +23 clocks), a small-shift attack/release follower (a few dozen ALMs), a log-domain threshold via a short table, and a parallel dip `y = x - d * BP(x)` driven per sample (one multiply, no coefficient tables, no steps), accepting the shape difference measured in F3 for this single band. That avoids the 23 ms window lag of the firmware-only plan (which reads the spectrum bank) and is click-free. **Recommendation: keep the firmware prototype first, as decided (D-S04); if it works but its lag is audible, the parallel hardware dip is the follow-up, credited to ZL's parallel dynamic filter concept in the file header (`PROVENANCE.md`).**

## 6. Tau Omega: AutoEQ and Equalizer APO import (new, highest user value)
Equalizer314's AutoEQ import shows the demand: headphone-correction profiles are lists of `PK`/`LSC`/`HSC` filters (frequency, gain, Q) plus a preamp. Android's DynamicsProcessing has to approximate them as band gains; **Tau can reproduce them exactly**, because the planned writable coefficient store holds real biquad coefficients: Omega converts each filter (with F1's matched design and F4's checks) into quantised coefficients for up to seven stages and a preamp (attenuate-only), and ships them in the `PRST` preset data. This needs the preset format to allow either "control positions" or "raw biquad list" presets. The profiles' measurement data has its own licences: ship none, let users import their own file.

## 7. Recommendations, in order
1. **Add the analog-magnitude-matched refinement to `gen_eq_coeffs.py`'s table output** (F1): offline, free, up to 1.1 dB better at the top two stages. Provenance note as in `PROVENANCE.md`.
2. **Keep 24-bit DF1 as the build plan; run the synthesis-only DSP/ALM/timing count; keep the 18-bit SVF stage as the documented fallback** (F2).
3. **Harden every tool that writes shelf stages:** clamp S to (0, 1], check stability after quantisation at the actual width (F4); add the same sweep to Tau Omega's tests and golden vectors.
4. **Preset format: allow a raw biquad list** and plan the AutoEQ/APO import in Omega (section 6).
5. **Hardware de-esser as an optional second step** (section 5), parallel dip on the existing engine, after the firmware prototype is judged.
6. **Do not build** an FIR/FFT EQ, per-band saturation or oversampling now.
7. Correct the docs: shelf stages are S = 0.70, not Q (done in the Halcyon spec errata).

## 8. What this review did not cover
Only the parts of each project listed in `PROVENANCE.md` were read; ZL's analyser, linear-phase FIR, EQ-match optimiser and loudness meter, FreeEQ8's linear-phase and match engines, and the bulk of JamesDSP (reverb, convolver, valve models) were not. Claims in the projects' READMEs and papers were not taken at face value. No listening test was done; all numbers are host models of the filters.
