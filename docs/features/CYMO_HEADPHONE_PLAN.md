# Cymo for headphone listening: engineering analysis and coupled plan (2026-10-07)

Evidence labels: [READ] source or doc read this session, [MODELLED] host arithmetic, [MEASURED] hardware, [EST] estimate, [OPEN] unknown that must be measured. Parent docs: `CYMO_GAIN_STAGE_DESIGN`, `CYMO_OUTPUT_STAGE_SPEC`, `CYMO_HALCYON_SPEC`, `CYMO_AUDIO_STACK_REVIEW`, `CYMO_DSP_REVIEW`, `CYMO_AUDIO_ENGINE`. Nothing here is built; D-H03 parked crossfeed, this document is the review it called for.

## 1. Verdict on the brief

The brief's order is right in spirit, but three of its premises need correcting and four things are missing that matter more for headphones than most of the list.

| Brief item | Verdict |
|---|---|
| FIR resampler | Agree, done. Headphones expose its absence most (the hold's image products sit in the audible band: modelled 27.7 dB SINAD, measured 10.8 dB OFF vs 54.5 dB ON, capture-limited). |
| Soft clipper + dither | Agree, but **dither is more valuable than the brief says, and for a reason it does not give**: the volume control is digital at 16 bits, so at a normal listening position (-20 to -30 dB) quiet passages use 10-12 bits and truncation distortion is correlated with the music. IEMs make it worse (sensitive, quiet noise floor). See 3. |
| 24-bit coefficients | Agree. Synthesis count: +2 DSP, +37 ALM [MEASURED synth only]; timing of the 64-bit accumulate is not yet known. |
| 16-bit I2S slot | Agree, and **it is a headphone-level question, not just a bit-depth one**: the serialiser sends 15 bits, 6 dB low. If the amp stage after the DAC is fixed-gain, high-impedance headphones lose 6 dB of maximum loudness. Test max level with a real load, not only SINAD. |
| Crossfeed | Agree it is the cheapest and most valued; **it can be much cheaper than BS2B-style**, see 4. |
| Correction EQ | Agree it is the biggest single gain, but it is **constrained by EQ state storage, not DSP or clocks**, see 5, and by data licensing. |
| Auto-preamp / headroom | Agree; correct placement is BEFORE the filters (gain-stage target), which needs the EQ's 24-bit input or the attenuation costs resolution. |
| Loudness-aware volume | Agree, with a trap: it must key off the **effective gain (volume x ReplayGain)**, not the volume position, and the absolute listening level is unknown for headphones (sensitivity ranges ~90-120 dB/mW). Needs a calibration-free design, see 6. |
| Mono / balance | Agree. Both are almost free in the existing hardware, see 7. |
| Gapless | **The brief's "no support" is right but the barrier is smaller than B-424 assumed**, see 8. |
| Spatial, hi-res output, bass boost | Agree they are low value. One correction on hi-res: the DAC being 48 kHz is not the problem; the problem is that 96 kHz FLAC is *refused* (about 180% of realtime) and 24-bit FLAC is **truncated to 16 bits without rounding or dither** (`fw/flac.c:804`, `v >>= bps - 16`, an arithmetic floor: -0.5 LSB DC and correlated distortion). That is a real defect for headphone users with 24-bit libraries, see 3. |

## 2. The signal chain these features share

```
decode (24-bit FLAC kept wide) -> [mono/balance prep] -> pcm_fifo -> GAIN STAGE (built, B-615) -> Cymo FIR resampler -> CROSSFEED -> Halcyon tone + CORRECTION EQ (one biquad engine) -> soft clipper -> final quantiser (dither) -> I2S 15/16-bit
                                  meters tap the FIFO output (before the gain stage)
```
Rules that fall out of the physics:
1. **Linear stages first, nonlinear last.** Crossfeed, tone, correction are all linear and time-invariant, so they commute except for clipping and per-channel differences; the only nonlinear parts (soft clipper, quantiser/dither) are last. Keep them last.
2. **Correction is the last linear stage before the clipper**: it compensates the transducer, so everything the listener chooses (tone, crossfeed) should be corrected too. Tone (Halcyon) is taste; correction is a property of the headphone.
3. **Level changes happen before the filters.** The gain stage already ramps volume, ReplayGain and fade. Fold the EQ preamp (negative, from the exact composite response) into its target so boosts cannot clip the *output*; this needs the 24-bit path from the gain stage into the EQ, else a -9 dB preamp costs 1.5 bits at the input.
4. **Every state change that alters loudness must ramp.** Today toggling the live resampler or the 16-bit slot (+6 dB) can step the level by 6 dB into someone's ears (the code comment says "lower the volume before switching it on"). For headphones this is a safety defect, not a nuisance. Route every such change through the gain stage's ramp (a loudness-neutral compensating offset).

## 3. Quantisation and dither, headphone-specific
- **24-bit sources**: stop truncating in the decoder. Keep the wide sample (Q.8 extra bits) into the gain stage (it was designed with a 24-bit output option) and quantise ONCE at the end with TPDF dither. Interim, firmware only: round instead of floor in `flac.c:804` (removes the -0.5 LSB DC bias), a one-line change with a host test. [READ]
- **Where dither matters**: not at full scale but after attenuation and on fades and reverb tails, which is where headphone listeners hear it. The TPDF silence gate (dither off after 32 zero samples) keeps true digital silence silent, which matters on sensitive IEMs where any dither hiss is audible.
- **Noise shaping** (error feedback pushing noise above ~15 kHz) can buy a few dB perceived at 3-4 kHz at very low levels; not now. It adds state, a stability argument and an A/B burden for a gain only audible below about -50 dB. Revisit after the plain TPDF is measured.
- **Soft clipper**: knee 0.9 FS per spec. With peak-based correction preamps and ReplayGain peak awareness (6) it will rarely engage, which is the goal: it is a safety net, not a sound.

## 4. Crossfeed (own derivation, [MODELLED])
A compact form exploiting mono invariance: with `d = R - L`, `c = b * z^-D * LP(d)`, output `L' = L + c`, `R' = R - c`. One low-pass (700 Hz one-pole), one delay line of D = 13 samples (0.27 ms, the interaural delay), one gain `b`, **one mono-ish datapath instead of two cross paths**. Properties:
- **A mono signal passes bit-exactly unchanged** (d = 0), so there is no tonal change for centred content and no loudness change to compensate. Classic designs need a compensating direct-path shelf to get this.
- Only the side signal's low frequencies are reduced, with the delayed copy supplying the interaural cue. Modelled (700 Hz, D 13): b 0.30 gives direct-path response -2.8 dB at 100 Hz, within +-1.3 dB elsewhere, cross-path level -10.5 dB at 100 Hz falling to -32 dB at 8 kHz (channel separation reduced about 8 dB at 100 Hz, 16 dB at 1 kHz); b 0.45 about 5 dB at 100 Hz. Worst case (full-scale anti-phase low frequencies) the output can exceed full scale by +3.9 dB (b 0.30), absorbed by the soft clipper; real material is far from this.
- Cost [EST]: one MAC-equivalent, a 13-sample delay line (MLAB, 24-bit), one-pole state, about 10-15 clocks per sample of the 1,250 free, no DSP block. It runs at 48 kHz after the resampler, so the 44.1 kHz path and the Cymo guard are unaffected.
- **Mono mode is the same datapath**: `c = (R - L)/2`, low-pass bypassed, delay 0, giving `L' = R' = (L + R)/2`. Balance and mono, the brief's section 3, therefore cost nothing extra.
- Controls: three presets (light 0.20 / medium 0.30 / strong 0.45), cutoff fixed 700 Hz. Do not expose free cutoff/delay: people choose the wrong values. Provenance: derived from the published interaural time/level difference principle; BS2B (MIT) and Linkwitz/Meier designs were not read for this; record in `PROVENANCE.md`.
- Tie-in: meters tap before it, ReplayGain unaffected, Halcyon unaffected (linear). Tempo/WSOLA runs earlier in the chain, unaffected.

## 5. Headphone correction (the biggest per-headphone gain, and the most constrained)
- **Not a DSP or clock problem.** The EQ engine is time-multiplexed: 116 clocks for 5 stages and 139 for 6, against 1,250 free per sample, so 13 stages (6 Halcyon + 1 subsonic + 6 correction) is about 150-200 clocks [EST]. **The cost is state storage**: the EQ keeps per-stage state in registers declared `logic` (1,659 ALMs for 5 stages, `CYMO_GAIN_STAGE_DESIGN` section 8). 13 stages in registers is roughly 2.5-3x that. So moving EQ state and the writable coefficient store into MLAB/M10K is a **prerequisite of the Halcyon RTL**, not an optimisation. Budget M10K: 68 free.
- **24-bit coefficients are mandatory for correction data** (high-Q bass bands, 40-100 Hz): measured 18-bit fails at 40 Hz +24 dB low shelf (`test_halcyon_generator.py`). The matched design removes the 1.1 dB cramping at the top shelf.
- **Preamp for correction presets is peak-based** (never clip), not the pink-weighted loudness match used for tone presets, which overshoots by up to +3.5 dB by design. A correction that boosts bass by 6 dB therefore lowers overall level by about 6 dB; say so in the UI ("headroom -6 dB"), it is correct behaviour.
- **Data and licensing**: measurements are per unit and per source; the AutoEQ code is MIT but the measurement databases carry their own terms. Ship none (PROVENANCE row): Tau Omega imports the user's own `ParametricEQ.txt`, converts `PK/LSC/HSC` with `design_stage` (clamp, matched, quantised stability check), writes a raw-biquad preset. Already supported in the model (`raw_preset`, at most six stages; raise to the engine's correction budget when the RTL exists).
- **Unknowns that bound the value** [OPEN]: the Pocket's headphone amp output impedance (multi-driver IEMs change their frequency response with source impedance, so a correction measured on a 0.1 ohm source is wrong on a 10 ohm one) and its own response into 16-300 ohm loads. Measure the Pocket into a few dummy loads (32 ohm resistor, a real IEM and headphone through a measurement chain) before promising correction accuracy. **The Pocket amp may itself need correcting**, which is also a Halcyon use.

## 6. Loudness compensation that survives real headphones
Equal-loudness compensation depends on absolute level; the Pocket cannot know it. Design so that it does not need to:
- Key off the **effective attenuation from a reference point** the user sets ("compensation is zero at this volume"), using the gain stage's `cur` (volume x ReplayGain x preamp), so ReplayGain changes track correctly (a quiet-mastered track played at the same position is the same loudness problem).
- A strength control (off / mild / strong), shelf gains on the existing low and high shelves via the gain-indexed table (D). Coefficients change in 0.5 dB steps at the ramp rate to avoid clicks.
- Hearing-safety companions that belong with it: a **maximum volume position** setting and a **start-up volume cap**, because correction/crossfeed/16-bit changes alter loudness. Cheap, firmware only. [READ: nothing like it exists today]

## 7. Mono and balance
Mono as above. Balance: the gain stage multiplier is already shared by L and R in a 12-clock sequence, so **per-channel targets cost two registers and one more ramp state**, attenuate-only (the louder side is reduced). Place it in the gain stage now (slightly before crossfeed, an acceptable approximation); if a final per-channel trim ever matters, move it to the output stage. Do this as a small extension of B-615 before the FPGA fit is collected? The fit is already running; fold it into the Halcyon RTL fit instead.

## 8. Gapless, re-scoped
Hard facts [READ]: the FIFO is 2,048 samples (46 ms at 44.1 kHz); every track change flushes it and fades in from silence; the LAME/Info tag is parsed only for the encoder string, not the delay and padding fields that follow it; FLAC has exact sample counts.
- **No second decoder and no second data slot are needed** for the common case. At the natural end of a track the old file is fully read and decoded into the FIFO, so the new file can be opened and decoded while the FIFO still plays the old tail. The requirement is simply **load latency < FIFO depth** (open + first frame, about 100-300 ms with the library path [OPEN, measure]), plus **do not flush at a natural end** (flush is for seeks and manual changes).
- So the work is: (1) a deeper FIFO, 8,192 entries (186 ms) is about 26 M10K of the 68 free, or a PSRAM-backed ring; (2) skip the flush and the fade at a natural boundary; (3) trim MP3 encoder delay and padding from the LAME tag (FLAC needs nothing); (4) restrict to **same sample rate** (the drain rate is global; a 44.1 to 48 kHz boundary legitimately needs a drain), and keep the Cymo guard state continuous across the boundary.
- Gain-stage coupling: `adv` already ignores gaps; the fade state must not restart at a gapless boundary (do not pulse FADE_NOW). The resampler and feed queue keep running (no clear).
- This is the most valuable item for live and classical albums on headphones and belongs ahead of crossfeed for those listeners, but it touches the track-load path and the FIFO, so it needs the audio-first load work and a measurement of load latency first.

## 9. Ranked plan (changes the brief's order where the engineering says so)
| # | Item | Why now | Gate |
|---|---|---|---|
| 0 | Measure the Pocket headphone chain: level and SINAD into 32 ohm and a real headphone, 15 vs 16 bit, maximum undistorted level, output impedance | everything below assumes a transparent amp; the 16-bit decision is a level decision | owner capture; DEV 104 |
| 1 | Round (not floor) 24 to 16 in `flac.c`; make every loudness-changing toggle ramp (16-bit, live resampler) | removes a real distortion and a real safety defect, firmware only | host test, then listening |
| 2 | Gain stage hardware (B-615 built, fit running), then per-channel targets + peak-aware positive ReplayGain (needs the clipper) | RG +gain for quiet masters is a top headphone need; the stage's 18-bit multiplier already takes cur above unity | fit, DEV test |
| 3 | Halcyon RTL: EQ state and coefficient store to MLAB/M10K, 24-bit input, soft clipper, final quantiser with dither | prerequisite of correction and of proper 24-bit handling | two-seed fit with `TAU_EQ_COEF24` |
| 4 | Crossfeed + mono (one datapath, mono-invariant) | cheap once the engine slot exists; no DSP | listening, host model first |
| 5 | Correction presets via Tau Omega (raw biquad lists), peak preamp, max-volume cap | the biggest per-headphone improvement | needs 0's impedance data |
| 6 | Gapless: FIFO depth + no-flush boundary + LAME trim | most valuable for live/classical; touches load path | load-latency measurement, audio-first spec |
| 7 | Loudness compensation (effective-gain keyed) | after 3 and the gain-indexed table | listening |
| - | Spatial virtualisation, hi-res output, bass boost presets | parked | |

## 10. What would change this plan
A measured Pocket headphone amp with high output impedance or strong load dependence moves correction up and makes a per-load calibration step necessary; a measured load latency above 186 ms makes a PSRAM ring (not M10K) the gapless buffer; an A/B in which 16-bit gives +6 dB real headroom makes the maximum-volume cap and loudness-neutral toggling urgent rather than optional.
