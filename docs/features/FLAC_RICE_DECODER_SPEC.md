# FLAC Rice decoder speed-up: specification

Status: **specification, not built into the firmware.** The design below was prototyped and measured on the host RV32 simulator (`tools/lab/flacperf/opt_rice.inc`, candidate "C1"); the prototype
is a measurement variant, not drop-in firmware. Target file: `fw/flac.c`. Branch of origin: `meter-builder`.

## 1. Purpose

Rice residual decoding (`unary()` + `bits()` + zig-zag) is the largest single cost of the software FLAC decoder, even with the hardware LPC unit on. Measured on the host simulator
(**MEASURED instructions, ESTIMATED cycles**): 139 of 223 instructions per channel-sample (62.6%), about 200 of 350 cycles (57%). The cause is not the algorithm but how it is expressed on RV32IM compiled `-Os`:
the 64-bit bit reservoir turns every shift into a libgcc call (`__lshrdi3`, `__ashldi3`) and the leading-zero count is `__clzdi2`; together about 38 instructions (about 56 cycles) per sample.

Goal: cut the **whole per-sample decode cost by about 45%** (cycles, **ESTIMATE**; instructions per channel-sample **MEASURED** 222.9 to 118.1, almost all of it the Rice part) with **bit-identical output and bit-identical bitstream consumption**, for about +0.2 KB of code.

## 2. Non-goals

- No change to LPC reconstruction, the hardware LPC glue, stereo decorrelation (`EMIT`), `to16()`, the sink, or the 64-bit reservoir type in `flac_t`.
- No new buffers, no allocation, no change to `flac.h`'s public interface or to the `flac_t` layout.
- No dependency on firmware headers: `fw/flac.c` stays portable C99, host-buildable.

## 3. Current behaviour (reference, must be preserved exactly)

`flac_t` holds a 64-bit reservoir `bitacc` with `bitcnt` valid low bits (MSB-first), refilled four bytes at a time by `need()` from `f->buf[f->pos..f->have)`, falling back to single bytes via `byte()` / `fill()`.

One Rice value with parameter `k` (not the escape code): `q = unary(f)` (count of zero bits before the first 1, consuming the 1), `r = bits(f, k)`, `v = (q << k) | r`, result `(v & 1) ? -(int32_t)((v >> 1) + 1) : (int32_t)(v >> 1)`.
Callers: `rice_next()` (one value at a time, used by `subframe_stream()` interleaved with the hardware LPC call), and `residual()` (a whole partition run into an array, used by the batch `subframe()` for channel 0).

## 4. Design

### 4.1 Fast path: a left-aligned 32-bit window

For the non-escape case, per value:

1. **Ensure at least 32 valid bits.** If `bitcnt < 32` and a whole 4-byte word is available in the input buffer (`pos + 4 <= have`), shift the reservoir left 32 and OR in the next word (big-endian, as `need()` does today); `pos += 4`, `bitcnt += 32`. If no whole word is available, go to the slow path (4.2).
2. **Cut a 32-bit window** `w` holding the next 32 unread bits left-aligned, with 32-bit operations only: let `s = bitcnt - 32` (0..31), `w = s ? (hi << (32 - s)) | (lo >> s) : lo`, where `hi`/`lo` are the high and low halves of the reservoir.
3. **If `w != 0`:** `q = clz32(w)`; `used = q + 1 + k`. **If `used <= 32`:** `t = (w << q) << 1` (drops the `q` zeros and the terminating 1; two shifts so a shift count of 32 never occurs); `r = k ? t >> (32 - k) : 0`; `v = (q << k) | r`; `bitcnt -= used`; return the zig-zag decode. No overflow: `used <= 32` implies `q + k <= 31`, so `q << k < 2^31`.
4. **Otherwise** (`w == 0`, or `used > 32`): slow path.

`clz32` must be an **inline** function, not `__builtin_clz` (which compiles to a libgcc call under `-Os` on RV32IM): a binary search (16/8/4/2/1) after three quick top-bit checks for `q = 0, 1, 2`, which are the common quotients. Measured: `__builtin_clz` variant -37.6% cycles; inline clz with the small-quotient checks -45.1% (w1) / -46.3% (w2) (**ESTIMATE** cycles, **MEASURED** 118.1 instructions per sample).

### 4.2 Slow path (exactness by construction)

Anything unusual falls back to the **existing, unmodified** `unary()` and `bits()`: no whole word left in the buffer, 32 or more consecutive zero bits, or `q + 1 + k > 32`. Before the call the local state (`acc`, `cnt`, `pos`) is written back into `flac_t`; after it, reloaded. Because the slow path is the current code, every such case consumes exactly the same bits and handles end-of-stream and corrupt-stream guards (the `1 << 20` zero-run bound in `unary()`) exactly as today.

### 4.3 State in registers

Within a run of values, keep `acc`, `cnt`, `pos`, `have` in locals and write them back to `flac_t` once at the end of the run (or around a slow-path call), instead of loading and storing `f->bit*` per value. For `residual()` and the new `rice_block()` the run is a whole partition; for `rice_next()` it is a single value (the write-back is then per value; keep it to the three fields that change).

### 4.4 Entry points

- `residual()` and a block routine `rice_block(f, r, out, n)`: process `n` residuals from the current partition state, opening partitions (`bits(pbits)`, escape handling, empty partitions, `order` warm-up samples subtracted from the first partition) exactly as `rice_next()` does. The escape partition (`param == escape`) stays on `sbits()`.
- `rice_next()` becomes a thin inline wrapper: partition bookkeeping as today, then the fast value decode.
- `subframe_stream()` keeps calling one residual at a time (the hardware LPC sample depends on it), so it uses the per-value wrapper.

## 5. Invariants and edge cases (all must hold)

- Output samples identical to the current decoder for every valid and corrupt input; **bitstream position after every call identical** (the frame CRC check and the next frame sync depend on it).
- `k = 0` (no remainder bits), `k` up to 30 (the 5-bit parameter method; 31 is the escape), `q = 0` (a 1 bit first), long unary runs (including exactly 31 and 32 zeros, and more than 32), partitions of length 1, empty partitions, escape partitions with raw width 0..31, `order > 0` first-partition shortening, the end of the input buffer falling inside a value, a value straddling the reservoir refill boundary, `bitcnt` anywhere from 0 to 63.
- Truncated streams must end with the same error (`FLAC_ERR_SHORT`) at the same point: `f->eof` handling untouched because that code lives in the slow path.
- `FLAC_PROFILE` builds: the `flac_unary_calls` counters currently increment once per `unary()` call and drive the `U` row of Info and the `SR_T_DECPROF2` field. The fast path must increment them once per value (inside `#if FLAC_PROFILE`) so the profile figure keeps its meaning.
- No new global state; no dependence on the sink or the LPC unit; the TAU_LPC_FW redirect and its fallback path (`sim/test_flac_lpc_fw_redirect.py`) are unaffected.

## 6. Cost and size

| Item | Value |
|---|---|
| Whole per-sample decode (Rice is 62.6% of it today) | **MEASURED** 222.9 to 118.1 instructions per channel-sample (-47%); about -45% cycles (**ESTIMATE**, VexRiscv model, all cache hits) |
| CPU load of a 48 kHz stereo decode | about 50% to about 28% of the 66.67 MHz core (**ESTIMATE**: 50% x (1 - 0.451)) |
| `flac.o` growth | about +0.2 KB |
| Stack | a few extra locals; no arrays |
| Hot-RAM budget | `fw/flac.c` runs from hot RAM: check `tools/check_heap_gap.py` (the diagnostic builds are near their floor); keep the slow-path helpers `unary()`/`bits()` as they are |

Not covered by the model: cache misses, MMIO latency, `meters_feed()` and the UI. Real hardware will give somewhat less.

## 7. Verification plan

1. **Bit-exactness on the host, new test in `make test-host`:** a randomized differential test of the new routine against the current `unary()`/`bits()` implementation over generated bitstreams (random `k` 0..30, random quotients with a tail of long runs, random alignment and buffer-end positions), comparing every decoded value and the final `bitacc`/`bitcnt`/`pos`. Include the corrupt-stream cases (long zero runs, truncation).
2. **End to end:** the existing FLAC vectors and references (`sim/test_flac*.py`, `tools/flac_ref.py`, `tools/flac_verify.py`, `sim/flac_lpc_fw_harness.c` with `TAU_LPC_FW` 0 and 1) must still pass, and decoded PCM checksums must match the current decoder on real files (MacCunn 48 kHz 16-bit, Aphex 44.1 kHz, a 24-bit file, mono, all four stereo modes).
3. **Performance:** re-run `tools/lab/flacperf` (instruction counts per component) before and after on both workloads and record the result next to this spec.
4. **Hardware:** one Pocket run on the hard file (FLAC 415K 48.0K): Info `HEADROOM` idle and projected speed (`WS`), `UNDERRUNS n ALL m`, and the decode-profile row (`R`, `U`). Acceptance: audio checksums already proven identical on the host; on the Pocket no new error counters, no audible change, and idle strictly higher than the same build without the change.

## 8. Acceptance criteria

- All host tests pass, including the new differential test; no change to any existing test's expected output.
- Output and bitstream consumption identical to the current decoder on every test input.
- Measured instructions per channel-sample for the whole decode reduced by at least 35% on both host workloads (prototype: -47%).
- Heap-gap check passes (or the growth is accounted for), `fw/flac.c` still has no firmware includes.
- On the Pocket: higher idle on the hard file than the same build without the change.

## 9. Risks

- **Subtle reservoir bookkeeping** (shift counts of 0 or 32, `cnt` 0..63): mitigated by the differential test and by routing every unusual case to the unchanged slow path.
- **Profile counter meaning** changing: mitigated by counting per value in profile builds (section 5).
- **Hot-RAM growth** on builds near the heap floor: about 0.2 KB, to be checked with `tools/check_heap_gap.py`.
- **Measured gain smaller on hardware** than in the model (cache behaviour, MMIO latency): the host numbers are a guide; the Pocket run decides.

## 10. Related, not part of this spec

Per-stereo-mode `EMIT` specialisation (-7 to -9%, +3.6 to +7.4 KB), overlapping the hardware LPC call with the next residual decode (-1% to -12%, depends on the unit latency), and compiling `flac.c` at `-O2` (-23%, +8.7 KB) were also measured; they are not recommended as part of this change (code size, and the LPC overlap needs the real unit latency measured first).
Detail and tables: `docs/features/AUDIO_HEADROOM_AND_METER_THROTTLE.md` section 4; harness: `tools/lab/flacperf/`.
