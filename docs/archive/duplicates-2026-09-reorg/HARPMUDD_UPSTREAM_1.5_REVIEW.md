# HarpMudd upstream v1.5.0 review — what to port, what to drop

**Scope correction, checked against git, not assumed:** Tau's fork point is `git merge-base main v1.5.0`
= `7ef8f0f` ("ROADMAP: backlog QR scrobble export"), the last upstream commit **before** the "Merge v1.5.0"
commit. Tau did not fork at an early "v0.3.0" baseline the way an earlier note (B-102) implied — that was
Tau's *own* v0.3.0 release tag colliding by name with upstream's identically-named tag. **Tau forked right
before v1.5.0 landed, so nothing in v1.5.0 has ever reached Tau.** This review covers all of it, not just
the CJK font work B-102 already flagged.

**Also found:** upstream has an active `release/1.5.1` branch beyond the `v1.5.0` tag, already carrying real
hardware-measured work toward a v1.6.0 (a `clk_sys` frequency bump, FLAC-load/meter-cost management). The
single highest-value finding in this review comes from there, not from v1.5.0 itself — flagged explicitly
below so it isn't mistaken for part of the named release.

Verification method throughout: `git fetch upstream` (already configured, read-only), then `git diff`/
`git show` against the fork commit and against Tau's current `main` — every claim below is checked against
real diffs and Tau's own current source, not summarized from commit messages alone.

## Summary table

| Item | Source | Verdict | Why |
|---|---|---|---|
| **`clk_sys` 60 -> 66.667 MHz** | `release/1.5.1` (pre-1.6.0) | **PORT — but as its own Quartus experiment, not a drive-by edit** | +11.1% CPU headroom, hardware-measured, same PLL/VCO constraint Tau shares exactly; two silent-failure traps upstream found are present in Tau's code *verbatim*. Real risk to Tau's own hard-won blit-engine timing margins — needs a fresh fit, not assumed safe. |
| **PCM FIFO start-of-track cushion** | v1.5.0 | **PORT — low risk, self-contained** | Fixes a hardware-confirmed track-start glitch (empty-FIFO glide-then-jump). Tau's `pcm_fifo.v` is byte-identical to the fork point — zero merge risk. |
| **Meters yield to audio (`meter_afford()`)** | `release/1.5.1` (pre-1.6.0) | **PORT — firmware-only, buildable right now** | A software CPU-budget technique, no RTL, no dependency on the blit engine or `blit_probe_ensure()` — can be built and tested *while the Blit Test hang is still unresolved*, unlike the meter/blit integration currently on hold (B-182). |
| **Tag encoding: guess UTF-8 vs Windows-1252 for encoding 0** | v1.5.0 | **CONSIDER — small, independent win** | Tau's `id3_text_body()` currently treats encoding-0 bytes as raw Latin-1 with no UTF-8 detection, so a UTF-8-tagged file can render mojibake today even within the existing ASCII-only font. Real but narrow improvement; not blocked on the CJK font. |
| **Extended CJK/multi-script font (`mp3font.bin` in SDRAM)** | v1.5.0 | **PARKED, confirmed by independent evidence** | Already recorded (B-102, `PHASE_F_SPEC.md` section 13). New finding: upstream shipped it as an **SDRAM asset via a data slot**, not on-chip ROM — independently confirms Tau's own B-102/B-100 conclusion that PSRAM/SDRAM, not on-chip repacking, is the only real path to font-related blocks. |
| **Stop polling the card during playback** | v1.5.0 | **DROP (direct port); CROSS-REFERENCE (principle)** | Upstream's exact mechanism (periodic slot-identity polls) doesn't exist in Tau's architecture (media library + APF bridge, not simple playlist-slot polling). The underlying principle — never issue a blocking card command mid-decode without FIFO/ring headroom — is worth checking against Tau's own known-synchronous track-open path (B-093, parked pending the blit engine). |
| **Boot speedup (drop unconditional 250 ms sleep)** | v1.5.0 | **CONSIDER — cheap, needs a source check** | Real, near-zero-risk win upstream. Tau's own boot is currently dominated by the ~15 s PSRAM art decode (B-054), so the absolute payoff is smaller here, but worth a 10-minute look at whether `read_track_head()`'s equivalent path carries the same unconditional-sleep bug. |
| **Cassette meter (new visualizer)** | v1.5.0 | **DROP for now** | Pure aesthetic addition. Tau already has 8+ visualizer modes and a much larger meter/UI backlog (Tier 2 blit primitives, the held meter/blit integration); a new visualizer is Tier-3-equivalent work, not worth displacing anything on the current plan. |
| **Lyrics (`.lrc` sidecars, data slot 4)** | v1.5.0 | **DROP — not even shipped upstream** | Explicitly pulled from v1.5.0 by its own authors ("Pull lyrics out of 1.5.0"), still unreleased as of `release/1.5.1`. Nothing to port yet. |
| **VBR seek** | v1.5.0 | **DROP — deferred upstream too** | Same as lyrics: upstream's own commit says "deferred to ship with lyrics." Watch, don't act. |
| **Stack cut to 9 KB / heap audit** | v1.5.0 | **DROP — superseded** | Tau's own Phase G (cold code, PSRAM instruction fetch, the whole heap-gap/stack-peak-measurement discipline in `PHASE_F_SPEC.md` section 4.1) is already a materially more thorough answer to the same problem. Nothing to learn here that Tau hasn't already built past. |
| **FLAC cascade/decode-cost measurement work** | `release/1.5.1` (pre-1.6.0) | **CROSS-REFERENCE only** | Upstream is mid-investigation into its own FLAC underrun cause on this same hardware family; not a ready patch, but corroborating evidence that FLAC decode cost is a real, hardware-confirmed pressure point on this exact platform — relevant background for Tau's own still-gated audio-kernel decision (`PHASE_F_SPEC.md`: "audio kernels stay gated on a decoder profile"). |

## 1. `clk_sys` 60 -> 66.667 MHz — the highest-value finding, needs its own experiment

**Source:** `release/1.5.1` commit `27d5b26` ("Phase 2: clk_sys 60 -> 66.667 MHz, and the two places that
hardcode it"), hardware-measured, not simulated.

**Why it transfers directly.** Upstream's own reasoning: all four PLL outputs share one VCO; the 12 MHz
pixel clock must be *exactly* 60.000 Hz and the SDRAM controller needs 100 MHz, which pins the VCO at
600 MHz and leaves `clk_sys` = 600/N — 60, 66.67, or 75 MHz, nothing between, and 75 exceeds the real Fmax
(closes with only 69 ps). **Tau's own PLL setup is the identical constraint**, confirmed by reading
`core_game.vh`'s own header comment: `outclk_0 = 60 MHz CPU/system`, `outclk_1`/`outclk_2` = 12 MHz pixel
(exact 60.000 Hz), `outclk_3` = 100 MHz SDRAM — the same four-output, one-VCO plan, not a coincidence (both
descend from the same original core). Upstream's measured result on THIS constraint: Slow 0C setup improved
from +1.423 ns (60 MHz) — wait, corrected: **improved** to +1.423 ns at 66.67 MHz from a +2.168 ns baseline
at 60 MHz (still positive, i.e. real margin, not a regression) — and explicitly debunks its own prior
"66.67 MHz costs hold margin" claim as placement noise, not a real effect.

**The two silent-failure traps upstream found are present in Tau's code verbatim, not just conceptually:**
- `src/fpga/core/mp3_soc.v:746`: `eq_biquad #(.CLK_HZ(60_000_000), .RATE_HZ(48_000)) u_eq (...)` — a hardcoded
  instantiation parameter that only feeds `DIV = CLK_HZ / RATE_HZ`. A stale value neither fails to build nor
  to run — it just paces every EQ preset's corner wrong (upstream's own estimate: 11% high, inaudible-ish but
  wrong).
- `src/fpga/core/mp3_soc.v:778`: `pcm_rate <= 32'd3435974; // 48 kHz at clk_sys = 60 MHz` — the exact same
  reset-default hardcode upstream had to fix.
- Confirmed a third: `fw/player.c:924`'s own comment, `/* R_CYCLES is a 32-bit 60 MHz counter: it wraps every
  71.58 s. ...`, matches upstream's PRE-bump figure exactly — the same cycle-counter-wrap idiom
  (`(int32_t)(cycles() - deadline) >= 0`) is used throughout Tau's firmware (`CLK_HZ` appears 50+ times in
  `fw/player.c`, almost entirely as `* n`/`/ n` forms that would scale automatically) — but every deadline
  needs auditing against the new, shorter wrap period (64.4 s instead of 71.6 s) the same way upstream
  enumerated rather than assumed.

**What is NOT free about porting this.** Tau's RTL has diverged far more than upstream's own baseline by the
time of this measurement: `core_game.vh` (+450 lines), `mp3_soc.v` (+486 lines), `mp3_fb.sv` (+747 lines) —
all of Tau's own SDRAM Phase 2 CPU window, PSRAM controller, and the entire Phase F blit engine did not exist
when upstream made this measurement, and were themselves fought over for timing margin repeatedly this
session (B-109/B-111/B-116/B-150/B-157 — four separate near-zero-margin paths found and fixed, all in
`clk_sdram`-domain logic that a `clk_sys` change does not directly touch, but the CPU-side interfaces to it
— MMIO register timing, the CDC paths `tau_cdc_gray_ctr.sv` builds — run at `clk_sys` and have never been
fit at any rate other than 60 MHz). **This is not a safe assumption to carry over — it needs its own
dedicated Quartus experiment** (the same "synthesis-first, isolate one variable" discipline this whole
session's blit-engine timing work has used), not a drive-by edit alongside other work. The payoff if it
holds is real and directly relevant to Tau's own still-open question ("audio kernels stay gated on a decoder
profile that has never been run" — `PHASE_F_SPEC.md`): +11.1% CPU headroom changes that calculus materially.

**Recommended next step, not started:** a synthesis-only (not full-fit) sanity check of `clk_sys` at
66.667 MHz combined with Tau's current full G3+blit configuration, checking specifically whether the four
previously-marginal paths (`glyphbuf` write network, BAR/SBLIT/CHAR dispatch, `A_COMPOSE`) still close — the
same cheap-first-experiment pattern B-152's `generate`-gated check already used successfully.

## 2. PCM FIFO start-of-track cushion — low risk, self-contained, worth porting soon

**Source:** v1.5.0 commit `4d396bf`. **Symptom (upstream, hardware-confirmed):** after a flush, the FIFO
drains from the moment the first sample arrives, so every track start runs nearly empty — an underrun at
second 0 with a ~50-sample discontinuity, heard as glide-to-silence-then-jump, on tracks with no underrun
anywhere else. **Fix:** wait for the FIFO to reach half (~23 ms at 44.1 kHz) before the first sample leaves;
cleared only by flush or reset, so mid-track underrun behaviour is unchanged; the priming glide deliberately
does not raise the underrun flag (it is carrying silence on purpose, not failing).

**Why this is a clean, low-risk port for Tau:** `git diff <fork-point> -- src/fpga/core/pcm_fifo.v` against
Tau's current `main` is **empty** — Tau has never touched this file. The upstream patch is small (30 lines in
`pcm_fifo.v`, a 106-line testbench) and entirely self-contained; it does not touch SDRAM, PSRAM, MMIO, or any
of Tau's own additions. Tau's own Check infrastructure already tracks "late underruns" as its audio-safety
verdict (`CT_R1-3`/`CT_AUD`/`CT_BLT` all use this convention) — a start-of-track discontinuity of this shape
would very plausibly show up in Tau's own testing as an unexplained isolated underrun at track start,
never specifically diagnosed as this class of bug. Worth checking Tau's own Check history for exactly that
signature before porting, but the fix itself needs no Tau-side redesign — port the RTL, port (or adapt) the
testbench, verify with `make test-rtl`, no firmware change needed since `pcm_flush()`'s existing contract is
unchanged from the caller's point of view.

## 3. Meters yield to audio — firmware-only, buildable right now, complements the held meter/blit work

**Source:** `release/1.5.1` (pre-1.6.0) commit `8f5eb11`. Upstream's spectrum/cassette meters run a
per-sample octave cascade costing ~6% of the CPU — the same order as the margin their most demanding FLAC
files are short of. `meter_afford()` reads PCM FIFO/ring headroom exactly the way `audio_cushion()` already
gates card polling, with two hysteresis thresholds (stop below 1/3 full, don't resume until 2/3) so the
meter doesn't flicker on/off at a single threshold — and degrades correctly by construction: a skipped
window holds the bands at their last value rather than decaying toward nothing, so "the meter updates less
often" not "the meter goes wrong."

**Why this is directly relevant to Tau right now, not just an interesting pattern:** this is the *opposite*
direction from the currently-held meter/blit-engine integration (B-182) — instead of making the meter's
*hardware draw cost* cheaper (which needs the blit engine, which needs `blit_probe_ensure()`, which is the
function implicated in the unresolved Blit Test hang), this makes the meter's *CPU compute cost* self-limit
when audio margin is tight, and it is **pure firmware, zero RTL, zero dependency on the blit engine or the
suspect probe function**. It can be designed, built, and host-verified immediately, without waiting on ISSP.
Tau's own spectrum filter bank is cheaper than upstream's cascade to begin with (`PHASE_F_SPEC.md` section 7:
"the shipped software octave filter bank... it already runs at 1.5% CPU" vs upstream's ~6%), so the acute
need may be smaller for Tau today — but the *pattern* (afford-check before spending CPU on cosmetic work,
hold-last-value degradation) is cheap, general-purpose good practice worth adopting regardless, and directly
strengthens exactly the audio-safety margin the blit-engine integration is itself trying to protect.

## 4. Tag encoding: UTF-8 vs Windows-1252 guess for encoding 0

**Source:** v1.5.0 (bundled with the CJK font work, but logically separable). Checked against Tau's own
`id3_text_body()` (`fw/player.c` ~8186): for ID3 encoding byte 0 (ISO-8859-1/ambiguous), Tau currently copies
bytes through as raw Latin-1 with no detection — a genuinely UTF-8-tagged file (common; many taggers write
encoding 0 with UTF-8 bytes despite the spec technically calling for Latin-1) can render mojibake today
(each UTF-8 continuation byte read as its own wrong Latin-1 character) even within Tau's existing ASCII-only
font, where the correct behaviour would be a clean `?` substitution for the non-ASCII character, matching
what Tau's own UTF-16 path already does deliberately (`fw/player.c`'s own comment: "a code unit that does
not fit becomes '?', which loses an accent but keeps the title"). **This is a real, narrow, currently-
reachable correctness fix, independent of the CJK font question** — Tau's own BUG-001 (accented *filenames*,
`docs/issues/001`) is a different problem (FAT-volume path Unicode normalization, not ID3 tag decoding) and
this does not fix it, but is worth doing on its own merits.

## 5. Corroboration: CJK font belongs in SDRAM, not on-chip — independently confirms B-102/B-100

Upstream's extended font (`mp3font.bin`, 833 KB, 22,800 characters) ships as an **SDRAM asset loaded through
a data slot**, not baked into `font_rom.v` the way Tau's current ASCII font is. This independently confirms
what Tau's own B-100/B-102 already concluded from the opposite direction (measuring that on-chip repacking
delivered +0 blocks, not the hoped +4): **PSRAM/SDRAM is the only real path for extended font storage on
this device family**, not a Tau-specific conclusion. No new action — already parked correctly in
`PHASE_F_SPEC.md` section 13.

## Status update, 2026-09-24: four of five ported

Owner decision: bring in the PCM FIFO cushion, meters-yield-to-audio, the UTF-8/Windows-1252 tag guess, and
the cassette meter. Park the `clk_sys` bump explicitly until after the blit engine and the M10K/RAM-shrink
track are done, at a minimum. All four built, verified, and documented in `docs/AUDIT_TRAIL.md` B-184.

One real regression found and fixed along the way, worth recording here since it's a genuine correctness
issue this port exposed rather than introduced: the pre-existing `sim/tb_pcm_decay.v` (unrelated to this
session, already in the tree) pushed one sample at a time, which never reaches the new priming cushion
(half the FIFO) — with the cushion in place, its first `while (!empty)` wait never returns, hanging the
testbench indefinitely. **Upstream's own `tb_pcm_decay.v`, checked directly against `release/1.5.1`, has
the identical unfixed bug** — their cushion commit added a new `tb_pcm_fifo.v` but never touched the
pre-existing decay test, so this looks like a latent hang in their own tree too, not something this port
introduced. Fixed on the Tau side by priming once before the first decay check (`push_prime()`, a burst
crossing the cushion), then leaving every later `push_sample()` in the file untouched — `primed` is cleared
only by flush or reset, neither of which this testbench ever asserts again, so one burst up front is enough.

## What this changes on the current plan

Nothing here reorders the standing priorities (`PHASE_F_SPEC.md` section 14, the Blit Test hang, the held
RAM-shrink track) — but it adds two genuinely low-risk, high-value items that don't compete with any of that
work for the same resources:

1. **Item 3 (meters yield to audio)** can be built *immediately*, in parallel with waiting for JTAG cable
   access — it is firmware-only and has zero dependency on `blit_probe_ensure()` or any suspect code path.
2. **Item 2 (PCM FIFO cushion)** is a small, self-contained, zero-merge-conflict RTL port worth doing on its
   own schedule, independent of the blit engine entirely.
3. **Item 1 (`clk_sys` bump)** is the one item worth real attention once the Blit Test hang is resolved and
   a Quartus slot is free for a dedicated experiment — the highest single potential payoff in this review
   (+11.1% CPU headroom, hardware-precedented on the same PLL constraint Tau shares), but it needs its own
   verification pass against Tau's own blit-engine timing margins before being trusted, not a quick port.
