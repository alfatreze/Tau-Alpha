# FLAC bit-reader kernel: scoping (nothing built)

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
