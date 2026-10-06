# Cymo C3 slice 2: the hardware gain stage (design, 2026-10-07)

**Status: DESIGN ONLY. Nothing built.** Parent: `CYMO_OUTPUT_STAGE_SPEC.md` (sections 3 and 6). Read it first: this document assumes its review (F2 first, dither at the real requantisation points, the soft clipper belongs with the EQ rework C5, torn-sample and silence rules). Resource figures are from the shipped fit (`AUDIT_TRAIL.md` B-606).

## 1. What it is

One RTL block that applies the loudness to every sample, replacing the per-sample multiply the firmware does today. It does three jobs that today are three separate pieces of firmware:

| Job | Today (firmware, `fw/pcm_push.h`) | In the stage |
|---|---|---|
| Volume | dB-taper table (Q15), 5 ms ramp, 149 per sample pair, `(x*g + 16384) >> 15` | a target register, a per-sample ramp, the same rounding |
| ReplayGain | folded into the volume target (`rg_target()`, attenuate-only) | unchanged: firmware still computes the target, the stage just applies it |
| Fade-in after any discontinuity | `fade_left` counter, 2048 samples, `(2048-left)>>3` as an 8-bit gain, set at every flush and seek | hardware: every `pcm_flush` starts the fade by itself |

The firmware keeps the policy (which volume, which ReplayGain, when to mute); the hardware does the arithmetic. Firmware no longer touches samples: `cymo_push()`/`pcm_push_pairs()` shrink to "wait for FIFO room, write the raw sample", the CPU saves the multiplies, and the tempo path, the MP3 burst push and FLAC all get the same behaviour from one place.

## 2. Where it sits (the decision that matters)

Today: `decoder -> firmware gain -> pcm_fifo -> [Cymo resampler] -> EQ -> I2S`. Candidates:

| Position | For | Against |
|---|---|---|
| **A. at the FIFO input (what the firmware does now, moved into RTL)** | simplest; same behaviour as today | the FIFO is up to 2,048 samples deep (about 46 ms at 44.1 kHz): a volume change or a mute reaches the speaker 46 ms late; the fade-in is applied to audio that is already queued |
| **B. at the FIFO output, before the resampler/EQ (recommended)** | volume, mute and fade act within about 5 ms; one place for every source; the EQ keeps its headroom exactly as today (volume before EQ) | the hardware meters (spectrum bank, wave meter) tap `fifo_l/fifo_r`, i.e. they currently see audio AFTER the volume; behaviour would change (see decision 2) |
| C. after the EQ | EQ independent of volume | EQ boosts see full-level audio and overshoot far more often; needs the soft clipper first |

**Recommendation: B**, with the stage's output wider than 16 bits (24 bits) feeding the EQ, and one final quantiser (round, dither, later the soft clipper) AFTER the EQ. That makes the dither the only requantisation at the end, as the review required. Consequence: `eq_biquad` needs a 24-bit input port (its state is Q20.16 in 36 bits, so it takes 24-bit input with a different input scaling and no new state), and its FLAT preset can no longer be a literal wire-through: a final quantiser stage always runs. The invariant to keep: **with gain at unity, dither off and EQ FLAT, the output is bit-identical to today** (proved by test, section 5). The 16-bit/15-bit slot (F2) is decided independently by the A/B and only changes the last shift.

## 3. Behaviour

- **Gain:** 16-bit unsigned Q15-style (32768 = unity), up to 65535 allowed (+6 dB) for future use; firmware keeps writing the table values and the attenuate-only ReplayGain target. `out = (x * gain + 16384) >> 15` at 24 bits (the extra 8 bits are kept, not rounded away; the rounding point moves to the final quantiser).
- **Ramp:** the gain moves toward the target by a fixed step per output sample (48 kHz tick), about 5 ms for full scale (137 per sample), steady state costs nothing. A target change never jumps; mute is a target of 0, exact silence once reached; unity skips nothing (the multiply is cheap) but is proven exact.
- **Fade-in:** a `pcm_flush` pulse (already a signal in `mp3_soc.v`) sets the current gain to 0 and ramps to the target over a programmable length (default 2048 samples, 43 ms, register `FADE_LEN`), replacing `fade_left`. Seeks and track starts need no firmware step. A software `FADE_NOW` pulse covers the other discontinuities the firmware marks today (resume, stop/restart).
- **Dither (final quantiser, after the EQ):** per-channel TPDF from two 1-bit draws of separate LFSR taps, off by default and a register bit; silence gate (dither off after 32 consecutive zero input samples so true digital silence stays silent); the host model from `tools/lab/cymo_out_model.py` is the reference, with the two fixes in review D (full +-1 target-LSB span, separate L/R taps).
- **Atomic output:** L and R are updated in the same cycle (review F), so `sound_i2s` never captures a torn word.
- **One DSP, time-multiplexed:** the multiply for L and R runs on consecutive clocks of the 48 kHz tick (1,388 clocks available at 66.7 MHz; the stage needs under 10), one operation per clock per the project rule.

## 4. Interface and safety

- **Registers** (free range `0x168`-`0x1FC`, `docs/MMIO_ALLOCATION.md` to be updated when built): `GAIN_CTRL` (bit 0 enable, sticky, reset 0; bit 1 snap = set current gain to target now; bit 2 fade-now pulse; bit 3 dither enable; read: bit 31 present), `GAIN_TARGET`, `GAIN_STEP`, `FADE_LEN`, `GAIN_CUR` (read-back for the Info page and the tests).
- **Probe-then-adopt:** `hw_gain` read at boot like `hw_cymo`/`hw_a16`. With the bit absent or disabled the firmware keeps its software gain exactly as today (older bitstreams, the release core if it ships without the unit). When the hardware stage is on, the firmware gain is forced to unity, and the Info page shows which path is active: double application (or none) is the one real danger, so there is a single `gain_hw_active` flag consulted by `cymo_push()`, the burst push and the FLAC path, with a host test that fails if both multiply.
- **Reset and track change:** enable is a sticky register reset in the `if (rst)` block (as `cymo_live_en` and `audio16_reg` are); `pcm_flush` also restarts the fade.

## 5. Verification (same discipline as every unit here)

1. **Host model first** (`tools/lab/cymo_gain_model.py` plus a C golden): the settled-gain output equals `pcm_gain_apply()` for every sample and every volume position (proving equivalence with the shipped firmware path), the ramp rate and arrival time, the fade-in curve, mute exactness, the dither statistics and silence gate, and unity/FLAT bit-identity end to end with the EQ model (`tools/eq_model.py`).
2. **RTL + testbench replaying golden vectors**, with mutants: no ramp, wrong rounding offset, snap ignored, fade length ignored, mute not exact, dither bias, torn L/R, silence gate off, enable not reset.
3. **Fit** against the current macro bundle (about 150-250 ALMs and 1 DSP [EST]; the headroom is ample: about 5,270 ALMs, 46 DSP free). Watch timing at the EQ input (the hold margin is +0.14 ns on the shipped fit).
4. **Hardware:** volume steps and ramps with `cymo_loopback.py track` (flagged events, steps, no clicks), mute, fade-in on seek and track change, ReplayGain Off/Track/Album, and the low-volume noise floor against today's -69 dB-at-volume-65 figure (the dither and the wider internal word should improve it); meters behaviour per decision 2.

## 6. Build order

G0 host model and vectors, G1 RTL and testbench with mutants, G2 firmware path (probe, single-owner flag, `cymo_push()` and the burst push shrink, Info row), G3 fit (two seeds), G4 hardware test. Independent of the 16-bit A/B and the EQ rework, but its output quantiser is where the soft clipper (C5) and the F2 shift finally land, so build the quantiser as one block with those two slots reserved.

## 7. Decisions for the owner

1. Position B (recommended), A or C?
2. **Meters:** the hardware spectrum bank and wave meter tap the FIFO output, so with position B they would see audio BEFORE the volume (volume-independent), where today they see it after (they shrink with the volume). The software level meters already read the decoder's PCM before the gain, so today the two families disagree. Volume-independent meters everywhere, or tap after the stage?
3. Ramp time (5 ms as now) and fade-in length (43 ms as now)?
4. Dither on by default with the silence gate, or a Diagnostics-only switch for the A/B first?
5. EQ input widened to 24 bits now (cleanest) or keep a 16-bit stage output until C5?
