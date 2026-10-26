# Halcyon: v0.6.0 Hardware/Software Implementation Architecture

Scope: the hardware-software implementation (RTL kernels, MMIO, firmware integration). Not the UI or Tau Omega.

## 1. Hardware feature layers

### MP3 window unit (`tau_mp3_poly.sv`, B-292)
- FDCT32 synthesis filterbank kernel, 170-slot lookup table.
- Verified bit-exact against the Helix golden model (`sim/mp3_poly_model.c`, 170 slots, four mutants killed).
- Reduces Subband from 55-59% to 22% of realtime.
- MMIO 0x100-0x110. Macro `TAU_POLY`.

### FLAC LPC unit (`tau_flac_lpc.sv`, B-368/B-372)
- Sequenced MAC state machine: IDLE, WARM, S_MULT, S_PUSH, DONE. One operation per clock.
- Single time-multiplexed DSP-slice MAC; 45-bit accumulator is sufficient (48-bit implemented). Fits one Cyclone V 27x27 DSP block.
- Validation: 20,000 synthetic legal-range cases + 27 worst-case corners (B-365/B-367), 15,671,871 real LPC steps across 7 files, 0 mismatches (B-366).
- History in one MLAB block; `hist_mem` has a single driver (B-372 fixed a dual-driver synthesis failure Icarus tolerated but Quartus rejected).
- MMIO 0x120-0x13C, sticky index+data auto-increment; `sample_rd` is a read-triggered side effect. Macro `TAU_LPC`.
- Per-channel cost (B-363): channel 0 33-36%, channel 1 63-66% of realtime; total FLAC about 99% of realtime.

### Talos blit engine
| Op | Status |
|----|--------|
| B1 generalised blit | built |
| B2 colour-key transparency | built |
| B4 scaled blit (OP_SBLIT) | built |
| B6 column fill (OP_BAR) | built |
| B8 CLUT blit (256 entries, OP_CBLIT) | built, hardware-proven |
| B9 palette re-index | built |
| B11 OP_RRECT (rounded rect) | built, timing-fixed (B-231) |
| B5 alpha blend | pipelined (B-327), closes timing on its own fit; shelved earlier at -2.972 ns |
| B12-B17 | designed, not built |

### Helios
- Vblank: `tau_cdc_sync1.sv` + frame counter, MMIO 0xD0, measured about 60 Hz on hardware.
- Beam position: `tau_cdc_gray_bus.sv`, MMIO 0xE8, `helios_rows_safe()`; skip-and-retry on scanout collision.
- Other CDC: `tau_cdc_gray_ctr.sv` (SDRAM busy counter, MMIO 0xBC).

## 2. MMIO register map (summary)

| Address | Function |
|---------|----------|
| 0xBC | SDRAM busy counter |
| 0xC0 / 0xC4 | Blit descriptor index / data |
| 0xC8 / 0xCC | CLUT index / data |
| 0xD0 | Vblank level + frame counter |
| 0xDC / 0xE0 / 0xE4 | Spectrum bank |
| 0xE8 | Beam position |
| 0x100-0x110 | MP3 window unit |
| 0x118 / 0x11C | Double buffer (H2) |
| 0x120-0x13C | FLAC LPC unit |

Authoritative source: `docs/MMIO_ALLOCATION.md`.

## 3. Firmware integration
- Wrappers: `fw/mp3_poly_hw.h`, `fw/flac_lpc_hw.h/.inc` (separate files because `flac.c` is its own compile unit).
- Boot probes: `POLY_READY()`, `LPC_READY()`; software fallback if the bitstream lacks the unit.
- Redirects: `Subband()` for MP3; `subframe()` and `subframe_stream()` for FLAC.
- B-370 fixes: `subframe_stream()` consumed the residual before checking hardware success (bitstream desync on failure); a redundant `i++` after `EMIT` skipped a sample. Both caught by `sim/test_flac_lpc_fw_redirect.py` (8-way on/off x clean/mid-failure/early-failure comparison).
- Build macros: `POLY_FW`, `LPC_FW` (firmware, default 0; enabled for diagnostic targets). `TAU_POLY`, `TAU_LPC` (RTL, inert when not fitted).

## 4. all6-combined RTL (v0.6.0 candidate)
Bundle: RAM shrink 192 KB (`tau_main_ram.sv`), clk_sys 66.667 MHz, pipelined alpha blend, H2 double buffering, MP3 window, spectrum.
Quartus fit (`all6-combined-s1`, B-371): Successful, 0 errors, setup +5.695 ns, hold +0.037 ns (hold is razor-thin). M10K 235-240/308, DSP 14-17/66.

## 5. Open items
1. FLAC about 99% of realtime; a kernel must serve both channels (channel 1 dominates).
2. `TAU_LPC` fit `lpc-b372` pending; no hardware result yet.
3. Pre-existing "Track changes" Check failure, unexplained.
4. `glyphbuf` falls back from MLAB to M10K in the combined build.
5. all6-combined hold margin (+0.037 ns) is thin.
