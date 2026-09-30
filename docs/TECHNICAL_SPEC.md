# Tau technical specification

The systems inside the Tau core (v0.5.0), as built. This page is a map with the key numbers; each section cites the document or
source file the numbers come from, and where a number could not be checked it says "see" a document instead of stating one. Design
history and evidence are in [AUDIT_TRAIL.md](AUDIT_TRAIL.md); what is planned is in [ROADMAP_PUBLIC.md](ROADMAP_PUBLIC.md).

**Evidence labels used in the source docs:** hardware-confirmed (a Pocket result), simulation-verified (RTL or host tests),
design only (nothing built), [EST] (estimate to be measured).

Contents: [1 System overview](#1-system-overview) · [2 FPGA design](#2-fpga-design) · [3 Video and the draw engine (Talos)](#3-video-and-the-draw-engine-talos) ·
[4 Firmware architecture](#4-firmware-architecture) · [5 Storage formats](#5-storage-formats) · [6 Memory budgets](#6-memory-budgets) ·
[7 Timing and verification methodology](#7-timing-and-verification-methodology)

## 1. System overview

There is no audio decoder chip in the Pocket, so the FPGA hosts a soft CPU that decodes audio in software, with the audio queue,
equalizer, meters' measurement blocks and the screen drawing engine built as hardware around it. Files arrive from the SD card
through the Analogue framework (APF) bridge.

```text
 SD card --APF bridge--> [ main RAM ring 24 KB ]--> VexRiscv CPU (RV32IM, 66.667 MHz) --+--> Helix MP3 / FLAC decode
                                                      |   |   |                      |
                              MMIO 0x8000_0000 -------+   |   +-- PSRAM window       +--> PCM FIFO --> EQ (5 biquads/ch) --> I2S --> Pocket audio
                                                          |       (cold code+data,   |
              SDRAM window 0xA010_0000 (playlist, etc.)---+        art, library)    +--> draw commands --> Talos (mp3_fb.sv) --> SDRAM framebuffer --> video
                                                                                     |
        hardware blocks fed by the PCM sample stream: spectrum bank, level/scope block, MP3 synthesis-window unit (PCM-side assist)
```

Sources: [HOW_IT_WORKS.md](HOW_IT_WORKS.md), `src/fpga/core/mp3_soc.v`, `src/fpga/core/mp3_fb.sv`, [MMIO_ALLOCATION.md](MMIO_ALLOCATION.md).

| Component | What it is | Source |
|---|---|---|
| CPU | VexRiscv (SpinalHDL 1.9.4 generated `VexRiscv_Full.v`), RV32IM, no FPU, 66.667 MHz (raised from 60 MHz in v0.6.0, `TAU_CLK66`); measured about 1.65 cycles per instruction | `src/fpga/rtl/VexRiscv_Full.v`, [HOW_IT_WORKS.md](HOW_IT_WORKS.md) |
| Audio decode | Helix MP3 (integer), FLAC decoder in `fw/flac.c` | `third_party/libhelix-mp3/`, [FLAC.md](FLAC.md) |
| Cover decode | picojpeg (baseline JPEG), or the `TIM1` palette reader | `third_party/picojpeg/`, [COVER_TIMG_READER.md](COVER_TIMG_READER.md) |
| Screen | 400 x 360 RGB565 framebuffer in SDRAM, drawn by the Talos 2D engine | `src/fpga/core/mp3_fb.sv` |
| Extra memory | Pocket SDRAM (32 MB part) and PSRAM (32 MiB), both reached through CPU windows | [MMIO_ALLOCATION.md](MMIO_ALLOCATION.md), [SDRAM_MEMORY_ARCHITECTURE.md](SDRAM_MEMORY_ARCHITECTURE.md) |

## 2. FPGA design

Target device `5CEBA4F23C8` (Cyclone V), built with Quartus Prime Lite 25.1 ([FPGA_BUILD.md](FPGA_BUILD.md)). Top-level sources are in
`src/fpga/core/`; the Analogue framework files are in `src/fpga/apf/`.

### 2.1 Clocks

The PLL (`src/fpga/core/mf_pllbase/mf_pllbase_0002.v`) produces four outputs; the bridge runs on the framework's own `clk_74a`.

| Clock | Frequency | Used for |
|---|---|---|
| `clk_sys` | 66.667 MHz (raised from 60 MHz in v0.6.0, `TAU_CLK66`) | CPU, MMIO, PCM FIFO, EQ, hardware meter blocks |
| `clk_sdram` | 100 MHz | SDRAM controller, arbiter, draw engine (Talos) |
| video clock | 12 MHz | Pixel clock: 500 x 400 total for exactly 60.000 Hz (`mp3_fb.sv`) |
| video clock (shifted) | 12 MHz, 20,833 ps phase | Video clock for the panel interface |
| `clk_74a` / `clk_74b` | supplied by the framework (see the Analogue docs) | APF bridge |

Crossings between domains use small, separately tested blocks: `tau_cdc_sync1.sv` (single-bit synchroniser), `tau_cdc_gray_ctr.sv`
(a free-running counter, Gray-coded), `tau_cdc_gray_bus.sv` (a multi-bit value, Gray-coded, used for the beam position) and
`tau_vs_counter.sv` (level plus a free-running frame counter in `clk_sys`, because the vsync pulse is about 167 us wide and cannot
be polled). Sources: the files named, [MMIO_ALLOCATION.md](MMIO_ALLOCATION.md). Note: the 60 to 66.667 MHz `clk_sys` bump, first evaluated via
[HARPMUDD_UPSTREAM_1.5_REVIEW.md](HARPMUDD_UPSTREAM_1.5_REVIEW.md) and [OPENFPGAOS_REVIEW.md](OPENFPGAOS_REVIEW.md), is adopted and shipped in v0.6.0-alpha.1
(`TAU_CLK66`).

### 2.2 Memory map

CPU addresses, from `fw/link.ld` and [MMIO_ALLOCATION.md](MMIO_ALLOCATION.md):

| Range | What | Notes |
|---|---|---|
| `0x0000_0000` (192 KB) | Main RAM (on-chip BRAM), cached | Firmware image, heap, MP3 ring, stack. Shrunk from 256 KB in v0.6.0-alpha.1 (`TAU_RAM_192K`, section 6) |
| `0x8000_0000` page | MMIO registers | Table below; the decoder now spans `0x000..0x1FC` |
| `0xC000_0000` | Uncached alias of main RAM | Reached through pointers, not in the link map |
| `0xA010_0000` .. `0xA3FF_FFFF` | SDRAM window (uncached) | Playlist buffers (13,312 B) sit at the start in SDRAM-playlist builds; the framebuffer and the draw engine's grid own the low SDRAM |
| `0xA400_0000` .. `0xA5FF_FFFF` | PSRAM window (uncached, 32 MiB) | Word offset `[22]` chip, `[21]` die; the last CPU word of each 8 MiB die is a guard (reads 0, stores ignored) |
| `0xA400_0000` (15,360 B) | Album-art accumulator | at ART_IMG 128 |
| `0xA401_0000` .. | Library index and queue areas | `0x010000..0x40FFFF` reserved |
| `0xA480_0000` (1 MiB) | Cold data | Loaded at boot from `tau-cold.bin`; empty unless `TAU_COLD=1` |
| `0x2480_0000` (1 MiB) | Cold code (instruction alias of PSRAM) | PSRAM byte offset = address minus `0x2400_0000`; one image carries cold data then cold code |

Everything else, including any other unmapped address, ends as a bus error. Sources: `fw/link.ld`, [MMIO_ALLOCATION.md](MMIO_ALLOCATION.md),
[PHASE_G_SPEC.md](PHASE_G_SPEC.md).

Selected MMIO registers (offsets from `0x8000_0000`; the full table, with bit fields, is [MMIO_ALLOCATION.md](MMIO_ALLOCATION.md)):

| Offset | Register | Purpose |
|---|---|---|
| 0x0C | CYCLES | free-running `clk_sys` counter |
| 0x34 / 0x38 | PCM_ST / PCM_RATE | PCM FIFO status and rate increment |
| 0x3C | INPUT | menu and controller keys |
| 0x40 | VERSION | RTL interlock (firmware refuses a mismatched bitstream) |
| 0x48-0x58 | FB_* | draw engine commands (address, size, colour, go, stall counter) |
| 0x68 | EQ | equalizer preset |
| 0x6C / 0x70 | SET_IDX / SET_DAT | persisted settings words |
| 0x74-0x84 | SDR_* | SDRAM mailbox |
| 0x88-0xAC | expansion window | PSRAM diagnostic mailbox |
| 0xB0-0xB8 | IF_* | instruction fetch from PSRAM counters and config |
| 0xBC | SDR_BUSY | SDRAM port-busy cycles (Gray-coded CDC counter) |
| 0xC0-0xCC | BLT_IDX/DATA, CLUT_IDX/DATA | sticky blit fields and the 256-entry CLUT |
| 0xD0 | VBLANK | vblank level plus frame counter |
| 0xD4 / 0xD8 | RC_IDX / RC_DATA | rounded-rect corner-cut table |
| 0xDC-0xE4 | SPEC_* | hardware spectrum bank |
| 0xE8 | SCAN | display beam position |
| 0xEC-0xFC | WAVE_* | hardware level and scope block |
| 0x100-0x110 | POLY_* | MP3 synthesis-window unit |
| 0x114 | TEXT_MODE | light-polarity text weight table |

### 2.3 CPU

VexRiscv, RV32IM (no FPU). Chosen after measurement: minimp3 was over three times slower (floating point with no FPU) and PicoRV32 needed
114 to 351 MHz to decode in real time ([HOW_IT_WORKS.md](HOW_IT_WORKS.md), [STAGE0_RESULTS.md](../STAGE0_RESULTS.md)). Instruction fetch
can come from on-chip RAM or, through the alias at `0x2400_0000`, from PSRAM (cold code). The generated `VexRiscv_Full.v` carries no
licence header of its own; see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

### 2.4 Audio path

Decoded PCM goes into a hardware FIFO (`pcm_fifo.v`) that drains at the file's own sample rate, so the CPU can spend about 20 ms on a
frame without the sound breaking up; the FIFO is primed to half full before output starts (a fix ported from upstream). The equalizer
(`eq_biquad.v`, coefficients in `eq_coefs.vh`) is five cascaded biquads per channel sharing one time-multiplexed multiplier, using 116
of the 1,250 clocks available between output samples, with a bit-exact model and loudness-matched presets ([EQ_DESIGN.md](EQ_DESIGN.md),
[HOW_IT_WORKS.md](HOW_IT_WORKS.md)). `sound_i2s.v` (agg23's I2S bridge) feeds the Pocket. Volume, speed (a resampling rate increment,
`PCM_RATE`) and the EQ preset are set from the CPU without an audio gap.

### 2.5 Hardware measurement and decode blocks

| Block | File | What it does | Status |
|---|---|---|---|
| Spectrum filter bank | `tau_spec_bank.sv` (MMIO 0xDC-0xE4) | 16 octave-style bands; the mean magnitude of each over a completed 1024-sample window; replaces the software cascade | in the shipped bitstream; Info > SPECTRUM reports it |
| Level and scope block | `tau_wave_meter.sv` (0xEC-0xFC) | Left/right peaks; a 256-column min/max capture of the mono mix with a zero-crossing trigger | on hardware; the firmware's use of the scope capture is currently compiled out (drawing 256 columns cost about 21x a normal meter), the software scope runs instead |
| MP3 synthesis-window unit | `tau_mp3_poly.sv` (0x100-0x110) | The polyphase synthesis window: a 1,024 x 32 history array, 264-entry coefficient ROM generated from Helix's tables, a sequenced 64-bit multiply-accumulate, the same rounding, shift and clip as Helix. 4,691 clocks per stereo slot | hardware-confirmed (B-309) |
| Frame counter / beam | `tau_vs_counter.sv`, `tau_cdc_gray_bus.sv` (0xD0, 0xE8) | Frame counter (about 60/S) and the scanned line, so drawing can race the beam | hardware-confirmed (VBLANK 60/S) |
| Text weight tables | inside `mp3_fb.sv` (0x114) | A second table for the Light polarity (dark text on a light ramp) | fit-closed, built into the release bitstream |

**MP3 window unit, bit-exactness.** A golden model (`sim/mp3_poly_model.c`) equals Helix's real `PolyphaseStereo` on 170 slots including clipped
samples; the RTL is bit-exact on those 170 slots (5,440 PCM words) and four deliberately broken variants are all caught (`make
test-rtl-mp3-poly-mutation`). Helix's FDCT32 emits only 32 unique words per call, so the unit stores them exactly as Helix computes them: bit-exact by
construction, with no sign or rounding assumption. The firmware runs the real Helix `Subband()` with the window stage redirected, checks the first
slots against software at start, and falls back to software if the unit ever disagrees. Result on hardware (alpha.30): `HW 404712 SLOTS 0 BAD 0 TMO`;
filterbank share of decode 22% at 1.0x, against 55-59% before ([MP3_FILTERBANK_KERNEL_DESIGN.md](MP3_FILTERBANK_KERNEL_DESIGN.md), AUDIT_TRAIL B-292, B-309).
The unit accelerates MP3 only; FLAC is unchanged.

### 2.6 Storage controllers

- **SDRAM** (`tau_sdram_*`, agg23's controller): one port shared by scanout, the draw engine and the CPU window. Scanout always wins arbitration. See
  [SDRAM_MEMORY_ARCHITECTURE.md](SDRAM_MEMORY_ARCHITECTURE.md). The busy share is measurable (SDR_BUSY).
- **PSRAM** (`tau_psram_async.sv`, `tau_psram_bus.sv`): an asynchronous controller for the Pocket's two-chip, two-die 32 MiB PSRAM,
  running on the chip's power-up defaults. The read-sample timing has two clocks of margin (index 9; index 7 also passed, index 6 failed on hardware).
  Contract and results: [PSRAM_TIMING_CONTRACT.md](PSRAM_TIMING_CONTRACT.md), [PSRAM_IMPLEMENTATION_PLAN.md](PSRAM_IMPLEMENTATION_PLAN.md).
- A CPU-visible **cold-code path** with two clients (data and instruction) on the PSRAM bus, with a counter pair for fetch beats and stall cycles
  (`IF_N`, `IF_CYC`).

## 3. Video and the draw engine (Talos)

**Video.** 400 x 360 at 60.000 Hz (500 x 400 total on a 12 MHz pixel clock), an exact 4x integer scale of the Pocket's 1600 x 1440 panel, so nothing is
filtered. One pixel is one 16-bit SDRAM word (RGB565) with a stride of 512 words (rows stay inside one SDRAM page). Source: `src/fpga/core/mp3_fb.sv`.

**Talos** (`mp3_fb.sv`) is the 2D draw engine. The CPU pushes commands through a 256-entry command FIFO (`clk_sys` to `clk_sdram` async), and the engine decomposes
each into single-burst units that return to the dispatcher between rows, so a scanout fill (which always wins) waits at most one burst (about 500 cycles against
about 4,167 cycles of slack per scanline). The engine exists because drawing from the CPU was measured causing audible jitter.

| Opcode | Name | What it does |
|---|---|---|
| 0 | `OP_RUN` | Fill a horizontal run of one colour |
| 1 | `OP_RECT` | Fill a w x h block |
| 2 | `OP_CHAR` | One anti-aliased scaled glyph (Inter, 16 x 16 cell, 4-bit coverage) |
| 3 | `OP_COPY` | Copy a block (row buffer limit applies) |
| 4 | `OP_BLIT` | Generalised copy with independent source and destination base and stride; optional colour-key transparency and blend |
| 5 | `OP_BAR` | Meter column: a lit and an unlit segment, two chained fills |
| 6 | `OP_SBLIT` | Scaled blit (Bresenham stepping, one read per output pixel) |
| 7 | `OP_CBLIT` | Palette blit: an 8-bit index plane expanded through the 256-entry CLUT (plus a sticky re-index offset) |
| 8 | `OP_RRECT` | Rounded rectangle from a 16-entry corner-cut table |

Sticky state loaded through `BLT_IDX` / `BLT_DATA` (auto-incrementing): field 0 `SRC_BASE`, 1 `SRC_STRIDE`, 2 `DST_BASE`, 3 `DST_STRIDE`, 4 `KEY` (enable and colour),
5 `BLEND` (enable, mode, alpha), 6 `REINDEX`. The command opcode is 4 bits wide. A failed probe (`BLIT_READY()`, `RRECT_READY()`) makes the firmware fall back to
software drawing, so an older bitstream still runs. Sources: `mp3_fb.sv` (opcode localparams, header comments), [MMIO_ALLOCATION.md](MMIO_ALLOCATION.md),
[PHASE_F_SPEC.md](PHASE_F_SPEC.md).

**Alpha blend status.** `TAU_BLIT_BLEND` (alpha or shift-add blend modes in `OP_BLIT`) failed timing for a long time (worst setup -2.5 to -2.9 ns, one
path family through the glyph buffer and an unregistered DSP). It was rebuilt as a three-stage pipeline (capture, blend, write) in B-327; the fit
`blend-pipe-b327` closed timing on 2026-09-27 (seed 1 all corners positive, setup min +0.755 ns; RAM 304/308; DSP 17/66), and the later T2-00
`glyphbuf` fix (B-398) closed it again as part of the combined v0.6.0-alpha.1 bitstream. **It ships in v0.6.0-alpha.1 and the firmware uses it**: the
Winamp Scope trail fade (`ui_bg_blend()`/`BLEND_READY()`) and the Settings menu cross-fade transition both drive it on real hardware
([ALPHA_BLEND_ANALYSIS.md](ALPHA_BLEND_ANALYSIS.md), AUDIT_TRAIL B-326, B-327, B-398, B-404, B-405).

**Rounded rectangles** (`OP_RRECT`, B11) had a timing violation that was fixed in B-231 by retiming, and is part of the current bitstream; the firmware
uses it for selected rows behind a probe.

**Row budget of the framebuffer grid** (the draw engine addresses 1024 rows of 512 words; [COVER_TIMG_READER.md](COVER_TIMG_READER.md)): rows 0-359 the visible frame;
360-487 the art stash; 488-839 meter thumbnails; 840-967 the cover index plane; 984-1023 the Chladni tile plane and the probe cell.

**Helios** ([HELIOS_SPEC.md](HELIOS_SPEC.md)) is the UI layer over Talos: regions with dirty flags, a flush point in the main loop, and beam-aware drawing
(`helios_rows_safe()` lets a draw through only when the beam is in blanking, has passed the region, or is safely ahead of it). The rule was verified
exhaustively on the host (837,600 cases). The meter block is the first adopter, so meter drawing no longer tears; full double buffering (H2, `TAU_DBUF`)
ships in v0.6.0-alpha.1 and is hardware-confirmed, driving the Settings/fullscreen/Configure redraw path.

## 4. Firmware architecture

Source in `fw/` (C, `fw/player.c` plus `.inc` and header files; `fw/build.sh` builds it).

### 4.1 Hot and cold code

Only code that must be fast stays in on-chip RAM (both decoders, the audio refill path, the drawing primitives). Everything else is **cold**: menus, the
library browser, the playlist overlay, cover-art handling, meter draw paths, Chladni, the QR encoder, the diagnostics. Cold code executes from PSRAM
through the instruction alias; cold read-only data (meter previews, help text, tables) is read from PSRAM. Both live in **one image**, `tau-cold.bin`
(data slot 6), loaded at boot and verified; it is layout-bound to the ROM, and a mismatch is refused. Hot-to-cold calls must pass a `COLD_READY()` gate;
a bitstream without PSRAM instruction fetch falls back cleanly (no library, no menus, single-file playback still works). The price is fetch latency:
worst case about 27,300-28,850 CPU cycles for a meter draw call, about 1.7% of one audio frame. Sources: [PHASE_G_SPEC.md](PHASE_G_SPEC.md), `fw/cold.inc`,
`tools/pack_cold.py`, `tools/check_cold_calls.py`, AUDIT_TRAIL B-202, B-213.

### 4.2 Data slots

Declared in `data.json` (source: `tools/tau_data_slots.py`): slot 5 `tau-library.tdb` (optional, deferload), slot 6 the cold image, slot 7 pre-scaled
covers (opened by name; nothing loaded at boot), slot 8 `tau-assets.bin` (optional). Playlist and track files are opened by absolute path into the
existing slots using the framework's file-by-name mechanism.

### 4.3 Decoders

- **MP3:** Helix (integer). MPEG-1 and MPEG-2 Layer III, all standard bitrates and sample rates; its decoder instance (23,816 B) sits in a 24,576 B arena.
  The window stage runs in hardware (section 2.5).
- **FLAC:** `fw/flac.c`, up to 48 kHz, 8/16/20/24-bit, mono and stereo; verified with generated test vectors (`tools/flac_make_test.py` and the
  `flac_*_check.py` tools). A 24-bit 44.1 kHz track uses about 80% of the CPU time available.
- **Covers:** picojpeg for baseline JPEG (about as slow as the file is heavy); or the `TIM1` reader (section 5).

### 4.4 UI layers, themes and meters

Theme roles (12 base plus extension roles, `fw/theme.h`) replace fixed colours; two built-in themes each have Dark and Light (`themes/*.json` to
`fw/theme_data.h` via `tools/gen_themes.py`). Meters are described by manifests (`meters/*/meter.json`); `tools/gen_meters.py` generates the enum,
names, order and parameter tables (`fw/meter_gen_*.h`, `fw/meters_gen.h`) so adding a meter is one manifest. A cost estimator (`tools/meter_cost_estimate.py`)
enforces a draw-command budget per meter in `make test-host`. The Configure page is generic over the manifests. Specs: [THEME_SPEC.md](THEME_SPEC.md),
[METER_MODULE_SPEC.md](METER_MODULE_SPEC.md), [METER_CONFIG_SPEC.md](METER_CONFIG_SPEC.md), [METER_REGISTRY.md](METER_REGISTRY.md).

### 4.5 Settings and persistence

The core never opens a file to save settings. It declares them in `interact.json` as persist variables and the Pocket writes
`interact_persist.json`. The hardware persist register file was widened to 32 words in v0.6.0 (from 16), but Analogue's own
`interact.json` UI has a separate, hard 16-entry cap of its own — past that, entries are silently dropped from both display
and persistence. **Theme and mode now persist across a restart** (declared within the cap); **meter Configure preset values
stay session-only** — their `interact.json` entries were dropped to stay under the 16-entry cap and have no persistence path
yet (AUDIT_TRAIL B-455/B-456, [ROADMAP.md](ROADMAP.md) item 8, [METER_CONFIG_SPEC.md](METER_CONFIG_SPEC.md)). Also see
[SETTINGS_ARCHITECTURE.md](SETTINGS_ARCHITECTURE.md).

## 5. Storage formats

| Format | Where | Summary | Spec |
|---|---|---|---|
| Library index `tau-library.tdb` | data slot 5 | Magic `TLIB`, 128-byte header with CRCs, fixed-size artist (8 B), album (20 B) and track (16 B) records, string pool, order and letter tables, optional playlists; caps: 16,384 tracks, 2,048 albums, 1,024 artists, 64 playlists, 4 MiB; read into PSRAM | [MEDIA_LIBRARY_0.4_SPEC.md](MEDIA_LIBRARY_0.4_SPEC.md) |
| Cold image `tau-cold.bin` | data slot 6 | Cold data then cold code, layout-bound to the ROM | [PHASE_G_SPEC.md](PHASE_G_SPEC.md) |
| Cover `.timg` (`TIM1`) | data slot 7 | 16-byte header then a payload of a 512-byte CLUT and indices; palette-256 at 128 px on the long side; drawn by `OP_CBLIT`. Not frozen | [COVER_TIMG_READER.md](COVER_TIMG_READER.md), [IMAGE_FORMATS.md](IMAGE_FORMATS.md) |
| `tau-assets.bin` (`TAUA`) | data slot 8 | Container with `THEM` (themes) and `METR` (meter presets) sections; every CRC is checked and a failure keeps the built-ins | [THEME_FILE_FORMAT.md](THEME_FILE_FORMAT.md), [THEME_SPEC.md](THEME_SPEC.md) |
| Meter modules | `meters/*/meter.json` | Manifest, generated parameter tables, capability registry | [METER_MODULE_SPEC.md](METER_MODULE_SPEC.md), [METER_CAPABILITIES.md](METER_CAPABILITIES.md) |
| Check / QR report (`TAUD1`) | screen QR and persist words | `TAUD1:` plus base64url of a binary record (header, then tag-length-value entries for tests, timings, counters, settings, error codes) with a CRC32; unknown tags are skipped; typical size 250-400 bytes before encoding | [TEST_SUITE_SPEC.md](TEST_SUITE_SPEC.md), `tools/decode_tau_suite.py` |

QR encoding is done in firmware by `fw/qrcode.h` (byte mode, error level L, versions 1 to 38), verified against the `segno` reference for every version
and mask and by decoding the rendered image.

## 6. Memory budgets

| Resource | Figure | Source |
|---|---|---|
| Main RAM | 192 KB physical (shrunk from 256 KB in v0.6.0-alpha.1, `TAU_RAM_192K`); image, heap, 24 KB MP3 ring, 4 KB tag buffer and an 8 KB stack | `fw/link.ld` |
| Release heap gap | 15,808 B in v0.6.0-alpha.1 (192 KB layout); was 49,712 B in v0.5.0 on the 256 KB layout (30,528 B before code moved to PSRAM; a 61,808 B peak in B-214) | AUDIT_TRAIL B-214, B-331, B-333/B-458 |
| Minimum heap gap enforced by the build | 4,096 B floor (diagnostic), 6,144 B floor (release-style) | `fw/build.sh`, [RAM_SHRINK_192K_PLAN.md](RAM_SHRINK_192K_PLAN.md) |
| Worst measured stack peak | 1,672 B of 16,384 B | AUDIT_TRAIL B-230 |
| M10K blocks | 240 of 308 in the shipped v0.6.0-alpha.1 bitstream (down from 304/308 in v0.5.0, mainly the `glyphbuf` single-write-port fix, T2-00) | AUDIT_TRAIL B-316, B-398, B-458, [ROADMAP.md](ROADMAP.md) |
| DSP blocks | 19 of 66 in the shipped v0.6.0-alpha.1 bitstream (MP3 window unit, FLAC LPC unit, alpha blend) | AUDIT_TRAIL B-458 |
| SDRAM framebuffer | 512 x 360 x 2 B, about 360 KB of the 32 MB part | `mp3_fb.sv` |
| PSRAM | 32 MiB window; art accumulator, cold code and data, library index | `fw/link.ld` |

**The 192 KB RAM shrink** (`TAU_RAM_192K`) is fit-proven in RTL: RAM blocks 235 of 308 instead of about 298-300, timing closed on both seeds (AUDIT_TRAIL
B-235). It would release 64 blocks. The firmware **does not fit 192 KB yet**; [RAM_SHRINK_192K_PLAN.md](RAM_SHRINK_192K_PLAN.md) lists where the missing bytes
could come from. Until it does, the 256 KB bitstream is what ships.

## 7. Timing and verification methodology

- **Quartus fits.** Timing claims need a fit on at least two seeds with all four corners positive (Slow 85C and 0C, Fast 85C and 0C), and a bitstream is only
  paired with firmware built for the macros it was fitted with (a fit missing the product macros once produced a core that looked broken). Fits run on a VM with
  `tools/vm_fit.py` (launch, status, collect); the qsf macro bundles are `tools/*_qsf_append.txt`. Details: [DEVELOPERS.md](DEVELOPERS.md), [FPGA_BUILD.md](FPGA_BUILD.md).
- **Retiming.** Long combinational chains fed from a BRAM read are moved one cycle earlier onto the same raw read data (BAR, SBLIT, CHAR, RRECT, blend).
- **Simulation and host tests.** `make test-host` (Python and C harnesses, including the RV32 firmware running the real code paths in an emulator, golden frames
  that equal the JS preview ports command for command, exhaustive rule checks) and `make test-rtl` (Icarus testbenches for every RTL block, plus a reference
  renderer that diffs the draw engine pixel for pixel).
- **Mutation tests.** Each tested block has deliberately broken variants that the tests must catch (draw engine, MP3 window unit, PSRAM controller). A test that cannot
  catch a mutant is rewritten or removed (a Gray-code counter mutation was found untestable in a zero-delay simulator and dropped).
- **Fail-safes.** Anything new that needs hardware support probes it with a known-answer test at start and falls back if it fails.
- **Evidence discipline.** Every hardware result is logged in [AUDIT_TRAIL.md](AUDIT_TRAIL.md) with an evidence label; a design is "design only" until a build passes, and "hardware-confirmed" only
  after a Pocket run. The Diagnostic Build's Check produces reports that can be decoded from a screenshot ([guide/DIAGNOSTICS.md](guide/DIAGNOSTICS.md)).
