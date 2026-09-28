# FLAC LPC reconstruction kernel: design, resource fit, and verification plan

Status: **DESIGN ONLY (2026-09-28). Nothing built.** Supersedes `docs/research/FLAC_BITREADER_KERNEL_SCOPING.md`
as the active FLAC hardware plan — that document's own sections 5-6 already retargeted the effort from the
bit-reader (the original B-342 premise) to LPC reconstruction once real hardware data (B-360/B-361/B-363)
showed the bit-reader is a 4-11% minority cost and LPC reconstruction is 89-96% of channel 0's pass, with
channel 1 (fused bit-read + reconstruction + decorrelation, `subframe_stream()`) costing *more* than channel
0, not the same as `fw/flac.c`'s own inherited assumption held. Kept as a fresh, correctly-named document
rather than a fourth correction bolted onto a file titled after the wrong target.

**The gate this document answers "yes" to, with real evidence:** B-363's `t_pct` reading — FLAC decode
measures **~99% of realtime** on ordinary tracks (6 consistent readings, one session), essentially zero
margin, and directly correlated with the owner's own reported "constant low clicks" on FLAC that MP3 doesn't
have. Unlike the MP3 window unit (built for a comfortable, non-urgent CPU-saving win once M10K freed up),
this one is motivated by a real, audible, currently-happening problem.

## 1. What LPC reconstruction is (`fw/flac.c`, real and fixed-predictor subframes)

Per subframe, per channel, per sample beyond the warm-up: `out[i] = residual[i] + (sum(coef[j] * out[i-1-j])
for j in 0..order-1) >> shift`. Two variants share the shape: **FIXED** predictors (order 0-4, small fixed
integer coefficients, `int32_t` accumulate, no shift) and **LPC** predictors (order 1-32, coefficients up to
15-bit signed read from the stream, `int64_t` accumulate, then one variable right-shift 0-31 applied once).
Channel 0 runs this as two separate, timeable passes (`residual()` then the reconstruction loop, `subframe()`
~line 561-591); channel 1 (`subframe_stream()`) fuses bit-reading, the identical reconstruction math, and
stereo decorrelation into one interleaved per-sample loop that cannot be split apart in software (`fw/flac.c`'s
own comment) — but a hardware unit doing only the reconstruction step is equally callable from both sites,
since the math itself is identical; only the surrounding software (how residuals arrive, what happens to the
output afterward) differs.

**Why this dominates on `rv32im`:** the true-LPC accumulate is `int64_t`, and `rv32im` has 32x32 hardware
multiply (M extension) but no native 64-bit multiply — each 64-bit product needs several 32-bit
multiply/add instructions to synthesize in software. This runs once per tap per sample, unconditionally,
for the whole reconstruction — a fixed per-sample cost, unlike Rice decoding whose cost varies with
entropy. (B-360.)

## 2. What a hardware unit needs

* **A MAC:** signed coefficient (up to 15-bit) x signed sample history (up to ~25-bit: 24-bit audio, +1 for
  mid/side's `a<<1` in the decorrelation math one level up) into a **45-bit accumulate** (section 4,
  proven sufficient, not the 64-bit this document originally assumed before that check existed), then one
  variable right-shift (0-31, arithmetic, matching FLAC's own `p >> shift`) — no rounding, no clip (FLAC's
  spec does not round or clip the prediction sum; only `to16()` afterward clips, and only for the DAC path, not for
  the reconstructed sample itself, which must stay full precision for later taps to read as history).
* **History on chip, small:** up to 32 samples per channel, ~25-bit each — under 100 bytes. Coefficients:
  up to 32 x 15-bit signed, under 64 bytes. Both trivially fit a single MLAB block (the same
  `ramstyle="MLAB, no_rw_check"` forcing technique already proven on this device, `TAU_MLAB_MIGRATE`,
  B-100) — nowhere near M10K territory the way the MP3 window unit's 512-word-per-channel V history needed.
* **Throughput:** one reconstructed sample needed roughly every ~10-23 us (44.1-96kHz); the fabric clock is
  60-66.667MHz. Given section 6's revised framing (owner-validated, correcting this document's own earlier
  draft toward a parallel array): a **single time-multiplexed DSP slice**, walked sequentially through up
  to `order` taps (worst case 32 cycles), finishes a full per-sample prediction in well under 1 us —
  comfortably inside budget with one DSP block, not several.
* **Sequential across samples, not pipelineable:** `out[i]` needs the *final* reconstructed value of
  `out[i-1..i-order]` — a genuine data dependency (B-361/B-363's own analysis). The unit computes one
  sample fully, the result becomes new history, then the next sample starts. No cross-sample pipelining is
  possible or needed; the per-tap sum within one sample is where the (modest) parallelism lives, and even
  that is not required given the cycle budget above.

## 3. Does it fit? (trivial, unlike the MP3 window unit)

Nothing here approaches M10K pressure. One MLAB block (history + coefficients, well under the ~640-bit
chained-instance granularity B-120's own KB entry on MLAB chaining measured), one DSP block (the sequential
MAC), a handful of ALMs for the shift/control state machine. This is a much smaller ask than the MP3 window
unit's 4 M10K + 1-2 DSP, and should not compete with the RAM-shrink/blend/clk66/dbuf M10K budget at all.

## 4. Host symmetry / bit-exactness check — DONE (2026-09-28, B-365), result closes the open precision question

Built `sim/test_flac_lpc_symmetry.py` (wired into `make test-host`), mirroring `sim/mp3_poly_probe.c`'s own
method: synthetic random + explicit worst-case corners over FLAC's *real legal parameter bounds* (order
1-32, coefficient precision up to 15-bit signed — `prec == 16` is rejected by `fw/flac.c`'s own check,
sample magnitude up to 25-bit signed — 24-bit audio capped by `fw/flac.c`'s own bps check, +1 bit for the
wider side channel of a decorrelated pair), not guessed or left to random chance alone — the same choice
`mp3_poly_probe.c` made to stay CI-reproducible rather than depend on external music files.

**Result: a 45-bit signed accumulator is provably sufficient**, not the 64-bit width this document's
earlier draft assumed by default. Derived exactly (not estimated): worst case is `order(32) x
|coef|(2^14) x |sample|(2^24) = 2^43`, needing 45 signed bits including margin — verified against
`fw/flac.c`'s own `int64_t` arithmetic across 20,000 random legal-range cases, 27 explicit worst-case
corners (every combination of order in {1, 2, 32}, shift in {0, 15, 31}, and max-positive/max-negative/
alternating-sign coefficient and sample patterns), and the FIXED-predictor shape (order 1-4, no shift)
separately — every case exact, at 45, 48, and 64 bits alike (48 and 64 kept in the test as a margin check,
not because they're needed). **This directly resolves section 2's open sizing question**: 45 bits fits
comfortably inside a single Cyclone V 27x27 DSP block's native accumulator width, no per-tap chaining
required — a smaller, simpler unit than this document's own earlier framing assumed.

**Real-file confirmation — DONE (2026-09-28, same session).** `tools/flac_lpc_capture.py` reuses
`tools/flac_verify.py`'s own bit-exact parser (proven correct via whole-file MD5 checks) to capture real
`(order, shift, coefficients, sample-window)` tuples from actual FLAC subframes — both channels, since
`flac_verify.py`'s `subframe()` is called identically for each and channel 1's real-world math is the same
as channel 0's, only `fw/flac.c`'s own buffer-sharing optimization differs (confirmed by inspection, not
yet by decoding `subframe_stream()` specifically — see the remaining gap below). Run against all 7 real
files used in this session's Check measurements (MacCunn, Clementi, Aphex Twin, Rite of Spring 48kHz and
96kHz, Nausicaa Requiem 96kHz): **15,671,871 real-LPC prediction steps, 0 mismatches at 45, 48, or 64 bits.**
Real-world parameter ranges, worth recording: order only ever reached 2-12 across all seven files (real
encoders never approached the legal maximum of 32 in practice), coefficients got close to the legal bound
(max |coef| 16381 of 16384 — real encoders do use near-maximum precision), sample magnitudes stayed well
under the legal ceiling (max 2,750,763 of 16,777,216). This is a genuine, thorough confirmation on top of
the already-exhaustive synthetic legal-range proof, not just a formality.

**Remaining, smaller gap, not yet closed**: this captures real subframe data via the reference parser's
own generic `subframe()`, which does not replicate `fw/flac.c`'s specific channel-1 buffer-sharing
optimization (`subframe_stream()`) — the arithmetic is identical (confirmed by reading both), but a direct
capture from that exact code path, rather than an equivalent one, would be the fully rigorous version. Low
priority given the arithmetic match is already established both ways; worth doing before RTL, not blocking
the golden-model step.

## 5. MMIO register proposal — AS BUILT (B-369, `docs/MMIO_ALLOCATION.md`)

Reuses the sticky-index-load convention already proven twice on this device (`R_BLT_IDX`/`DATA`, B-103;
`R_CLUT_IDX`/`DATA`, B-148) rather than inventing a new interface shape. No separate `LPC_ID` register —
`LPC_STATUS`'s own bit 0 carries the build-present marker, same as `POLY_ST`'s bit 0.

| Offset | Name | Dir | Fields |
|---|---|---|---|
| 0x120 | LPC_CFG | W | `[5:0]` order (1-32), `[11:6]` shift (0-31) — written once per subframe, before any coefficient/warm-up load |
| 0x124 | LPC_COEF_IDX | W | `[4:0]` index 0..31 |
| 0x128 | LPC_COEF_DATA | W | signed coefficient at the index above (sign-extended internally); auto-increments the index, mirroring `R_CLUT_DATA`'s own convention |
| 0x12C | LPC_WARM_IDX | W | `[4:0]` index 0..31 — index 0 = the tap paired with the MOST RECENT sample (`fw/flac.c`'s own `coef[0]` convention, not oldest-first as an earlier draft of this table had it) |
| 0x130 | LPC_WARM_DATA | W | signed warm-up/history sample at the index above; seeds the internal ring once per subframe |
| 0x134 | LPC_RESIDUAL | W | next Rice-decoded residual; **starts computation** — the unit runs its sequential MAC over `order` taps, adds the residual, applies the shift, and pushes the result into the history ring (dropping the oldest) |
| 0x138 | LPC_SAMPLE | R | the just-computed reconstructed sample; read after `LPC_STATUS` shows done. **Reading this register is itself the acknowledgement** that clears `done` and lets the next residual be issued — wired straight to the bus's own one-cycle read-request pulse, not a separate write-to-ack step. |
| 0x13C | LPC_STATUS | R | `[0]` built-in, `[1]` busy, `[2]` done |

**One unit, two call sites:** channel 0 (`subframe()`) and channel 1 (`subframe_stream()`) both call the
same primitive — write `LPC_CFG` once, load coefficients/warm-up once per subframe, then one
`LPC_RESIDUAL` write + `LPC_SAMPLE` read per sample thereafter. Channel 1's own decorrelation math
(`EMIT`'s L/R computation) runs in software exactly as today, just on the value read back from `LPC_SAMPLE`
instead of a software-computed one — this is what makes the unit help channel 1 despite `subframe_stream()`
never being splittable into separate timed passes (B-361's own finding): the unit doesn't need the
software loop to be split, only the one MAC-heavy step inside it to be replaceable.

## 6. Fail-safe (same discipline as `BLIT_READY()`/`hw_poly`)

A boot-time or first-use probe (`LPC_READY()`-style): write a known small coefficient/warm-up/residual set,
read back `LPC_SAMPLE`, compare against the host-verified golden model's own expected value for that exact
input. Old bitstreams read `LPC_ID` as 0 and the probe fails cleanly; firmware falls back to the existing
software reconstruction loop per-subframe, same convention as `hw_poly`'s per-slot fallback. `CORE_VERSION`
does not need bumping (additive, inert without the macro, same reasoning as the PSRAM probe window).

## 7. Build order and status

1. ~~Host symmetry check~~ **done (section 4, B-365):** `sim/test_flac_lpc_symmetry.py` (synthetic,
   exhaustive over the legal parameter space, in `make test-host`) plus `tools/flac_lpc_capture.py`
   (real-file confirmation: 15.7M real prediction steps across 7 files, 0 mismatches). Result: **45-bit
   accumulator proven sufficient**, both by exhaustive legal-range proof and real-world data. Remaining,
   low-priority gap: a direct capture from `subframe_stream()`'s own code path rather than an equivalent
   one (section 4's note) — not blocking.
2. ~~Golden model~~ **done (2026-09-28, B-367):** `sim/flac_lpc_model.c` (portable C, no external decoder
   to link against — unlike the MP3 case, this model IS the reference), checked against an unbounded
   `int64_t` computation of `fw/flac.c`'s own real-LPC arithmetic across the same 20,000 random legal-range
   trials and 27 worst-case corners as the Python check (section 4) — an independent, second-language proof
   of the same claim, this project's own standing "prove it twice, differently" discipline. 0 mismatches, 0
   overflows. Writes 20,000 RTL testbench vectors to `build/rtl/flac_lpc_vectors.txt`
   (`sim/test_flac_lpc_model.py`, wired as a Makefile file-target dependency mirroring
   `sim/test_mp3_poly_model.py`'s exact pattern — not yet consumed by any testbench, since none exists).
3. ~~RTL + testbench~~ **done (2026-09-28, B-368):** `src/fpga/core/tau_flac_lpc.sv` — a 5-state sequenced
   machine (`S_IDLE→S_MAC→S_SHIFT→S_ADD→S_PUSH→S_DONE`), one multiply-add per clock into the 48-bit
   accumulator (section 2), never chained combinationally with the shift/add/history-push steps that
   follow (the project's own timing rule, section 3). `sim/tb_tau_flac_lpc.v` replays all 20,000
   `flac_lpc_vectors.txt` vectors bit-exactly (`make test-rtl-flac-lpc`) plus a dedicated hand-computed
   sequential test proving the history push/shift actually updates between successive predictions within
   one subframe (the main vector loop reloads warm-up fresh per vector and structurally cannot exercise
   this). 5 mutation hooks (`make test-rtl-flac-lpc-mutation`), all correctly caught — but 3 of the 5
   were NOT caught on the first attempt, a real finding worth keeping: a symmetric tap-order reversal
   (reversing both coefficient and history indices together) is mathematically undetectable since
   addition is commutative, so the mutation had to be made asymmetric (history side only) to be a real
   bug; two mutations (sign-extension-dropped, order-off-by-one) existed only as unused parameter
   branches and had to be actually wired into the arithmetic; and the no-history-push mutation needed
   the dedicated sequential test above, since the main vector-replay loop never exercises push/shift at
   all. `rtl-lint` clean (harmless unused-bit warnings only).
3.5. ~~Wired into `mp3_soc.v`~~ **done (2026-09-28, B-369):** new `LPC_ENABLE` parameter (generate-gated,
   inert netlist when 0, same convention as `POLY_ENABLE`), `TAU_LPC`→`TAU_LPC_EN` macro in
   `core_game.vh`, both `mp3_soc` instantiations updated, `tau_flac_lpc.sv` registered in `ap_core.qsf`.
   Register map at 0x120-0x13C (section 5, now AS BUILT — `docs/MMIO_ALLOCATION.md` updated). `sample_rd`
   is wired straight to the bus's own one-cycle read-request pulse (`d_req & d_is_mmio & ~dWE &
   (mmio_reg == R_LPC_SAMPLE)`) rather than a firmware-issued ack write, since the read of `LPC_SAMPLE`
   already IS the acknowledgement the module's own state machine needs to clear `done`. `make rtl-lint`,
   `make test-rtl` (full suite, including the real-CPU PSRAM fw/ifetch boot simulations that exercise the
   regenerated `mp3_soc_sim.v`) and `make test-host` all pass, 0 failures — confirms the wiring compiles
   and the rest of the SoC is unaffected. **Not yet done: any Quartus build** (section 3's cost estimate is
   unverified against a real fit) and no firmware caller exists yet, so this is currently dead weight on
   any bitstream that defines `TAU_LPC` — inert (reads 0) on every bitstream that doesn't.
4. ~~Firmware~~ **mostly done (2026-09-28, B-370):** `fw/flac_lpc_hw.h`/`.inc` (same separation
   `fw/mp3_poly_hw.h`/`.inc` uses for `subband.c`'s own redirect — `fw/flac.c` stays a portable,
   host-testable translation unit with no MMIO addresses in it), a boot probe (`hw_lpc`, `R_LPC_STATUS`
   bit 0) in `fw/player.c`, and the redirect wired into both `subframe()` and `subframe_stream()`'s
   reconstruction loops, gated by `TAU_LPC_FW` (`fw/build.sh`'s `LPC_FW=1`, default 0 everywhere — no
   Quartus fit or hardware result exists yet, so this stays opt-in only, the same caution `POLY_FW`
   itself observed before B-309's alpha.30 hardware confirmation). Info page row added
   (`fw/settingsui.inc`, "FLAC LPC": samples reconstructed / timeouts).

   Verified on the host, not assumed: `sim/flac_lpc_fw_harness.c` + `sim/test_flac_lpc_fw_redirect.py`
   (wired into `make test-host`) hand-build one valid, real LPC subframe (reusing
   `tools/flac_make_test.py`'s own bit writer) and drive `fw/flac.c`'s REAL `subframe()`/
   `subframe_stream()` code directly (via a `FLAC_TEST_EXPOSE`-gated test-only wrapper, never defined by
   `fw/build.sh`) six ways — `{subframe, stream} x {TAU_LPC_FW=0, TAU_LPC_FW=1 clean, TAU_LPC_FW=1 with an
   induced mid-subframe hardware failure}` — plus two more for an immediate (sample-0) failure. All eight
   must match a Python-computed expectation exactly; this proves the GLUE (warm-up-array reversal into
   the hardware's most-recent-first index convention, the residual/`out[]` handoff, the fallback control
   flow), not the arithmetic, which section 4's symmetry check and `tau_flac_lpc.sv`'s own testbench
   already proved two independent ways.

   **Two real bugs found and fixed by this test, not assumed correct:** (1) `subframe_stream()`'s
   hardware path originally called `tau_lpc_hw_sample(rice_next(f, &r), &ok)` directly — `rice_next()`
   as a bare argument consumes the residual from the bitstream unconditionally, so a hardware failure
   discarded that residual and silently desynced every sample after it; fixed by reading the residual
   into a local first and reusing it in the software completion for that one sample. (2) The fallback
   code's own `EMIT(...)` call already advances `i` internally (it is a `do {...} while(0)` macro whose
   last statement is `i++`); an extra explicit `i++` after it silently skipped a sample and corrupted the
   LPC history for everything after — found by the same fallback test case, fixed by removing the
   redundant increment. Both bugs were only in the NEW firmware glue, not in `tau_flac_lpc.sv`,
   `fw/flac.c`'s own pre-existing code, or the math.

   Every named `fw/build.sh` target still builds with `LPC_FW` unset (default 0): rebuild is
   byte-identical/idempotent (checked by hash, not assumed), confirming zero footprint on any shipped
   build. `LPC_FW=1` also builds clean (`player-library-diagnostic-profile` and `release` both checked).

   **Not yet done:** a `Check` test comparing hardware vs. software reconstruction on a real track, and
   any Quartus build/hardware test at all — `hw_lpc` will read false on every bitstream built so far, so
   `LPC_FW=1` currently only exercises the pure-software fallback path even when built, not the hardware
   path itself.
5. Synthesis-only check, then a real fit (should be cheap given section 3's trivial resource ask — likely
   foldable into the next scheduled fit rather than needing its own dedicated Quartus run), then hardware
   `t_pct`/`c1_pct` re-measurement (B-361's own instrumentation) to confirm the real-world CPU-time win —
   not started.

## 8. Cross-references

- `docs/research/FLAC_BITREADER_KERNEL_SCOPING.md` — the superseded original scoping (bit-reader premise),
  its own sections 5-6 are the paper trail for how the target moved to this document.
- `docs/AUDIT_TRAIL.md` B-360 (LPC dominance found), B-361 (`t_pct`/`c1_pct` built), B-363 (the real ~99%
  reading + channel-1-dominant finding that gates this document's "yes, pursue it" status).
- `docs/features/MP3_FILTERBANK_KERNEL_DESIGN.md` — the proven precedent this document's structure and
  verification discipline mirror throughout.
