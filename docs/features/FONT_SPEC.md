# Fonts: Space Grotesk (SemiBold, Bold) loaded from the cold image, selectable in Appearance

**Status: DESIGN, reviewed 2026-10-16; the RTL (phase 3) is built and simulated (2026-10-17), default font Tamzen Regular, `CORE_VERSION` rev 27; the firmware loader and the fit are DONE (fit `fontram-b810`, in the shipped bitstream `stats-b821`, `fw2/gfx/font.c`, on hardware, in v0.6.0; open: pair kerning, Omega FONT section, icons, size tuning; checked 2026-10-26). Built without RTL (2026-10-16): the atlas tool and records (phase 1), the UI string-fit test (phase 2), the Latin-1 text path in the library tool and the firmware draw-time fold (phase 3b). Still waiting on the RTL (phases 3 and 4).** Supersedes the "Loadable fonts" part of `docs/features/HELIOS_SPEC.md` section 7.1 for everything it covers (that section stays right about offline rasterisation, no runtime scaling for loaded fonts, the fallback rule and the Omega export idea). Owner request (2026-10-16): replace Inter with Space Grotesk, offer SemiBold and Bold **and the Tamzen pixel font (Regular and Bold)**, load the font from the cold image, add a choice under Appearance, **and cover Latin-1 (accents) end to end; Japanese and the rest of Unicode are parked.** Reviewed as a senior 2D engine and graphics engineer and an Analogue Pocket core developer would; the review findings are folded in and listed in section 12.

## 1. What is true today (read from the source, 2026-10-16)

| Fact | Where |
|---|---|
| One font, Inter SemiBold, 95 glyphs (0x20..0x7E), 16 x 16 cell, 4 bits of coverage per pixel, 8 pixels per 32-bit word, 2 words per row, 32 words per glyph, 3,040 words in all | `tools/gen_font_rom.py`, `src/fpga/core/font_rom.v` |
| It is a ROM in the bitstream, read by the drawing engine only: `mp3_fb.sv` state `A_ROWFETCH` puts `char_base + row*2 (+1)` on `font_addr` (12 bits) and takes the two registered words; the port is in `clk_sdram` | `mp3_fb.sv` lines 608-611, 1601-1612 |
| Coverage is not the blend weight: `cov_weight()` (dark) and `cov_weight_light()` map the 4-bit coverage to a 0..16 weight, fitted to Inter on the palette | `mp3_fb.sv` 875+, `tools/gen_text_gamma.py` |
| Per-glyph advance (ink width + 2, capped at 16) lives in firmware (`fw/font_metrics.h` `font_adv[95]`); the engine paints the whole 16 x 16 cell (background included), glyphs are left-aligned, so the next glyph overwrites only padding | `gen_font_rom.py`, `fw2/gfx/gfx.c` `gfx_text`, `gfx_text_width` |
| Text is drawn by queuing one `OP_CHAR` per glyph; the engine runs them later and in order | `gfx_text` |
| Player 2 draws text at 1x only; characters outside 0x20..0x7E draw as a space | `gfx.c` `glyph_of` |
| tau-assets.bin (`TAUA`) already carries `THEM`, `METR`, `PRST`; `FONT` and `ICON` are named as planned | `docs/THEME_FILE_FORMAT.md` |
| The firmware states the bitstream features it needs in a ROM marker (`TAUFWNEED`), and packagers and the installer refuse a mismatch | `tools/check_fw_bitstream_pair.py`, `tools/fit_manifest.py` |

## 2. The gap the first plan left open

The earlier spec said a font is a data-slot asset in PSRAM cold data. The engine cannot draw from PSRAM: glyph rows come from the on-chip font memory. "Loaded from cold" therefore needs one engine-side change. The options, as the review weighed them:

| Option | Blocks | Verdict |
|---|---|---|
| A. A ROM per weight, chosen by a select register | 2 x 12 M10K | Works, no loader, but burns blocks for a choice and cannot take a font from Tau Omega. **Rejected.** |
| **B. The font memory becomes a RAM the CPU writes; the cold image carries the atlases; the bitstream holds a default** | **same 12 M10K as today** | **Chosen.** Capacity neutral, any number of fonts, Omega can export one. |
| C. The engine fetches glyph rows from PSRAM or SDRAM | 0 blocks | A new bus client in the draw engine's hottest loop, with the arbitration and timing risk this project has paid for before (B-109, B-150). **Rejected for now;** revisit only for a font that does not fit on chip. |

## 3. Design

### 3.1 Hardware (`TAU_FONT_RAM`, the one RTL change)

* `font_rom.v` becomes `font_ram.v`: a simple dual-port memory, **write port in `clk_sys` (CPU), read port in `clk_sdram` (the draw engine), unchanged and registered**, so `font_q` timing is exactly today's. Same inference pattern as the CLUT (`tau_clut_wr.sv`): `ramstyle = "M10K, no_rw_check"` (read-during-write is don't-care because the firmware never writes while text is drawing, section 3.3).
* **Initial contents = the default font, Tamzen Regular (owner 2026-10-17)** through the existing `initial` block, so boot splash, loading screens and any build where the cold image fails to load still draw text. This is the fail-safe the first spec asked for ("the built-in font stays resident").
* **Coverage is Latin-1: U+0020..U+007E and U+00A0..U+00FF, 191 glyphs, 6,112 words.** `FONT_WORDS` is a parameter (6,112), the read address grows from 12 to **13 bits**, the memory from 12 to about **24 M10K blocks** (6,112 x 32 bits = 195,584 bits; at the 256 x 32 geometry that is 24 blocks; **estimate, the fit reports it**). The block ledger stands at 240 of 308 (B-337 fit, `ram192`), so this takes it to about 252, leaving 56 for everything else; that is the price of accents and the owner should see it (decision 5 in section 10).
* **The glyph field grows from 7 to 8 bits without a new register.** `OP_CHAR` carries its glyph in `R_FB_GO` bits 9:3 (7 bits, ASCII). The two bits `cmd_glyph_hi` (`R_FB_GO` 16:15) were added for the wide bar (B-454) and CHAR does not use them: **bit 15 becomes glyph bit 7 for CHAR.** A glyph below 0x80 is sent exactly as before, so **today's firmware and the old player stay byte-compatible** (their CHAR commands have bit 15 = 0). In the registered `q_char_base` stage (already retimed off the command FIFO, B-114) the base address is `(g - 0x20) * 32` for 0x20..0x7E (unchanged) and `(g - 0xA0 + 95) * 32` for 0xA0..0xFF; 0x7F..0x9F select a blank glyph. One compare and one subtract in a stage that has its own register: no new timing path on the compose side. Sim: scene-reference test for both ranges, a mutant for the wrong offset above 0xA0.
* Registers (MMIO decode is open from 0x1A0): `FONT_CTRL` 0x1A0 read: bit 31 present, [15:0] `FONT_WORDS` (6,112 for Latin-1; a bitstream built with fewer glyphs reports its own, and the loader checks it), [27:16] a generation counter that increments on every completed load; write bit 0 = reset the write index. `FONT_IDX` 0x1A4 (word index, auto-increments on every data write, like `CLUT_IDX` and `HAL_IDX`) and `FONT_DATA` 0x1A8 (write a word). Documented in `docs/MMIO_ALLOCATION.md`.
* Cost estimate: a few registers, one address counter, no new block RAM, no DSP. **Not a measurement;** the fit decides. Bundle it with the next RTL fit (roadmap item 17's I2S diagnostic widen and the `eq_in_l` rename are waiting for one), two seeds, as always.

### 3.2 The font file format (shared by the cold image and Tau Omega)

One font = one record, little endian, built by `tools/gen_font_atlases.py`:

```
"TFNT" | version u16 = 1 | flags u16 | crc32 of the rest u32
family[16] | weight u16 (400..900) | cell_w u8 = 16 | cell_h u8 = 16
nranges u8 = 2 | baseline u8 | reserved u16
ranges[nranges]                    first u16, last u16: 0x0020..0x007E, 0x00A0..0x00FF (in this order, contiguous in the atlas)
advance[n]                         one byte per glyph, n = the sum of the range sizes (191), then zero bytes up to a multiple of 4 (the words that follow start on a 4-byte boundary)
atlas words                        n * 32 x u32, the exact words font_ram expects
```

* The firmware loads a record only after the CRC, the cell size and the range check pass; a failing record leaves the current font untouched and sets an Info code (same convention as `THEM`/`PRST`).
* Built-in fonts live in the cold image as `COLD_DATA` (two records, about 99 KB (4 x (191 x 128 B + 191 B + header)), in the existing PSRAM cold region; generated into `fw2/generated/font_data.c`, one file name added to `link.ld`'s cold rules, as every other cold object).
* The same record goes into tau-assets.bin as a new section `FONT` (the container's 64 KiB limit holds two Latin-1 fonts of 24.5 KB each; the choice list is SemiBold, Bold and up to one more from a file). Omega writing it is a later phase (section 9); the firmware accepts it from day one so the format is exercised.

### 3.3 Swapping fonts safely (the review's main finding)

The CPU writes the font memory while the engine may still hold queued `OP_CHAR` commands (text is queued, not drawn at call time) or be composing a row. A swap in that state tears glyphs. Rule, enforced in one function `font_select(i)`:

1. Refuse during a Helios compose or crossfade (`gfx` already knows these states) and while `FB_HELD`-style overlays own the engine; Appearance is a Settings page, so the choice is made with the page idle.
2. `gfx_fence()` so every queued command has executed, then wait for the engine's idle bit.
3. Write `FONT_IDX = 0`, stream the 6,112 words (24 KB of MMIO writes on a 66 MHz core, about 1 to 2 ms by estimate; measured in the host harness, then on hardware), read back `FONT_CTRL`'s generation to confirm the load completed.
4. Copy the font's 191 advance bytes into the hot table `font_adv_cur[191]` (191 B of RAM replaces today's 95 B ROM table: +96 B).
5. `views_refresh()`: the whole screen repaints in the new weight inside one frame (the same bracket a theme change already uses). Nothing is drawn between steps 2 and 5.

A double-banked memory (swap by a select bit, no quiesce) was considered and rejected: it doubles the blocks to avoid a once-in-a-while repaint the theme change already pays.

### 3.4 Selection, persistence, boot order

* Appearance gets a `FONT` row: SEMIBOLD, BOLD, TAMZEN, TAMZEN BOLD (then any `FONT` sections from tau-assets.bin, named by family and weight, up to the choice list limit). Row style is the existing `RK_CHOICE`.
* Persisted: three bits of **word 18 (persist id 29), bits 20-22**, next to the five meter-preset fields (`SF_FONT`, max 7, default 0). Eight choices: the four built-ins and up to four file fonts. No new interact.json entry (the 16-entry cap, KB-079, is not touched).
* Boot: the cold image loads (`cold_boot_load`) before the views start; apply the saved font right after it and before `views_init`, so a Bold user never sees a flash of the default font after the first screen. The default (SemiBold) is already in the memory, so a SemiBold user pays nothing at boot.
* No font is persisted by Core Settings; the row mirrors the value like the others.

### 3.5 Pairing and compatibility

* `TAU_FONT_RAM` is a **bitstream feature**: add `FONT` to the `TAUFWNEED` list the firmware declares and to `FEATURES` in `tools/fit_manifest.py`. The packagers and the installer then refuse the new firmware on a bitstream without the font memory and refuse the new bitstream's atlases on an old ROM; no `CORE_VERSION` bump (a mismatch is cosmetic and is caught by the marker, the project's probe-first convention).
* Runtime probe too: `caps.font` from `FONT_CTRL` bit 31; without it the Appearance row is not shown and the (old) built-in font stays. The 95-byte Inter advance table is not kept: Player 2 builds are always packaged with a bitstream that has the feature.
* The default font in the bitstream must equal `FONT_DEFAULT` in firmware: a generated constant (`fw2/generated/font_default.h`: advances plus the atlas CRC) read by both sides; a host test fails if the bitstream's initial block and the cold record disagree.

### 3.6 Latin-1 end to end: the text path (the part that makes accents actually appear)

A bigger font is useless while the strings still arrive as ASCII. Today `gfx.c` turns anything outside 0x20..0x7E into a space, the library tool folds names to ASCII (B-026), and `id3_text_body` treats ordinary-encoding bytes as raw bytes with no UTF-8 check.

* **One internal text format: Latin-1, one byte per character.** `glyph_of` accepts 0x20..0x7E and 0xA0..0xFF; control and C1 codes (0x00..0x1F, 0x7F..0x9F) draw as a space.
* **One conversion function, `text_to_latin1()` (pure, host-tested, cold),** used wherever text enters from outside: ID3 v2 text frames in all four encodings (0 Latin-1 or UTF-8 guess, 1 and 2 UTF-16 with and without BOM, 3 UTF-8), Vorbis comments (UTF-8), file and folder names, playlist text. It decodes to code points and keeps those up to U+00FF. A short fold table maps the common typographic ones: U+2018/2019 to `'`, U+201C/201D to `"`, U+2013/2014 to `-`, U+2026 to `...`, U+00A0 to a space; **anything else becomes `?`** (never a silent drop and never a half character). Truncation never splits a UTF-8 sequence.
* **Library index and sync tool.** The index stores Latin-1 strings, so `tools/tau_library.py` and `sync_media.py` stop folding accents to ASCII (B-026) and write Latin-1; sorting uses the existing folded key so "Eno" and "Éno" sort together. This is what closes BUG-001 (accented names were skipped). The index format version is unchanged, but a library built this way needs the new firmware to show accents; an old firmware shows the bytes above 0x7E as spaces. Cross-project entry in `docs/features/CROSS_PROJECT_INTERFACE.md` (Tau Omega's index writer follows the same rule).
* **Verification:** a host test with real tags (mutagen-written ID3 in all four encodings, a Vorbis comment, a UTF-8 filename) through `text_to_latin1()` to the exact bytes expected, including the fold table, the `?` rule and the no-split rule; mutants for each encoding branch and for the fold.

## 4. The atlas tool (`tools/gen_font_atlases.py`)

* Input: the static TTFs (`SpaceGrotesk-SemiBold.ttf`, `SpaceGrotesk-Bold.ttf`; **checked 2026-10-16: both cover all of Latin-1** (every code in 0x21..0x7E and 0xA1..0xFF has a glyph) **and Latin Extended-A (128 of 128), but only 3 Greek and no Cyrillic letters;** the repo's `third_party/font/` gets the files and the OFL text; `docs/PROVENANCE.md` gets the entry; SIL OFL allows it, the font file is data, the project stays MIT).
* **Rasterise by 8 x supersampling and box-filter down**, not by the font engine's hinted renderer. Measured today: hinted rendering at 15 px made Medium and SemiBold identical (same glyphs, same advances); supersampling gives three clearly different weights.
* **Size and baseline are tool parameters with acceptance checks, not constants.** Space Grotesk's x-height and cap height differ from Inter's; the tool takes `--size` (fractional allowed through the supersample) and `--baseline`, matches the cap height to Inter's reference, and **fails the build when any glyph's ink touches cell row 0, or touches row 15 other than a descender's tail, or reaches column 15**. (At 15 px / baseline 12 neither SemiBold nor Bold clips in the sample line.)
* **Tabular digits:** the advance of 0-9 is the widest digit's, so clocks and counters do not jitter.
* **Advance rule** stays ink + 2, capped at 16, space from the font (today's rule). Optional **pair kerning** (a handful of pairs such as `T w`, `A V`, `L T`, applied in `gfx_text` from a small cold table, 0 hot bytes) is listed as phase 5; the left-aligned cell makes it free to add later, and the "Tw in" gap in the preview shows why it is worth having.
* Coverage 8 to 4 bits with rounding (as `gen_font_rom.py` documents), then **a per-font pre-curve** chosen so the mean coverage of a vertical stem matches the reference font's. That keeps the fitted RTL weight tables valid without a new fit per font. `gen_text_gamma.py --check` runs for every font and must pass for dark and light polarity.
* Both ranges are rendered with the same size, baseline and clipping checks; **accented capitals are the tight case** (an `Å` or `Ö` with its ring or dots must stay inside rows 0..15 at baseline 12): the tool fails the build if one does not, and the fix (a smaller size for that font, or flattening the accent by a documented rule) is decided by looking at the preview.
* Outputs: the cold records (`fw2/generated/font_data.c`), the default font's `initial` block for `font_ram.v`, `fw/font_metrics.h`'s replacement (`fw2/generated/font_default.h`), a PNG preview of every font on the real cell grid, and a line count of the strings that no longer fit (section 5).
* Deterministic: the same inputs give byte-identical outputs; `--check` regenerates and compares, wired into `make test-host`.

## 5. UI impact (the part a font swap always forgets)

Every layout depends on advances: right alignment (`gfx_text_width`), truncation, the track-title box, menu labels, button hints. Space Grotesk is wider on some strings and narrower on others.

* A host test renders **every fixed UI string** (menus, hints, page titles, Info row names) with each font through `font_adv` and fails if a string exceeds the width its slot has; the strings come from the same fixtures `tools/ui_snapshot_renderer.py` already uses. Dynamic text (titles, artists, albums) is already clipped to its box; the test also checks the clip, not just the fit.
* The UI snapshot fixtures are regenerated; the visual review is the owner's on the Pocket.
* Text colour and weight: Bold on the light theme is the sensitive case (the light weight table is the youngest, B-316): the check above covers it, and the owner listens/looks at both polarities in the first test build.

## 6. Verification

| Layer | Test | Mutants it must kill |
|---|---|---|
| Tool | determinism; ink inside the cell; tabular digits; tau-font record round trip; CRC refuses a flipped bit | cell clipped, digits proportional, CRC ignored |
| RTL (sim) | write through the MMIO path then draw: the engine's output equals the Python reference renderer using the loaded atlas (`blit_reference.py` already parses `font_rom.v`; it gains a "load this atlas" step); default content equals the cold record; auto-increment and generation counter | ignore write, index not incremented, wrong word order, write during read corrupts a neighbour, default not initialised |
| Firmware (host) | `font_select` fences first (a mock engine with queued chars must complete before the first write: red then green); bad CRC leaves the font untouched; saved choice applied before `views_init`; advance table switches with the atlas | skip the fence, skip the CRC, advances not switched |
| Packaging | marker/manifest: the new firmware is refused on a bitstream without `FONT`; the new bitstream is refused by an old ROM | feature missing from either list |
| Hardware | a **font self-test** (new row in the CHECK page and a boot probe): draw `H` and `g` in a corner of the stash, read the pixels back through the mailbox, compare to the tool's golden coverage; run for both fonts and after a swap | n/a (this is the proof) |

## 7. Resources

| | Cost |
|---|---|
| M10K | +12 (the font memory doubles, from about 12 to about 24 blocks; the one real cost of Latin-1) |
| ALMs | a few dozen (estimate) |
| Hot RAM | +96 B (the advance table is 191 bytes and moves from ROM to RAM) |
| Cold image | +99 KB for four records (cold PSRAM, not hot RAM) |
| Boot time | +0 for the default, about 2 ms for a Bold user (estimate; measured in the first test build) |
| Persist | 2 bits in an existing word |

## 8. Risks

| Risk | Handling |
|---|---|
| A swap tears glyphs | The fence, idle wait and full repaint of section 3.3; the host test with a mock queue. |
| The bitstream's default and the firmware's expectation drift | One generated constant read by both, a host test that compares them, plus the marker pairing. |
| Clipped glyphs or ugly spacing | Acceptance checks in the tool; the UI string-fit test; kerning is an optional phase. |
| Text weight looks wrong on one polarity | `gen_text_gamma.py --check` per font, per polarity; per-font pre-curve. |
| Timing | `font_q`'s read side is untouched; the write side is a plain registered MMIO path into a dual-clock memory (the CLUT's pattern, which closed at the first attempt). Two seeds, as always. |
| Wider font overflows a fixed slot | The string-fit test. |
| Old bitstream in the field | Marker refuses the package; the installer already checks. |

## 9. Phases (each ends with something a human can look at or test)

1. **Tool and preview** (host only): `gen_font_atlases.py` (both ranges), the two cold records, the PNG preview including the accented capitals, determinism and clipping checks. *Owner looks at the preview.*
2. **UI fit test** (host only): the string-width test over every fixed string with both fonts; fixes list.
3. **RTL**: `font_ram.v` (6,112 words, 13-bit address), the three registers, the 8-bit glyph field and the second range in `q_char_base`, simulation against the reference renderer for both ranges, mutants, MMIO doc. No fit yet.
3b. **Text path** (host only, can run before 3): `text_to_latin1()`, the library and sync tools writing Latin-1, tests with real tags.
4. **Firmware**: `font_select`, cold records, the Appearance row, persistence, `caps.font`, marker, self-test row; host tests. Packaged on a **new fit**: launch with the next RTL bundle, two seeds, estimate and finish time stated at launch.
5. **Optional**: pair kerning table; the `FONT` section written by Tau Omega (format already accepted), a row in `CROSS_PROJECT_INTERFACE.md`.

## 10. Decisions for the owner

1. **Default font: Tamzen Regular (decided 2026-10-17).** It is native 8 x 16, monospace, crisp, and needs no cold image at all to look right at boot; Space Grotesk SemiBold and Bold and Tamzen Bold are chosen in Appearance.
2. **Where the choice is persisted:** word 18 bits 20-21 (recommended) vs session-only like theme.
3. **Pair kerning:** build in phase 5 or never.
4. **Omega fonts:** accept the `FONT` section now (cheap) and write it from Omega later (recommended).
5. **Latin-1 costs about 12 extra block RAMs** (about 252 of 308 afterwards, estimate). Accept, or fall back to ASCII only (no extra blocks, the plan otherwise unchanged). *(recommended: accept; 56 blocks stay free)*

## 11. Not in this spec

**Japanese and the rest of Unicode (parked, owner 2026-10-16).** Space Grotesk has no kana or ideographs, no Cyrillic and almost no Greek. Latin Extended-A (128 more glyphs) would cost about another 16 blocks and is not planned. CJK needs the route in `PHASE_F_SPEC.md` section 13 and `HARPMUDD_UPSTREAM_1.5_REVIEW.md` section 5 (a 1-bit bitmap face of about 800 KB streamed from PSRAM through a glyph-index marker: option C of section 2 here, the string path of section 3.6 widened to UTF-8 code points, and the compose-stage timing fix upstream needed). The record format (`nranges`, 16-bit range bounds, a version) and the 8-bit glyph field do not prevent it. Text scaling other than the engine's integer 2x/3x on the built-in font. A third weight (the preview shows Regular and Medium are good candidates if wanted later: the list is data).

## 12. Review notes (what changed from the first plan)

1. **The engine cannot read PSRAM**, so "loaded from cold" needed the writable font memory: the first plan's storage section was incomplete.
2. **Swap safety:** queued `OP_CHAR` commands and the font RAM's two clocks make an unfenced write a real hazard; fence, idle wait, repaint.
3. **Boot:** default in the bitstream (fail-safe); saved choice applied before the first view so there is no weight flash.
4. **Pairing:** a bitstream feature on the existing `TAUFWNEED` rail, not a new interlock; the default font is a generated constant both sides read.
5. **Rasterising:** supersampled, because hinted rendering collapses Medium onto SemiBold.
6. **Metrics:** size and baseline are checked parameters; tabular digits; per-font pre-curve keeps the fitted RTL weight tables valid.
7. **UI fit test** for every fixed string and each polarity: the thing font swaps forget.
8. **Hot/cold accounting:** 24.5 KB cold, +95 B hot; no new block RAM; persistence in spare bits.
9. **Hardware proof** is a pixel read-back self-test, not a screenshot judgement.
10. **Kerning and Omega export** are kept as later phases, with the format already versioned for them.
11. **Latin-1 scope:** the glyph field gets its eighth bit from `cmd_glyph_hi` (compatible with the old firmware), the memory goes to 13 address bits, and the string path gets one conversion function: without it a bigger font shows nothing new.

## 13. Icons: the same idea, a separate record, and no new RTL

Asked 2026-10-16; the owner's strip received 2026-10-17. **Same container, separate section.** Icons go into `tau-assets.bin` as their own section `ICON` (the container already names it as planned), not into the `TFNT` record: a font needs the font memory in the bitstream, an icon does not.

* **The icon set (`assets/icons/player_icons_strip.png`, the owner's design): 14 icons, each 28 px tall, 12 to 26 px wide, greyscale with anti-aliased edges:** `play` (12), `pause` (17), `back` (20), `play_box` (22), `repeat` (22), `repeat_one` (22), `shuffle_off` (24), `shuffle` (24), `note` (22), `mute` (22), `vol0` (12), `vol1` (18), `vol2` (22), `vol3` (26). Names `play_box`, `back` and `vol0` are the tool's guesses; the ids are what the firmware uses, so renaming costs nothing.
* **Tool and record (built, host-tested): `tools/gen_icons.py` writes `fw2/generated/icons/player.ticn` (4,272 bytes).** Format `TICN` version 1: a CRC, a count, and per icon an id, its size, a name and a packed 4-bit coverage bitmap (two pixels to a byte, rows top to bottom). The icons are located by an explicit column table (neighbouring icons are only 4 to 6 columns apart, the same as the gaps inside the pause, speaker and wave icons), and the tool fails if any inked column lies outside it. `sim/test_icons.py` checks the record against the strip's own pixels, the limits below, determinism and two mutants.
* **Drawing path (existing, hardware-proven, no RTL):** the palette blit `OP_CBLIT` with the 256-entry colour table, as the meter thumbnails are drawn. At draw time the firmware loads a **16-entry table: entry c is the background colour blended toward the foreground colour by c/15** (so the anti-aliasing becomes real colour steps and the icon follows the theme: foreground and background are theme roles chosen per draw), then builds the 1-byte-per-pixel index plane in the off-screen stash (one rectangle per run of equal pixels, the thumbnail code's method) once per icon and size, and copies it with one `OP_CBLIT` per redraw.
* **Limits that apply:** width at most **127 px** per blit (the engine's row buffer; the widest icon is 26), height limited only by the stash rows (360 and below; 28 per icon, the 14 icons together are 14 x 28 x about 22 words in the stash). One bitmap per size; no runtime scaling. Palette: 16 entries used (4-bit coverage), 256 available.
* **Transparency:** `OP_CBLIT` writes an opaque rectangle (the colour-key test applies to `OP_BLIT` only), so the icon's own background is the background colour loaded into the table. That is exact on the flat panels the transport bar and settings rows use; over a gradient it leaves a flat patch. The exact alternative is `OP_CBLIT` into the stash, then a colour-keyed `OP_BLIT` to the screen (two commands per icon): **needs a hardware test before relying on it.**
* **Firmware (next, with the font loader):** an icon table in cold data, `icon_draw(id, x, y, fg_role, bg_role)`, the plane cache in the stash, and a Check row that draws every icon and reads back a checksum through the mailbox. Not built yet.
* **Status:** the asset, tool, record and tests are built; drawing is firmware work that needs no bitstream change.

## 14. Tamzen (pixel font): why 8 x 16, and what it changes

Added 2026-10-16 (owner). Tamzen (Suraj N. Kurapati's fork of Tamsyn, fixed-width bitmap, 8 x 16 and other sizes) is a bitmap font, so it must be used at a native size and never scaled.

* **Size chosen: 8 x 16 (Regular and Bold), the native size that fills the cell exactly.** The cell is 16 x 16 and the font's bounding box is 8 wide, 16 tall, 4 rows below the baseline: its baseline is row 12, the same as the rest. Every set pixel is drawn as a real pixel (full coverage, no grey), so it is crisp with no anti-aliasing; the engine's text weight table maps coverage 15 to the full colour. The other Tamzen sizes (10 x 20, 8 x 15 and smaller) either do not fit 16 rows or would leave rows unused; integer 2x of a smaller one (for example 8 x 8) is not offered because the engine's text scale applies to the whole 16-row cell.
* **Metrics:** monospace, advance 8 for every glyph (digits are tabular by nature), so strings are narrower than in Space Grotesk and every UI string-fit check holds with room to spare (`sim/test_font_fit.py`).
* **Latin-1:** the 8 x 16 BDF has 189 glyphs and lacks 17 Latin-1 codes (§ ª ¬ ® ¯ ± ² ³ ´ µ ¶ · ¹ º ¼ ½ ¾). `tools/gen_font_atlases.py` fills each with the plain sign the firmware's draw-time fold uses (S a - R - + 2 3 ' u P . 1 o 1 1 3), drawn in Tamzen, so there is no hole; all other accented letters are the font's own.
* **Licence:** Tamzen's own licence (copied to `third_party/font/LICENSE-Tamzen`, read 2026-10-16) grants permission "to use, copy, modify, and distribute it as you see fit", as is, no warranty; the Tamsyn author's identical grant is included. Compatible with this MIT repository; the notice stays with the files (`docs/PROVENANCE.md`).
* **Source files:** `Tamzen8x16r.bdf` and `Tamzen8x16b.bdf` from `github.com/sunaku/tamzen-font` (`bdf/`), 25 KB each, downloaded 2026-10-16 with the owner's go-ahead; SHA-256 recorded in PROVENANCE.
* **Tool:** the same record format; a BDF is parsed (not rasterised), so the records are exact. Tests: the record equals the BDF's own bitmap for `A` (read straight from the file), every pixel is full or empty, every advance is 8.

