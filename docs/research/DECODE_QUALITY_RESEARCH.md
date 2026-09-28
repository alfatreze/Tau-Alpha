# Decode quality: what hardware acceleration does and doesn't change

Status: research only, 2026-09-27. No RTL, no firmware, no card/VM touched. Answers the owner's question:
"now that we have hardware [decode acceleration] for MP3 and FLAC, what's the potential for increasing
decode quality — 48kHz FLAC or more? 320 MP3?"

## 0. Three different questions, not one

The word "quality" conflates three separate things this project's evidence answers differently:

- **(a) Ceiling** — the highest sample-rate/bit-depth a file can play at all. Only FLAC has a real
  ceiling today (`FLAC_MAX_RATE`, below). MP3 has none.
- **(b) Already-solved cases** — formats/rates that work today with zero further work, just unconfirmed
  by a specific test. 320 kbps MP3 falls here.
- **(c) Headroom** — CPU margin freed by hardware acceleration that does NOT change what plays, only how
  much slack exists for other things (playback speed multipliers, UI/meter cost, future features). Both
  the shipped MP3 window unit and the not-yet-built FLAC kernel are headroom levers, not ceiling levers,
  under current evidence.

Only FLAC's ceiling can plausibly move, and only with new RTL not yet built, and its payoff cannot yet be
sized (see section 3).

## 1. MP3 — 320 kbps is already fully supported, unconditionally

`fw/player.c` has no bitrate or sample-rate gate on MP3 anywhere — confirmed by grep (searched for
`mp3.*rate`, `bitrate`, `mp3.*gate`, `MP3_MAX`; every hit is either a duration/seek calculation that
*reads* the decoded bitrate, or unrelated FLAC-rate code). Helix MP3 decodes any standard MPEG-1/2
bitrate (including 320 kbps CBR/VBR) and sample rate (32/44.1/48 kHz) in software, and nothing in this
firmware restricts that.

**This has already been played on real hardware.** `work/test-music/tau_sdram_wst/common/TAU A-102
stress/` contains `01 320CBR 44k1 stereo.mp3` and `02 320CBR 48k stereo.mp3`, and `docs/AUDIT_TRAIL.md`
A-102's session records (lines ~3869-3975) show both tracks played through multiple stress levels
(R0-R3) on the Pocket with 0 late underruns and clean CRCs across several runs. So: 320 kbps MP3 at both
44.1 and 48 kHz is not just theoretically unblocked, it has a real hardware play-through on record,
predating this cycle's MP3 hardware kernel.

**What the MP3 window unit (`tau_mp3_poly.sv`, `TAU_POLY`) changed:** it accelerates `Subband()` — the
polyphase synthesis filterbank / DCT32 stage in `third_party/libhelix-mp3/real/subband.c` — which
`docs/AUDIT_TRAIL.md` B-087 measured at 55-59% of MP3 decode cost pre-kernel. Hardware-confirmed on
alpha.30 (B-309): `Info > MP3 WINDOW` reads `HW n SLOTS 0 BAD`, and 1.75x playback speed now plays
cleanly where ~1.25x used to stutter. This is a **headroom** change (category c) — it makes MP3 decode
cheaper, which raises the speed multiplier ceiling and frees CPU for other things, but it never gated
which MP3 files could play. 320 kbps CBR/VBR MP3 needed nothing built this cycle; it was already correct.

**Recommendation:** no work needed. Optionally add one line to the release notes / README stating 320
kbps MP3 (any sample rate up to 48 kHz) is supported, since the owner's question suggests this isn't
obviously known. Effort: ~0. Payoff: closes the open question with no engineering.

## 2. FLAC — the real, measured software-only ceiling is 48 kHz

`fw/player.c:4105-4119` (`FLAC_MAX_RATE`) documents a real measured limit, not a design choice:

```
16/44.1 = 74% of realtime
24/44.1 = 80%
24/88.2 = 150%
24/96   = 180%
```

Cost tracks sample rate almost linearly. 48 kHz at 24-bit lands near 87%, which fits; everything above
48 kHz doesn't (88.2 kHz would need the decoder to be ~1.5x faster than the largest single optimization
found so far bought on one of its two passes). The code's own comment: "Refusing is the kind thing to
do... without this they play through to the end sounding broken." This measurement **predates any FLAC
hardware kernel** — nothing has changed it yet.

So today: 16/44.1, 24/44.1, and 24/48 kHz FLAC all play (48 kHz is the practical ceiling for hi-res).
88.2 kHz and 96 kHz FLAC are refused with a graceful in-app message (`ui_rate_unsupported()`).

**Whether hardware acceleration can raise this ceiling is a real, open, unanswered question** — see
section 3. It is the only lever in this document that could plausibly move the *ceiling*, not just free
headroom, but it does not exist yet even as a design.

## 3. FLAC hardware kernel — scoped, not designed, not built; payoff genuinely unknown

`docs/research/FLAC_BITREADER_KERNEL_SCOPING.md` (B-342) found a structurally favorable target: all FLAC bit
reading funnels through one 64-bit accumulator and three primitives (`bits()`, `sbits()`, `unary()`),
no block-type branching — closer in shape to the already-tractable MP3 window unit than to the harder
MP3 IMDCT stage (section 5). `unary()` — the Rice-code quotient scan, called on most samples in a frame,
its own source comment calls it "the hottest function in the decoder" — calls `__builtin_clzll` on a
64-bit value, but this firmware targets `rv32im` with **no hardware count-leading-zeros instruction**,
so GCC lowers it to a multi-instruction software routine on every call. This is a plausible explanation
for FLAC's overall 64-76%-of-decode bit-reading share (an older, coarser measurement folding
`bits`+`sbits`+`unary()` together), but **not yet separately measured**.

**The load-bearing gap:** B-343 built the measurement tool (`flac_unary_calls` counter + a boot-time CLZ
calibration, exposed as a `U<pct>` row via the bench UI and this week's `SR_T_DECPROF` Check/QR export) —
but per `docs/handoffs/SESSION_HANDOFF_2026-09-27_V050_AND_06_RTL.md` section 2, **no real hardware reading of
this split exists yet**. Nobody has played a FLAC track on a packaged build and read the `U` row. Without
that number, there is no way to say:

- whether `unary()` is actually the dominant cost (vs. `bits()`/`sbits()`/the byte-refill path — the
  scoping doc names this as a real alternative, not just a formality: if the byte-refill path dominates
  instead, the hardware unit would need a view into the input buffer, a materially different and larger
  design)
- how much realtime-percent a hardware leading-zero-count unit could plausibly recover
- therefore, whether it could plausibly move the 48 kHz ceiling up at all (to what — 88.2 kHz? partway?),
  since the cost curve is measured to scale ~linearly with sample rate and an unquantified partial
  speedup cannot be turned into a new ceiling number without knowing how large it is

**This document deliberately does not promise a new FLAC sample-rate ceiling.** Any number here would be
invented. The honest scope is: a hardware CLZ unit is plausible, cheap (a priority encoder over 64 bits
"no M10K needed" per the scoping doc), and structurally close to buildable — but its payoff is unscored
until the profiler split is actually read on real hardware.

## 4. A cheap, orthogonal lever that helps everything: clk_sys 66.667 MHz

`TAU_CLK66` (B-338/B-344) raises the system clock ~11.1% (60→66.667 MHz). **Timing-closed on real
hardware with real margin** (`clk66-b338`, both seeds, all four corners positive: seed 1 setup min
+0.978 ns / hold +0.097 ns, seed 2 +0.990 ns / +0.124 ns; RAM 299/308, DSP 11/66, unchanged from
baseline). Not yet combined with anything else or installed on a card (per the handoff doc's own table,
none of the five queued 0.6.0 RTL changes are combined or on hardware yet).

This gives roughly proportional CPU headroom to **every** decode path at once — MP3, FLAC, UI, meters —
for zero new kernel design work, unlike a codec-specific hardware unit which only helps that one codec's
one stage. It is a materially cheaper, lower-risk lever than the FLAC kernel, but it is also a smaller
and less targeted one: ~11% more headroom everywhere, vs. a kernel that (if `unary()` truly dominates)
could plausibly remove a much larger single-codec bottleneck. The two are complementary, not competing —
clk66 buys some margin immediately; a FLAC kernel, if it pays off, buys more, later, only for FLAC.

## 5. What's NOT on the table here: MP3 IMDCT

`docs/research/MP3_IMDCT_KERNEL_SCOPING.md` (B-341) confirms this only matters for headroom (category c — CPU
margin / playback speed), never for which MP3 files decode (MP3 already has no rate/bitrate ceiling, per
section 1). It was correctly scoped and NOT started: unlike the already-shipped window unit's one
fixed-shape 32-point transform, IMDCT selects between two different transforms per frequency band by
block type (a granule can be mixed long/short), plus AntiAlias/windowing/overlap-add state carried
across granules — a materially harder, multi-week design, not a quick follow-on. The existing profiler
folds it into one "I" bucket (9-13% of decode); no finer split exists yet either (unlike FLAC's, which
was built in B-345 but also not yet read on hardware). Correctly deferred behind the FLAC bit reader per
the owner's own standing roadmap order (`docs/ROADMAP.md` item 10, re-confirmed 2026-09-22 and again this
session).

## 6. On-chip resource headroom, for feasibility grounding

Recent fits (post-RAM-shrink, `ram192-blend-b333`, B-339): **240/308 M10K blocks used, 68 free** — a real
resource cushion that removes the M10K-scarcity constraint that had this deferred as "later" in earlier
design docs. Pre-shrink fits ran 298-304/308 (nearly full). DSP usage is 11-17 of 66 across recent
builds, also with headroom. **This resource state changes the "can we afford it" answer for both a FLAC
kernel and IMDCT — it does not change the design-complexity or measurement gaps above.** A FLAC
leading-zero unit ("no M10K needed" per its own scoping) barely touches this budget either way; it isn't
the blocker.

Caveat: the RAM shrink (192 KB) and `TAU_CLK66` currently share one `CORE_VERSION` chain and are
mutually exclusive in the same bitstream (per the handoff doc) — a real integration cost to account for
before assuming all these levers stack for free.

## 7. Ranked recommendations

1. **Read the FLAC profiler split on real hardware.** Play a FLAC track on an already-packaged alpha
   build (B-343's bitstream/firmware exist — `alfatreze.TAU_0_6_0_A_5` or equivalent per the handoff
   doc) and read the `U<pct>` row / `SR_T_DECPROF` Check QR export. **Effort: near zero — no RTL, no new
   firmware, just running an existing build and reading a screen.** Payoff: this is the single
   measurement that turns "plausible" into "designable" for the entire FLAC hardware kernel question —
   without it, nothing below can be sized. This is the single most important open question in this
   research.
2. **Adopt clk_sys 66.667 MHz** (combine `TAU_CLK66` into a real installed build, on its own or paired
   with other independent 0.6.0 changes per the handoff doc's table). Effort: low — already
   timing-closed, just needs packaging + a card install + a hardware soak. Payoff: ~11% CPU headroom
   across every decode/UI path immediately, general-purpose, no codec-specific design risk.
3. **Design a FLAC hardware CLZ/bit-reader kernel — conditional on #1's result.** If the profiler split
   shows `unary()` dominating FLAC's bit-reading cost (plausible, not yet confirmed), this is the one
   lever in this document that could plausibly raise the 48 kHz ceiling, not just free headroom. Effort:
   scoped as comparable to the MP3 window unit (days, not weeks, given the favorable uniform-shape
   structure) IF `unary()` is confirmed dominant; materially larger and different in shape if the
   byte-refill path turns out to matter more instead. Payoff: **cannot be quantified yet** — do not
   promise a specific new sample-rate ceiling until #1 lands.

Not recommended right now: MP3 IMDCT kernel (correctly deferred, only affects headroom/speed multiplier,
not ceiling, and is a multi-week design); any MP3 bitrate/rate work (already fully solved, see section 1).
