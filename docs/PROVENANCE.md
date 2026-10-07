# Provenance of Tau's DSP work: what is ours, what is public, and what was read

**Rule (D-P01, 2026-10-07).** Tau is MIT. Third-party code under GPL, AGPL or LGPL, and the constants, tables and fitted numbers inside it, are never copied into Tau, and Tau code is not written by transcribing it. Such projects may be read for ideas, behaviour and bugs; everything we build comes from published papers, public specifications, public-domain algorithms (the RBJ Audio EQ Cookbook) or our own derivations and measurements. Where an idea in a Tau design was seen in, or substantially informed by, another project, it is recorded here and in the doc that uses it. The same one-way rule already governs GPL RTL (audit B-085). If code is ever taken from a permissively licensed source it needs an attribution in the file and an entry here.

## Sources read for the 2026-10-07 DSP review (read-only shallow clones in a scratch folder, nothing executed, deleted afterwards)

| Project | Licence | Commit read | Read | Not read |
|---|---|---|---|---|
| [faust93/alsa-jamesdsp](https://github.com/faust93/alsa-jamesdsp) (ALSA port of JamesDSP, pre-EEL2) | GPL-2.0 (contains MIT parts, see below) | `d1f1685` (2024-02-12) | README, `iirfilters.c`, `compressor.c`, `JLimiter.c`, `ArbFIRGen.c` (outline), `vdc.c` (head), the effect chain in `EffectDSPMain.cpp` | reverb, convolver, valve models, ViPER code |
| [GareBear99/FreeEQ8](https://github.com/GareBear99/FreeEQ8) | GPL-3.0 (JUCE has its own licence) | `11376c1` (2026-08-01) | `Biquad.h`, `SvfBiquad.h`, `IntentMode.h`, `AUDIT.md`, `CHANGELOG.md`, the file layout | linear-phase, match EQ, resonance detector internals, UI, licensing code |
| [bearinmindcat/Equalizer314](https://github.com/bearinmindcat/Equalizer314) | GPL-3.0 | `ecf218e` (2026-10-06) | README, `dsp/BiquadFilter.kt` (head), `ui/SimpleEqController.kt` (head), AutoEQ import call sites | the rest of the Android app; its DSP is Android's DynamicsProcessing API, not its own |
| [ZL-Audio/ZLEqualizer](https://github.com/ZL-Audio/ZLEqualizer) (ZL Equalizer 2) | AGPL-3.0 | `c4e83f6` (2026-10-05) | file layout, `ivantsov_svf_coeff.cpp` (head), `gain_compensation.hpp` (head), `dynamic_base.hpp`, `dynamic_parallel.hpp`, `ps_follower.hpp`, `dynamic_side_handler.hpp` (outline) | the analyser, linear-phase FIR, EQ match optimiser, loudness meter, GUI |

Findings from these are marketing-free: where a project's README or audit makes a claim, only what the code and its own tests show is used here.

## What in Tau is independent, and where each idea came from

| Tau item | Origin | Seen in the sources? | Taken? |
|---|---|---|---|
| RBJ cookbook biquad design (`tools/gen_eq_coeffs.py`) | R. Bristow-Johnson, *Audio EQ Cookbook* (public) | yes, all four | No code; our own implementation from 2026-09 |
| Fixed-point DF1 engine, state and accumulator widths, round-to-nearest, clamp (`eq_biquad.v`, `eq_model.py`) | Own design, measured (EQ_DESIGN, B-0xx) | no | n/a |
| Pink-weighted loudness preamp from the exact composite response | Own (2026-09, `gen_eq_coeffs.py`) | ZL uses a different, fitted estimator | No |
| Soft clipper after the EQ (knee 0.9 FS, table) | Own decision 2026-09-29 | JamesDSP's limiter is a dynamic peak follower, a different thing | No |
| Six perceptual controls over six biquad stages, rebuilt presets as data | Own (owner's research note plus the existing five-stage engine) | FreeEQ8's "intent modes" bias a resonance detector, Equalizer314's simple mode is a 10-band graphic EQ: different concepts | No |
| 24-bit coefficient option; measured LF coefficient sensitivity | Own measurement (`eq_coeff_precision.py`) | none of them report it | No |
| Offline analog-magnitude-matched coefficient refinement (**built B-614**: `gen_eq_coeffs.design_matched`, numpy-only Levenberg-Marquardt; worst error 1.14 to 0.065 dB, `sim/test_halcyon_generator.py`) | Generic least-squares fit of the digital magnitude to the analog prototype; motivated by the same cramping problem that ZL ("ideal" filters), Equalizer314 (a Vicanek option) and FreeEQ8 (acknowledges it) address | the problem and the existence of analog-matched designs | **Inspired by the problem statement only**; no code or constants. If built, cite Vicanek, *Matched Second Order Digital Filters* (2016), and Orfanidis, *Digital Parametric Equalizer Design with Prescribed Nyquist-Frequency Gain* (JAES 1997) |
| TPT state-variable-filter structure study (`eq_svf_precision.py`, `eq_structure_noise.py`) | Public derivations: Zavalishin, *The Art of VA Filter Design*; Simper/Cytomic 2013 (re-derived, **verified numerically against the cookbook biquad**) | FreeEQ8's `SvfBiquad.h` implements the same public equations; it was read before the output-mix coefficients were re-derived, and its role for the bell mix was cross-checked against it | **Substantially informed by reading, independently re-derived and verified**; nothing copied; if an SVF stage is built, cite Zavalishin and Simper |
| Dynamic EQ / de-esser design (planned, not built) | Textbook: Giannoulis, Massberg, Reiss, *Digital Dynamic Range Compressor Design* (JAES 2012) for the detector and gain computer; parallel dynamic bands are a standard technique | ZL's `DynamicParallel`, `PSFollower` and FreeEQ8's per-band follower use the same pattern | **If built: substantially informed by ZL Equalizer's parallel dynamic filter and follower structure**; no code or constants; credit in the file header |
| AutoEQ / Equalizer APO profile import in Tau Omega (planned) | The Equalizer APO text format and the AutoEQ project (MIT) | Equalizer314 imports it | No code; implement the parser from the format; keep measurement data out of the repository (its own licences) |
| Shelf-slope clamp and parameter-sweep test (`halcyon_model.clamp_slope`, `test_halcyon_model.py`) | Lesson from FreeEQ8's published audit and changelog (NaN in about 11% of its parameter space for shelf slope above 1) | yes | **Inspired by their bug report**; our own clamp and test |

## Permissively licensed pieces that exist in those trees (not used)

JamesDSP's `compressor.c` is MIT (Sean Connelly's *sndfilter*, itself derived from Chromium's `DynamicsCompressorKernel`, BSD-3). The BS2B crossfeed (libbs2b) is MIT. Both could be reused with attribution if ever wanted; neither is used. Everything else read is copyleft and stays unused.

| Crossfeed in difference form (`L' = L + c, R' = R - c, c = b z^-D LP(R-L)`), mono-invariant (planned, `CYMO_HEADPHONE_PLAN.md` section 4) | Own derivation from the published interaural time and level difference principle; host-modelled | BS2B (MIT) and Linkwitz/Meier crossfeeds exist; **not read** for this | No code, no constants taken; if built, state that the 700 Hz / 0.27 ms values are the textbook ITD/ILD figures |
