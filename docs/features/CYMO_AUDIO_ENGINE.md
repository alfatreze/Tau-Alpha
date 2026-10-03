# Cymo audio engine: audit, findings and proposal

> An independent design review of this document is in [`CYMO_AUDIO_ENGINE_REVIEW.md`](CYMO_AUDIO_ENGINE_REVIEW.md) (B-424). It recommends changes to the clocking (LRCK-pulled resampler), the phase order (firmware tempo first), gapless, the limiter and the audiobook scope. Two corrections are already applied below; the rest await owner decisions.

Status: **audit and design only, 2026-09-29.** Nothing was built, no RTL or firmware changed, no card, VM or fit touched.
Branch `cymo`. Owner scope: MP3 and FLAC audio capability, MP3/FLAC synergies, playback efficiency, architecture
improvements, new FPGA features, the equalizer (keep or replace), and speed-up distortion ("pitch only?").
Everything in that list is now one subsystem, named **Cymo**. Proposed RTL modules use the prefix `cymo_`; existing modules
keep their names until a change actually touches them.

## 0. How to read this document

Every claim carries an evidence label, in the same spirit as `docs/AUDIT_TRAIL.md`:

| Label | Meaning |
|---|---|
| **[HW]** | Measured on a Pocket and recorded in the audit trail (entry id given). |
| **[READ]** | Read directly from RTL or firmware source in this session. True of the code, not necessarily heard on hardware. |
| **[MODEL]** | Computed by a host model written this session (`resamp_model.py` logic, reproduced in section 4). Not a hardware result. |
| **[EST]** | An estimate from arithmetic. Must be replaced by a measurement before it drives a decision. |
| **[OPEN]** | Unknown. A measurement or owner decision is needed. |

Two things I could **not** verify this session: the Analogue documentation on the Pocket's audio output (the
`analogue-pocket-dev` skill checkout is not present in this cloud worktree), and any listening result. Anything that depends
on either is marked **[OPEN]**.

## 1. Executive summary

The playback chain works and its safety invariant holds (late underruns are 0 in every recorded stress and soak run, **[HW]**
B-213, B-146). The hardware decode kernels (MP3 polyphase window, B-309; FLAC LPC, B-386) genuinely moved the CPU budget.
The audit's main finding is that **the weakest part of the engine is not decode, it is everything after decode**. That final
stage was inherited almost unchanged from the upstream template and is mostly software-driven, duplicated between MP3 and
FLAC, and lossy in three specific ways.

Ranked findings (details in section 3):

| # | Finding | Evidence | Effect |
|---|---|---|---|
| F1 | The output is **nearest-neighbour resampled** to 48 kHz. No interpolation, no anti-alias filter. | [READ] `sound_i2s.v`, `pcm_fifo.v`, `eq_biquad.v` | Timing-jitter distortion on every 44.1 kHz and 32 kHz track, and heavy imaging on 22.05/24 kHz spoken-word MP3s. Folding of high frequencies at every speed above 1.09x. |
| F2 | The I2S stage passes **15 effective bits**, `{sign, audio[15:1]}`, i.e. audio shifted right by one. | [READ] `sound_i2s.v:75-91`, instantiated 16-bit signed in `core_game.vh:881` | One bit of resolution and 6 dB of level are lost after the EQ. Needs a hardware A/B before changing (may be intentional headroom). |
| F3 | Volume, fade and 24-to-16-bit reduction are **software, per sample, in two duplicated loops**, with no dither, an instantaneous gain step and a linear (not dB) taper. | [READ] `fw/player.c:7252-7280`, `9692-9726`, `1091-1095`; `fw/flac.c:650-655` | Truncation distortion at low volume and on 24-bit sources, zipper noise on volume steps, two code paths that have already drifted apart once (FLAC volume was missing entirely at one point, per the comment at `player.c:7253`). |
| F4 | **Speed is varispeed**: pitch follows tempo. The distortion at speed is three separate problems (pitch, resampler error, decoder starvation). | [READ] `player.c:833-873` | Each has a different fix. Section 5. |
| F5 | The PCM buffer is only **2,048 samples, 43-46 ms**, and every track change flushes it and fades from silence. | [READ] `pcm_fifo.v`, `player.c:6301` | Any stall over ~45 ms (SD hiccup, UI burst, cover decode) is audible. No gapless playback exists. |
| F6 | The EQ is a **fixed five-band preset ROM**, running on an already-imaged signal, with a hard clamp at the end. | [READ] `eq_biquad.v`, `docs/EQ_DESIGN.md` | Works and is free of CPU cost, but is not user-adjustable and the clamp is audible on boosted presets. |
| F7 | **No ReplayGain, gapless, crossfade or dither exists anywhere.** | [READ] grep of `fw/player.c`, `fw/flac.c`, `fw/flac.h` found none of these features (the only "dither" hits are the UI gradient) | Feature gaps, not bugs. Listed for completeness. |
| F8 | FLAC decode was at ~99% of realtime before the LPC unit; the unit roughly halved worst-case block latency. The 48 kHz FLAC ceiling has **not been re-measured** with hardware LPC. | [HW] B-363, B-386 | Whether 88.2/96 kHz FLAC is reachable is an open, cheap measurement. |

Recommendation in one paragraph: put a **single hardware output stage** (`cymo_out`) between the decoder and the DAC that
owns resampling, gain, ramps, ReplayGain, EQ, limiting and dither, and let firmware push raw decoded PCM only. That fixes F1,
F2 (after verification), F3, F5 (with a deeper buffer), F6 and F4's resampler component at once, and it is the natural home for
pitch-preserving speed. Build order and gates are in section 9.

## 2. The signal path today

```
 SD card (APF bridge, 4 KB refills, blocking)
      |
      v
 ring buffer (compressed bytes)  ->  Helix MP3 (software + HW polyphase window)   \
                                 ->  flac.c   (software + HW LPC MAC unit)         >  int16 PCM, decoder rate
                                                                                  /
      firmware, per sample, two copies of the same loop:
        meters_feed()  ->  volume (Q8 gain, attenuate only)  ->  fade-in after discontinuity  ->  wait if FIFO full
        -> one 32-bit MMIO write per stereo frame (R_AUDIO)
      |
      v
 pcm_fifo.v   2048 x 32, half-full priming, drains at file_rate x speed (32-bit phase accumulator),
              glides to zero on underrun, sticky underrun flag, sample_tick for the spectrum bank
      |
      v   (holds the newest sample; no interpolation)
 eq_biquad.v  5 cascaded biquads x 2 ch, ONE multiplier, fixed 48 kHz tick (free-running clk_sys / 1250),
              preset 0 = true bypass, output clamps
      |
      v
 sound_i2s.v  4-deep dual-clock FIFO -> serializer at 48 kHz; takes the newest sample at each LRCK
              15-bit magnitude slot (see F2)
      |
      v
 Pocket DAC / amp
```

Side taps: the spectrum bank reads `sample_tick` and the FIFO output, so meters see pre-EQ audio. The firmware meters read
the decoder buffer, also pre-EQ (`EQ_DESIGN.md`, "meters will not react to the EQ").

## 3. Findings in detail

### F1. Nearest-neighbour resampling (the largest quality issue, and the one that governs speed distortion)

How it happens **[READ]**:

1. `pcm_fifo.v` advances its read pointer at `file_rate x speed` and *holds* the latest sample in `out_l/out_r`
   (its own header calls this "crude resampling ... worth replacing with a real interpolator later").
2. `eq_biquad.v` samples that held value at a free-running 48 kHz tick. With a preset active this is a second
   nearest-neighbour step. With FLAT the bypass mux forwards the held value.
3. `sound_i2s.v` writes a sample into a 4-deep FIFO whenever the value changed, and the serializer loads whatever is newest at
   each left-channel frame. If the source is faster than 48 kHz the extra samples are simply overwritten.

The consequence is a timing error of up to one *source* sample period on every output sample. A host model of exactly that
behaviour (newest sample wins, compared against the ideal band-limited value after removing the constant half-sample delay)
gives, for a 44.1 kHz source on a 48 kHz DAC at 1.0x:

| Method [MODEL] | 1 kHz | 5 kHz | 10 kHz | 15 kHz | 18 kHz |
|---|---|---|---|---|---|
| Nearest neighbour (today) | 27.7 dB | 13.8 dB | 7.8 dB | 4.5 dB | 2.9 dB |
| Linear interpolation | 54.6 | 26.8 | 15.0 | 8.5 | 5.7 |
| 4-point cubic (Catmull-Rom) | 89.4 | 45.0 | 24.1 | 12.3 | 7.6 |
| 8-tap windowed sinc (crude, un-normalised kernel) | 62.4 | 65.6 | 52.7 | 32.5 | 14.0 |

These are signal-to-error ratios for a full-band tone (higher is better; 16-bit ideal is about 98 dB). Caveats that matter:

- The model assumes a pure tone and measures *total* error, not what is audible. Music is dominated by low frequencies, and
  the error is spread by the hold pattern rather than being harmonic, so real-world audibility is likely much lower than the
  raw numbers suggest. **The owner has listened to the current output for many builds without reporting this as the problem**,
  which supports "audible only on bright or spoken-word material" rather than "broken". Treat the table as a *ranking of
  methods* and a bound, not a listening verdict.
- The sinc row's low-frequency figure (62 dB) is limited by my crude kernel not being normalised, not by the method. A real
  design normalises it. Only the ordering (windowed sinc >> cubic at high frequencies, cubic >> linear >> nearest overall) is
  meaningful.
- At **48 kHz sources and speed 1.0x** the error is zero (rates match). So this affects mostly 44.1 kHz music, which is most
  music.
- Spoken-word MPEG-2 rips (22.05 or 24 kHz, in the Test Album's LibriVox clip class) are the worst case: a ratio of about 2,
  so every sample is held twice and the images land inside the audible band.
- **Real hardware measures worse than this model predicts, and the gap is still unexplained (B-430/B-431/B-467/B-470).** At
  1 kHz the model says nearest-neighbour should give 27.7 dB SINAD; two independent hardware recordings (the original
  phase-accumulator MCLK, and B-457/B-463's replacement dedicated-PLL MCLK) both measured **10.8 dB** -- a ~17 dB gap the MCLK
  fix (real, timing-closed, but a null result for this specific problem, B-467) has now ruled out as the cause. The 24/32 kHz
  integer-ratio cases are worse again: modelled ~23-26 dB, measured ~3.1-3.35 dB. Two RTL-level simulations of the
  `pcm_fifo`->`sound_i2s` clock-domain crossing (behavioural and Intel's own real `dcfifo` model, B-430/B-442) both reproduced
  the *model's* prediction exactly, not the worse hardware number -- and a real-silicon diagnostic (B-470, no JTAG needed)
  then measured the CDC's own update interval directly: min values matched the theoretical `CLK_HZ/rate` almost exactly at
  both 44.1 kHz and 48 kHz, ruling the CDC out too. **Both leading hypotheses (MCLK jitter, the CDC) are now closed by direct
  hardware measurement. The remaining candidate is past the serializer/DAC entirely**, outside this core's RTL. See section 15
  below for a hardware-proven alternative architecture, and its own sub-section on whether this is even worth pursuing
  further without a real listening test.

Speed makes it worse in a second way. Playing at N x raises every source frequency by N, and with no anti-alias filter anything
that ends up above 24 kHz folds back down **[MODEL]**:

| Speed (44.1 kHz source) | Source content above this frequency folds |
|---|---|
| 1.10x | 23.7 kHz (nothing audible) |
| 1.20x | 21.8 kHz |
| 1.50x | 17.4 kHz |
| 1.75x | 14.9 kHz |
| 2.00x | 13.1 kHz |
| 2.50x | 10.4 kHz |

Real music carries little energy at those frequencies, so folding is a smaller effect than the timing error, but cymbals, air
and sibilance at 2x are exactly where it lands. The firmware comment already says the drop is "audible" above 48 kHz
(`player.c:845`).

**Fix:** a real fractional resampler (section 6, module `cymo_resamp`), with its cutoff scaled by the speed ratio when
downsampling. It also makes 88.2/96 kHz FLAC *correctly* playable if the decoder can keep up (F8), because it can decimate 2:1
with a proper filter instead of aliasing.

### F2. The DAC slot carries 15 bits **[READ]**

`sound_i2s.v` builds each channel word as `{sign, audio[CHANNEL_WIDTH-1 -: 15]}`. For the 16-bit signed instantiation at
`core_game.vh:881` that is `{a15, a15, a14, ... a1}`, which is the input arithmetically shifted right by one. Effect: the least
significant bit is discarded and the output is 6 dB quieter than a pass-through would be.

The Pocket documentation specifies 16 data bits per channel (plus 16 spacer bits) of signed audio, so passing all 16 is within the spec (skill `hardware-video-audio-input.md`). This module is inherited from the Analogue example (`Copyright (c) 2022 Adam Gastineau`), and its header documents the 15-bit
magnitude slot, so it may be deliberate headroom for the Pocket's amplifier. I am **not** recommending an edit. I recommend a
one-line A/B: pass the full 16 bits and compare level and headroom on hardware, keeping the change behind a macro like every
other RTL experiment here. If the amplifier clips, the answer is to keep the shift and say so in the header.

### F3. Volume, fade and bit-depth reduction are software, duplicated, and undithered **[READ]**

- Two hand-written push loops (`player.c:7219-7280` FLAC, `9692-9726` MP3) each apply `(l * vol_gain) >> 8`, then the
  fade, then wait on the FIFO, then write. The comment at `7253` records that the FLAC path shipped with no volume at all
  for a while because the two loops had diverged.
- The gain is applied by an arithmetic right shift, which floors. At low volume that is truncation with no dither, so the
  error correlates with the signal.
- `vol_apply()` maps volume linearly, `gain = volume * 256 / 100` (`player.c:1095`). Perceived loudness is closer to
  logarithmic, so most of the useful control range is squeezed into the bottom of the slider.
- Gain changes instantly. There is no ramp, so a step on a loud passage is a small click (zipper noise).
- 24-bit FLAC is reduced with `v >>= bps - 16` and a clamp (`flac.c:650-655`): truncation, no dither, and the extra bits are
  thrown away *before* volume and EQ, even though the EQ's internal state is 36-bit with 16 fractional bits and would
  happily take 24-bit input for free.
- The order is decoder, volume, EQ, output. Volume before the EQ means the EQ boost can push a quiet signal back toward
  full scale and clamp, while attenuating after would leave the EQ its full-scale headroom.

CPU cost of the software path is small **[EST]** (a few multiplies and one MMIO read and write per sample, on the order of a
couple of percent of a 66 MHz CPU at 44.1 kHz), so the argument for moving it is **quality and one code path**, not CPU.

### F4. Speed handling, and what the "distortion" actually is

Current behaviour **[READ]** (`player.c:833-873`): `pcm_rate_apply()` scales the FIFO drain rate by a rational
`speed_num/speed_den`, so **tempo and pitch move together** (varispeed, like a tape machine). Positions and durations are
derived from file position, so they stay correct.

"Pitch only?" can mean two things, so this document answers both. **My reading of the question:** "is the distortion of
speeding up just the pitch change, and can that be separated from tempo?" Answer: no, three separate effects are mixed:

| Effect | Cause | Present at | Fix |
|---|---|---|---|
| Pitch rises with speed | varispeed by design | any speed != 1.0x | pitch-preserving time stretch (section 7) |
| Resampler error and folding | nearest neighbour, no anti-alias | any speed, worse when high | `cymo_resamp` (F1) |
| Decoder starvation (micro-stutter) | decode must run N x faster | speeds above the CPU budget | more decode headroom (HW kernels), independent of pitch handling |

Measured history for the third row **[HW]**: 1.25x stuttered before the MP3 window unit; 1.75x plays cleanly with it (B-309),
2.0x clean in later owner tests (B-328). So at high speed the starvation problem is largely solved for MP3, and what remains
audible is the first two rows, which are the resampler and pitch. FLAC is the exception: it has no speed headroom at all
because 1.0x already used ~99% before the LPC unit (B-363).

An important asymmetry that is easy to miss:

| Mode | Decode throughput needed |
|---|---|
| Faster tempo (varispeed or pitch-preserving) | **N x** the decode rate. Stretching does not reduce this. |
| Slower tempo | less than 1x (free headroom) |
| **Pitch shift at constant tempo** | **1x**, no extra decode |

So pitch-only shifting is the cheapest of the three to *run*, even though it needs the same stretch primitive.

### F5. The buffer is shallow and every track change hard-cuts **[READ]**

The FIFO holds 2,048 stereo samples (46.4 ms at 44.1 kHz). It is half-primed at track start (cushion of about 23 ms), glides to
zero on underrun, and `pcm_flush()` at every track load empties it and arms a 2,048-sample fade-in. There is therefore always
a silent gap equal to the load time, and no gapless or crossfade. The audio-first load spec
(`docs/features/AUDIO_FIRST_TRACK_LOAD_SPEC.md`) measured a 3.7 s load of which 2.8 s was cover decode **[HW, earlier build]**;
the TIM1 reader has since cut cover time to about 90 ms **[HW]** (B-330), so what remains is head read, prefill and decoder
warm-up. The Info page's LOAD MS row already reports those.

A deeper buffer is the single biggest lever on audible glitches from *non-decode* stalls (SD refill latency, UI bursts,
art). It is also a precondition for gapless playback.

### F6. Equalizer: what exists and what it costs **[READ], [HW]**

Five biquad bands per channel (low shelf, three peaks, high shelf), eight ROM presets (FLAT, BASS, ROCK, POP, JAZZ, CLASSICAL,
VOCAL, TREBLE), one time-multiplexed multiplier, coefficients Q2.16, state Q20.16 (36-bit), accumulator 58-bit, all
bit-exact against `tools/eq_model.py`. CPU cost is zero. Hardware use is 116 of 1,250 clocks per sample **[HW]**
(`PERFORMANCE.md`). No click on preset change **[HW]** (2026-08-05, `EQ_DESIGN.md`).

Weaknesses, in order of importance:

1. **Presets only.** The user cannot adjust a band. This is the largest functional gap versus what a music player user
   expects.
2. **It operates after the hold**, so the EQ sees imaged audio and, with a preset active, adds a second nearest-neighbour step
   (F1). `EQ_DESIGN.md` argued this was harmless; the F1 numbers say the resampler is the bigger contributor.
3. **Hard clamp.** Presets are loudness-matched, not peak-matched, so a boosted band on hot material exceeds full scale by up to
   about 3.6 dB and is clipped. There is no limiter.
4. **Free-running 48 kHz tick** against the DAC's own clock, so the EQ output is re-sampled again at I2S with a beat between
   two nominally-equal clocks (size not measured, **[OPEN]**).
5. **Meters do not follow it** (known, documented, accepted).
6. **Input is 16-bit**, wasting the internal precision.

## 4. Playback efficiency

What is measured **[HW]**:

| Item | Result | Source |
|---|---|---|
| MP3 filterbank share of decode | 22% at 1.0x (was 55-59%) | B-087, B-309 |
| MP3 speed with clean playback | 1.75x, 2.0x in later owner tests | B-309, B-328 |
| FLAC decode at 1.0x, software | ~99% of realtime (`t_pct`), channel 1 costing 63-66% vs channel 0's 33-36% | B-363 |
| FLAC worst single LPC call | 11 ms software vs 5-6 ms hardware | B-386 |
| FLAC microstutter | present on the software A/B build, absent with hardware LPC on the same bitstream | B-381 |
| Underruns | 0 late in every stress, soak and ENDURANCE run | B-146, B-213 |
| SD sequential read | 736 KB/s, never the FLAC bottleneck | `FLAC.md` |
| Cold-code cost on the audio path | ~1.7% of one audio frame | B-202, B-213 |

What is **not** known, and should be before any large decision:

- **A trustworthy CPU headroom number.** The Info page's CPU LOAD reads 100% in every state (`CURRENT_STATUS.md`), which cannot
  be true while 1.0x MP3 decodes with room to spare. The firmware does compute an idle share (`fl_idle_pct`, latched once a
  second at `player.c:5538`, accumulated in both push loops' FIFO-full wait), yet it reports zero idle. Either the FIFO-full
  wait is genuinely never reached, or the accounting has a gap. **[OPEN]** and cheap to resolve, because a real headroom metric
  is the yardstick every other proposal here needs.
- Hardware-LPC `t_pct` for FLAC. Only the software-path percentages exist.
- The MP3 IMDCT/AntiAlias/Dequantize split. `SR_T_DECPROF2` exports it (B-353) but I found no recorded reading in the audit
  trail. `MP3_IMDCT_KERNEL_SCOPING.md` estimates 6-9% CPU saved, **[EST]**.
- Decoder ms per frame and minimum FIFO fill per track, which together say how close each format runs to the edge.

Structural efficiency notes **[READ]**:

- The push loop does one MMIO status read and one MMIO write per stereo sample. At 44.1 kHz that is under 0.2% of bus
  cycles, so it is not a bottleneck, but reading FIFO free space once per block instead of once per sample would remove the
  per-sample status read and simplify both loops.
- Refills are blocking 4 KB reads, and a far read on the streaming slot corrupts its fragment cache (documented at
  `player.c:7311-7320`). That constrains any new feature that wants to read a different file mid-playback, for example a
  next-track pre-read for gapless. It must read the *same* slot sequentially or wait.
- Meter drawing shares the decode budget, and a meter that overspends causes audible jitter (the scope regression, B-296 to
  B-299). `meter_afford()` is the existing guard.

## 5. MP3 and FLAC: shared code and real synergies

Today the two formats share: the compressed-data ring and its refill, the FIFO push, the meters feed, volume and fade (by
copy), and the track/library layer above. They do **not** share the hardware kernels, and shouldn't be forced to.

| Opportunity | What it unifies | Verdict |
|---|---|---|
| **One output stage** (`cymo_out`, section 6) | volume, fade, ReplayGain, dither, limiter, bit-depth reduction, resampling, currently two software copies | **Do it.** This is the whole point of Cymo. |
| **One firmware sink** (`cymo_push()`) | the two duplicated push loops, until the RTL stage exists | Do first, it is small and removes the drift risk. Both decoders emit interleaved int16. |
| **One resampler** for both formats | MP3's 8-48 kHz and FLAC's 8-96 kHz onto the DAC's 48 kHz | Do it (F1). |
| **One MAC datapath** for polyphase window + LPC + EQ | `tau_mp3_poly.sv`, `tau_flac_lpc.sv`, `eq_biquad.v` each own a multiplier | **Do not merge the RTL.** DSP blocks are not scarce (19 of 66, `CURRENT_STATUS.md`), only one decoder runs at a time but the EQ and spectrum run concurrently with it, and each unit is timing-proven and mutation-tested. Merging saves control ALMs at the price of re-proving three units. Unify the *register convention* (sticky index+data, read-to-ack) instead, which already matches. |
| **Shared bit reader** | FLAC's Rice/unary reader and MP3's Huffman | Different shapes (`FLAC_BITREADER_KERNEL_SCOPING.md`, superseded by the LPC finding). Not now. |
| **Shared gapless / track-boundary logic** | encoder delay/padding (MP3 LAME tag) and FLAC sample counts into one "trim" primitive | **Not small (corrected, B-424):** it needs a second audio data slot (audio comes from one slot today), a non-blocking track load, decoder handoff without a flush, and encoder-delay trimming. The deeper buffer helps but is not the gate. Treat as a separate large item; see `CYMO_AUDIO_ENGINE_REVIEW.md` C2. |
| **Shared tag pipeline for ReplayGain** | ID3 `TXXX` and LAME replaygain field, FLAC `VORBIS_COMMENT` | Small firmware job, or precomputed by Tau Omega into the library index so the player reads one number per track. |

A synergy worth stating separately: **the resampler is what makes hi-res FLAC honest.** Today the ceiling is `FLAC_MAX_RATE = 48000`
because 88.2/96 kHz measured 150-180% of realtime with the software decoder (`FLAC.md`, `player.c:4399`). The hardware LPC unit
changes that arithmetic and the Diagnostic Build's ACCEPT ALL RATES toggle exists to measure it, but if it ever lands, a 96 kHz
stream drained at 96 kHz and picked at 48 kHz aliases. With `cymo_resamp` doing a proper 2:1 decimation the same hardware plays
it correctly.

## 6. New FPGA features (Cymo modules)

Budget context **[HW]** (`CURRENT_STATUS.md`): the current bitstream, with the RAM shrink and the Talos `glyphbuf` fix, fit at
M10K 240 of 308 (68 free), DSP 19 of 66, with the ALM crisis resolved. The earlier FLAC LPC fit was at 98% ALM (B-378), so
ALMs were then the resource to watch, but that figure predates the T2-00 fix (about 6,500 ALMs freed), so re-read it from the current fit report first (section 11); DSP and, after the shrink, M10K have room.

### 6.1 `cymo_resamp`: fractional polyphase resampler (replaces the zero-order hold)

Sits where `pcm_fifo`'s drain currently is. Input: samples at `file_rate`. Output: one sample per 48 kHz tick, computed from the
fractional read phase. Two selectable kernels:

- **Cubic (4 taps)** for ratios at or below 1 (normal playback and slowdown): cheap, no coefficients ROM beyond a few
  constants, and it removes most of F1's error (table above).
- **Windowed-sinc polyphase (16 taps, 32 phases)** with a coefficient bank selected by ratio, so the cutoff tracks
  `24 kHz / (file_rate x speed)` when downsampling. Coefficients: 32 x 16 x 18 bits is about 9 kbit per cutoff bank, so a few
  banks fit in one M10K or in MLAB **[EST]**.

Cost: 16 taps x 2 channels = 32 MACs per output sample against about 1,250 clocks available, one DSP, tiny **[EST]**. Also
becomes the single owner of the 48 kHz tick, so `eq_biquad`'s free-running `divctr` and the beat in F6.4 disappear: the EQ runs
on the resampler's tick.

Verification, in this project's usual order: Python golden model first (extend the F1 model with the real kernel and measure
SNR across tone frequency and ratio), bit-exact testbench against it, mutation test (coefficient-index off by one, wrong phase
rounding, missing anti-alias), then fit on two seeds, then a hardware A/B on the Test Album's 44.1, 48 and 22.05 kHz tracks.

### 6.2 `cymo_out`: output stage (gain, ramps, ReplayGain, dither, limiter)

One RTL block after the EQ (or fused with it), replacing the software volume and fade:

- **Log-tapered, ramped gain.** A Q4.12 gain register, a target/step register pair, per-sample linear ramp over about 5 ms.
  Removes zipper noise, makes fade-in after a discontinuity the same mechanism as a volume change, and lets firmware stop
  touching samples.
- **Separate ReplayGain register** applied ahead of the EQ, so a boost cannot push past full scale unnoticed.
- **TPDF dither** with a small LFSR when reducing the internal 24-bit-capable path to 16 bits. About 30 ALMs **[EST]**.
- **Soft clipper** in place of the EQ's hard clamp (**decided by the owner, 2026-09-29; a look-ahead limiter was the alternative and is
  not chosen**). A small lookup table in the output path that leaves everything below a knee (about 90% of full scale) untouched and
  rounds the rest smoothly toward full scale. No state, no look-ahead delay, almost no logic. It gives the loudness-matched presets safe
  headroom, and it must be verified to be exactly transparent below the knee (a bit-exact test on a ramp of sample values) and to be monotonic and never exceed full scale above it.
- **24-bit input path.** Firmware pushes up to 24 bits (two writes or a second register). `flac.c`'s truncation goes away, and
  the EQ's 36-bit state finally receives the precision it was designed around.
- **F2 decision** lives here: 16-bit versus 15-bit slot, behind a macro, decided by the hardware A/B.

Order after Cymo: `resample -> ReplayGain -> EQ -> limiter -> volume ramp -> dither -> I2S`.

### 6.3 Deeper PCM buffer

Raise `pcm_fifo`'s `AW` from 11 to 13, giving 8,192 samples, 186 ms at 44.1 kHz **[EST]**. The current 2,048 x 32 memory uses
about 8 M10K blocks **[EST]**, so 8,192 would use about 32, comfortably inside the 68 free after the shrink but not free of
cost, and it competes with the alpha-blend and other pending consumers, so decide it against the block-RAM ledger.
The alternative is an SDRAM- or PSRAM-backed PCM ring with a small DMA, which gives seconds of buffering at the price of a new
bus master and SDRAM contention (the reason this project has spent so long on that arbiter). Recommendation: **M10K first**.

**Coupling to fix first (review C4):** `pcm_fifo.v` sets `PRIME = DEPTH >> 1`, so `AW=13` would delay every track start by about 93 ms unless `PRIME` becomes its own parameter, and `player.c:5773-5774` hard-code the 2048-entry depth in `METER_STOP`/`METER_GO`. Make depth one shared constant, then change `AW`.

Benefits: rides through SD refill latency and UI bursts, absorbs part of the load gap on track change, and is the enabling piece
for gapless. It also keeps the existing priming and glide logic unchanged, which is a strong argument for the simple option.

### 6.4 `cymo_stretch`: pitch-preserving time stretch (section 7)

Speech-tuned WSOLA for audiobook tempo (section 7). Firmware first; this hardware correlator is built only if the measured firmware cost is too high. It depends on 6.1 and, for headroom, 6.3.

### 6.5 Programmable EQ (section 8)

Replace the preset ROM with a **gain-indexed coefficient ROM** for a 10-band graphic EQ. No new datapath.

### 6.6 What not to build

- **An indexed or lookup-table framebuffer-style shortcut for audio.** Not applicable.
- **A phase-vocoder (FFT) stretcher.** The project has decided against an FFT (`PHASE_F_SPEC.md`), and WSOLA needs none.
- **A parallel FIR array for a linear-phase EQ.** A 512-tap FIR at 2 channels needs about 1,024 of the 1,250 clocks per sample
  from one MAC, which is nearly the whole budget for little audible gain on a handheld.
- **44.1 kHz native I2S to the Pocket's DAC.** Not allowed: the Pocket documentation states the audio bus is signed 16-bit stereo at exactly 48 kHz and that sample-rate adjustment is not permitted (skill `hardware-video-audio-input.md`; agg23's Sound wiki agrees). The resampler is therefore *required* for 44.1 kHz material, not optional. A 44.1 kHz rate remains possible only on the cartridge sink, which is our own I2S.

### 6.7 Analog loopback measurement (decided: part of the plan, owner will capture)

Purpose: every quality claim in this document so far is a model or a code reading. This is the only way to show what the Pocket's real analog output does, and to prove
each later phase helps. It is the first item of C0(f), before any Cymo RTL, so there is a baseline to compare against.

**Setup (owner):** Pocket headphone jack to a computer's line-in or a USB audio interface with a 3.5 mm cable; record at 48 kHz, 24-bit if the interface allows, with the
computer's input level set once and never changed between runs. The comparison that matters is *before versus after a change*, so the Pocket's own amplifier need not be perfect.
Use the same volume setting, headphone load (or none) and EQ FLAT for every run, and record the build name with each capture.

**Status (B-427): built and self-tested.** The owner has a USB audio interface. `tools/lab/cymo_loopback.py` writes the test files (`gen`), analyses a recording (`analyze`, tone or sweep) and compares two runs (`compare`); `selftest` and `sim/test_cymo_loopback.py` (in `make test-host`) check it against known signals. The 22.05 kHz files and the sweep are untested on the player. Run order is in the script's header.

**Test material (built):** lossless FLAC files, so the decoder cannot be the variable:
| File | Content | What it shows |
|---|---|---|
| tone 1 kHz, 5 kHz, 10 kHz at 44.1 kHz | pure sine, about -6 dBFS, 10 s each | image and alias tones from the nearest-neighbour resampler (F1) |
| same three tones at 48 kHz | identical to above | control: the resampler should add nothing at 48 kHz |
| tone at 22.05 kHz source | 1 kHz and 5 kHz, 22.05 kHz FLAC (or MP3) | the worst imaging case, speech-rate material |
| swept sine 20 Hz to 20 kHz at 44.1 kHz | log sweep, 20 s | frequency response and where distortion appears |
| full-scale ramp and -3 dBFS and 0 dBFS bursts | short, repeated | level accuracy (F2, one bit and 6 dB), clipping behaviour (soft clipper) |
| silence | 10 s of zeros | noise floor and idle behaviour |

**Analysis (built; the wording below is the original plan, the script's header is authoritative):** a small host script, working name `tools/lab/cymo_loopback.py`, that reads a recording and reports, per tone: level, the strongest
non-harmonic component and its distance in dB from the tone (image level), THD+N, and for the sweep the frequency response; it prints one table per capture and a difference
table between two captures (the baseline and a later build). It uses only the Python standard library and a WAV reader plus an FFT written for the purpose, or an installed
numeric library if present; the choice is made when building it.

**Acceptance thresholds** (proposed, to be fixed after the baseline is captured, because until then there is no number to compare with): after the resampler, image tones at 1, 5
and 10 kHz are at least a stated number of dB lower than the baseline, and no worse at 48 kHz; the FLAT path level is unchanged unless the 15-bit slot A/B (F2) deliberately changes it;
the soft clipper leaves a -6 dBFS tone identical and never exceeds full scale on the 0 dBFS burst.

**Validation of the analyser itself:** on synthetic signals it reproduces the plan's own prediction: nearest-neighbour 44.1 to 48 kHz reads 27.7 dB SINAD with images at 4,899.9 and 2,900.4 Hz, cubic interpolation reads 85.6 dB, and a clean 16-bit tone reads 86 dB, so a real capture is judged against known numbers.

### 6.8 Time-resolved tone tracking: did the pitch change, and is anything glitching? (B-511)

**Why this exists.** By B-499 the Cymo investigation had gone through four rounds of "owner listens, I read the RTL, I fix what the source suggests" (the borrowed tick, the missing reset, the ungated push,
the half-latched gate), and every round was judged by ear, comparing ON against ON. Nothing had ever measured whether the *same* tone is stable with Cymo **OFF**, which is the baseline the whole question
depends on, and the owner reasonably doubted their own earlier attention. `analyze` cannot settle it: it is one 1.4 s snapshot at 0.7 Hz resolution, so it cannot show a pitch that steps between toggles, drifts,
or wobbles, nor a click that comes and goes.

**The tool.** `python3 tools/lab/cymo_loopback.py track rec1.wav rec2.wav ... --freq 1000` demodulates the tone in overlapping 50 ms blocks and reports, per recording, the frequency (from the phase slope between
blocks: about 0.001 Hz resolution at a healthy signal level), the level, and a per-block SINAD from an exact weighted least-squares sinusoid fit, then prints one comparison row per recording. Blocks are flagged
as events when the frequency moves more than `--fthr` Hz (default 0.3) off the median, the level moves `--lthr` dB (0.5), or the SINAD drops `--sthr` dB (6) below its median. `--csv` writes the frequency series.
Pure Python, about a second per ten seconds of 48 kHz audio.

**How it was validated** (so a clean report means something): a synthetic 1000.37 Hz tone is recovered to 0.0000 Hz error (std 0.0001 Hz) with block SINAD 66.2 dB against a true 66; a 2 Hz step, a +-2 Hz 3 Hz vibrato,
six 2 ms click bursts and an 8 ms dropout are each detected; `sim/test_cymo_loopback.py` (in `make test-host`) runs these, and 4 of 4 deliberately broken versions of the maths (frequency sign, hop length, residual,
level) fail it. Two instructive bugs were found while building it: a rectangular window leaks about -40 dB of a fractional-cycle tone into its own residual, and even a Hann-windowed demodulation leaves ~1e-6 of the
tone's image, more than a 66 dB noise floor, so the residual subtraction went negative in 60% of blocks; the exact least-squares fit fixed both.

**Reading the numbers.** The absolute frequency includes the offset between the Pocket's DAC clock and the interface's ADC clock (tens of ppm, a few hundredths of a Hz at 1 kHz). That offset is the same in every
recording, so compare recordings by their *difference*. One cent is 0.58 Hz at 1 kHz; a steady shift under about 0.3 Hz (half a cent) is below what anyone hears as a pitch change when two tones are heard in turn, so
a table of medians within that spread means the signal is stable and what is being heard is something else.

**The protocol** (same capture chain as 6.7: headphone out to interface line-in, WAV 48 kHz, identical gain, Pocket volume and EQ FLAT for every file; start recording, start playback, record about 12 s of steady tone):

| Recording | Core state | File played | What it answers |
|---|---|---|---|
| `a_off_1.wav`, `a_off_2.wav` | Cymo **OFF**, two separate plays | `tone_1k_44100.flac` | The baseline: is the tone already unstable with Cymo off, and how reproducible is the chain? |
| `b_on_1.wav`, `b_on_2.wav`, `b_on_3.wav` | Cymo **ON**, toggled OFF and ON again between each; note the `STALE` count read from the Info page right after each | `tone_1k_44100.flac` | Does the pitch differ between toggles, and does `STALE` correlate with it? |
| `c_48_off.wav` | Cymo OFF | `tone_1k_48000.flac` | Control: a 48 kHz source needs no resampling at all |
| `c_48_on.wav` (optional) | Cymo ON | `tone_1k_48000.flac` | **Expected to be wrong**: the resampler is fixed at 147:160 for 44.1 kHz input and mis-resamples anything else by design. A shift here is not a defect |
| `s_off.wav`, `s_on.wav` | OFF, ON | `silence_44100.flac` | The "constant noise": does the noise floor change with Cymo on? (`analyze` on each reports the floor) |

```
python3 tools/lab/cymo_loopback.py track a_off_1.wav a_off_2.wav b_on_1.wav b_on_2.wav b_on_3.wav c_48_off.wav --freq 1000
python3 tools/lab/cymo_loopback.py track c_48_on.wav          # no --freq: the shifted tone is outside the +-1% search window
python3 tools/lab/cymo_loopback.py analyze s_off.wav ; python3 tools/lab/cymo_loopback.py analyze s_on.wav
```

**How to read the outcome** (decided in advance so the result is not re-interpreted afterwards):

| Result | Meaning | Next step |
|---|---|---|
| The two OFF recordings differ from each other about as much as the ON ones | The shift is not Cymo: it is in the base path (decoder timing, the FIFO drain, the clocks, or the analog chain) | Stop Cymo RTL work; investigate the base path with this same tool |
| OFF steady, ON medians differ between toggles by more than ~0.1 Hz | The start-up phase problem is real and still present | Redesign the hand-off (derive the resampler's start from the FIFO's own tick rather than a second free-running one); do not add another gate |
| ON steady in frequency but SINAD events or level events appear | Occasional sample slips or repeats rather than a pitch shift | Correlate event times and counts with the `STALE` readings |
| Everything steady and clean | The signal does not move; the difference heard is not a pitch change (the resampler's different image spectrum or level are the candidates) | Compare spectra with `analyze` rather than chasing pitch |

## 7. Speed and pitch: options and recommendation

Definitions used here: **varispeed** = tempo and pitch both scale (today). **Tempo** = tempo scales, pitch unchanged.
**Pitch** = pitch shifts, tempo unchanged.

| Mode | Mechanism | Extra decode? | Extra hardware | Quality risk |
|---|---|---|---|---|
| Varispeed (today) | scale the read rate | N x for N > 1 | none (fix the resampler) | none, tape-like character |
| Tempo (keep pitch) | time-stretch by N | **N x**, same as varispeed | `cymo_stretch` + `cymo_resamp` at ratio 1 | stretch artifacts, worst on dense polyphonic music, best on speech |
| ~~Pitch (semitones)~~ | resample by ratio `p`, then stretch by `1/p` | **1x** | both | **Dropped for audiobooks (owner, 2026-09-29).** Kept here for the record only. |

**Owner decision, 2026-09-29: the main use is audiobooks, so the target is speech that sounds correct at a different speed.**
That settles the choice:

- **Chosen: pitch-preserving tempo change (WSOLA, speech-tuned).** A narrator at 1.5x should still sound like the same voice.
  Varispeed raises the pitch (the chipmunk effect), which is exactly what "sound correct" rules out.
- **Dropped: the semitone Pitch mode.** Audiobooks have no use for it. It is removed from the plan (C7 becomes Tempo only), which
  also removes a whole set of ratio and interaction cases to test.
- **Kept only as a fallback: varispeed** (today's behaviour) for music and for builds where the stretch path is absent
  (`CYMO_READY()` false), so an old bitstream or a failed probe keeps working.
- Order of work: fix the resampler first (6.1) because it improves every speed, then add Tempo.

Why WSOLA is the right method for this material, not just a default:

- **Speech is close to periodic.** Voiced speech has a pitch period of roughly 2.5 to 12 ms (about 80 to 400 Hz). A WSOLA search of
  about +/-10 ms therefore always spans at least one full period, which is what lets it splice grains without an audible seam.
- **It is a time-domain method**, so it needs no FFT (the project has decided against one) and adds no phase-vocoder "phasiness",
  which is the usual complaint on speech.
- **Speech is the easy case.** The known weakness of WSOLA is dense polyphonic music, which this feature no longer has to serve well.
- **The workload is light.** Audiobooks are usually mono or low-bitrate MPEG-2 (22.05 or 24 kHz) or 44.1 kHz mono. Mono halves the
  stretch work, and low-bitrate mono is also the cheapest thing the decoder does, so the N x decode requirement is far easier to meet
  than for a stereo music file **[EST, to be measured on the LibriVox clip]**.

Design choices specific to speech:

| Choice | Setting | Reason |
|---|---|---|
| Analysis window | 20-25 ms, 50% overlap | long enough for one to two pitch periods, short enough to follow phonemes |
| Search range | about +/-10 ms | covers the pitch periods above |
| Correlation input | mono mix, decimated to about 5.5 kHz whatever the file rate | cost becomes independent of sample rate; a short full-rate refinement (a few samples either side) fixes the fine alignment |
| Speed range and steps | 0.8x to 3.0x in 0.05x steps | listeners use fine steps on speech; above about 2.5x needs silence handling to stay intelligible |
| Pause shortening | **Included (owner, 2026-09-29).** See the subsection below | the biggest time saved per listener on audiobooks, and it falls out of the same framework |
| Position and bookmarks | unchanged | position is derived from file position, so resume points stay exact (`player.c:859-866`) |

Pause shortening (included):

- **What it does.** When the narration has a pause longer than a threshold, the stretch stage plays only part of it, so 1.5x on a
  slow reader saves more than 1.5x. Speech itself is only stretched by the tempo setting, never cut.
- **Detection.** A short-term energy envelope of the same decimated mono signal the WSOLA search already computes, so it costs almost
  nothing extra. Threshold is relative to a tracked noise floor (a slowly-updated minimum of the envelope), not a fixed level, because
  recordings differ in room noise and MP3 quantisation noise. Hysteresis stops it flickering on breaths and quiet consonants.
- **Rules that protect the listening experience** (all tunable constants, defaults proposed, to be set by listening):
  - a pause is only shortened if it lasts longer than about 250 ms;
  - it is shortened to a fraction of its length, never below a floor of about 120 ms, so sentence and paragraph rhythm survives;
  - a single cut is capped (about 700 ms removed), so a deliberate long silence such as a chapter gap is shortened, not erased;
  - the removed span is taken from the **middle** of the pause, with a look-ahead so a word onset is never clipped;
  - the join uses the same overlap-add crossfade as the WSOLA splice, so it is not audible as a click.
- **Setting.** **Off (the default), Small, Medium, High** (owner, 2026-10-03; renamed from Gentle/Normal/Strong), stored as one small persist word alongside the tempo
  setting. Active only in the audiobook tempo mode, never for music. Measured starting values from the host model (B-536, `tools/lab/cymo_tempo_model.py` `PRESETS`; owner listened and approved the three presets): Small = pauses over 400 ms, keep 60% (at least 200 ms), cut at most 400 ms, detector 6 dB over the noise floor; Medium = 250 ms / 40% / 120 ms / 700 ms / 9 dB; High = 150 ms / 25% / 80 ms / 1,000 ms / 12 dB. Saved on the 331 s Twain clip: 3.0% / 6.2% / 9.8%. The noise floor is the 5th percentile of the envelope over +-4 s (not the window minimum, which sits on digital silence in an MP3).
- **Position and time.** Position is derived from file position, so the resume point stays exact. The remaining-time display will
  drop faster than real time while pauses are being skipped, which is correct, and should be labelled or smoothed so it does not
  look like a bug **[to decide with the UI]**.
- **Decode cost.** The decoder must still decode the skipped audio (MP3 needs every frame for its overlap and bit reservoir), so this
  raises the input consumption rate exactly as a faster tempo does. Silent frames are the cheapest frames to decode, so the extra load
  falls where the decoder has the most spare time **[EST, to measure]**.
- **How to judge it.** On the Test Album's LibriVox clip: seconds saved at each setting, count of clipped word onsets (must be zero,
  checked by comparing the energy in the first 30 ms after each cut point against the same words played without shortening), and an
  owner listening pass for naturalness.

Where it runs, given the project's budget rules: the correlation search on a 5.5 kHz decimated signal is about 110 samples x 110
lags, roughly 12,000 multiply-accumulates per 10-15 ms hop **[EST]**, which is on the order of 5-10 million cycles a second in
firmware, or under about 15% of a 66 MHz CPU. That is small enough that the **first implementation can be firmware in cold code**, not new RTL, provided the decoder headroom (the C0 metric) confirms it. A hardware correlator is then a
measured follow-up only if the firmware version costs too much, which keeps this inside D-C04 (probe-gated, old bitstream unchanged) and
inside the 6.5-12 KB firmware heap limit (section 11). Both figures are estimates until measured.

**Measured (B-549, instruction count of the real fixed-point core built for rv32im and run under `tools/rv32sim.py`):** `fw/wsola_core.h` costs about 130,600 instructions per grain, i.e. 11.25 million instructions per second of output (1.56 M multiply-adds per output second, about 7 instructions each including the rest of the grain), which is 16.9% of the 66.7 MHz CPU at one instruction per cycle and probably 20-25% with real load-use and multiply latencies; code 4.8 KB, stack about 2.3 KB, state 2 KB per channel pair. That is above the 12-20% estimate and means: with the decode load measured at 1.00x/1.50x/2.00x (40/51/67% busy on the speech FLAC, B-545) plus about a quarter for the stretcher, firmware tempo fits to about 1.5x on that material and not to 2.0x without a cheaper search or the hardware correlator (the case this section made for `cymo_stretch`).

**Correction (B-421, found while checking collisions):** an earlier version of this section put the working buffers in the PSRAM window.
That is wrong for the correlation loop. PSRAM CPU reads cost about 32 cycles each and up to about 380 in the worst case **[HW]** (B-022, B-054), so
about 24,000 reads per hop would cost roughly 770,000 cycles, which is about the whole 12 ms hop at 66 MHz. The correlation working set (the
decimated windows, about 1-2 KB) **must live in on-chip RAM**. Only the sequentially-accessed grain and overlap buffers can go to PSRAM, and
those cost a few thousand cycles per hop because they are block copies. The heap limit in section 11 therefore has to absorb about 1-2 KB.

Why WSOLA and not simpler or heavier: plain overlap-add (fixed grains) is the cheapest but flutters audibly, and a phase
vocoder needs an FFT the project has deliberately excluded. WSOLA picks each grain's start by cross-correlating against the
previous one, which keeps periodic waveforms aligned. Its cost is the correlation search. **[EST]** With a 20-30 ms window
and a 10 ms search range, a coarse search on a 4x decimated mono mix is on the order of 25,000 multiply-accumulates per 15 ms
hop. Done in software with 64-bit accumulate on rv32im that is on the order of 10-25% of the CPU, which is too much on top of
decode. Done by one time-shared MAC in hardware it is about 25,000 clocks per hop, roughly 3% of one MAC's time. That is the
FPGA case for Cymo's stretch unit, and it mirrors the FLAC LPC unit's structure (sticky load, one MAC, done flag).

The stretch unit belongs **between the decoder push and the output stage**, working at the file's native rate, because then the
FIFO drains at native rate and the resampler stays at a fixed ratio. Firmware would keep pushing decoded PCM as fast as it can,
exactly as now; the unit consumes N x per output second.

Limits to state honestly: tempo at N > 1 still needs N x decode, so FLAC will not follow until hardware LPC headroom is
measured (F8); WSOLA on dense music is audibly imperfect; and none of this has been listened to. **The first step is a host
prototype** on the Test Album, scored with simple objective measures plus an owner listening pass, before any RTL.

## 8. The equalizer: keep, extend or replace

Options, evaluated against the existing hardware and this project's constraints (no FPU on the CPU, ALMs tight, EQ must not
use scarce M10K, everything bit-exact against a Python model):

| Option | What it is | Cost | Verdict |
|---|---|---|---|
| **A. Keep as is** | 8 fixed 5-band presets | zero | Acceptable as a *fallback*, not the target. |
| **B. Graphic EQ, gain-indexed ROM** | 10 fixed centre frequencies and Qs; per band a ROM of precomputed biquads for gain steps of 1 dB from -12 to +12 dB; user sets 10 sliders; presets become slider positions | ROM about 10 x 25 x 5 x 18 = 22,500 bits **[EST]** (a few M10K, or MLAB); 20 biquads use about 320 of 1,250 clocks **[EST]**, same one multiplier | **Recommended.** No trigonometry on the CPU, no new datapath, reuses the proven engine, bit-exact testable. Presets stay, as saved slider sets. |
| **C. Parametric EQ, CPU-computed coefficients** | user picks frequency, Q, gain; firmware computes biquad coefficients | a coefficient RAM (small) plus integer trig/sqrt on rv32im | Only if per-band frequency editing is a real requirement. Coefficients can be generated by Tau Omega on the host and loaded as data instead (below). |
| **D. Writable coefficient RAM, host-generated** | replace `crom` with an MLAB written through a sticky index+data port (the same convention as `R_LPC_COEF_*`); presets and user curves delivered as a `.teq` section in `tau-assets.bin` | small RAM plus a loader | Best *second step*: lets Tau Omega ship community presets and headphone or speaker corrections without an RTL rebuild. Pairs with B. |
| **E. Linear-phase FIR** | 512-tap FIR | nearly all clock budget | Reject. |
| **F. Loudness compensation** | shelf gains that vary with the volume setting | needs B's gain-indexed ROM, otherwise trivial | Cheap follow-on to B. |
| **G. Limiter** | replace the hard clamp | in `cymo_out` | Include regardless of A-D. |
| **H. Crossfeed, stereo width** | mid/side plus a low-pass | a few MACs | Park. |

Suggested placement change independent of the choice: run the EQ **after** the resampler on its tick, with a wider input and a
limiter behind it. The meters-do-not-follow-EQ limitation stays; feeding meters from post-EQ audio needs a level tap in RTL,
already noted as the "third option" in `EQ_DESIGN.md`, and is a separate decision.

## 9. Proposed build order and gates

Discipline matches the rest of the project: host model, then bit-exact testbench with a mutation test, then a two-seed fit,
then a hardware A/B with an evidence label. Nothing below has started.

| Phase | Work | Type | Gate to leave the phase |
|---|---|---|---|
| **C0** measure | (a) fix or explain the CPU LOAD 100% reading and surface a real headroom row; (b) FLAC `t_pct` with hardware LPC, plus 88.2/96 kHz via ACCEPT ALL RATES; (c) record the MP3 D/A/X split; (d) F2 A/B behind a macro; (e) host model of the real resampler kernel and a WSOLA prototype on the Test Album; **(f) analog loopback baseline (section 6.7)** | firmware and host only, no fit | Numbers in the audit trail; owner listening notes; a recorded loopback baseline of the current build |
| **C1** firmware unification | one `cymo_push()` replacing both loops; dB-tapered volume with a software ramp | firmware only, byte-identical audio at unity | `make test-host` green; release ROM change reviewed |
| **C2** `cymo_resamp` | section 6.1, EQ retimed onto its tick | RTL + fit | model equals RTL bit-exact; both seeds close; A/B on 44.1, 48, 22.05 kHz |
| **C3** `cymo_out` | gain ramp, ReplayGain register, dither, limiter, 24-bit path | RTL + fit | bit-exact model; dither statistics test; 24-bit FLAC listening A/B |
| **C4** deep buffer | AW 13 (or PSRAM ring) | RTL + fit, block-RAM ledger | stall-injection test shows underruns absorbed; block budget signed off |
| **C5** EQ | option B, then D | RTL + tool + Tau Omega format | bit-exact model per gain step; no click on slider moves |
| **C6** gapless and ReplayGain tags | trim primitive, tag parsing or library-index field | firmware + tool | gapless test tracks sample-exact |
| **C7** tempo and pause shortening (audiobooks) | speech-tuned WSOLA plus envelope-based pause shortening, firmware in cold code with PSRAM buffers first; a hardware correlator (`cymo_stretch`) only if C0 shows the CPU cost is too high | firmware first, RTL only if measured | scored on the Test Album's LibriVox clip with objective measures plus an owner listening pass; decode still keeps up at the top speed; resume position exact |
| **C8** decode kernels | MP3 IMDCT (per C0(c)), FLAC bit reader only if C0(b) says the CPU is still the wall | RTL | only if C0 shows a real bottleneck |

Suggested first slice if the goal is the biggest audible improvement for the least risk: **C0, then C1, then C2**.

Resource ledger for the RTL phases (everything **[EST]** until fitted): resampler about 1 DSP and 1-2 M10K or MLAB; output stage
about 2 DSP and a few hundred ALMs; buffer 24 additional M10K; EQ ROM a few M10K or MLAB; stretch about 1 DSP and a few M10K.
Fits should be run against the *current* macro set, not a bare base (the lesson of B-130).

## 10. Decisions for the owner

1. Is the scope above the right definition of Cymo, and is **C0 then C1 then C2** the right first slice?
2. ~~**Pitch:** which mode~~ **Decided 2026-09-29: pitch-preserving tempo for audiobooks; semitone pitch shift dropped.** Pause shortening is also included (owner, 2026-09-29).
3. **EQ:** option B (graphic, on-device sliders) with D (host-delivered curves) as the follow-up, or keep presets?
4. **Buffer:** M10K first (simple, bounded) or go straight to a PSRAM ring (seconds of buffer, more risk)?
5. **F2:** may I add a macro-guarded 16-bit I2S experiment for a hardware A/B?
6. **Hi-res FLAC:** worth measuring 88.2/96 kHz with hardware LPC now, given it would then also need the resampler to play correctly?
7. **Alignment and Bluetooth:** section 11 proposes decisions D-C01 to D-C05, and section 12.7 lists four Bluetooth-specific decisions.

## 11. Alignment with the architecture rework already done

Checked against the state of `main` as of 2026-09-29 (`CURRENT_STATUS.md`, `ROADMAP.md`, `DECISIONS.md`,
`TALOS2_REIMPLEMENTATION_PLAN.md`, `HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md`, `HELIOS_SPEC.md`, AUDIT_TRAIL B-409 to B-416).
Nothing on `main` has changed the audio path since the audit; what changed is the surrounding budget and rules.

| Development | Effect on Cymo | Action |
|---|---|---|
| **T2-00 shipped, Talos 2 P3 declined** (B-388/B-398, B-409). `glyphbuf` is back in MLAB with blend on, freeing about 6,500 ALMs. | My section 6 said ALMs were the resource to watch, citing the 98% LPC fit (B-378). That figure **predates T2-00** and is stale. The latest fit report gives RAM 240/308 and DSP 19/66, but I found no post-T2-00 ALM percentage. | Read the ALM figure from the T2-00 fit report as part of C0 before sizing C2/C3. Cymo does **not** depend on Talos; no Talos work is implied. |
| **Helios `helios_view_t` built** (item 4), and **HELIOS_SPEC 8.1 proposes `helios_audio_ok()`**, generalising `meter_afford()` into a shared audio-health gate (B-354). | This is Cymo's consumer. The gate needs a trustworthy audio-headroom signal, which is exactly C0(a) (the CPU LOAD 100% gap, also `ROADMAP` item 11). | **Merge the two.** Build the headroom metric once (FIFO minimum fill plus corrected idle share) as a Cymo export, and feed both the Info row and Helios's gate from it. Do not build two definitions of "audio is healthy". |
| **192 KB RAM shrink** in the current bitstream. Firmware heap on 192 KB builds is about 12 KB (release) and 6.5 KB (Diagnostic) (B-333). | Any Cymo firmware that grows heap, especially a firmware WSOLA prototype or a next-track pre-read buffer, does not fit. | Strengthens the case for putting Cymo work in RTL and deleting per-sample software (C1 must be size-neutral). Any firmware Cymo code goes in cold code behind `COLD_READY()`, like the rest. |
| **M10K ledger:** 240/308 with the shrink, 68 free. The parallel `test/720` branch (B-409) already plans a buffer widening (its Group A3) on the same pool. | Cymo's deeper buffer (about 24 more blocks), resampler and EQ ROM all draw on the same 68. | Keep **one block-RAM ledger** (`PHASE_F_SPEC` section 4 table) and add Cymo's asks to it, as estimates, before any fit. 720 is last by owner decision, so Cymo must stay resolution-agnostic, which it already is. Video at 720 raises SDRAM pressure, which is a further reason to prefer an on-chip buffer over an SDRAM ring. |
| **Persist widened to 32 words**, 21 used (`SW_N`, `fw/settings.inc`), 11 free. | Cymo settings (speed mode, route, ReplayGain mode, 10 EQ gains) do not all fit as separate words. | Persist only small state (mode, route, EQ preset index, tempo/pitch mode, about 5 words). Store curves, EQ band sets and presets as a **`EQ` section of `tau-assets.bin`**, per decision **D-M01** (one container, not one slot per asset type), and log the new section in `CROSS_PROJECT_INTERFACE.md` when built. |
| **`tau-assets.bin` and Tau Omega** (D-M03, D-M09). | Host-generated EQ curves and headphone corrections are the same authoring flow as theme and meter presets. | Omega owns generation, Tau-Alpha owns the format. No shared files. |
| **Meter contract decisions** (D-M05, D-M07, D-M08). | A Bluetooth delay-compensation option (section 12) changes when meters draw. | Must be opt-in and must not alter legacy meters. |
| **Standing rules**: two seeds per timing claim, `READY()` probe pattern for new bitstream features, hot-to-cold calls gated, fits use the current macro set (B-130), installs via `tools/install_dev_core.py`. | Apply directly. | Every RTL Cymo phase ships behind a `CYMO_READY()` probe and a macro, byte-identical firmware behaviour when absent, so an old bitstream keeps today's path (as `BLIT_READY()`, `RRECT_READY()` and `DBUF_READY()` do). |
| **Roadmap items 10 and 11** (FLAC kernels, the CPU LOAD gap). | Item 11 is C0(a). Item 10 (FLAC LPC done) leaves the bit reader, which C0(b) will decide. | Fold C0 into item 11 and C8 into item 10 rather than adding parallel lines; the roadmap says only that file orders work. |

Proposed decision-register entries, for the owner to accept or reject (all **Proposed**, none decided):

- **D-C01** Cymo is the name of the audio output subsystem (resampler, output stage, buffer, EQ, stretch, sinks); new modules use `cymo_`.
- **D-C02** Audio output ends in one canonical stream (48 kHz, stereo, 16-bit, strobe) that every sink consumes (section 12).
- **D-C03** Cymo settings persist as small words, curves live in `tau-assets.bin` (per D-M01).
- **D-C04** Every Cymo RTL feature is probe-gated and macro-gated so an old bitstream is byte-identical in behaviour.
- **D-C05** Do not merge the polyphase, LPC and EQ multipliers.

## 12. Future second output: a custom cartridge with an ESP-based Bluetooth transmitter

Owner scenario: a custom cartridge carrying an ESP module that provides Bluetooth audio, alongside the Pocket's own DAC.
This section records what the current design already supports, what it would need, and how Cymo should be shaped now so
that adding it later is small. **Nothing here is built, and the electrical facts are mostly open.**

### 12.1 What the design has today

- **Pins [READ]** (`core_top.v:26-43, 124-138`). The core exposes 30 cartridge lines: `cart_tran_bank0[7:4]`, `bank1[7:0]`,
  `bank2[7:0]`, `bank3[7:0]`, plus `cart_tran_pin30` and `cart_tran_pin31`, and the link port lines `port_tran_si/so/sck/sd`.
  Today banks 1-3 are inputs (`dir = 0`, `Z`), bank 0 is **driven high** with `dir = 1`, pin 30 is driven low and pin 31 is an
  input. The link port is all inputs.
- **Direction control is per bank, not per pin [READ from the port list; the electrical meaning is inferred]**: there is one
  `_dir` output for each 8-bit bank. If that is a level-translator direction pin, every line in a bank has the same
  direction. That is a hard constraint on any custom cart pinout.
- **The I2S source already exists [READ]**: `sound_i2s` generates MCLK (about 12.288 MHz), the internal serial clock and LRCK
  and shifts the sample out. The serial clock (`audgen_sclk`) is internal today and would need to be brought out.
- **Only one sink exists**: the DAC, at a fixed 48 kHz, in one clock domain.

### 12.2 Architecture for a second sink

```
 cymo_resamp -> ReplayGain -> EQ -> limiter ----> canonical 48 kHz stereo 16-bit stream (+ strobe)
                                                       |
                       +-------------------------------+----------------------+
                       v                                                      v
              per-sink gain/ramp                                     per-sink gain/ramp
                       |                                                      |
                 sink_dac (existing sound_i2s)                    sink_cart (I2S out on cart pins)
                                                                              |
                                                                    ESP32: I2S slave RX -> A2DP source
```

Design rules this adds to Cymo:

1. **Fan out after the EQ and limiter, before per-sink gain and dither.** That keeps one processing chain and lets the
   speaker and the Bluetooth link have independent volume. The gain and dither blocks in `cymo_out` (6.2) therefore become
   per-sink, which costs almost nothing.
2. **A route register**: none, DAC, Bluetooth, or both. With Bluetooth-only, the DAC path must be **muted, not stopped**:
   keep its clocks running to avoid pops and just feed it zero, otherwise the speaker and the headset play the same audio.
3. **Same serializer, second pin set.** The simplest sink reuses the existing MCLK/LRCK/serial signals and the same sample word
   on cart pins. It needs no second clock generator and no second CDC, and a testbench can compare the two outputs
   bit-for-bit. A second, independent sink rate (44.1 kHz for Bluetooth) needs a second serializer and a second resampler
   output; defer it. ESP32 A2DP's default is 44.1 kHz stereo 16-bit SBC PCM in the sources I found
   ([ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP)); whether it accepts 48 kHz cleanly is **[OPEN]** and only
   matters if the ESP resamples badly.
4. **Everything upstream stays sink-agnostic**, which is the same discipline as "resolution-agnostic" for 720.

### 12.3 Pin plan under the per-bank direction constraint

Direction is documented as per group (skill: "directions per group"), so the split must respect it. The closest prior art is the Analogue
devkit debug cart (a USB-UART cart at up to 2 Mbps), which uses **bank0 as outputs and bank3 plus pin31 as inputs** (KB-018, community-reported).
That also matches today's tie-off (bank0 driven, banks 1-3 inputs), so it is the lowest-risk plan:

| Group | Direction | Signals |
|---|---|---|
| bank0 [7:4] (4 lines) | output | I2S BCLK, LRCK, DATA and UART TX: exactly four lines, no MCLK (an I2S slave normally does not need it) |
| bank3 (8 lines) | input | UART RX, "ESP present / ready", spare status |
| bank1, bank2 | unchanged, inputs | not used |
| pin30 | **do not use** | clamped low in 5 V mode until `cart_pin30_pwroff_reset` is asserted (skill), which the template ties to 0 |
| pin31 | avoid | it is also the cartridge line-level audio input (`audio_adc`, framework 1.2), so a digital use would fight it |

Speed: the closest measurement is on the link port, where signal integrity degraded above about 6 MHz (KB-013, community-reported). A 3.072 MHz
bit clock is inside that with a factor of two, and a 2 Mbps UART well inside; verify on the actual cart PCB with a loopback pattern.

**The default states matter for the custom PCB**: bank 0 is currently driven high by the template
tie-off, so the cart must tolerate that or the tie-off must change.

### 12.4 Safety: do not drive pins into an unknown cartridge

A real Game Boy cartridge can be inserted in the same slot. If the core drove outputs into one, the result is bus contention
and possible damage. Therefore:

- Cart outputs stay high-impedance by default, exactly as banks 1-3 are today.
- They are enabled only when **both** a macro/settings toggle is on **and** a handshake passes (the ESP asserts a "present" line
  or answers a UART hello with a known ID and protocol version).
- A missing or wrong reply returns everything to inputs. This is the same probe-then-use pattern as `BLIT_READY()`.

Also **[OPEN, safety-critical]**: the logic level on the cart pins (3.3 V or 5 V, given Game Boy carts are 5 V systems) and
whether the translators can be set per bank. An ESP module is 3.3 V and not 5 V tolerant. **Do not connect one until the level
is confirmed (section 14 narrows this: the level follows a mechanical switch and Game Boy carts are 5 V) from Analogue's documentation or a measurement**, and use a level shifter regardless if there is any doubt. I could
not confirm this from public sources this session (searches returned only general descriptions, and the Analogizer project's
README defers to its wiki, `RndMnkIII/Analogizer`, which is prior art for driving the cart slot from a core and the natural
place to read pinout and level details).

### 12.5 Control channel and firmware

The audio stream is one-way, but pairing needs a control path. A two-wire UART (a tiny MMIO UART with a FIFO, on the order of
tens of ALMs **[EST]**) is enough for: scan and list devices, pair/connect/forget, connection status and codec, and passthrough of
headset buttons (play, pause, next, previous, volume) back into the player's input path. Optionally the firmware can send
title and artist to the headset over AVRCP. Firmware needs a Bluetooth Settings page, which should be a Helios view (section 11)
in cold code, and a `CYMO_BT_READY()` probe.

### 12.6 What Bluetooth changes for the rest of the engine

| Topic | Consequence | Response |
|---|---|---|
| **Codec ceiling** | ESP32's classic A2DP source supports **SBC only** ([source](https://github.com/pschatzmann/ESP32-A2DP)). SBC recompresses whatever we send. | Dither, high-quality resampling and 24-bit precision still help, but the audible ceiling on Bluetooth is SBC. Do not sell Bluetooth as a fidelity feature. |
| **Latency** | A2DP adds a substantial delay (commonly on the order of a hundred milliseconds or more, **[EST, general knowledge, not verified here]**). | Volume, seek and pause feel late; meters run ahead of the sound. |
| **Meters** | The spectrum and level analysis run on pre-FIFO audio. | Cheap fix: delay the *analysis output* (about 16 bands x 60 frames a second is a few hundred bytes for 0.3 s), not the audio. Scope and Chladni operate on raw samples and would need a large delay line (roughly 30 M10K blocks for 0.2 s **[EST]**), so leave them real-time and document it. Opt-in per D-M07. |
| **Clocks** | The FPGA is the I2S master. The ESP's Bluetooth timing comes from its own crystal, so its receive buffer slowly fills or drains. | The ESP side must drop or repeat a sample occasionally (its firmware, not ours). Verify with a long soak. |
| **Volume** | Two independent volumes (speaker and Bluetooth). | Per-sink gain (12.2 rule 1); firmware exposes one active control. |
| **EQ** | One shared EQ for both sinks. | A per-sink EQ needs a second engine state set and about 116 more clocks per sample (there are 1,250), so it is affordable if headphone-specific curves are wanted later. |
| **Power** | A Bluetooth radio can draw a meaningful current from the cartridge supply. The Pocket's FPGA power reading excludes cartridge power (`BATTERY_AND_POWER_PLAN.md`). | Add "cart peripheral attached" to the battery benchmark variable list. Whether the slot can supply a radio's peaks is **[OPEN]**. The purpose of `cart_pin30_pwroff_reset` is also **[OPEN]**. |
| **ESP variant** | Bluetooth Classic A2DP needs an ESP32 with Classic Bluetooth; several newer variants are BLE-only **[EST, general knowledge, not verified here]**. | Choose the module for Classic Bluetooth. |
| **Alternative for control only** | The link port (`port_tran_*`) could carry the UART, leaving all cart lines for I2S. | Not needed for a cart-only design; it does add a cable. |

### 12.7 Effect on the phased plan

The Bluetooth sink adds no work to C0 to C2. It is a **consumer of C3** (`cymo_out` needs per-sink gain and the canonical stream
tap), and its own phases can run after C3:

| Phase | Work | Type | Gate |
|---|---|---|---|
| X0 | Confirm how a custom cart selects the 3.3 V/5 V level, what the strict adapter-ID check does and what ID a custom cart can present, the cart power budget; pick the ESP module (classic Bluetooth) | research and hardware | written pinout with levels; owner sign-off. Direction grouping and pin30/pin31 behaviour are already documented (section 14) |
| X1 | Expose the serial clock, route register, gated cart-pin drive on bank0, handshake, per-sink gain, **and a separate Bluetooth core package with `cartridge_adapter: 0`** (the main Tau core keeps `-1`) | RTL + fit + packaging | testbench compares cart I2S to the DAC I2S bit-for-bit, with a mutation test; **three seeds** close; pins stay Hi-Z without the handshake; the main core's `core.json` is byte-unchanged |
| X2 | UART, Bluetooth Settings page (Helios view), route setting, mute-DAC logic | firmware | `make test-host`; `CYMO_BT_READY()` false on an old bitstream leaves behaviour unchanged |
| X3 | Reference firmware on the ESP and a bench test: I2S capture, then a real headset, long soak | hardware | zero underruns on the Pocket side; recorded drift behaviour on the ESP side |
| X4 | Delayed analysis for meters, AVRCP buttons and metadata | firmware | listening and viewing pass |

Owner decisions this adds: (a) is Bluetooth-only routing (mute the speaker) the wanted default when connected; (b) is 48 kHz
output on both sinks acceptable for v1; (c) which ESP module and who owns the ESP-side firmware; (d) are the cartridge pins to be
driven as outputs on bank0 and read on bank3 as in section 12.3, once the levels are confirmed; (e) ~~is shipping Bluetooth as a
separate core package~~ **decided 2026-09-29: yes, separate package (12.8)**.

### 12.8 Decision: Bluetooth ships as a separate core package (owner, 2026-09-29)

Decided. The cartridge Bluetooth output is **its own core package**, not a setting of the main Tau core. Consequences, all following from section 14:

- **Main core untouched.** `alfatreze.TAU` and `alfatreze.TAU_DIAGNOSTIC` keep `cartridge_adapter: -1` and `link_port: false`, so the cart stays unpowered and no
  inserted Game Boy cartridge can be reached by the core. Their `core.json` files must stay byte-identical (a release check should assert it).
- **The cart pins are driven only by the Bluetooth build.** Cart-pin drive is behind its own macro (working name `TAU_CYMO_BT`), which the main bitstream does not set,
  so the main bitstream never drives a cart pin, with or without a handshake. The handshake and Hi-Z-by-default rules (12.4) still apply inside the Bluetooth build.
- **New package**, working id `alfatreze.TAU_BT` (final name and platform art to decide), with `cartridge_adapter: 0`, the same Cymo firmware, and a bitstream built with
  the merged macro bundle plus `TAU_CYMO_BT`. It is a **second bitstream to fit, seed-sweep and maintain**, which is the price of the safety boundary. The Cymo RTL that is
  not cart-specific (resampler, output stage, buffer, EQ) stays in both bitstreams and is proven once.
- **Tooling and release.** `tools/make_release.py` gains a third core; `tools/install_dev_core.py` must treat it as its own core and keep the release-core protection;
  the package check asserts the two `cartridge_adapter` values. Log the new core in `docs/features/CROSS_PROJECT_INTERFACE.md`, because Tau Omega's package install, sync and
  remove paths must know a third core exists.
- **The player must handle absence cleanly.** On the main core the route setting and the Bluetooth page simply do not exist. On the Bluetooth core with no cart attached, the handshake
  fails and the DAC stays the only output.
- **Phase X1 gains** the packaging work above. X0 is unchanged and still blocks any powered test.

Proposed decision-register entry (for `docs/DECISIONS.md` when the owner next edits it): **D-C06** Bluetooth output is a separate core package with cart power on; the main cores keep cart power off.

## 13. Collision register: Cymo against the planned work and current resources

Checked in B-421 against `MMIO_ALLOCATION.md`, `tools/tau_data_slots.py`, `fw/settings.inc`, `tools/heap_gap_baseline.json`, the T2-00 status, the
parallel `test/720` spec (`origin/test/720`, `VIDEO_720_PHASED_SPEC.md`), and the RTL wiring in `mp3_soc.v`. **No collision is fatal. Four need a
decision or a design change, and one was a mistake in my own plan (fixed above).**

### Real collisions (need action)

| # | Collision | Evidence | Resolution |
|---|---|---|---|
| K1 | **MMIO range.** `test/720` claims 0x140-0x154 (`SCAN_LAT`, `VID_MODE`, `VID_CAPS`, `FB_DRAW_BASE`, `FB_DISP_BASE`) out of the free 0x140-0x1FC. The decode ends at 0x1FC, so only **48 registers exist in total**, and widening it again is another decode change. | [READ] `MMIO_ALLOCATION.md`, spec lines 85-109 | Partition now: 720 keeps 0x140-0x17C, **Cymo takes 0x180-0x1FC (32 registers)**. Cymo's estimated need is 16-24 [EST]. Record it in `MMIO_ALLOCATION.md` before either branch builds. |
| K2 | **Spectrum and level taps.** `tau_spec_bank` and `tau_wave_meter` are fed from `pcm_sample_tick` and the FIFO output at the *source* rate (`mp3_soc.v:917-964`). A resampler that replaces the FIFO drain would move them to a 48 kHz clock and shift every band frequency by 48/source rate. | [READ] | Place `cymo_resamp` **after** the FIFO's source-rate output register, consuming `out_l/out_r` with `sample_tick`. The taps stay untouched and the EQ input moves to the resampler output. |
| K3 | **Speed handling in firmware.** `pcm_rate_apply()` scales `R_PCM_RATE` by the speed. In tempo mode the FIFO must drain at 1x and only the stretch consumes N x. | [READ] `player.c:867-873` | Tempo mode must not scale the drain rate. Needs C1's single `cymo_push()` choke point first, which is where the stretch also sits. C1 is therefore a hard prerequisite of C7. |
| K4 | **Fit bundles and interlock.** Two branches each append their own macros (`TAU_*`) to separate qsf bundles; a fit that omits one silently builds the wrong bitstream (the B-130 failure). `CORE_VERSION` is at rev 26 and both branches could bump it. | [READ] B-130; 720 spec uses a caps register instead | One merged bundle before any shared fit. Do not bump `CORE_VERSION` for Cymo; report presence in a `CYMO_CAPS` register (as 720's `VID_CAPS` does) and gate firmware on it. |

### Resource ledger (all Cymo and 720 figures are estimates until fitted)

| Resource | Today | 720 plan | Cymo ask | Total | Verdict |
|---|---|---|---|---|---|
| M10K | 240 / 308 [HW] | +2-3 | +24 (buffer 8 -> 32 blocks), +1-2 resampler, +3 EQ ROM | about 270-275 | Fits with about 33-38 left. **The BT meter delay line (about 30) would consume most of it, which is why it is not proposed.** A PSRAM buffer avoids the 24 but is slower to build. |
| DSP | 17-19 / 66 [HW] | 0 | +4-6 | about 25 | Comfortable. |
| ALM | about 9,000 [EST, per the 720 spec; T2-00 report not in this worktree] of 18,480 | +600-1,000 | +1,500-2,000 | about 12,000 | Comfortable, **but read the real post-T2-00 number first**. Hold slack has been thin (+0.010 to +0.037 ns on some fits), so a large add near `clk_sys` needs two seeds. |
| MMIO | 0x140-0x1FC free (48) | about 10-15 | 16-24 | see K1 | Partition needed. |
| Persist words | 21 of 32 used, 11 free | none (mode never persisted) | 5-7 | 26-28 | Fits. Curves go in `tau-assets.bin`. |
| Data slots | 5, 6, 7, 8 used | none | none | unchanged | No new slot (D-M01). |
| Firmware heap | release 56,304 B on 256 KB builds; about 12 KB (release) and 6.5 KB (Diagnostic) on 192 KB builds [HW, B-333] | none | +1-2 KB (correlation windows) + stretch state | tight on 192 KB | Cold code; re-run `tools/check_heap_gap.py` per change. |
| SDRAM bandwidth | 720 native raises scanout to about 35-45% busy [EST, 720 spec] | yes | none if the buffer is M10K | n/a | **Cymo avoids SDRAM entirely**, which keeps it clear of the 720 contention work. |
| PSRAM bandwidth | shared by cold code, art and playlist | none | grain buffers only (block copies) | small | Fine, but never for random-access loops (see the correction in section 7). |
| Clocks | `clk_sys` 66.667 MHz, `clk_vid` 37.5 MHz for 720 | separate domain | none new (Bluetooth uses the existing I2S clocks) | n/a | No conflict. |
| CPU | FLAC about 99% of realtime in software; MP3 has headroom | none | speech WSOLA about 10-15% [EST] | fine for mono speech | The stereo hi-res FLAC + tempo combination is the one that will not fit; it is out of scope for audiobooks. |
| Cart pins | banks 1-3 idle, bank 0 driven high | none | Bluetooth only | n/a | Independent of everything else; gated by the open voltage question. |

### No collision found

`clk_sys` timing rules already apply to Cymo; the EQ already follows `CLK_HZ`; DSP and ALM budgets are not the constraint; the 720
mode is never persisted, so it does not compete for persist words; Helios views and the Bluetooth settings page are the same mechanism;
the Talos 2 P3 decision (declined) removes the one large change that would have touched the same blit fit bundles.

### One small existing inexactness the resampler removes

At 66.667 MHz the EQ's `CLK_HZ / 48000` is 1388.89, not an integer, so its tick runs about 0.006% off 48 kHz. Harmless today, but it is
one more free-running rate beating against the DAC. `cymo_resamp` owning the 48 kHz tick removes it.

## 14. Review against the `analogue-pocket-dev` skill (B-422)

The skill (`alfatreze/analogue-pocket-dev-skill`, read-only clone this session) holds Analogue's documented behaviour plus a graded knowledge
base. Its Analogue doc snapshots and project-private entries are excluded from the public repo, so I used its reference files, the public KB and
two agg23 wiki pages it cites. Verdicts below use the skill's own grading: **docs** (Analogue's documentation), **community** (community-reported), **local** (this project's own evidence).

### Confirmed or upgraded

| Claim in this plan | Skill says | Effect |
|---|---|---|
| The DAC path is fixed at 48 kHz (F1, section 6.1) | "I2S signed 16-bit stereo, exactly 48 kHz ... Sample-rate adjustment is not allowed" **[docs]** | The resampler is **mandatory** for 44.1/32/22.05 kHz material, not a nice-to-have. The earlier open question about native 44.1 kHz output is closed: not possible on the DAC. |
| F2: 16 bits is legal | "16 data + 16 spacer bits per channel" **[docs]** | The 15-bit slot is not required by the spec, so the hardware A/B is worthwhile. Whether it is deliberate headroom is still open. |
| Direction is per bank (section 12.1, was inferred) | "directions per group" **[docs]** | Upgraded from inference to documented. |
| Do not drive outputs into a real cartridge (12.4) | "wrong translator setup with powered cart can corrupt cartridge data" **[docs]** | Sharper than I stated: the risk is to the cartridge's **save data**, and it applies whenever the cart is powered. |
| `cart_pin30_pwroff_reset` purpose (was open) | pin30 is clamped low in 5 V mode until it is asserted **[docs]** | Resolved: avoid pin30 for signals. |
| Voltage (was open) | "5 V/3.3 V by mechanical switch" **[docs]** | Partly resolved: the level follows a **mechanical switch**, and Game Boy carts are 5 V systems. Assume 5 V unless a custom cart can select 3.3 V; a 3.3 V-only ESP still needs a level shifter. How a custom cart selects the level is **[OPEN]**. |
| A cart-based UART is known to work | devkit debug cart: 2 Mbps UART, bank0 out, bank3/pin31 in **[community]** (KB-018) | Direct precedent for the control channel and the pin plan (now used in 12.3). |
| Timing spread by seed | fits vary about 1.2 ns by seed; use 3-4 seeds, not ten **[community]** (KB-011) | This project uses two. Given the thin hold margins seen (+0.010 to +0.037 ns), Cymo's `clk_sys` additions should use **three** seeds. |
| Multi-bit crossings need Gray code or a FIFO | KB-009 **[community]** | Consistent with the project's existing `tau_cdc_gray_*` modules. Any new multi-bit Cymo status that crosses domains (for example the sink status) must use them. |

### New constraints this review adds

1. **The shipped core cannot power a cartridge.** `dist/Cores/alfatreze.TAU/core.json` declares `cartridge_adapter: -1` and `link_port: false`; the skill says `-1` maps to
   `0x80000000`, "leave cart power off". A powered ESP cart needs `cartridge_adapter: 0` (power on, no checks), and that also powers **any** real Game Boy cartridge in
   the slot. So the Bluetooth feature must **ship as a separate core package** (its own `core.json`), never as a setting in the main Tau core, whose default protects real
   carts. This is a packaging decision the earlier section did not have. Amend section 12.7: X1 includes a second `core.json`.
2. **Possible framework-level presence check.** The `cartridge_adapter` bitfield has a strict adapter-ID check (bit 17), a soft check (bit 16) and an adapter id in bits [7:0]
   **[docs]**. That may let the framework itself refuse an unexpected cartridge, which would be a stronger protection than my UART handshake. What ID a custom cart can
   present, and what exactly the check does, is **[OPEN]** and belongs in X0.
3. **Cart audio is an input.** Pin31 is the cartridge line-level analog audio *input* (`audio_adc`), so it cannot carry our output. Digital I2S on bank0 remains the route.
4. **The core has no SCLK pin to the system.** The Pocket re-creates SCLK, so the existing serializer's internal `audgen_sclk` is ours to expose on a cart pin; nothing to change on the DAC path.
5. **M10K surprises.** KB-010 reports Quartus inferring wide synchronizer chains into block RAM (M10K), and KB-073 confirms MLAB is simple-dual-port only (32x20, no mixed width) **[docs]**. Add to the ledger check: after each Cymo fit, search the report for `ALTSHIFT_TAPS` on new synchronizers, and design any writable EQ coefficient RAM as simple dual-port. KB-052 (partial-select writes may lose byte enables) applies to a coefficient RAM written a byte at a time; write full words.
6. **Toolchain claims.** The skill's rule is to distrust edition-gated advice. Cymo relies on none: no Rapid Recompile, no `DSP_BLOCK_BALANCING`, no Pro-only features.

### What the skill does not cover (still open)

Cart power budget and peak current for a radio; the exact cart-pin voltage rules for a custom cart; how the strict adapter-ID check works; any Pocket-specific
guidance on audio latency or buffering (the agg23 Sound page has none); headphone versus speaker switching (handled by the system, not the core). These stay in X0.

## 15. Prior art: a real, hardware-proven resampler (neoge/pocket-mp3)

While investigating the unexplained real-hardware degradation (F1, above), a search for other Analogue Pocket MP3 cores
turned up [neoge/pocket-mp3](https://github.com/neoge/pocket-mp3) -- a pure-HDL MP3 decoder for the same platform (no soft
CPU at all; the decode, resampling and I2S output are all hand-written SystemVerilog), tagged v0.3.2 and described as
"tested on hardware." Two things in it are directly relevant to Cymo's own open problem.

**A real polyphase FIR resampler, not a hold.** `src/fpga/resampler.sv` converts 44.1 kHz to 48 kHz using the *exact*
rational ratio 160:147 (44,100 x 160 = 48,000 x 147 = 7,056,000 -- no approximation in the ratio itself, unlike a
fixed-point accumulator that only approximates it). It keeps 160 phase banks of a 16-tap FIR (`TAPS` is a parameter),
picks one bank per output sample from a phase accumulator that advances by 147 mod 160, and runs a small FSM
(`S_IDLE -> S_SHIFT -> S_DECIDE -> S_TAP -> S_EMIT`) that does one MAC per clock against a per-channel ring buffer of
recent input samples -- explicitly pipelined ("history operand and DSP product are registered before the accumulate --
mux + multiply + 32-bit add miss 133 MHz chained"), the same retiming discipline this project's own timing lessons
(B-111/B-114/B-150/B-231) independently arrived at. A bypass path (`enable == 0`) passes 48 kHz sources straight through.
The coefficient-generation tool this design's own header references (`tools/gen_resamp_lut.py`) is not present in the
cloned tree -- only the ROM interface (`coef_addr`/`coef_data`, a synchronous ROM indexed by `bank * TAPS + tap`) is,
so the actual coefficients were not inspected.

**A structural difference worth noting: their design separates rate conversion from clock-domain crossing; Tau's does
not.** Their `audio_fifo.sv` is a 512-deep, Gray-coded, backpressured (`almost_full`) async FIFO that moves an
*already-48-kHz* stream from the producer's clock into the 12.288 MHz MCLK domain -- by the time anything reaches this
FIFO, the resampler upstream has already made every sample a real 48 kHz sample. Tau's `pcm_fifo.v`, by contrast, does
the rate-mismatch "hold" *at the same point* it also serves as the clock-domain-crossing buffer for the DAC path
(`sound_i2s.v`'s own 4-deep `sync_fifo`/`dcfifo`) -- one piece of logic is doing two structurally different jobs. This
is not proof that entanglement is *the* cause of F1's unexplained hardware gap, but it is a real, hardware-proven
counter-example that avoids the question entirely by construction, and a concrete reference architecture if Cymo ever
builds a real resampler (C0(e)/C1 in section 9): do the rate conversion once, upstream, at the producer's own clock,
and let the CDC layer stay a plain rate-matched buffer.

Their PLL wrapper (`src/fpga/pll.sv`) is a simulation-only placeholder (`assign outclk_mclk = refclk;` with a comment to
replace it with real Quartus IP) -- it does list MCLK as its own named output alongside SDRAM and pixel clocks, consistent
with the conclusion B-460/B-463 reached independently (12.288 MHz cannot share a VCO with a 12 MHz/100 MHz pixel/SDRAM
pair), but it contains no real PLL configuration to compare against.

Their own published resource report (v0.3.2 release notes) is for the **whole core** (hardware IMDCT, polyphase MP3
synthesis filterbank, decoder ROMs, OSD, resampler, everything): **9,793 ALMs (53%), 79 M10K blocks (26%), 22 DSP
blocks (33%), 1 PLL** on the same Cyclone V 5CEBA4 this project targets. That fits comfortably alongside everything
else on the device, which says the resampler itself -- almost certainly a small fraction of that total -- is cheap;
the IMDCT and filterbank are the resource-hungry pieces, not a 16-tap FIR.

### Would this be worth building for Tau, straight as-is?

Not as a literal port -- the architecture (exact-ratio polyphase FIR, phase-bank LUT, one time-multiplexed DSP MAC) is
right and cheap enough to afford, but two things would need to change:

1. **It only solves one ratio.** Their design is hard-wired for 160:147; Tau also has to serve 24/32/22.05 kHz
   (MPEG-2 half-rates, spoken-word rips), currently all served by the same hold. A port fixes only the single most
   common case unless generalised or deployed incrementally (44.1:48 first, others still falling back to the hold --
   the same probe-gated pattern already used for `POLY_FW`/`LPC_FW`).
2. **`pcm_fifo.v` already has half the machinery.** Its `rate_inc` fractional accumulator is structurally the same
   idea as their phase accumulator -- it currently uses the fraction to decide *when* to hold the same sample rather
   than *how much* to interpolate between two. Evolving that existing accumulator into a polyphase MAC fits this
   project's architecture better than adding their separate ring-buffer-plus-FSM module.

**Tap-count and window choice, modelled before any RTL** (`tools/lab/cymo_resamp_model.py`, built for this decision;
16-bit quantised coefficients, the same width `resampler.sv`'s own `coef_data` is, so this predicts what real hardware
would achieve, not an idealised float resampler):

| Window | 8 taps | 16 taps | 24 taps | 32 taps | 48 taps |
|---|---|---|---|---|---|
| Rectangular | 22-43 dB | 31-48 dB | 30-40 dB | 29-49 dB | (not worth it at this window) |
| Hamming | 15-76 dB | 39-73 dB | 53-66 dB | 57-71 dB | 62-72 dB |
| Blackman | 11-86 dB | 25-87 dB | 47-87 dB | 77-86 dB | **80-84 dB** |
| Kaiser (beta=8.6) | 11-89 dB | 25-89 dB | 47-87 dB | **83-86 dB** | 84-85 dB |

Each cell is the SINAD range across 1/5/10/15/18 kHz test tones (`sweep` subcommand); the low end of each range is
always the 18 kHz case, the hardest for a fixed-length lowpass. Compare against the current hold's **measured 10.8 dB**
at 1 kHz (B-430/B-467) and the Appendix's idealised-float **27.7 dB** prediction for the same case -- every windowed
option here, even at 8 taps, already exceeds both by a wide margin, and **32 taps with a Blackman or Kaiser window
gives a consistent 77-86 dB across the whole audible band** for only 1 DSP (`resources` subcommand's own capacity
estimate said 8 M10K blocks; the real fitted cost, confirmed 2026-10-01, B-475, is 16 -- the fitter did not pack the
coefficient ROM at the capacity-calculation's assumed density, still comfortably affordable against the 68 free
after the shrink). This is the recommended starting point
if C0(e)/C1 (section 9) goes ahead -- comfortably better than pocket-mp3's own unweighted 16-tap design, at a resource
cost this project can clearly afford, and cheap in CPU-cycle terms too: at clk_sys ~66.7 MHz and 48 kHz output there
are ~1,389 cycles available per output sample, of which a 32-tap MAC (1 cycle/tap, matching `resampler.sv`'s own
discipline) uses only 32 -- cycle budget was never the constraint, M10K for the coefficient ROM is.

**A cheap, coefficient-ROM-free alternative was checked and rejected.** Linear and cubic (Catmull-Rom) interpolation
need no LUT at all -- the phase fraction from the same P/Q accumulator computed above IS the only per-sample
coefficient, at the cost of a handful of multiplies and zero M10K blocks. Modelled the same way (`cymo_resamp_model.py
algebraic`): cubic reaches an excellent 89.3 dB at 1 kHz (better than the 32-tap FIR), but **collapses to 7.6 dB at
18 kHz -- worse than the current hold's own measured 10.8 dB baseline**. This is expected once stated plainly:
polynomial interpolation has no explicit anti-aliasing filter, so it degrades sharply as frequency approaches the
source Nyquist (22.05 kHz) -- exactly the range (cymbals, hi-hats, sibilance, bright synths) where a resampling
defect would be most audible in the first place. The LUT-based polyphase FIR earns its M10K cost (16 blocks fitted,
B-475) precisely where a free option cannot help.

**Nobody has actually listened yet -- the SINAD numbers alone cannot answer whether this is audible.** F1's own note
above ("the owner has listened to the current output for many builds without reporting this as the problem")
deserves more weight than another table. `tools/lab/cymo_ab_listen.py` renders real music (not synthetic tones) --
decoded with `tools/flac_ref.py`'s own bit-exact FLAC decoder -- through all three candidate output paths (hold,
32-tap Kaiser FIR, cubic) as plain WAV files, so an actual listening comparison on real material is the next step
before deciding whether C0(e)/C1 is worth building at all.

**2026-10-01 update: owner confirmed an audible difference on the Aphex Twin A/B (section 15's own listening test),
flagged as poor test material (glitchy electronic, not representative), and asked for a cleaner A/B on the Test
Album's MacCunn/Clementi tracks (classical, see docs/handoffs and the `#6c24` state note for the exact decision)
before committing to a build -- that second render was in progress at session start and is not yet confirmed. Owner
separately approved starting the RTL build in parallel with that render (the SINAD modelling and the architecture
review above were judged sufficient justification on their own): `tau_cymo_resamp.sv` (160-bank, 32-tap, 44100:48000-
only polyphase FIR, Q1.15 coefficients from `tools/gen_cymo_resamp_rom.py`'s own Kaiser-windowed ROM generator) is
now built, sim-verified bit-exact against its own golden model (`sim/cymo_resamp_model.c`, independently cross-checked
in Python by `sim/test_cymo_resamp_model.py` -- this project's own "prove it twice" discipline) on 4,354 output
samples, and all 5 mutation hooks are confirmed caught (`make test-rtl-cymo-resamp`/`test-rtl-cymo-resamp-mutation`).
This closes C2's "model equals RTL bit-exact" gate. See `src/fpga/core/tau_cymo_resamp.sv`'s own header for the full
design rationale, including a real protocol bug the testbench caught and fixed during development: the first draft
had the hardware consume a caller-pushed input sample during the SAME `start` call that raised the `pop_req` asking
for it, before the caller could possibly have supplied it -- the actual shift is now deferred to the start of the
NEXT `start` call.

**2026-10-01 update (B-472): wired into `mp3_soc.v`, synthesis-only check clean.** Before wiring, found and fixed a
real firmware-interface defect `done`/`pop_req` being one-cycle register pulses would have caused a real polling
loop to miss entirely -- both now behave like `tau_flac_lpc.sv`'s own held/read-ack `done` and a continuous-level
`pop_req`. Wired into `mp3_soc.v` behind `CYMO_RESAMP_ENABLE`/`TAU_CYMO_RESAMP`, registers 0x150-0x15C
(`docs/MMIO_ALLOCATION.md`). A `quartus_map` synthesis-only check against the exact shipped macro bundle (plus the
new macro) came back successful: 0 errors, 328 warnings, 20 DSP elements -- exactly +1 over that bundle's
established 19 DSP baseline, matching the "one time-multiplexed MAC" design precisely. NOT yet done, in order: a
real two-seed Quartus fit (resource/timing closure is a fitter question a synthesis-only check cannot answer),
wiring into `pcm_fifo.v` itself (replacing the hold at the point K2 above specifies -- after the FIFO's own
source-rate output register, consuming `out_l`/`out_r` via `sample_tick`, EQ moved downstream of the resampler),
firmware probe (`CYMO_RESAMP_READY()`, same pattern as `BLIT_READY()`/`POLY_FW`/`LPC_FW`), and the hardware A/B at
44.1 kHz (the other two gate ratios, 48 and 22.05 kHz, fall back to the existing hold until a later increment --
section 9's "start with one ratio" scope).**

**2026-10-01 update: the MacCunn/Clementi listening result came back.** Owner listened to all three (hold/FIR32/
cubic) on Mac speakers + cheap earbuds: **hold was easily the worst of the three**, consistent with every measured
number in this document. Between FIR32 and cubic, "really really hard to tell" apart -- a marginal, barely-
describable preference leaned toward cubic on this one track, the opposite of what the SINAD tables predict. Full
honest writeup in `docs/AUDIT_TRAIL.md` B-474: most likely explanation is this specific recording (orchestral/choral)
has little extreme-treble content near the 18-22 kHz range where cubic's own documented weakness (7.6 dB SINAD, vs
FIR32's 83-86 dB) actually shows up, compounded by limited-bandwidth playback equipment -- not evidence that cubic
is secretly fine. **Does not change the plan**: the case for FIR32 was always the measured SINAD tables plus the
hardware-proven architecture, not this listening test alone.

**2026-10-01 update (B-475): the real two-seed fit CLOSED CLEAN.** Both seeds Successful, every corner positive
slack on both (seed 1: Fast 0C hold +0.104/setup +5.943, Slow 85C hold +0.370/setup +1.188; seed 2 slightly tighter
on every corner). Seed 1 selected, RBF collected and hash-verified
(`work/diagnostics/cymo-b472/ap_core_s1.rbf`). DSP 20/66 confirmed at the fitter stage (matches B-472's synthesis
prediction exactly); RAM 256/308, a real +16 M10K cost -- double the design's own capacity-calculation estimate of
8 blocks (corrected above), still comfortably inside budget. **This fully closes C2's "model equals RTL bit-exact;
both seeds close" gate for the 44.1 kHz ratio** (48 and 22.05 kHz remain out of scope for this increment, section
9). Next, in order: the `pcm_fifo.v` integration (section 14/K2), a firmware probe, a card install, and the
hardware A/B.

## Appendix: the resampler model

The model behind the tables in section 3 is small enough to reproduce: for each output frame `k` it computes the source
position `t = k x (file_rate x speed / 48000)`, forms the output from the source samples around `t` (nearest: the newest
sample, `floor(t)`, compared against the ideal value at `t - 0.5` to remove the constant delay; linear, cubic and windowed-sinc:
against the ideal value at `t`), and reports the tone's signal-to-error ratio over about 30,000 source samples. It is a
scratchpad script, not a repo tool. If C0(e) goes ahead it should become `tools/lab/cymo_resamp_model.py` with the real
kernel, so the RTL testbench can compare against it.
