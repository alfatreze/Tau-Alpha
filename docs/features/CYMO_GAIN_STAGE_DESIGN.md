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
- **DSP multipliers (revised in section 8): two blocks, one per channel**, registered inside the block, both results latched in the same cycle (the original time-multiplexed single DSP is superseded).

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

## 8. Review: use more DSP blocks? (embedded-audio view, 2026-10-07; evidence: B-606 resource ledger, the source of the five biggest ALM users, B-116/KB-045, KB-036)

**Verdict: yes for the multiplies of this stage (two parallel multipliers instead of one time-multiplexed), but DSPs are not the lever for the ALM numbers.** Free: 46 DSP of 66, about 5,270 ALMs, 52 M10K. ALMs are at 71% so area is no longer the constraint; timing margin (hold +0.14 ns) and LAB packing (93%) are.

1. **Two parallel multipliers (recommended change to section 3).** One DSP per channel: the L and R products appear in the same cycle, so the pair is registered together and the torn-sample hazard (review F) cannot happen by construction, with no sequencing state and no L/R mux. A 24-bit by 16-bit multiply does not fit an 18x18 half, so each takes a block in 27x27 mode: 2 blocks of 46 free (4%). The time-multiplexed single DSP I proposed saves one block and costs a small FSM and a mux in logic; with this much DSP headroom the parallel form is the better trade.
2. **Make the DSP do the registering.** A Cyclone V DSP block has input, pipeline and output registers and (in the 27x27 mode used here) no fabric adder: write the multiplier with registered inputs and a registered output in the same clocked block (`(* multstyle = "dsp" *)`, signed operands) so Quartus packs those registers inside the block. That takes the multiplier's carry chain out of the fabric, which is where this project's timing violations have come from (B-109, B-111, B-150). Two traps from this project: no synchronous reset or clock-enable combination on the registers that feed the DSP (the packing then falls back to fabric: KB-036's "clear and load" conflict), and keep the DSP's pipeline depth fixed and documented so the testbench latency is exact.
3. **Where the ALMs actually go (so DSPs would not help there).** The EQ (1,659 ALMs, already 4 DSP) and the spectrum bank (1,213 ALMs, **zero multipliers**: shifts and adds only) are large because their state and tables are declared `ramstyle = "logic"`/`romstyle = "logic"` registers (EQ: 40 state words of 36 bits plus the coefficient and preamp ROMs; spectrum bank: the `lp/slp/acc/mean` arrays). The lever there is moving that storage to MLAB or an M10K block (52 free), not adding DSPs. That is a separate, cheap area item for the EQ rework (C5), worth maybe 1,000+ ALMs [EST, to be measured by a synthesis-only run].
4. **Resampler and FLAC accumulators.** The Cymo resampler (994 ALMs, 1 DSP) and the FLAC LPC unit (1,113 ALMs, 2 DSP) keep their wide accumulator adds in fabric. Cyclone V DSP blocks offer an accumulate mode (up to 64 bits in the accumulator configurations); moving the accumulate into the DSP would save the adder carry chains and improve timing, but both units are fit-closed and hardware-proven: a candidate for the next time they are touched, not now.
5. **The caution from this project's own history.** DSP use is not free of side effects: B-116 found that TAU_BLIT_BLEND's three extra DSP blocks (14 vs 11) competed for placement and routing with an existing DSP-mapped adder in the same region and cost 3.5-4.4 ns of timing. DSP blocks sit in a few fixed columns; adding two next to the audio path is very likely harmless (the EQ's four are already there), but the fit must be watched, and `DSP_BLOCK_BALANCING` per instance (KB-045, docs-verified, never tried here) is the tool if a multiplier is pulled into the wrong place. Budget check: gain stage 2, a future WSOLA correlator 1, a C5 EQ rework 0-4: well inside 46.
6. **A better long-term structure than a separate gain multiplier (for C5).** `eq_biquad` already multiplies every sample by a per-preset preamp. Folding the volume gain into that coefficient (the product of two Q15 values, recomputed once per ramp step) gives one multiply, one wide path and one rounding in the whole chain, and removes the stage in front of the EQ. The catch is the FLAT preset, which today is a literal bit-exact bypass with no multiply; the EQ would always run (unity coefficients for FLAT) and the final quantiser must reproduce today's bits at unity, which the test plan already requires. Recommendation: build the standalone stage now (it decouples this from the EQ rework and can ship earlier), and merge it into the EQ's preamp when C5 rebuilds the EQ.

**Changes to the design above:** section 3 "One DSP, time-multiplexed" becomes "two DSP blocks, one per channel, registered inside the block, both results latched in the same cycle"; section 5 adds a check that the fit report shows the two products packed into DSP blocks with their registers (not fabric) and a testbench check of the fixed pipeline latency; the resource line becomes about 100-200 ALMs and 2 DSP [EST].
