# Meter cost measurement (2026-10-03, step 1 of the removable-meters investigation)

Built `release` (RAM_192K CLK66 SDRAM_BUSY LPC_FW) and `player-library-diagnostic-profile` in the `meter-builder` worktree and grouped
`objdump -t` symbol sizes by name prefix (`tools/meter_size_report.py`). Prefix grouping is approximate: unnamed statics and inlined
helpers are not attributed, and the 11 legacy meters cannot be separated (below). Bytes, release build unless noted.

| Meter / group | Cold code | Cold data | Hot code+rodata | Hot RAM (bss/data) | PSRAM state |
|---|---|---|---|---|---|
| Layered Wave | 7,928 (diag 11,588) | 2,676 | 12 | 810 | 2,424 |
| Chladni | 6,640 | 0 | 582 | 2,728 | 0 |
| Winamp Bars + Scope (+ hardware scope) | 4,260 | 0 | 668 | 714 + 385 | 0 |
| VU Master | 2,440 | 0 | 0 | 18 | 0 |
| Fullscreen | 1,908 | 0 | 120 | 23 | 0 |
| 11 legacy meters (`ui_draw_dynamic_cold`, one function) | 4,684 | 0 | 176 (`ui_draw_dynamic`) | spec/peak statics ~200 | 0 |
| Thumbnails (RLE + palette + offsets) | 0 | ~4,730 | 0 | 0 | 0 |
| Descriptor, parameter and preset tables, infra | 1,812 | 6,676 | 553 | 1,430 | 0 |

## What it means

- Everything meter-related is roughly 44 KB of cold image (release cold image ~95 KB, window 1 MB, so ~4 percent of the window).
- Hot RAM for all meters is about 6.5 KB against a release heap gap of 14,144 B (diagnostic 5,360 B, floor 4,096 B). Chladni's
  2.7 KB of bss is the single biggest item and could move to PSRAM state without any loader.
- Removing meters therefore buys little *space* on its own. The real wins of a pack model are: only the **active** meter's state
  occupies hot RAM (peak, not sum), a big library costs no resident memory, and boot loads only what the user chose.
- The 8 older meters are separate source functions inlined into `ui_draw_dynamic_cold` (4.7 KB), sharing file-scope statics (see METER_GAP_AUDIT.md). They stay built in (D-M07); packing them is not worth it.
- Candidates worth packing first: Layered Wave (11.6 KB cold, 2.4 KB PSRAM state), Chladni, Winamp pair.

## Not measured

Per-frame CPU cost per meter (use the existing Meter Sweep Check QR), boot-time cost of a larger cold image, and the exact
attribution of unnamed statics. Reproduce: build with `RISCV_TOOLCHAIN_BIN` pointing at the main checkout's toolchain (the worktree
needs a `toolchain` symlink), then `riscv-none-elf-objdump -t fw.elf > name.sym`.
