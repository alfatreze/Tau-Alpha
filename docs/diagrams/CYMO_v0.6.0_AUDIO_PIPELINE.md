# Cymo: v0.6.0 Audio Pipeline Architecture

## Overview

Cymo is the audio processing and decode pipeline for Tau-Alpha v0.6.0. It integrates hardware-accelerated decode kernels (MP3 window unit, FLAC LPC unit) with the Helios display framework for real-time audio playback with synchronized visual feedback.

## MP3 Decode Path

### Architecture

```
MP3 Frame Input
    ↓
[Synthesis Window Unit]
    ├─ FDCT32 Filterbank (tau_mp3_poly.sv)
    │  └─ 170-slot lookup table, verified bit-exact vs Helix
    ├─ Subband synthesis (85% of prior realtime cost)
    └─ Output: PCM samples
    ↓
[PCM FIFO + Resampler]
    ↓
[I2S Audio Output to Codec]
```

### Hardware Acceleration: tau_mp3_poly.sv (B-292)

**Function**: FDCT32 synthesis filterbank kernel for MP3 decode

**Metrics**:
- Reduces Subband compute from 55–59% to 22% of realtime
- 170-slot lookup table, verified bit-exact against Helix golden model
- Latency: One FDCT32 window per MMIO transaction
- DSP usage: 11/66 blocks on Cyclone V

**MMIO Interface** (0x100–0x110):
- R_POLY_IDX: Lookup index
- R_POLY_DATA: Lookup value (read-triggered compute)
- Sample readback with probe (`POLY_READY` boot check)

**Integration**:
- Boot probe: `fw/mp3_poly_hw.h` (POLY_READY check + fallback)
- Firmware macro: `POLY_FW` (default off, enabled in diagnostic builds)
- Bitstream macro: `TAU_POLY` (unconditionally compiled in, silently inert if not fitted)
- Fallback: Pure software synthesis window if hardware unavailable

**Hardware Cost Breakdown** (real mp3 tracks, measured on hardware):
- H: Huffman/bit-reading: 3–5%
- D: Dequantize: 8–12%
- A: Anti-alias: 10–15%
- X: Hybrid/Subband: 22% (hardware accelerated; 55–59% in pure software)
- Total: ~48–51% of realtime (was 70–85% pre-acceleration)

## FLAC Decode Path

### Architecture

```
FLAC Frame Input
    ↓
[Bit Reader]
    ├─ Rice decoding (variable bitrate)
    └─ Frame header parsing
    ↓
[Subframe Reconstruction]
    ├─ LPC (hardware-accelerated via tau_flac_lpc.sv)
    ├─ FIXED predictor (channel 0: 33–36%, channel 1: 63–66% of realtime)
    └─ Per-sample residual add
    ↓
[Decorrelation]
    ├─ Mid-side or left-right reconstruction
    └─ Channel merge
    ↓
[PCM FIFO + Resampler]
    ↓
[I2S Audio Output to Codec]
```

### Hardware Acceleration: tau_flac_lpc.sv (B-368/B-372)

**Function**: Per-channel LPC (Linear Predictive Coding) MAC engine

**Metrics**:
- Total FLAC decode: ~99% of realtime (genuine bottleneck)
- Channel 0 (typically L): 33–36% of total FLAC decode time
- Channel 1 (typically R): 63–66% of total FLAC decode time (1.8–2.0× worse than Ch0)
- Latency: 5 state cycles per LPC tap (IDLE → WARM → S_MULT → S_PUSH → DONE)
- DSP usage: 1/66 block (single 27×27 time-multiplexed slice, 45-bit accumulator)

**Design**: Single time-multiplexed DSP-block MAC (not parallel array)
- Per-tap history + coefficients in MLAB buffers (8 entries max, per FLAC spec)
- Deterministic state machine (no back-pressure, always ready for next tap)
- 45-bit accumulator validated across 20,000 synthetic legal-range corners + 15.6M real samples

**MMIO Interface** (0x120–0x13C):
- R_LPC_IDX: Tap index (auto-increment, sticky across samples)
- R_LPC_DATA: Tap result (write: coeff/history, read: MAC output with sample readback side-effect)
- Sample read triggers MAC cycle + coefficient load

**Integration**:
- Boot probe: `fw/flac_lpc_hw.h` (LPC_READY check + fallback)
- Firmware macro: `LPC_FW` (default off, enabled in diagnostic builds)
- Bitstream macro: `TAU_LPC` (unconditionally compiled in, inert if not fitted)
- Fallback: Pure software LPC reconstruction if hardware unavailable

**Verified Performance**:
- Synthetic validation (B-365/B-367): 45-bit accumulator sufficient for all legal FLAC ranges
- Real-file validation (B-366): 15,671,871 real LPC steps across 7 tracks (2 channels), 0 mismatches
- Hardware-measured split (B-361): Independent per-channel profiling reveals Ch1 ≫ Ch0, not commensurate

**Known Limitations**:
- ~99% of realtime is still a performance constraint (playback at 1.0× has zero margin for UI)
- Per-channel asymmetry (Ch1 at 63–66%) not yet exploited for optimization (future work)

## Helios Integration

### Vblank Synchronization

**CDC Synchronizer**: `tau_cdc_sync1.sv` (single-bit synchronizer)

**MMIO** (0xD0):
- `R_VBLANK`: 16-bit free-running frame counter (60Hz vblank edges, measured on hardware)
- Used by firmware to:
  - Gate meter/visualizer redraws to vblank edges (eliminate tearing)
  - Measure elapsed frames for profiling/diagnostics
  - Helios `helios_flush()` waits for vblank before flushing dirty regions

**Cost**:
- CDC latency: ~2 clk_sys cycles (60.667MHz, <33ns)
- Frame counter: Zero cycle cost (free-running)

### Beam-Aware Drawing

**CDC Synchronizer**: `tau_cdc_gray_bus.sv` (multi-bit Gray-code, pixel beam position)

**MMIO** (0xE8):
- `R_BEAM_X` / `R_BEAM_Y`: Current scanout position (gray-coded, safe for async reads)
- Used by firmware:
  - `helios_rows_safe()`: Query which rows are safe to draw without scanout collision
  - Meter visualizers gate writes to safe regions
  - Skip-and-retry on collision (graceful degradation, no tearing)

## Build Configuration

### Firmware Macros

```makefile
POLY_FW=1   # Enable MP3 window unit (default: off everywhere)
LPC_FW=1    # Enable FLAC LPC unit (default: off everywhere)
```

**Default Behavior**:
- `player` / `release` / `player-library-*`: POLY_FW=0, LPC_FW=0 (pure software)
- `player-library-diagnostic-profile`: POLY_FW=1, LPC_FW=1 (hardware accelerated)
- Diagnostic builds use hardware kernels for proof-of-concept and performance measurement

### Bitstream Macros

```verilog
TAU_POLY       // Unconditionally compiled, compile-out in mp3_poly.sv when 0
TAU_LPC        // Unconditionally compiled, compile-out in tau_flac_lpc.sv when 0
TAU_VBLANK     // Vblank CDC synchronizer
TAU_DBUF       // Double-buffering framebuffer (H2)
```

All are silently inert if not fitted in the bitstream (no firmware feature detection needed beyond boot probes).

## All6-Combined RTL Verification

### Quartus Fit Result (B-371): all6-combined-s1

**Timing** (all four Slow/Fast corners, positive on both setup and hold):
- Slow 85C:  setup +5.695 ns, hold +0.037 ns
- Slow 0C:   setup +4.582 ns, hold +0.118 ns
- Fast 85C:  setup +6.223 ns, hold +0.045 ns
- Fast 0C:   setup +5.109 ns, hold +0.162 ns

**Resource Usage**:
- M10K BRAM: 235–240/308 (RAM shrink + all6 features)
- DSP Blocks: 14–17/66 (MP3 filterbank + FLAC LPC + other hardware)

**Macros Bundled**:
- TAU_MLAB_MIGRATE (glyphbuf to MLAB)
- TAU_FONT_REPACK (dual-path font ROM, 0 block savings)
- TAU_BLIT (Talos tier 1 + B8/B9)
- TAU_SDRAM_BUSY (busy-cycle counter)
- TAU_RAM_192K (192 KB on-chip RAM shrink, if enabled separately)
- TAU_CLK66 (clk_sys 66.667 MHz, if enabled separately)
- TAU_POLY (MP3 window unit)
- TAU_LPC (FLAC LPC unit)
- TAU_VBLANK (vblank sync)
- TAU_DBUF (H2 double buffering)

**Conclusion**: Timing is clean across all macros. This is the first all6-combined fit with real Quartus evidence. v0.6.0 release candidate.

## Performance Summary (Hardware-Verified)

| Codec | Metric | Value | Notes |
|-------|--------|-------|-------|
| MP3 | Subband (hw) | 22% | reduced from 55–59% |
| MP3 | Total decode | ~48–51% | 70–85% pre-accel |
| FLAC | Total decode | ~99% | genuine bottleneck |
| FLAC | Channel 0 | 33–36% | LPC only |
| FLAC | Channel 1 | 63–66% | LPC-dominant, 1.8–2.0× worse |
| Audio | Vblank sync | 60 Hz | measured on hardware |
| Audio | Meter gating | Beam-aware | skip-and-retry graceful degradation |

## Open Items

1. **FLAC ~99% Performance**: Per-channel asymmetry (Ch1 ≫ Ch0) not yet exploited. Possible future optimizations: dedicated Ch1 MAC, or dual time-multiplexed design.

2. **Alpha Blend Timing**: Pipelined B5 (TAU_BLIT_BLEND) carries -2.972 ns worst-case violation. Shelved pending rework (retiming, DSP re-pipelining, or DSP_BLOCK_BALANCING).

3. **Track Changes Failure**: Pre-existing firmware bug (B-082 parked as "UX friction"), unrelated to hardware acceleration.

4. **glyphbuf MLAB Inference Regression**: Standalone blit fits target glyphbuf to MLAB; all6-combined fit falls back to M10K. No functional impact; flagged for future investigation.

---

**Document**: v0.6.0 audio pipeline and hardware acceleration architecture  
**Date**: 2026-10-26  
**Scope**: Audio decode, hardware kernels, Helios integration, all6-combined RTL verification  
