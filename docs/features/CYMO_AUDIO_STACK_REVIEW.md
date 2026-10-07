# Cymo audio stack: in-depth engineering review (2026-10-07)

**Scope:** everything proposed since the 16-bit/15-bit A/B started: the hardware gain stage (`CYMO_GAIN_STAGE_DESIGN.md`), the final quantiser (round, dither, soft clipper; `CYMO_OUTPUT_STAGE_SPEC.md`), the six-stage Halcyon EQ with rebuilt presets (`CYMO_SOUND_SHAPING_SPEC.md`), the writable coefficient store, the firmware de-esser, persistence, Tau Omega data, and the DSP budget that all of it implies. Method: read the RTL and firmware, re-ran the models, and measured what was only argued before. Resource figures are from the shipped fit (`AUDIT_TRAIL.md` B-606: DSP 20/66, ALM 71%, M10K 256/308); the `audio16-b602` fit is still running and does not change them materially. Tags: [MEASURED] run here, [READ] from the source, [EST] estimate to be checked by a synthesis-only run.

## 1. The stack as it should be built

`FIFO -> [Cymo resampler] -> GAIN STAGE (volume x ReplayGain x EQ preamp, ramped, fade-in on flush) -> EQ (six biquad stages, writable coefficients) -> SOFT CLIP -> FINAL QUANTISER (round, dither, the 16/15-bit slot) -> I2S`

Hardware meters (spectrum bank, wave meter) tap the FIFO output, before the gain stage (recommendation 6).

## 2. Verdict

The architecture is sound and the pieces compose: one multiply for loudness, one EQ engine already inside its clock budget, one place that quantises. The review found **two real problems in my own earlier proposals** (coefficient precision, and an overstated DSP need), one design improvement to the recall dip, and several smaller items. None is a blocker; the first one changes the EQ datapath and must be decided before the RTL is written.

## 3. Findings, ordered by importance

### F1. 18-bit coefficients are too coarse for the two low stages [MEASURED, new, HIGH]
The shipped EQ stores coefficients as 18-bit Q2.16. A shelf or bell at 100-220 Hz at 48 kHz has its poles very close to z = 1, so the response depends on the last bits (`tools/lab/eq_coeff_precision.py`, the real design equations and quantiser):

| Stage | Worst response error, 18-bit (shipped) | +2 bits | +4 bits | +6 bits |
|---|---|---|---|---|
| S1 low shelf 100 Hz | **0.27-0.61 dB** | 0.11-0.18 | 0.001-0.045 | 0.003-0.016 |
| S2 body bell 220 Hz | up to 0.16 dB | 0.04 | 0.012 | 0.003 |
| S3 to S6 (1.8 kHz and up) | under 0.002 dB | 0 | 0 | 0 |

The audible effect is worse than the peak error: for the Bass slider, the response change per nominal 0.5 dB step is **uneven and sometimes backwards** at 18 bits (at 50 Hz it ranges from -0.34 to +1.06 dB with 2 non-monotonic steps out of 36), against 0.37-0.48 dB at +4 bits and 0.41-0.43 dB at +8 bits. A slider that jumps or reverses is a defect users hear immediately. The present presets hid this because they use a handful of fixed gains and nobody measured their response against the ideal.
**Options:** (A) widen the coefficient word to **24-bit Q2.22** for every stage (+6 bits, error under 0.02 dB, uniform steps); cost: a wider multiplier operand (probably +0 to +2 DSP blocks [EST]), the accumulator from 58 to 64 bits (60-bit products plus guard bits), and table bits +33% (about 26.6 kb, three M10K blocks). (B) a closed-loop table generator that searches the neighbouring 18-bit codes for the best response (measured: the worst error drops, e.g. 0.61 to 0.08 dB, but not everywhere: 0.277 stayed 0.277), zero hardware cost, not a guarantee. (C) delta-form coefficients for S1/S2 only (store b0-1, b1+2, b2-1, a1+2, a2-1 at higher scale and add the exact second-difference terms): no extra DSP, a few adders and a per-stage form flag, more verification. **Recommendation: A, uniform across stages**, with B as the table-generation method on top (it is free and fixes the residual non-uniformity). It is the one place where more DSP is the right answer. Confirm the DSP and timing cost with a synthesis-only run before the fit (the 64-bit accumulate in fabric is the thing to watch; the accumulate can move into the DSP's accumulator mode if timing asks).

### F2. The gain stage needs one DSP block, not two [READ, correction to my earlier review]
I said each channel needs a 27x27 block for a 24-bit by 16-bit multiply. That is wrong for today's data: the stage's **input** is the 16-bit FIFO/resampler sample; only the **output** is wider. A 16x16 multiply fits an 18x18 half-block, and Cyclone V's "two independent 18x18" mode puts L and R in **one** DSP block, both products in the same cycle (the torn-sample benefit stands). Two blocks (one 27x27-class multiplier per channel) are needed only when a 24-bit FLAC input path is added later. Corrected resource line: 1 DSP now, 2 with a 24-bit input; about 100-200 ALMs [EST].

### F3. Preset recall: clear the state during the mute, and the dip can be about 10 ms [READ, improves D-S03]
`eq_biquad` only mutes by switching to bypass; it does not clear its 40 state words on a preset change, so a swap leaves the old filter's ringing running against the new coefficients (the slowest preset needs 23 ms to settle, `eq_model.py`). I had sized the dip to that. Better: ramp the gain to zero (about 5 ms), **clear the EQ state while muted and swap the coefficients**, ramp back (about 5 ms). The new filter then starts from silence under a rising gain, with no ringing to hide. Total dip about 10 ms, not 25-30 ms. Needs one new RTL control: a state-clear pulse. Same fallback rule: build the dual-bank crossfade only if this is audible.

### F4. Feeding the EQ 24 bits needs no state widening [READ, clarification]
The EQ state has 16 fractional bits (Q20.16 in 36 bits). A 16-integer-bit sample with 8 extra fractional bits from the gain stage fits those 16 fractional bits as is: only the input scaling shift changes (<<8 instead of <<16). Decision 5 of the gain-stage design (EQ input 24 bits) is therefore cheap; do it now.

### F5. Bass boost on the Pocket's own speaker [REASONED, needs measurement, MEDIUM]
The core cannot tell speaker from headphones (no output-sense input is documented), and the Pocket's small speaker has little output below a few hundred Hz. Boosting 100 Hz by 4-5 dB (BASS, LOW VOLUME) spends headroom and speaker excursion on energy the speaker cannot reproduce and adds distortion. **Recommendations:** cap the low-shelf stage at +6 dB in presets (LOW VOLUME is +5.5 now; keep it there or lower), add a fixed **subsonic high-pass** (below), make LOW VOLUME volume-dependent later (plan option F) instead of a fixed boost, and capture loopback sweeps of the real output chain (speaker and headphone) before freezing voicings.

### F6. Meters: tap before the gain stage [DECISION, recommended]
Hardware spectrum and wave meters read the FIFO output, so they currently see audio after the firmware volume and shrink when the volume goes down, while the software level meters already read pre-volume PCM. Recommendation: **volume-independent visuals** (tap at the FIFO output, before the gain stage): turning the volume down should not blank the display, it matches the software meters, and it needs no change. Cost: none.

### F7. Persistence: use a separate `interact.json` for the release core [READ, improves D-S05]
The 16-entry cap binds per core. Ids 20-23 are the retired legacy playlist words and are used only by the Diagnostic Build's Check summary. **Ship the normal core's `interact.json` without ids 20-23 and keep them in the Diagnostic Build's:** that frees four entries in the release core with no loss of function (a missing id in an old persist file is simply ignored), enough for four user preset slots plus the current positions. Needs the packager to emit a per-variant `interact.json` and the persist-range guard test to cover both. This is better than option (a) "one slot" and avoids option (c), the data-slot write-back this project rejected.

### F8. Writable coefficient store: engine budget and atomicity [READ, EST]
Six stages: 139 clocks of 1,388 (10%). A RAM read adds about one clock per coefficient fetch: roughly +30-60 clocks, so about 190 of 1,388 (14%) [EST]. The engine is idle for over 1,200 clocks per sample, so a **shadow-bank commit when idle** is trivial and cannot tear a coefficient set; the mutant that commits mid-sample must still be in the testbench. The ALM saving from moving the coefficient and state tables out of logic is an estimate (600-1,000); measure it with a synthesis-only run.

### F9. Boosted bass needs a fixed subsonic filter [REASONED, LOW cost]
Any low-shelf boost also boosts DC and rumble below the audio band, which costs headroom and drives small drivers without adding music. Add a **seventh, fixed stage: second-order high-pass about 35-40 Hz, Q0.7, always on**. Fixed coefficients, no table, +23 clocks (about 160-210 of 1,388 total), and it makes SPEECH's rumble reduction real (a shelf at 100 Hz only reduces rumble by a few dB; a high-pass removes it). Since it is a fixed stage it does not touch the control mapping or the table size.

### F10. The soft clipper is now clearly justified, but its multiplier is tiny [READ]
With the EQ on, the pink-weighted preamp deliberately leaves up to +3.6 dB of overshoot (all eight rebuilt presets: worst +3.42 dB), so the soft clipper is a required part of the EQ chain, not an extra (correcting my earlier "narrow reach" remark, which holds only with the EQ off). Its interpolation multiplies a segment slope of at most 128 by a 7-bit fraction: an 8x7 multiply, a few dozen ALMs; use fabric or a DSP half as timing prefers. One M10K for the 257-entry table.

### F11. Dither and the 16-bit result [PENDING]
The dither design stands (final quantiser, TPDF, per-channel, silence-gated), but its position and size depend on the F2 A/B (15-bit slot or 16-bit): with 16 bits there is no drop to dither at the end and the dither only matters where the internal word is reduced. Hold until the `audio16-b602` fit and the loopback run are done.

### F12. De-esser (firmware-only) [READ]
Feasible with no RTL as planned, but note the limits again: the spectrum bank's 1,024-sample window gives about 23 ms lag and window-rate steps, S5's coefficient updates are the same ramped writes as the sliders, and a threshold relative to broadband level is subjective to tune. Keep it behind a flag and out of the default path.

### F13. Tau Omega interface [READ]
`PRST`/`EQST` sections need an entry in `CROSS_PROJECT_INTERFACE.md`, CRC-checked like the theme sections, range validated, and **golden vectors** (preset tuple to stage gains to coefficient codes) shared as data so Omega's response preview and the firmware tables provably agree (the meter golden-frame approach). With 24-bit coefficients (F1) the format carries 24-bit codes.

## 4. DSP and resource budget with everything above

| Item | DSP | ALM [EST] | M10K | Notes |
|---|---|---|---|---|
| Today (shipped) | 20 | 13,207 | 256 | B-606 |
| Gain stage (F2) | +1 (2 with 24-bit input later) | +100..200 | 0 | one block, two independent 18x18 |
| EQ coefficients 24-bit, 64-bit accumulate (F1) | +0..+2 | +50..150 | 0 | confirm by synthesis-only run |
| Writable store, six stages plus subsonic stage (F8, F9) | 0 | -600..-1,000 | +3 | tables out of logic into RAM; 26.6 kb of coefficients |
| Soft clipper table and interpolation (F10) | 0 | +50..100 | +1 | |
| Final quantiser, dither, silence gate | 0 | +60..100 | 0 | |
| **Total after** | **21-24 of 66 (32-36%)** | **about 12,700-13,000 (69-70%)** | **about 260 of 308 (84%)** | |

**On the extra DSP use specifically.**
- *Pros:* DSPs are the cheap resource here (46 free); they take multiplier and accumulator logic out of the fabric, which is where this project's timing violations come from; wider coefficients (F1) are only practical in DSP blocks; two independent 18x18 per block makes the stereo gain stage almost free.
- *Cons:* DSP columns are fixed in a few places and adding blocks near existing DSP-mapped logic caused real congestion once (B-116: 14 vs 11 DSP cost 3.5-4.4 ns); a DSP block used only for a tiny multiply is a poor trade versus a few dozen ALMs; every DSP-packed register has input-register rules (no sync-reset plus enable conflict, fixed latency) that must be tested.
- *Policy:* spend DSP where precision or width demands it (F1, F2), keep tiny multiplies in fabric (F10), run a synthesis-only DSP-and-ALM count before each fit, watch timing on the first fit, and keep `DSP_BLOCK_BALANCING` per instance (KB-045, untried here) as the remedy if a block lands badly.

## 5. Pros and cons of the whole direction

**Pros.** One multiply and one quantiser for loudness and precision; the EQ engine stays far inside its clock budget; macros are composable exactly (stage responses add in dB, 0.0000 dB error measured); presets are data, editable in Omega and resettable; the work reuses the proven engine, models and fit discipline; ALMs fall rather than rise; nothing here touches the decoders.
**Cons and risks.** The voicing, not the engineering, is the real effort and needs listening plus sweeps through the real output chain; coefficient precision (F1) must be fixed first or the Bass slider misbehaves; the EQ datapath widening needs its own model, vectors, mutants and fit; the preset editor needs a data-format commitment shared with Omega; on-device persistence is capped (F7); more stages mean more to verify (each RTL step follows model, bit-exact testbench with mutants, two-seed fit, hardware listening gate).

## 6. Recommendations, in order

1. **Decide F1 now: 24-bit Q2.22 coefficients, uniform, with closed-loop table generation.** Update the EQ model first (`tools/eq_model.py`, `gen_eq_coeffs.py` with a 24-bit mode), then a synthesis-only run for DSP, ALM and accumulator timing. Nothing else in the EQ rework should start before this.
2. Build the **gain stage** as one DSP block (two independent 18x18), EQ input 24 bits via the scaling shift (F2, F4), preamp folded into its target, meters pre-gain (F6).
3. **Writable coefficient store plus the fixed subsonic stage plus the soft clipper** together, so the EQ is rebuilt once (F8, F9, F10), with the state-clear pulse for the recall dip (F3).
4. Ship a **per-variant `interact.json`** (F7) and the `PRST`/`EQST` data with golden vectors (F13).
5. Voice the presets by ear and sweep on the real output (speaker and headphones) before freezing the defaults (F5); cap the low shelf for presets.
6. Run the final quantiser and dither decision **after** the 16-bit A/B result (F11).
7. De-esser last, flagged (F12).

## 7. The open decisions, with my recommended answers

| Decision | Recommended |
|---|---|
| Gain stage position | before the EQ (B) |
| Meters | volume-independent (tap before the gain stage) |
| Ramp and fade lengths | 5 ms ramp, 43 ms fade-in as today |
| EQ input width | 24 bits now (no state change needed) |
| Dither | on by default with the silence gate, after the A/B shows the 15/16-bit result; a Diagnostics switch for the A/B itself |
| Coefficient width (new) | 24-bit Q2.22, uniform |
| Subsonic filter (new) | fixed 7th stage, 35-40 Hz |
| Persistence (D-S05) | per-variant `interact.json` (release drops legacy 20-23), four user slots |
| Recall | gain-dip with state clear (about 10 ms) |
