# MOD/tracker format support — parked feature idea

Not scheduled. Written down so the reasoning and open questions survive to whenever this is picked
up, per the project's own "park rejected/deferred options, don't re-litigate" convention (see
`docs/PHASE_F_SPEC.md` section 13, `docs/ARCHITECTURE_ROADMAP.md`'s parked items). No code, RTL,
card or VM touched writing this.

## 1. What this is

Add playback of Amiga/tracker module formats (MOD at minimum; XM/S3M/IT are related but
meaningfully heavier — see section 6) alongside the existing MP3/FLAC decoders. A module file
carries patterns (note/instrument/effect sequences per channel) plus raw sample data, and the
player synthesizes audio by sequencing patterns and mixing resampled sample data per output tick —
a fundamentally different job from MP3/FLAC's bitstream decode.

## 2. Software vs hardware

**This is not a bitstream-decode problem like MP3/FLAC, so Phase D's kernel-profiling lessons don't
transfer directly — but its top-level discipline does: profile before building anything in RTL.**

MOD/tracker playback cost is dominated by the per-tick mixer: for each active channel, resample
(interpolate) its sample data to the output rate, apply volume/panning, and sum into the output
buffer. There is no perceptual-coding stage (no Huffman, no IMDCT, no LPC/Rice) — the CPU cost
scales with channel count x samples-per-tick x interpolation quality, not with a fixed per-frame
decode cost like MP3's Subband stage (B-087: 41-59% of MP3 decode time). For a classic 4-channel
Protracker MOD at nearest-neighbour or linear interpolation, this is a small fraction of what the
existing MP3 Subband/IMDCT stages already cost per sample on this CPU — the same class of
workload many other openFPGA/retro cores already run entirely in software.

**Recommendation: build the software mixer first, exactly as Phase D's own "profile first, and if
it's fast enough, stop" rule (`docs/ARCHITECTURE_ROADMAP.md` Phase D step 1) says for any audio
work here.** Do not design an RTL mixer/resampler before a real measurement shows the software path
can't hold real-time at the target channel count. If a hardware kernel is ever justified, the
likely candidate is a shared multiply-accumulate resampler (structurally close to the spectrum
filter bank's "one time-shared DSP MAC" already parked for Phase D step 5), not a Amiga-Paula-chip
reimplementation.

## 3. Toggleable Amiga output filter

Real Amiga hardware puts a fixed passive low-pass in the audio output path, and on some models
(A500, A2000) a second switchable "LED" filter in series that further attenuates high frequencies
when engaged. **The specific cutoff figures need a verified source before this is built — treat any
number here as OPEN, the same convention this project already uses for unverified hardware
timing** (`docs/PSRAM_TIMING_CONTRACT.md`'s page-referenced-or-OPEN values is the model to follow):
commonly-cited figures for the fixed filter and the LED filter differ by model revision, and the
"~14 kHz" figure floated when this was proposed does not match anything in this session's own
knowledge of the real filter stages — needs a datasheet/schematic or a measured reference (e.g. a
trusted tracker player's own documented filter model) before picking a number, not an assumption.

**Proposed feature, independent of the exact cutoff:** a Settings-exposed toggle for MOD playback
only (mirrors the existing EQ/Speed toggle pattern in Settings > Playback), applying a low-pass
filter to the mixed output. Whether it defaults on (matches how the tune was authored/heard on
real hardware) or off (crisper, matches most modern trackers' default) is an owner call, not
assumed here.

**Implementation is pure software** — a single one-pole (or biquad, reusing the existing
`eq_biquad` hardware block's *coefficients*, not necessarily the block itself) low-pass applied in
the mixer, negligible added CPU cost per sample. No RTL required unless profiling says otherwise.

## 4. "Non-linear drop / 8-bit quantization" — needs disambiguation before scoping

Two different things could be meant by this, and they're not the same feature:

- **(a) Bit-depth reduction of the final mix** (e.g. truncate or dither the mixed output to 8 bits)
  — a *linear* quantization effect, the closest match to genuinely emulating the real Amiga
  Paula chip's output stage, which is 8-bit **linear**, not non-linear.
- **(b) A non-linear (companding-style, e.g. u-law/A-law) quantization curve** — a real audio
  technique, but not something the original Amiga hardware did; this would be a stylistic lo-fi
  effect layered on top of authentic playback, not a hardware-accuracy feature.

**Open question for whoever picks this up: which one was meant, or both as separate toggles?**
Recorded here rather than guessed, since they serve different goals (authenticity vs. a deliberate
effect) and would want separate UI language ("Amiga sound" vs. "Lo-fi/bitcrush").

**Implementation is pure software either way** — a single quantize/dither op per output sample,
negligible cost, no RTL.

## 5. MOD tracker visualization

A "now playing" view for module files showing live pattern data (row/note/instrument/effect per
channel, Protracker-style scrolling rows) instead of, or as an alternative to, the album-art view
used for MP3/FLAC.

- Needs the software module player to expose its current pattern/row/channel state to the UI layer
  once per tick — cheap, no new hardware, just a small struct the mixer already has to track
  internally for playback anyway.
- **Natural fit for the blit engine (Phase F), not before it.** Multiple independently-updating
  text rows (one per channel) is exactly the "independent multi-row tickers" idea already parked in
  `docs/PHASE_F_SPEC.md` section 13 — building the tracker view on top of a full-frame CPU redraw
  per tick would be the wrong order (the project has already learned this lesson once with the
  library/settings/playlist full-screen overlays). Park the tracker-view UI work until B3's
  firmware coordination (CHAR sub-glyph clipping, also section 13) or the ticker primitive exists,
  whichever lands first.

## 6. Additional opportunities and open questions (not asked for, worth flagging)

- **Format scope needs a decision before any implementation starts.** "MOD" covers a whole family:
  plain Protracker MOD (4-8 channels, small, no volume-ramp/no true stereo panning subtleties) is
  the cheapest and most standardized; XM/S3M/IT add per-channel volume envelopes, more effect
  commands, higher channel counts (commonly 16-32), and looser format-compliance across the
  tracker ecosystem (the same "different players disagree on edge-case effect behaviour" problem
  that makes tracker-format compatibility notoriously fiddly). **Recommend scoping to MOD (and
  optionally XM) first**, not the full family, unless there's a specific reason to want S3M/IT.
- **Effect-command correctness needs its own test corpus**, the same way MP3/FLAC got a
  content-controlled Test Album (B-092) rather than trusting a single sample file. Tracker effects
  (vibrato, arpeggio, portamento, volume slides) are exactly the area where players silently
  diverge from each other; a handful of known-tricky public-domain/CC modules exercised against a
  reference player's output would catch this early rather than after it ships.
- **Library/metadata integration is not free.** The existing media library (Phase E,
  `docs/MEDIA_LIBRARY_0.4_SPEC.md`) is built around ID3/Vorbis-style tags and embedded cover art;
  MOD/XM have no such embedded art and only a plain title string in the module header. Needs its
  own "what does a library row look like for a tracker file" decision — likely a generated
  placeholder or a pattern-preview thumbnail rather than album art.
- **Resampling quality as a general, reusable setting.** Whatever interpolation the MOD mixer uses
  (nearest/linear/cubic) is a good candidate for a shared, named quality setting if the existing
  MP3/FLAC output path ever needs one too — worth designing once, not per-format, if it comes up.
- **Per-channel mute/solo** is cheap once the mixer exists and fits this project's existing
  Diagnostics/Check culture (a natural "MOD channel test" page) as well as being a genuinely fun
  listening feature — low cost, high value, worth remembering when the mixer is built.
- **This is architecturally independent of Phase D's kernel work.** MOD/tracker decode shares
  no code path with MP3/FLAC (no bit reader, no Huffman, no perceptual synthesis) — it doesn't
  compete for the same CPU budget analysis or benefit from any MP3/FLAC kernel built under Phase D.
  Treat it as its own phase/track when scheduled, not a Phase D sub-item.

## 7. Recommended order, if and when this is picked up

1. Software MOD (Protracker-format) player + mixer, profiled against the same
   ALM/DSP/M10K/underrun stress matrix Phase D already uses (`docs/ARCHITECTURE_ROADMAP.md` Phase D
   step 4) before considering any RTL.
2. Amiga filter toggle and quantization effect(s) (section 4's disambiguation resolved first) —
   both software, both cheap, can ship alongside step 1.
3. Library/metadata integration for module files.
4. Tracker visualization UI — gated on the blit engine ticker/overlay primitives existing.
5. XM support (if wanted) as a second pass once MOD is proven, given the higher channel count and
   effect surface.
