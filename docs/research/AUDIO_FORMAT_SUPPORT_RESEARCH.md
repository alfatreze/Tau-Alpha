# Additional audio format support: survey (nothing built)

Status: research only, 2026-09-27. No RTL, no firmware change. Written at the owner's request to
survey formats beyond the current MP3 (Helix, hardware-accelerated synthesis filterbank as of
`tau_mp3_poly.sv`) and FLAC (`fw/flac.c`, software only, bit-reader kernel scoped but not built —
`docs/research/FLAC_BITREADER_KERNEL_SCOPING.md`) support, using this project's own scoping-document rigor
(see `docs/research/MP3_IMDCT_KERNEL_SCOPING.md` and `docs/research/FLAC_BITREADER_KERNEL_SCOPING.md` as precedent:
read the real code, separate measured from estimated, flag complexity honestly, recommend scoping
before building).

## Platform constraints used to judge feasibility

- **On-chip RAM (M10K block-RAM) is the scarce resource.** Recent fits run 235-304 of 308 M10K blocks
  depending on configuration; a whole "Phase G" effort (cold-code + PSRAM offload) exists specifically
  to free BRAM for other features. The RAM-shrink track (`ram192-blend-b333`, B-337) frees roughly
  64 blocks (240/308 used, ~68 free) but is not yet adopted in a shipped build — current committed
  reality is closer to ~9-15 free blocks, not 68, until that lands. Any format assessment below treats
  M10K budget as tight.
- **DSP blocks are comparatively available** (11-17 of 66 used recently) — multiply-heavy kernels are
  less of a bottleneck than RAM.
- **CPU is a plain rv32im core, no hardware float, no B-extension (no hardware CLZ), no existing
  DSP/MAC acceleration beyond what this project has custom-built for MP3's filterbank.** Every
  "software decoder" cost on this platform is fixed-point, hand-optimized C, same as `fw/flac.c` and
  the vendored Helix MP3 decoder — this is the correct baseline for estimating a new software codec's
  CPU cost, not a desktop or even a typical embedded-Linux baseline.
- **Sample-rate output path:** `pcm_fifo.v`'s `rate_inc` is a phase accumulator (`sample_rate * 2^32 /
  CLK_HZ`), i.e. the output stage is a fractional-N resampler feeding a fixed I2S clock domain, not a
  hard-coded 44.1/48 kHz path — moderately flexible for *conventional* PCM rates. It is a standard PCM
  interpolator, not built for anything resembling DSD's bitstream rates (next section explains why
  that matters for DSD specifically).
- **WAV/AIFF: confirmed absent today.** Grepped `fw/player.c` and every `fw/*.inc` case-insensitively
  for `wav`, `aiff`, `WAVE` — the only hits are unrelated (`wave[]`/`VIZ_WAVE`, the oscilloscope
  meter's sample-history array, nothing to do with a file format). There is no WAV or AIFF container
  parser anywhere in this codebase today. This is a real, concrete gap, not an estimate.

## Summary table (highest priority first: lowest effort + highest relevant user base, ties toward lower effort)

| Format | Type | Est. relevance (this audience) | Decode complexity vs existing code | New hardware needed? | Feasibility/Priority | Effort estimate |
|---|---|---|---|---|---|---|
| **WAV** | Uncompressed PCM | Very high — universal, always a safe fallback format | Trivial: header parse + pass-through, no decode step at all | None | **Highest** | Small (days) |
| **AIFF** | Uncompressed PCM | High — common on Mac-origin libraries, classic archival format | Trivial: same as WAV, big-endian variant, different chunk IDs | None | **Highest** | Small (days), can likely share most code with WAV |
| **FLAC** | Lossless | Very high | *(already have — software, hardware bit-reader kernel scoped not built)* | — | Done | — |
| **ALAC** | Lossless | High — dominant lossless format in the Apple/iTunes ecosystem, common in ripped libraries | Moderate: LPC-prediction codec, architecturally close to FLAC's own shape; real code reuse against `fw/flac.c`'s structure is plausible but unconfirmed | Likely none new; could someday reuse a FLAC bit-reader kernel if one is ever built | **High** | Medium (a few weeks, untested estimate) |
| **MP3** | Lossy | Very high | *(already have — software + hardware filterbank kernel, IMDCT kernel scoped not built)* | — | Done | — |
| **Monkey's Ape (APE)** | Lossless | Niche/enthusiast — real subculture, but small relative to FLAC/ALAC | High: adaptive/neural-ish prediction filters, materially more complex than FLAC's fixed+LPC scheme | None expected, but genuinely new algorithm, no reuse path | Low-Medium | Large (untested estimate, likely multi-week+) |
| **WavPack** | Lossless (+ hybrid lossy/lossless mode) | Niche/enthusiast, smaller community than APE | Moderate — simpler entropy coding than APE, still a distinct bitstream/prediction scheme from FLAC | None expected | Low-Medium | Medium-Large (untested estimate) |
| **AAC** | Lossy | Low for THIS audience specifically — huge in streaming/Apple ecosystem playback, but rare as a personal *archival* library format for lossless-capable-device owners | High: MDCT-based, closer in shape to MP3's own (unbuilt) IMDCT kernel than to FLAC; licensing is also a real question (AAC decode typically requires a patent license, unlike MP3 which is now patent-free) | Could someday reuse an MP3 IMDCT hardware kernel if built, but that kernel does not exist yet either | Low | Large (real decoder + real licensing question) |
| **Ogg Vorbis** | Lossy | Low-Medium — free/open format with a real historical niche following, some personal libraries exist, but small relative to MP3/FLAC for this specific audience | High: also MDCT-based (same family as AAC and unbuilt MP3 IMDCT), plus its own container/codebook complexity | No reuse path from anything currently built | Low | Large |
| **Ogg Opus** | Lossy | Very Low as a personal *archival* format for this audience — Opus dominates modern streaming/VoIP, essentially never how someone stores a personal lossless-adjacent library | Very High: hybrid SILK (speech) + CELT (music) codec, genuinely complex modern design | No reuse path at all from this codebase | Very Low | Very Large |
| **DSD (DSD64/128/…)** | 1-bit sigma-delta PCM alternative | Niche/enthusiast (SACD rippers, high-end audio community) — real but small for a portable device | Fundamentally different signal representation, not PCM at all; typical playback path is either (a) heavy software PCM conversion or (b) native DSD output, neither of which this core's existing I2S/PCM pipeline is built for | **Likely yes** — `pcm_fifo.v`'s fractional-N interpolator targets conventional PCM rates; DSD64 is a 1-bit stream at 2.8224 MHz, structurally nothing like this path. Native DSD output would need new RTL beyond a decoder; PCM-conversion-in-software avoids new RTL but is a heavy, different kind of decode (a large FIR/decimation filter, not a bitstream parser) | Very Low | Very Large, and the RTL question alone needs its own scoping pass before any decoder work |
| **WMA** | Lossy | Very Low — declining relevance, small remaining install base, weak fit for this audience | Not assessed in detail given very low relevance — plausibly MDCT/LPC-hybrid depending on profile, similar concerns to AAC (patent/complexity) | Unknown, not scoped | Very Low | Not recommended to scope |

## Per-format detail

### WAV — highest priority

**Relevance:** Universal uncompressed format; effectively the lowest-common-denominator container
every ripping/conversion tool can produce, and a natural fallback/safety format for a lossless-capable
device's audience even though most of that audience prefers FLAC for the smaller file size.

**Decode complexity:** None in the traditional sense. A WAV file is a RIFF container with a `fmt `
chunk (describing PCM format: sample rate, bit depth, channel count) followed by a `data` chunk of
raw interleaved PCM samples. "Decoding" is parsing ~44 bytes of header and then handing the PCM bytes
straight to the same output path FLAC and MP3 both decode *into* today — this project already has a
working PCM output pipeline (`pcm_fifo.v` + the I2S path) since every existing decoder's whole job is
to *produce* PCM for it. There is no entropy coding, no prediction, no transform, nothing CPU-bound at
all beyond a byte copy (and possibly bit-depth/endian conversion if the source isn't already 16-bit).

**New hardware:** None. This is squarely a firmware-only, low-risk addition — no RAM for decode
buffers/coefficient tables, no CPU decode loop of any real cost, and the output path is proven.

**Confidence:** High. This is a well-understood, extremely simple container format; the estimate here
is not hedged the way the lossy/complex formats below are.

### AIFF — highest priority (tied with WAV)

**Relevance:** The historically standard uncompressed format on Mac-originated libraries; still shows
up in ripped/archived collections from that ecosystem, alongside WAV as the two common raw-PCM
containers this audience would actually own files in.

**Decode complexity:** Architecturally identical to WAV's case — an IFF-style chunked container
(`COMM` chunk for format, `SSND` chunk for the sample data) with the same "no decode step" property.
The main practical difference from WAV is chunk ID names and being big-endian (Motorola byte order)
where WAV is little-endian — a straightforward, mechanical difference, not a complexity difference.
Given how close the two are, a shared internal PCM-container-parsing path (one small header parser
per format, common pass-through logic) is a reasonable implementation shape, though this wasn't
verified against `fw/player.c`'s actual file-dispatch structure in this research pass — flagged as an
implementation detail for the scoping/build stage, not assumed here.

**New hardware:** None.

**Confidence:** High, same reasoning as WAV.

### ALAC — high priority

**Relevance:** The default lossless format in Apple's ecosystem (iTunes/Music, Apple Music lossless
tier); for a device audience that cares about lossless portable playback, a meaningful fraction likely
already owns ALAC-ripped libraries specifically because of past or present Apple-ecosystem use, even
though FLAC is more common in the wider lossless-audio community.

**Decode complexity:** ALAC and FLAC are both LPC (linear predictive coding) based lossless codecs
from roughly the same era, and are frequently described as architecturally similar — both use a
fixed-frame structure, linear prediction with quantized coefficients, and Rice/Golomb-style residual
coding. This research pass did **not** read ALAC reference-decoder source against `fw/flac.c` in
detail (that comparison is exactly the kind of "read the real code, don't just assert similarity" step
this project's own scoping docs insist on, and is better done as its own dedicated scoping pass if the
owner wants to pursue this) — so the "moderate complexity, real reuse potential" rating here is an
informed estimate, not a verified one. What can be said with more confidence: FLAC's own bit-reader
shape (`fw/flac.c`'s `bits()`/`sbits()`/`unary()` triad, the same functions `docs/
FLAC_BITREADER_KERNEL_SCOPING.md` scoped for hardware acceleration) is a plausible building block ALAC
decoding could reuse directly, since ALAC also needs a general-purpose bitstream reader with similar
primitives (though ALAC's residual coding differs from FLAC's Rice coding in its exact parameterization
— this needs the dedicated read to confirm, not assumed here). If a FLAC bit-reader hardware kernel is
ever built, ALAC decode could plausibly benefit from the same MMIO unit — again, plausible, not
confirmed.

**New hardware:** Likely none required as a first cut (pure software, same class of effort as FLAC
today); potential future reuse of a FLAC bit-reader kernel if that gets built first (per the standing
roadmap order, FLAC's own kernel is already ahead of further MP3 kernel work — ALAC support would slot
in as software first, hardware-reuse-eligible later, matching this project's own build discipline).

**Confidence:** Medium — the format-family similarity to FLAC is well-established audio-engineering
knowledge; the specific claim about how much of `fw/flac.c`'s actual code could be reused is an
estimate pending a real comparative read.

### Monkey's Audio (APE) — mentioned per the owner's ask, low-medium priority

**Relevance:** A real, long-standing lossless-audio enthusiast format (better compression ratio than
FLAC, at the cost of much higher decode CPU cost) — meaningfully present in a subset of audiophile/
archival libraries, but a smaller population than FLAC or ALAC users for this device's audience.

**Decode complexity:** Materially higher than FLAC. APE historically uses adaptive filters (a cascade
of NLMS-style adaptive predictors, not FLAC's fixed set of small LPC orders) which is a genuinely
different and heavier class of prediction than anything this codebase currently implements — this is
an estimate based on general knowledge of the format's public design, not a code-level comparison
(no APE reference decoder was read in this research pass). The heavier prediction stage would likely
dominate CPU cost on this platform's rv32im core far more than FLAC's own residual/LPC cost does today.

**New hardware:** Not assessed in detail; given the complexity gap from FLAC, any hardware kernel here
would be a from-scratch design exercise with no code-reuse path from the FLAC or MP3 kernels, similar
in spirit to how this project's own MP3_IMDCT scoping found IMDCT to be "materially harder" than the
already-built window unit despite being in the "same" codec.

**Confidence:** Low-medium; explicitly flagged as an estimate from general format knowledge, not a
codebase- or reference-decoder-grounded read.

### WavPack — mentioned per the owner's ask, low-medium priority

**Relevance:** Another real lossless-enthusiast format, notable for supporting a hybrid lossy+
correction-file mode; smaller community than Monkey's Audio or FLAC/ALAC for this audience.

**Decode complexity:** Estimated as moderate — WavPack's prediction scheme is generally described as
simpler than Monkey's Audio's adaptive filters but is still its own distinct bitstream and prediction
design, not directly reusable from FLAC's code. Not read against reference source in this pass.

**New hardware:** Not assessed; no obvious reuse path.

**Confidence:** Low; general-knowledge estimate only.

### AAC — low priority for this audience

**Relevance:** Enormously common in general media (streaming services, Apple's ecosystem, broadcast) —
but the owner's framing is correct to distinguish this from "common as a personal archival library
format for a lossless-capable-device audience." AAC is lossy; someone who owns this kind of device and
cares enough about lossless playback to want FLAC/ALAC support is unlikely to have deliberately
archived a personal library in a lossy format when better options exist. AAC's actual presence in this
audience's libraries is more likely a small tail of low-priority streaming-service downloads than a
primary format.

**Decode complexity:** AAC is MDCT-transform-based (like MP3's synthesis side and unlike FLAC's LPC/
Rice scheme), making it structurally closer to MP3's own IMDCT stage than to anything FLAC-shaped.
`docs/research/MP3_IMDCT_KERNEL_SCOPING.md` found MP3's own IMDCT to be "materially harder" than the already-
built window unit — two block-type-dependent transforms, AntiAlias, cross-granule overlap-add state —
and a full AAC decoder is generally a larger, more feature-laden design than MP3 (more profiles, more
transform block-size options, more prediction tools depending on profile). A software AAC decoder on
this platform would be a substantially bigger undertaking than FLAC or ALAC, closer in scope to "a new
MP3-sized effort" than an incremental addition.

**New hardware:** No existing reuse path today (the MP3 IMDCT kernel that AAC's MDCT stage might
eventually share logic with hasn't been built — it's scoped-not-built, same as FLAC's bit-reader
kernel). If MP3's IMDCT kernel is ever built, there may be *some* shared arithmetic-block potential
with AAC's MDCT (both are DCT-family transforms), but this is a genuine estimate, not something
grounded in either codebase.

**Additional concern not present for any other format on this list:** AAC decoding is generally
understood to require patent licensing in commercial contexts (unlike MP3, whose core patents have
now expired, and unlike the royalty-free FLAC/ALAC/Vorbis/Opus formats). This is a real practical
consideration for an open-source hobbyist core distributing to end users, separate from and in
addition to the technical effort — flagged here as something worth checking before any AAC work is
scoped, not resolved in this pass.

**Confidence:** Medium-high on the relevance judgment (matches the owner's own framing and general
market knowledge); medium on the complexity estimate (grounded in this project's own MP3 IMDCT
scoping precedent, but AAC's reference source itself wasn't read); the licensing flag is a general-
knowledge caution, not verified against current legal status.

### Ogg Vorbis — low priority

**Relevance:** A genuinely free/open lossy format with a real historical community (some archival/
personal libraries exist, particularly from users who deliberately avoided MP3's historical patent
status), but a small population relative to MP3 or the lossless formats for this specific audience —
someone choosing a portable lossless-capable player is more likely to care about FLAC/ALAC than about
Vorbis specifically.

**Decode complexity:** Also MDCT-based (same transform family as AAC and MP3's IMDCT stage), plus its
own codebook-based entropy coding and container structure distinct from either MP3 or FLAC. No
existing code in this repo to build from. Estimated as a large effort, similar order of magnitude to
AAC, without AAC's redeeming factor of the format at least being extremely widely used elsewhere.

**New hardware:** No reuse path from anything currently built.

**Confidence:** Low-medium; general-knowledge estimate, no source read.

### Ogg Opus — very low priority

**Relevance:** Opus is genuinely dominant in *modern* lossy use — real-time voice/video (WebRTC), and
increasingly the default for streaming-service lossy tiers — but essentially nobody uses it as a
personal *archival* library format the way this audience would use FLAC/ALAC/WAV. It's the clearest
case in this survey of "common in general use, essentially irrelevant to this specific audience's
actual library contents."

**Decode complexity:** Opus is a genuinely complex modern hybrid codec — SILK (a speech-optimized
linear-prediction coder, originally from Skype) for low bitrates/voice content, CELT (an MDCT-based
low-latency codec) for music/high-quality content, with dynamic mode-switching between and within
streams. There is no code-reuse path from anything in this codebase for either half — SILK shares
nothing with FLAC's LPC approach in implementation terms despite superficial conceptual similarity
(both are "linear prediction," but Opus's SILK is a full independent codec design), and CELT is a
distinct MDCT design from MP3/AAC's.

**New hardware:** None — no reuse path exists.

**Confidence:** Medium on complexity (well-documented as a genuinely hard modern codec in general
knowledge); high on the relevance judgment matching the owner's own explicit framing.

### DSD — very low priority, flagged as needing its own RTL scoping question before any decoder work

**Relevance:** A real audiophile/SACD-ripper niche format; present in some high-end audio libraries,
but a small population, and likely a smaller fraction of this specific device's owners than any
lossless-PCM format.

**Decode complexity / fundamental architecture mismatch:** DSD is not PCM at all — it's a 1-bit
sigma-delta pulse-density-modulated stream at very high sample rates (DSD64 = 2.8224 MHz, DSD128
double that, etc.), a fundamentally different signal representation from every other format on this
list. There are two conventional ways to play it:
1. **Software PCM conversion (DSD-to-PCM):** decimate/filter the 1-bit stream down to a conventional
   PCM rate before handing it to the existing output pipeline. This avoids needing new RTL (the
   existing `pcm_fifo.v` fractional-N path can carry the resulting PCM), but the decimation filter
   itself is a real, CPU/DSP-heavy signal-processing task — a large FIR filter running at a very high
   input rate — a different *kind* of decode cost than any bitstream-parsing codec on this list, and
   not something this research pass estimates a concrete cost for.
2. **Native DSD bitstream output:** would require the output path itself to carry a 1-bit stream at
   MHz rates instead of conventional PCM samples. `pcm_fifo.v`'s `rate_inc` phase-accumulator design
   is built for interpolating between conventional sample rates into a fixed I2S clock — nothing in
   the current RTL suggests it was designed to carry a raw high-rate 1-bit stream, and confirming
   whether the existing I2S path could be adapted to this (vs. needing genuinely new RTL) was not done
   in this pass — flagged explicitly as an open RTL question, not answered here.

Given this, DSD is the one format on this list where the honest answer is "the decode-complexity and
new-hardware assessment can't be finished without a dedicated scoping pass focused specifically on
whether the existing audio-output RTL can carry a DSD-rate stream at all" — treating it as a plain
software decoder like the lossy formats above would understate the real question.

**New hardware:** Likely yes for native output; the software-conversion path avoids new RTL but shifts
to a different (and not yet estimated) CPU cost.

**Confidence:** Low-medium on the general DSD-format facts (well-established); the RTL-carrying-
capacity question is explicitly unanswered, not merely uncertain.

### WMA — not scoped in detail

**Relevance:** Declining and already low; the owner's own framing ("only if still relevant") is judged
correct — this format is not worth detailed technical scoping effort at this time given every other
format above is either more relevant to this audience or, for the true niche cases (APE/WavPack), has
a stronger enthusiast-community rationale than WMA does today. Noted for completeness only.

## Closing recommendation

Following this project's own established "scope first, build only after approval" workflow (the
precedent set by `docs/research/MP3_IMDCT_KERNEL_SCOPING.md` and `docs/research/FLAC_BITREADER_KERNEL_SCOPING.md`), the
formats worth actually scoping next, in order:

1. **WAV + AIFF together, as a combined scoping pass.** These are not "hardware kernel" scoping docs
   in the sense the existing two are — there's no algorithm to characterize — but they still warrant a
   short design note covering: how file-type dispatch in `fw/player.c` should route to a PCM
   pass-through path instead of a decoder, what bit-depth/channel-count combinations to support at
   first (8/16/24-bit? mono/stereo only, or more?), and how this interacts with the existing playlist/
   library scanning code that currently assumes a compressed-format header it can parse for metadata
   (WAV/AIFF have only minimal built-in tagging, unlike FLAC's Vorbis comments or MP3's ID3). This is
   genuinely the lowest-risk, fastest-to-ship item in this whole survey and the clearest "just missing"
   gap in the current format list.
2. **ALAC**, specifically as a comparative-code-reading scoping pass against `fw/flac.c` (the same
   rigor `docs/research/MP3_IMDCT_KERNEL_SCOPING.md` applied by reading Helix's real `imdct.c` before
   estimating) — confirm or correct the "moderate complexity, real FLAC-code-reuse potential" estimate
   made here before committing to a build.
3. **Monkey's Audio or WavPack**, only if the owner wants to serve the deeper lossless-enthusiast
   niche beyond FLAC/ALAC — recommended as the *third* priority specifically because both estimates in
   this survey are general-knowledge-only (no reference decoder read), so a real scoping pass would be
   needed before even a rough effort number could be trusted, and the audience size for either is
   smaller than ALAC's.

AAC, Vorbis, Opus, DSD and WMA are not recommended for scoping at this time: each is either a poor
relevance match for this audience despite general-media popularity (AAC, Opus), a large effort with no
code-reuse path and a smaller enthusiast base than the lossless niche already covered by FLAC/ALAC/APE/
WavPack (Vorbis), an open RTL question before the decoder question even applies (DSD), or simply
low-priority by the owner's own standing (WMA).
