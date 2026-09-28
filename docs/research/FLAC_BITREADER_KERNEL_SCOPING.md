# FLAC bit-reader kernel: scoping (nothing built)

**Superseded 2026-09-28 by `docs/research/FLAC_LPC_KERNEL_DESIGN.md`.** Sections 5-6 below are the paper
trail for why: real hardware data (B-360/B-361/B-363) found the bit-reader is a 4-11% minority cost and
LPC reconstruction — not scoped here at all originally — is the real 89-96% target, with channel 1 costing
*more* than channel 0. The successor document is the active plan; this one is kept for the history.

Status: scoping only, 2026-09-27 (B-342). Read the real `fw/flac.c` bit-reading code. No RTL, no
firmware change. Owner decision (this session): FLAC bit reader before any further MP3 kernel work
(confirms `docs/ROADMAP.md` item 10's standing order over IMDCT, see `docs/research/MP3_IMDCT_KERNEL_SCOPING.md`).

## 1. The shape (favourable, unlike IMDCT)

Every FLAC field this decoder reads goes through one 64-bit shift-register accumulator
(`f->bitacc`/`f->bitcnt`, `fw/flac.c`) and three primitives, all in the same file:

- **`bits(n)`** — return the next `n` bits (n <= 32), refilling the accumulator a byte (or a fast-path
  32-bit word) at a time from the input buffer.
- **`sbits(n)`** — `bits(n)` plus sign-extension.
- **`unary()`** — count leading zero bits up to the first 1 (Rice-code quotient); its own comment calls
  it "the hottest function in the decoder."

No block-type branching, no alternating algorithms, no per-band state — a single, uniform bit-window
engine. This is structurally much closer to the MP3 window unit's own tractability than to IMDCT's.

## 2. Why `unary()` is plausibly the real cost, not just "bit reading" in general

`unary()` calls `__builtin_clzll` on a 64-bit value. `fw/build.sh` compiles this decoder for
`rv32im` — **no B (bit-manipulation) extension, no hardware count-leading-zeros instruction** — so
GCC lowers a 64-bit `clz` to a multi-instruction software routine (typically a handful of compares/
shifts across the two 32-bit halves), executed on **every single Rice residual**, which is most of a
FLAC frame's samples. `FLAC.md`'s own 64-76%-of-decode figure covers `bits`+`sbits`+`unary()` together;
which of those actually dominates is not separately measured yet (same gap the IMDCT scoping found for
MP3's "I" bucket, and the same fix: a finer profiler split before committing to a design, in case the
byte-refill path or `bits()` itself turns out to matter more than the CLZ).

## 3. A plausible hardware shape (not designed, sketched only)

If `unary()` is confirmed as the dominant cost: a small MMIO unit that firmware feeds the current
64-bit window (or the raw input bytes directly, if the byte-refill logic moves too) and reads back the
leading-zero count in one cycle — a priority encoder over 64 bits is cheap in LUTs, no M10K needed,
and structurally simpler than any of the MP3 window unit's or IMDCT's arithmetic. `bits()`/`sbits()`
could stay in firmware (a barrel shift the CPU already does in a few cycles) or move too, as a second,
separate question once real numbers are in.

## 4. Recommended next step, not started

1. **Firmware-only, cheap:** split FLAC's profiler bucket (mirroring `mp3_profile.h`'s own convention)
   into `bits()`+`sbits()` vs `unary()`, measure on the existing Test Album + Hyperion FLAC tracks.
2. If `unary()` dominates as suspected: design and host-verify a leading-zero-count unit against real
   `unary()` output first (the same discipline `sim/mp3_poly_probe.c` used for the MP3 window), before
   any RTL.
3. If `bits()`/the byte-refill path matters more than expected: that is a different, larger design
   (the unit would need its own view into the input buffer), reconsider scope then.

This scoping pass stops here, at the owner's implicit continuation point — the next step (1) is
firmware-only and needs no Quartus time, so it does not compete with the `clk66-b338`/H2 fits already
queued.

## 5. Correction (2026-09-28, B-360): real hardware measurements invert the premise — LPC dominates, not bit-reading

Step 1 above was built (B-343, `SR_T_DECPROF2`'s `u_pct`) and run for real on 6 independent FLAC tracks
across two sessions (Hyperion pieces, MacCunn/Clementi classical, Aphex Twin electronic, a Rite of Spring
excerpt, a Nausicaa soundtrack cue — deliberately varied genre/complexity, not one file type). Every single
reading shows `r_pct` (the residual/bit-reader pass's share of `res+lpc`, `bits()`+`sbits()`+`unary()`
together, exactly this doc's own section 2 framing) at **4-11%**, meaning the reconstruction pass
(`flac_lpc_cyc` — the LPC/FIXED predictor's multiply-accumulate loop, `fw/flac.c` lines ~561-591) is
**89-96%** of the same measured pass. This is the opposite of `FLAC.md`'s original 64-76%-of-decode claim
for bit-reading, and — unlike the MP3 "I bucket" case (B-087) — the original files that produced that
number are confirmed gone (B-086), so a direct re-test on the same material isn't possible; what stands
instead is six consistent real-hardware readings across genuinely different content converging on the same
answer, which is stronger evidence than the single older claim it replaces.

**Why LPC plausibly costs this much, read from the code rather than guessed:** the real-LPC path
(`type >= 32`) accumulates in `int64_t` — `p += (int64_t)coef[j] * out[i-1-j]` — for every sample, every
tap (`order`, 1-32). `rv32im` has hardware 32x32 multiply (the M extension) but no native 64-bit multiply;
each 64-bit product term needs several 32-bit multiply/add instructions to synthesize in software. That
runs once per tap per sample, unconditionally, for the entire reconstruction — a fixed, guaranteed cost
per sample, unlike Rice decoding whose per-symbol cost varies with entropy. This is a plausible, code-
grounded explanation for LPC dominating even though `unary()`'s own software-CLZ cost (section 2's original
concern, confirmed real via `u_pct`) is genuine too — it is real, just not the *larger* piece.

**What this changes:** section 3's sketched hardware shape (a leading-zero-count/priority-encoder unit for
`unary()`) targets the *minority* cost. **If a FLAC hardware kernel is built at all, LPC reconstruction —
a small fixed-point multiply-accumulate/FIR-filter unit, not a bit-reader — is the target the real numbers
now point at.** This is structurally a much better match for Cyclone V's hardened DSP blocks than a
priority encoder would have been, and the project already has a proven, de-risked pattern for exactly this
shape of unit: `tau_mp3_poly.sv` (the MP3 hardware window/synthesis unit, B-290..B-309, hardware-confirmed).
Not the same transform (FDCT32 polyphase synthesis is a fixed block matrix-multiply, LPC reconstruction is
a variable-order sequential FIR predictor — genuinely different math, not literally reusable RTL) but the
same *pattern*: firmware stages the input into a small buffer, an MMIO-controlled unit runs the
multiply-accumulate through the FPGA's DSP blocks, firmware reads the result back — proven safe-to-adopt
via the same `hw_poly`-style boot probe / fail-safe discipline `tau_mp3_poly.sv` already established. A
real precision question is open and NOT yet scoped: FLAC LPC coefficients are up to 15-bit (`prec` capped
at 15, `type >= 32`'s decode above) against sample history up to 32-bit-ish (24-bit audio plus headroom
across a chain of predictions) — Cyclone V's hardened multipliers are natively 18x18 or 27x27, so whether
one tap fits in one DSP block or needs chaining is a real design question, the same class of scoping work
already done for the MP3 window unit and the PSRAM controller's own timing contract, not assumed free.

## 6. Three follow-on design questions and an external note, evaluated (2026-09-28)

Owner raised three specific questions and shared two pieces of external "food for thought." Evaluated each
against this project's real code and constraints rather than accepted at face value (the external notes'
citations, on inspection, read as decorative rather than real sourcing, and their headline architecture --
an FPGA-side SDRAM-DMA file streamer, and a "shared IMDCT/channel-mixer" both formats route through -- do
not match this core at all: `fw/player.c`'s own header confirms bytes arrive via the Pocket host's own APF
DMA into an on-chip RAM ring buffer, no FPGA-side SDRAM file streaming exists or is needed; and `track_fmt`
selects exactly one active decoder per track, so MP3 and FLAC never decode concurrently -- there is no
"two decoders contending for the bus" scenario here, and FLAC, already time-domain, never touched IMDCT to
begin with, so there is nothing to "bypass"). What follows is only the parts that survived that check.

- **Variable-width bit splitting over word boundaries** -- cheap and solved territory in hardware (a barrel
  shifter + refill FIFO, mirroring what `f->bitacc`/`f->bitcnt` already do in software). Real, but this is
  the bit-reading side, section 5's confirmed 4-11% minority cost -- not where the real budget goes.
- **Sequential recursion vs. pipelining in LPC** -- the sharp question, and it resolves cleanly from the
  code: `out[i]` needs the *final* value of `out[i-1..i-order]` (`fw/flac.c`'s reconstruction loop), so
  prediction cannot pipeline ACROSS samples -- a true data dependency, not an implementation choice. The
  per-sample tap sum (up to 32 independent coefficient x history products) has no such dependency and can
  run in parallel or be time-multiplexed. Given audio runs at 44.1-96kHz against a 60-100MHz fabric clock
  (hundreds of cycles available per sample), a **single time-multiplexed DSP slice**, walked sequentially
  through up to 32 taps (worst case ~32 cycles, comfortably inside the per-sample budget), needs far fewer
  DSP blocks than a parallel MAC array -- revises this doc's own earlier framing (section 5 originally
  reasoned toward a parallel array) toward the more resource-frugal shape, consistent with this project's
  standing preference for minimal DSP/M10K footprint everywhere else (the CLUT, the MLAB migration, the RAM
  shrink). Coefficients + the 32-sample history belong in a tiny dual-port MLAB block (<100 bytes, the same
  MLAB-forcing technique `TAU_MLAB_MIGRATE`, B-100, already proved works on this exact device), not SDRAM.
- **Predictor coefficient quantization and shift precision** -- real, and it is section 5's own already-
  flagged open question: coefficients up to 15-bit, an `int64_t` accumulate, one variable right-shift
  (0-31) applied once at the end (`fw/flac.c`'s real-LPC branch). Any hardware unit must reproduce this
  exactly -- same signed arithmetic, same shift -- to be bit-exact, the same discipline `tau_mp3_poly.sv`
  was held to via a host-verified golden model before any RTL was trusted. 15-bit coefficient x ~25-bit
  sample does not fit Cyclone V's native 18x18 DSP block directly; whether 27x27 mode or tap-chaining is
  needed is still unscoped, not assumed free.

**Revised sketch, not yet designed in full:** a small MMIO-staged unit -- firmware still does all Rice/bit-
reading and hands over coefficients + `order` + `shift` + the running history window; a single DSP-backed
MAC state machine walks the taps sequentially per sample (section 6's revision of section 5's parallel-array
framing); history/coefficients live in one MLAB block. This is the same *pattern* `tau_mp3_poly.sv` already
proved safe to adopt on this device (MMIO handshake, boot-probe fail-safe), sized down considerably given
the time-multiplexing insight. Still gated, per section 5, on first building the real percent-of-realtime
measurement (B-361) to confirm FLAC decode is worth accelerating on this hardware at all before any RTL.

**A real, still-open gap this correction does not close:** every number above (`r_pct`, and by extension
the implied LPC share) is a ratio *within* the `res+lpc` pass, not a percent of realtime the way MP3's
`h_pct`/`i_pct`/`s_pct`/`d_pct`/`a_pct`/`x_pct` are (`SR_T_DECPROF`/`DECPROF2`). FLAC has no equivalent
"percent of realtime" figure at all today — the retired `L` metric (B-086/B-087) covered this once but was
superseded, not replaced. Every FLAC track tested this session and last (6 different files, including a
96kHz one) has shown 0 late underruns and `audio_full: true` regardless — real evidence FLAC decode is
comfortably inside budget on everything tried so far, but not proof it is *cheap*, since the margin itself
is unmeasured. **Recommended next step, ahead of designing any FLAC hardware unit:** build FLAC's own
percent-of-realtime split (the same `MPROF_T0`/`MPROF_ADD`-style hook MP3 already uses, applied to the
whole `residual()`+reconstruction pass against wall-clock realtime, not just the ratio between its two
halves) — the honest first question is whether FLAC decode is even a real bottleneck on this hardware at
all before committing RTL effort to accelerating a specific piece of it.
