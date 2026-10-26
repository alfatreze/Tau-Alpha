# Tau-Alpha Architectural Diagrams Reference

Complete technical specifications for five architectural diagrams. Use these as a reference to construct diagrams in Figma.

---

## Diagram 1: Helios UI Library Architecture

**Dimensions:** 800px × 600px  
**Background:** Light gray (#F5F5F5)

### Layout Grid (from top-left, 40px padding):
- **Title Bar** (0, 0, 800, 40px)
  - Text: "Helios UI Library Architecture" (Bold, 24px, #000000)
  
### Content Sections (left to right, vertically centered at y=120):

**Left Panel: Input Layer** (40, 120, 200, 400)
- Container box (stroke: #333, fill: #E8F4F8)
- Title: "Input" (Bold, 14px, #0066CC)
- Items:
  - Poll SRAM halfword cache (text, 11px)
  - Button debounce (text, 11px)
  - Menu/Library/Settings routing (text, 11px)

**Center-Left Panel: Helios Core** (260, 120, 220, 400)
- Container box (stroke: #333, fill: #FFF8DC)
- Title: "Helios Region Manager" (Bold, 14px, #FF8C00)
- Key elements:
  - 16 helios_region_t entries (text, 11px)
  - dirty_flags bitmap (text, 11px)
  - redraw callback array (text, 11px)
  - vblank_active() reader (text, 11px, color: #0066CC)
- Arrow from left (label: "input events")
- Arrow to right (label: "redraw queue")

**Center-Right Panel: Render** (500, 120, 180, 400)
- Container box (stroke: #333, fill: #F0FFF0)
- Title: "Render & Blit" (Bold, 14px, #228B22)
- Items:
  - RRECT_READY() probe (text, 11px, bold)
  - BLIT_READY() probe (text, 11px, bold)
  - OP_RRECT commands (text, 11px)
  - OP_BLIT replication (text, 11px)

**Right Panel: MMIO Output** (700, 120, 100, 400)
- Container box (stroke: #333, fill: #FFE4E1)
- Title: "MMIO" (Bold, 14px, #DC143C)
- MMIO Addresses:
  - 0xC0/0xC4: BLT_IDX/DATA (text, 10px)
  - 0xD0: VBLANK (text, 10px)
  - 0xD8: R_RRECT (text, 10px)

### Bottom Flow (y=540):
- Horizontal arrow from "Render" to "MMIO" (label: "vblank-gated flush")
- Feedback arrow bottom to top (label: "double-buffer base pointer swap via MMIO 0xD8")

---

## Diagram 2: Talos Blit Engine Architecture

**Dimensions:** 900px × 700px  
**Background:** Light gray (#F5F5F5)

### Title (0, 0, 900, 40px):
- Text: "Talos Blit Engine: Proven Opcodes & MMIO" (Bold, 24px, #000000)

### Left Panel: Opcode Table (40, 60, 280, 600)
- Container (stroke: #333, fill: #F9F9F9)
- Header: "9 Proven Opcodes" (Bold, 12px)
- Table rows (10px text, alternating row backgrounds):
  | Op# | Name | Purpose | Status |
  |-----|------|---------|--------|
  | 0 | OP_RUN | Idle/scanout | Shipped v0.5 |
  | 1 | OP_RECT | Solid fill | Shipped v0.5 |
  | 2 | OP_CHAR | Glyph draw | Shipped v0.5 |
  | 3 | OP_COPY | ROM->SDRAM | Shipped v0.5 |
  | 4 | OP_BLIT | Gen. blit | Shipped v0.5 |
  | 5 | OP_BAR | Meter column | Shipped v0.5 |
  | 6 | OP_SBLIT | Scaled blit | Shipped v0.5 |
  | 7 | OP_CBLIT | Palette blit | Shipped v0.5 |
  | 8 | OP_RRECT | Rounded rect | Shipped v0.5 |

### Center Panel: MMIO Allocation (340, 60, 280, 600)
- Container (stroke: #333, fill: #F0F8FF)
- Header: "MMIO Registers (128 slots)" (Bold, 12px)
- Sticky Register Map (monospace, 9px):
  ```
  0xC0: R_BLT_IDX (cmd opcode, dst/src base)
  0xC4: R_BLT_DATA (per-command fields)
  0xC8: R_CLUT_IDX (palette index)
  0xCC: R_CLUT_DATA (palette entries)
  0xD0: R_VBLANK (vblank level+frame)
  0xD8: R_DBUF_BASE (double-buffer swap)
  0xDC: R_SPEC (spectrum data)
  0xE0: reserved
  0xE4: R_WAVE (waveform data)
  0xE8: R_BEAM_Y (scanout Y)
  0xEC: reserved
  0xF0: reserved
  0xF4: reserved
  0xF8: reserved
  0xFC: reserved
  ```

### Right Panel: Timing & Status (640, 60, 260, 600)
- Container (stroke: #333, fill: #FFF5EE)
- Header: "Timing-Closed Status" (Bold, 12px)
- Content (11px text):
  - **Build Phase:** B-235 (B11+RAM-shrink fix)
  - **Bitstream:** all6-combined
  - **Seed 2 Margins:**
    - Setup: +0.556ns
    - Hold: +0.010ns
  - **RAM Usage:** 240/308 M10K blocks
  - **DSP Usage:** 17/66 blocks
  - **Features:**
    - Color-key transparency (B2)
    - Scaled blit (B4)
    - Alpha blend: **SHELVED** (timing cliff)
    - Gradient bar: parked (B13)
  - **Open Issues:**
    - Blit Test page hangs (B-166, JTAG diagnosis pending)
    - Track changes Check failure (undiagnosed)
    - B11 (rounded rect) corner-cut LUT needs retiming

### Bottom Status Bar (y=680):
- Gray box with text: "Last verified: B-235 (Successful), timing margin +0.556ns seed 2, no negative slack on all four corners"

---

## Diagram 3: FPGA Resource Allocation & Memory Map

**Dimensions:** 900px × 750px  
**Background:** Light gray (#F5F5F5)

### Title (0, 0, 900, 40px):
- Text: "FPGA Resource Allocation & Memory Architecture" (Bold, 24px, #000000)

### Left Panel: M10K Block Allocation (40, 60, 220, 650)
- Container (stroke: #333, fill: #F9FAFB)
- Header: "M10K Usage" (Bold, 12px)
- Pie chart representation (text-based):
  - **Used: 240/308 blocks** (11px, bold)
  - Breakdown (10px):
    - Framebuffer: 96 blocks (40%)
    - ROM (font/meter/cold): 80 blocks (33%)
    - PSRAM scratch: 32 blocks (13%)
    - BRAM LUTs: 20 blocks (8%)
    - Glyphbuf/CLUT/DBUF: 12 blocks (5%)
  - **Free: 68 blocks** (11px, bold, #228B22)
  - RAM Shrink Target: 192 KB (saves 64 blocks)

### Center Panel: Memory Map (280, 60, 320, 650)
- Container (stroke: #333, fill: #F0FFF0)
- Header: "On-Chip RAM Layout" (Bold, 12px)
- Memory layout (monospace, 9px):
  ```
  Current: 256 KB (49,152 words)
  ├─ 0x0000_0000: Vector table
  ├─ 0x0000_2000: Boot code (8 KB)
  ├─ 0x0000_4000: Player.c (60 KB, HOT)
  ├─ 0x0000_F000: MP3/FLAC decode (16 KB, HOT)
  ├─ 0x0001_3000: Audio processing (8 KB)
  ├─ 0x0001_5000: PCM FIFO buffers (8 KB)
  ├─ 0x0001_7000: Settings scratch (1 KB)
  ├─ 0x0001_8000: Stack+Heap (8 KB)
  │   • release: 30.5 KB gap
  │   • diagnostic: 4.3 KB gap (TIGHT)
  └─ 0x0002_0000: End
  
  PSRAM: 4 MB (0xA000_0000)
  ├─ 0xA000_0000: CPU instruction fetch
  ├─ 0xA010_0000: Playlist buffers (13 KB)
  ├─ 0xA020_0000: Library index (64 KB)
  ├─ 0xA030_0000: Cold code (Settings/Chladni)
  └─ 0xA040_0000: Album art accumulator (192 KB)
  
  SDRAM: External
  ├─ Frame buffers (400x360 x2, RGB565 = 576 KB)
  └─ Audio codec buffers
  ```

### Right Panel: DSP & CLK Status (620, 60, 260, 650)
- Container (stroke: #333, fill: #FFF5EE)
- Header: "Specialized Hardware" (Bold, 12px)
- DSP Allocation (10px):
  - **DSP 17/66 blocks used**
  - MP3 filterbank window: 2 blocks
  - FLAC LPC MAC: 1 block
  - Audio I2S/mixing: 14 blocks
- Clock Status (10px):
  - **Shipping:** 60 MHz clk_sys
  - **Experiment:** 66.667 MHz (b339+, timing closed, 11% CPU headroom)
  - **Open:** 100 MHz (openfpgaOS pattern, ~2x margin risk)
- Timing Margin (10px, bold):
  - **No negative slack any corner**
  - Hold: +0.010ns (razor-thin)
  - Setup: +0.556ns
- Feature Gates (10px):
  - TAU_VBLANK: 1 (shipped)
  - TAU_DBUF: 0 (H2, designed)
  - TAU_BLEND: 0 (shelved, -2.97ns)
  - TAU_LPC: 1 (FLAC LPC unit, B-369)

---

## Diagram 4: Firmware Architecture & Build Targets

**Dimensions:** 900px × 800px  
**Background:** Light gray (#F5F5F5)

### Title (0, 0, 900, 40px):
- Text: "Firmware Architecture & Build System" (Bold, 24px, #000000)

### Top Section: Boot & Hot Code (40, 60, 820, 200)
- Large container (stroke: #333, fill: #FFF8DC)
- Title: "On-Chip RAM: Hot Code (runs in real-time)" (Bold, 12px, #FF8C00)
- Grid layout showing modules:
  
  **fw/player.c** (100, 90, 140, 150)
  - Box (fill: #FFE4B5)
  - Text (10px):
    - Main loop 60 KiB
    - Track load/play
    - UI chrome/overlays
    - Settings UI (TAU_SETTINGS_UI)
  
  **fw/mp3dec.c / fw/flac.c** (260, 90, 140, 150)
  - Box (fill: #FFE4B5)
  - Text (10px):
    - MP3 bit-reader (H/I/S % profile)
    - FLAC rice/LPC (t/c1 % profile)
    - Hardware redirects
    - Boot probes
  
  **fw/player_audio.c** (420, 90, 140, 150)
  - Box (fill: #FFE4B5)
  - Text (10px):
    - I2S DMA setup
    - PCM decay
    - Meter afford() gate
    - Volume control
  
  **fw/ui_*.c modules** (580, 90, 140, 150)
  - Box (fill: #FFE4B5)
  - Text (10px):
    - ui_draw_chrome()
    - ui_draw_meter()
    - Text/glyph rendering
    - Background restore
  
  **fw/suite.inc** (740, 90, 100, 150)
  - Box (fill: #FFE4B5)
  - Text (10px):
    - Check suite
    - CT_* test rows
    - QR encoding
    - Persist writer

### Middle Section: Cold Code (40, 280, 820, 240)
- Large container (stroke: #333, fill: #F0FFF0)
- Title: "PSRAM Cold Code (loaded on first use, address 0xA030_0000)" (Bold, 12px, #228B22)
- Five columns showing cold functions:
  
  **Settings UI** (100, 310, 140, 180)
  - Box (fill: #CCFFCC)
  - 15 KiB (text, 10px)
  - Functions (9px):
    - set_open/close/tick
    - Colour/EQ/Meter config
    - Theme load/apply
  - Load trigger: Start button
  
  **Library** (260, 310, 140, 180)
  - Box (fill: #CCFFCC)
  - 8 KiB (text, 10px)
  - Functions (9px):
    - lib_open/close
    - List navigation
    - Track select
  - Load trigger: Select button
  
  **Chladni Meter** (420, 310, 140, 180)
  - Box (fill: #CCFFCC)
  - 12 KiB (text, 10px)
  - Functions (9px):
    - chladni_tick()
    - Physics update
    - Symmetric quarter fold
  - Load trigger: Meter select
  
  **Fullscreen** (580, 310, 140, 180)
  - Box (fill: #CCFFCC)
  - 4 KiB (text, 10px)
  - Functions (9px):
    - fullscreen_draw()
    - Chladni full-res
    - Input dispatch
  - Load trigger: Select+Y
  
  **Diagnostics** (740, 310, 100, 180)
  - Box (fill: #CCFFCC)
  - 8 KiB (text, 10px)
  - Functions (9px):
    - Test runners
    - Info rows
    - QR export
  - Load trigger: Diag Build only

### Bottom Section: ROM & Build Targets (40, 540, 820, 240)
- Container (stroke: #333, fill: #F5F5F5)
- Title: "ROM Layout & Build Targets" (Bold, 12px)
- Left column: ROM Sections (100, 570, 200, 180):
  - Box (fill: #F0F8FF)
  - Content (10px):
    - Boot loader: 2 KB
    - Decode kernels: 1.2 KB
    - Meter thumbnails: 6 KB
    - Theme data: 4 KB
    - Font ROM (8.5K): 34 KB
    - Chladni presets: 8 KB
    - Audio samples: 16 KB
    - **Total ROM:** ~71.2 KB
- Right column: Build Targets (320, 570, 480, 180):
  - Table (9px text):
    | Target | ROM Size | Heap Gap | When |
    |--------|----------|----------|------|
    | release | 68 KiB | 30.5 KB | User release |
    | player-diagnostic | 72 KiB | 4.3 KB | Stress/soak |
    | player-library-diagnostic-profile | 74 KiB | 6.5 KB | Meter profiling |
    | player-profile | 60 KiB | 15 KB | Decoder breakdown |
    | all (9 targets total) | varies | varies | CI/coverage |
  - Note (10px, bold, #DC143C):
    "RAM-shrink target: 192 KB link → release +12.3 KB heap"

---

## Diagram 5: Cymo Unified Audio Framework

**Dimensions:** 900px × 750px  
**Background:** Light gray (#F5F5F5)

### Title (0, 0, 900, 40px):
- Text: "Cymo: Unified Audio Decode Framework (MP3 + FLAC + Hardware Kernels)" (Bold, 24px, #000000)

### Top Section: MP3 Pipeline (40, 60, 820, 280)
- Container (stroke: #333, fill: #FFF5E6)
- Title: "MP3 Decode: Filterbank Dominates (55-59% of realtime at 1x speed)" (Bold, 12px, #FF8C00)
- Pipeline stages (left to right):
  
  **Synchronization** (80, 90, 100, 240)
  - Box (fill: #FFDAB9)
  - Text (10px): "Sync frame / CRC"
  
  **Bit Reader** (200, 90, 100, 240)
  - Box (fill: #FFDAB9)
  - Text (10px):
    - H: Huffman decode
    - Measure: 3-5%
  
  **Side Info** (320, 90, 100, 240)
  - Box (fill: #FFDAB9)
  - Text (10px):
    - Channel/scale factors
    - Measure: 2-3%
  
  **Subband** (440, 90, 100, 240)
  - Box (fill: #FFB6C1, bold border #DC143C)
  - Text (10px, bold):
    - **Filterbank (CPU)**
    - Measure: 55-59%
  
  **IMDCT** (560, 90, 100, 240)
  - Box (fill: #FFB6C1, bold border #DC143C)
  - Text (10px, bold):
    - **IMDCT36 (CPU)**
    - Measure: 18-22%
  
  **Output** (680, 90, 100, 240)
  - Box (fill: #FFDAB9)
  - Text (10px):
    - AntiAlias/PCM
    - Measure: 4-6%

- Hardware Status (80, 340, 740, 30):
  - **MP3 Window Unit (B-290/B-291):** Hardware kernel proven on hardware, 404,712 slots, saves 50ms→10ms per track, shipped v0.5.0

### Middle Section: FLAC Pipeline (40, 380, 820, 280)
- Container (stroke: #333, fill: #E6F5FF)
- Title: "FLAC Decode: LPC Dominates (~99% of realtime, NO MARGIN)" (Bold, 12px, #0066CC)
- Pipeline stages (left to right):
  
  **Frame Header** (80, 410, 100, 240)
  - Box (fill: #B0E0E6)
  - Text (10px): "Sample rate / meta"
  
  **Subframe Type** (200, 410, 100, 240)
  - Box (fill: #B0E0E6)
  - Text (10px): "VERBATIM/FIXED/LPC"
  
  **Bit Reader** (320, 410, 100, 240)
  - Box (fill: #B0E0E6)
  - Text (10px):
    - U: Unary (CLZ heavy)
    - Measure: 4-11%
  
  **LPC Reconstruct** (440, 410, 100, 240)
  - Box (fill: #87CEEB, bold border #0066CC)
  - Text (10px, bold):
    - **MAC/history loop**
    - Ch0: 33-36%
    - Ch1: 63-66%
  
  **Decorrelation** (560, 410, 100, 240)
  - Box (fill: #B0E0E6)
  - Text (10px):
    - Stereo combine
    - Measure: 5-8%
  
  **Output** (680, 410, 100, 240)
  - Box (fill: #B0E0E6)
  - Text (10px): "PCM frame"

- Hardware Status (80, 660, 740, 30):
  - **FLAC LPC Unit (B-369/B-370):** Hardware kernel (1 DSP block, 5-state MAC/shift/add/push, 48-bit accum), channel-1 optimized, fitted B-372, shipped v0.6.0

### Critical Open Questions (left side, y=60-750):
- Container (stroke: #DC143C, fill: #FFE4E1)
- Title: "⚠ Open Issues" (Bold, 11px, #DC143C)
- Items (9px):
  - **96kHz FLAC:** No margin (expected 180%+ realtime), decoder refuses
  - **MP3 1.75x speed:** Confirmed smooth (H cost 22%)
  - **IMDCT32 hardware:** Deferred (filterbank priority solved first)
  - **Decorrelation DSP:** Investigate if LPC+Decor can share MAC
  - **Unary hardware:** CLZ bottleneck, BExt extension not available

### Shipping Status (right side, y=60-750):
- Container (stroke: #228B22, fill: #F0FFF0)
- Title: "✓ Shipped Features" (Bold, 11px, #228B22)
- Items (9px):
  - MP3 window unit (v0.5.0)
  - FLAC LPC unit (v0.6.0)
  - Decode profiler split (H/I/S/D/A/X + U/t/c1)
  - ACCEPT ALL RATES toggle
  - Dynamic audio-UI gating
  - Theme-aware UI refresh

---

## Technical Notes for Figma Construction

### Color Palette:
- **Background:** #F5F5F5 (light gray)
- **Hot Code:** #FFE4B5 (peach)
- **Cold Code:** #CCFFCC (light green)
- **MMIO/RTL:** #E8F4F8 (light blue)
- **Emphasis (problem areas):** #FFB6C1 (light pink) with #DC143C (crimson) border
- **Success/Shipped:** #F0FFF0 (honeydew) with #228B22 (forest green) text
- **Title/Headers:** #000000 (black) on light backgrounds
- **Accent colors:** #0066CC (blue), #FF8C00 (orange), #DC143C (red)

### Typography:
- **Titles:** 24px, bold
- **Section headers:** 12-14px, bold
- **Body text:** 10-11px, regular
- **Monospace (code):** 9px, courier

### Spacing & Alignment:
- 40px padding from diagram edges
- Vertical centering within containers
- 20px margins between major sections
- All boxes have 1-2px strokes, rounded corners (4-8px radius optional)

### Essential Visual Relationships:
1. **Diagram 1:** Left-to-right flow (Input → Manager → Render → MMIO)
2. **Diagram 2:** Three independent columns (Opcodes | MMIO | Status)
3. **Diagram 3:** Left-to-right (M10K | Memory Map | DSP/CLK)
4. **Diagram 4:** Vertical stacking (Boot/Hot → Cold → ROM/Targets)
5. **Diagram 5:** Three rows (MP3 | FLAC | Open Issues/Status), horizontal pipelines

All diagrams should include a subtle gray footer with "Last verified: [date]" for future updates.
