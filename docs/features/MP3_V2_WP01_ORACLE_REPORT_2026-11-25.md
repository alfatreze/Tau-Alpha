# MP3 v2 WP-01: pristine minimp3, the float oracle, rv32 size, the plan's [FETCH] claims (2026-11-25)

**Status: report (work package WP-01 of `docs/features/MP3_DECODER_V2_PLAN_2026-11-25.md`).** Host-only: no firmware, RTL, fit, card or VM was touched. **Evidence labels:** **[MEASURED]** run in this session (host build and run, rv32 build run on the project's RV32IM simulator `tools/mp3_v2_measure/rvprof.c`, used read-only); **[READ]** read in the downloaded file; **[CONFIRMED / CORRECTED / UNVERIFIABLE]** verdicts on the plan's [FETCH] statements. Nothing ran on a Pocket.

## 1. What was vendored

`third_party/minimp3/`: `minimp3.h`, `minimp3_ex.h`, `LICENSE`, `UPSTREAM.md`, `UPSTREAM.sha256` (mirrors `third_party/libhelix-mp3/`). Source: `git clone` of <https://github.com/lieff/minimp3> into an empty scratch directory, `git checkout 48e3a085f0e1a99446b34614e1e7f17f09dc2e82` (committed 2026-11-22). Files are byte-identical to upstream at that commit; `minimp3.h` SHA-256 `eb4b4481...875a` equals the value B-1168 recorded. `LICENSE` is the full CC0 1.0 Universal legal code (read; `<http://creativecommons.org/publicdomain/zero/1.0/>`); both headers carry the dedication ("the author(s) have dedicated all copyright and related and neighboring rights to this software to the public domain worldwide"). No author name or credit line is in `minimp3.h` (URL only). `.gitignore`: the "evaluated and rejected" comment is replaced (minimp3 is adopted by D-C01) and only `third_party/minimp3/vectors/` plus a stray `.git/` stay ignored. The plan's `LICENSE` hash `6a1ee543...d01c9` is a slip; the file ends `...e94d01` (full value in `UPSTREAM.md`).

## 2. The float oracle

`tools/mp3_v2/oracle.c` (+ `oracle.py` build/run wrapper): own code around the pristine header, no change to minimp3 files. Built with `-O2 -ffp-contract=off -DMINIMP3_NO_SIMD -DMINIMP3_ONLY_MP3 -DMINIMP3_FLOAT_OUTPUT` (`-ffp-contract=off` so an arm64 host cannot fuse multiply-adds: the output is the same on any host). It decodes a whole file (skips ID3v2 by declared size plus the footer flag, strips ID3v1 and APEv2 tails as `minimp3_ex` does) to 16-bit PCM (`mp3dec_f32_to_s16`, minimp3's scalar converter), a float dump, and a JSON summary (frames, decoded, hz, channels, pairs, float peak, samples beyond 1.0, rail samples). Checked [MEASURED]: the 16-bit output equals a build without `MINIMP3_FLOAT_OUTPUT` byte for byte on a 6,694-frame Test Album file; it is deterministic (PCM and float dump identical on two runs, `sim/test_mp3_v2_oracle.py`); on four generated streams (MPEG-1 stereo, MPEG-2 LSF stereo, mono short blocks, joint stereo with mixed blocks) it agrees with ffmpeg's decoder within 2 LSB at lag 0 (this also validates the stream builder `tools/mp3_v2/synth_stream.py`, whose Huffman table 1 and side-information layout were written from the standard). The "matches Helix within 1 LSB RMS on the Test Album" gate of the dispatch table is **not** run here: the brief asked for the oracle and its determinism test, and the Helix differential is WP-04/WP-06 work.

## 3. rv32 size, stack and soft-float [MEASURED]

Build: `riscv-none-elf-gcc` 15.2 from the project toolchain, `-march=rv32im -mabi=ilp32 -mno-relax -ffunction-sections -Wl,--gc-sections`, `MINIMP3_ONLY_MP3`, no SIMD, **int16 output, float internals** (the "float build"), harness `tools/mp3_v2/rv32_harness.c`, report `tools/mp3_v2/rv32_size.py`. Three Test Album files, 300 frames each.

| | `-Os` | `-O2` |
|---|---|---|
| `.text` total (incl. libgcc soft-float 7,404 B and the harness) | 24,452 B | 26,612 B |
| minimp3 functions only | 16,464 B (`mp3dec_decode_frame` 11,872; `L3_dct3_9` 952; `L3_imdct12` 816; `L3_imdct36` 760; `mp3d_synth_pair` 740) | 18,424 B |
| `.rodata` | 8,144 B (`tabs` 4,328 = **2,164 int16 entries**; `g_win` 960; `g_pow43` 580; `g_scf_*` 824 total; `__clz_tab` 256 from libgcc) | 8,128 B |
| `.bss` | 11,276 B (`mp3dec_t` + a 4,608 B PCM frame) | 11,276 B |
| static stack of `mp3dec_decode_frame` (`-fstack-usage`) | 16,656 B | 16,656 B |
| measured worst-case stack (watermark) | 16,960 B | 16,944 B |

Layer I/II (build without `MINIMP3_ONLY_MP3`, `-Os`): code +1,564 B, rodata +344 B, stack +704 B (17,360 B static, 17,792 B measured).

Per frame on the RV32IM simulator (instruction counts exact; cycle model only):

| File | Instructions per frame | Soft-float (libgcc) calls per frame | Of which `__mulsf3` / `__addsf3` / `__subsf3` / `__clzsi2` | Share of instructions inside libgcc |
|---|---|---|---|---|
| 02 MacCunn 128 kb/s 44.1 stereo | 10,761,315 | 150,294 | 58,192 / 45,835 / 25,043 / 14,219 (+ 2,304 each `__gtsf2`, `__ltsf2`, `__fixsfsi`) | 89.8 % |
| 05 MacCunn 320 kb/s 48 stereo | 10,935,272 | 150,014 | 59,095 / 44,962 / 24,164 / 14,776 | 89.8 % |
| 11 Speech 128 kb/s mono | 8,152,473 | 115,071 | 47,094 / 35,856 / 16,244 / 8,878 | 89.1 % |

That is 39 times the 274 k instructions of Helix as Player 2 ships it (plan section 2.1): 13.2 M model cycles per stereo frame against about 1.74 M cycles available per frame at 44.1 kHz and 66.67 MHz. A **variant with the window loop of `mp3d_synth_granule` removed** (a scratch copy of the header, never the vendored file; this is the "window in the unit" situation, the DCT-II and everything else still software): stereo 63,272 calls (22,480 `__mulsf3`, 18,547 `__addsf3`, 16,115 `__subsf3`, 6,037 `__clzsi2`), 4.35 M instructions per frame; mono 30,705 calls, 2.30 M instructions.

## 4. The plan's [FETCH] claims, one by one

| Claim (plan section) | Verdict | Evidence |
|---|---|---|
| Pinned commit `48e3a085...`; `minimp3.h` SHA `eb4b4481...875a`; LICENSE is CC0 1.0 | **confirmed** (LICENSE hash in the plan: **corrected**, see 1) | files hashed and read |
| No author/credit in `minimp3.h` | **confirmed** | header lines 1-8 |
| SIMD: guards `HAVE_SSE`/`HAVE_SIMD`, NEON guard `__ARM_NEON \|\| __aarch64__ \|\| _M_ARM64 \|\| _M_ARM64EC`, `MINIMP3_NO_SIMD`, auto `MINIMP3_ONLY_SIMD` on x86_64/aarch64, `have_simd()` with `minimp3_cpuid`/`__cpuid`, `HAVE_ARMV6` `ssat` | **confirmed** | lines 87-205 |
| Six functions have SIMD and scalar versions (`L3_midside_stereo`, `L3_antialias`, `L3_imdct36`, `mp3d_DCT_II`, `mp3d_synth`, `mp3dec_f32_to_s16`) | **confirmed** | `#if HAVE_SIMD` at each |
| `HAVE_ARMNEON` does not exist | **confirmed** | grep |
| Layer I/II: five `L12_*` functions, tables `g_alloc_L1`, `g_alloc_L2M2`, `g_alloc_L2M1`, `g_alloc_L2M1_lowrate`, `g_bitalloc_code_tab`, `g_deq_L12`, `L12_scale_info` on the stack; `ONLY_MP3` removes it | **confirmed** (names); sizes **corrected**: removing it saves **1,564 B code (not 3-4 KB), 344 B tables (about 0.4 KB, confirmed), 704 B stack (0.9 KB estimated)** | section 3 |
| `g_scf_long` 8x23, `g_scf_short` 8x40, `g_scf_mixed` 8x40 = 824 B | **confirmed** | 184 + 320 + 320 |
| `g_win[240]` 960 B; `qmf_state` 960 floats; `syn[33][64]` 8,448 B; `mdct_overlap[2][288]`; `mp3dec_t` about 6.7 KB; `mp3dec_scratch_t` about 16.3 KB on the stack with `maindata[2815]`, `grbuf[2][576]`, 4 `L3_gr_info_t` | **confirmed** | struct text lines 17-25, 232-239; `.bss` 11,276 = `mp3dec_t` + PCM; static stack 16,656 |
| `g_pow43` float[145] | **confirmed** | 580 B |
| `tabs` count "not determined by the fetch; expected 1.9-2.1 k" | **now measured: 2,164 entries (4,328 B)**, slightly above the estimate | `nm -S` |
| `tabindex[32]` int16, `tab32[28]`, `tab33[16]`, `g_linbits[32]`, `g_pan[14]`, `g_expfrac[4]`, `g_preamp[10]`, `g_scfc_decode[16]`, `g_scf_partitions[3][28]` | **confirmed** | lines 659-787, 967 |
| `hdr_compare` does not compare bitrate or channel mode | **confirmed** | compares version/layer bits, sample rate and free-format only |
| Header-change handling: a changed bitrate or channel mode alone keeps state, other changes zero it | **confirmed** | `memset(dec, 0, ...)` on a failed compare |
| Reservoir underflow returns 0 samples; side-info error calls `mp3dec_init` and returns 0 | **confirmed** | `return success*hdr_frame_samples(...)`; `mp3dec_init(dec); return 0;` |
| CRC: 16 bits read and discarded, never computed | **confirmed** | `get_bits(bs_frame, 16)` |
| `mp3d_match_frame` up to 10 following headers; ID3v1/null tail ends a match; `MAX_FRAME_SYNC_MATCHES` 10; `MAX_FREE_FORMAT_FRAME_SIZE` 2,304 | **confirmed** | lines 50-55, 1684-1724 |
| Only `memcpy`, `memset`, `memmove`, `memcmp`; no assert, no math.h, no malloc | **confirmed**; **new**: `fw2/platform/libc_min.c` has `memcpy`, `memmove`, `memset` but **no `memcmp`** (needed by `hdr_is_id3v1` and `mp3d_match_frame`; the harness supplies its own). `<stdlib.h>` is included but unused | build log |
| Unaligned 16/32-bit loads in the bit and table code: "believed byte-wise" | **confirmed** | `get_bits` reads bytes; no pointer cast to a wider type exists in `minimp3.h` (grep). The compiled rv32 code was not scanned for unaligned loads the compiler itself may generate; the project's `-mno-relax -march=rv32im` build has no strict-align flag, so that audit (plan WP-13) stays |
| `minimp3_ex.h`: ID3v2 skip by declared size, footer +10, no unsynchronisation, no extended header, APE footer, no VBRI, TOC skipped, `mp3dec_load_index` full scan, stdio/mmap/ring | **confirmed**, with three details the plan lacks: (1) it detects an ID3v2 tag only if the low four flag bits are zero and all four size bytes are below 0x80; (2) its APEv2 skip ignores the "header present" flag, so a tag with a header leaves 32 bytes; (3) it **does** parse Xing/Info with LAME delay and padding: delay = field + 529, padding = field - 529 (`mp3dec_check_vbrtag`), which is consistent with the standard's 529 in plan 3.5 | `minimp3_ex.h` 163-285 |
| "About 59 k float operations per frame even with the unit" (audit B-1170) | **confirmed**: 57.2 k float multiplies, adds and subtracts (63.3 k libgcc calls incl. the normalising helper) per stereo frame with the window removed; the full float decoder needs 129 k float operations (150 k libgcc calls) | section 3 |

## 5. minimp3's test vectors: licence status and the files that depend on them

The upstream `vectors/` directory is 26 MB: `l1-*` and `l2-*` (Layer I/II), `l3-*` (Layer III: `l3-compl`, `l3-he_32khz`, `l3-he_44khz`, `l3-he_48khz`, `l3-he_mode`, `l3-hecommon`, `l3-si`, `l3-si_block`, `l3-si_huff`, `l3-sin1k0db`, `l3-test45`, `l3-test46`, plus `l3-nonstandard-*` for ID3, APE, VBR tags and side-info edge cases), `ILL2_*`, `ILL4_*`, `M2L3_*` (MPEG-2 Layer III), `fuzz/`, and `performance/` (`MEANDR90.mp3`, `MEANDR_PHASE0.mp3`, `MEANDR_PHASE90.mp3`, `MIPSTest.mp3`, `noise_meandr.mp3`). **Licence status:** the repository has one `LICENSE` (CC0 1.0) and no per-file or per-directory statement about the vectors; the upstream README mentions them only as conformance data. The names `compl`, `he_*`, `hecommon`, `si`, `si_block`, `si_huff`, `sin1k0db` match the names of the ISO/IEC 11172-4 conformance bitstreams as far as I remember them [MEMORY, not checked], which would be third-party (ISO or Fraunhofer) material that a repository-wide CC0 file cannot relicense. Unresolved; they were **not** copied into the repo and are git-ignored.

**What `sim/common/test_mp3*.h` contain.** `test_mp3.h` (9,601 bytes): the comment says "CC0 synthetic performance test vector, from minimp3 vectors/performance/MEANDR90.mp3", 320 kb/s 48 kHz (the worst case of the Stage 0 benchmark). `test_mp3_128.h` (32,768 bytes): "CC0 compliance vector: minimp3 vectors/l3-sin1k0db.bit (first 32 KB)", a 1 kHz sine at 128 kb/s 44.1 kHz; `sin1k0db` is one of the ISO-style names above, so this is the header most likely to be third-party material. **Dependents** (every file that includes them), for the owner's ruling on D-C02 D4: `sim/fw_helix/main.c` (includes both; the Stage 0 Helix benchmark firmware), `sim/fw_minimp3/main.c` (includes `test_mp3.h`; the Stage 0 minimp3 benchmark firmware). Both are built only by `sim/build_fw.sh helix|minimp3` (Stage 0 measurement scripts, not part of `make test-host` and not in any shipped image). `docs/ATTRIBUTIONS.md` line 45 lists them as "minimp3 test vectors, CC0, test data, not shipped"; the other mentions are research documents and provenance CSV/JSON inventories (`docs/research/**`, `docs/features/MP3_DECODER_V2_PLAN_2026-11-25.md`). Removing the two headers would break only `sim/build_fw.sh helix` and `minimp3`. Vendoring `third_party/minimp3/` also makes `sim/build_fw.sh minimp3` buildable again (its `MINIMP3` path is that directory).

## 6. What was added and what runs

`third_party/minimp3/*`, `.gitignore`, `tools/mp3_v2/{oracle.c, oracle.py, rv32_harness.c, rv32_size.py, synth_stream.py}`, `sim/test_mp3_v2_oracle.py` (17 checks: file hashes against `UPSTREAM.sha256` and `UPSTREAM.md`, CC0 text, ignore rules, red change detection, 40-frame generated stream decoded twice with identical PCM, float dump and summary, a different stream decodes differently), wired into `make test-host`. Reproduce the size report: `eval "$(python3 tools/toolchain.py export)"; python3 tools/mp3_v2/rv32_size.py <scratch> 300 <file.mp3> [--opt -O2] [--with-layer12] [--inc <dir>]`.
