# Cover art from a pre-converted image (TIM1 reader), design and status

Status: **built behind `TAU_ART_TIMG` (default off), host-tested, NOT run on a Pocket.** B-285. The 128 px layout is now the default screen (B-286). Format and encoder: `tools/tau_image.py`,
study: `docs/IMAGE_FORMATS.md`. Owner decision: palette-256, 128 px on the long side, no crop, no letterbox.

## 1. What it does
For a **library** track, look for `<track folder>/tau-art/cover_128.pal256.timg` (written by `tools/sync_media.py --art-variants`). If it is
there and valid, draw it instead of decoding the embedded JPEG (2.6 to 15.8 s). If not, the embedded-cover path runs exactly as today.

## 2. Memory placement (the hard part)
The draw engine can only reach framebuffer-grid words (`fb_cmd_addr` is 19 bits: 1024 rows of 512 words); the CPU cannot write those rows
directly (the window starts at `PL_SDRAM_BASE`, B-192), so the CPU writes through the **SDRAM mailbox**, the path `fw/chladni.inc` proved.

| Rows | Use |
|---|---|
| 0..359 | the visible frame |
| 360..487 | the art stash (`ART_STASH_Y` = 360, `ART_H` = 128); rows 360..399 were unused |
| 488..839 | meter thumbnails, **compacted** to the 11 live meters (32 rows each) |
| **840..967** | **the cover index plane (`TIMG_PLANE_Y`), 128 rows x up to 128 words** |
| 984..1023 | the Chladni tile plane and the probe cell |

Before, the art stash sat at row 400 and the thumbnail stash (15 slots, 480 rows) ended at 984, so there was no room; with the art now 128 px the stash moved up to row 360 and the thumbnails are compacted. Four enum slots are retired meters with no thumbnail
data, so `thumb_slot(viz)` (behind the macro; identity otherwise) removes them from the stash. A `_Static_assert` in `fw/settingsui.inc` pins
plane start = thumbnails end and plane end <= 984. One index per 16-bit word (low byte), exactly what `OP_CBLIT` reads.

## 3. Load sequence (`fw/timg.inc`, cold code)
1. Cover path from the current library track's path (`lib_path`): folder + `tau-art/cover_128.pal256.timg`. Its FNV-1a hash is the **reuse key**
   stored in `art_sig`, so the 13 tracks of an album load the cover once (same idea as B-075's same-picture reuse; the JPEG `art_sig` and this one
   cannot be confused, they are compared only against values of the same kind in practice, and a collision only costs a redundant reload).
2. Open the file by absolute path into **data slot 7** (new: `tools/tau_data_slots.add_cover_slot`, `package_dev_build.py --cover-slot`), via
   `pl_open_into`, the mechanism the playlist slot uses.
3. Read 16 bytes, `timg_parse` (fw/timg_core.h); read the 512-byte CLUT straight into the engine's CLUT (`R_CLUT_IDX/DATA`).
4. Stream the indices 4 KB at a time through the tag buffer; pack two pixels per mailbox write into the plane.
5. `OP_CBLIT` through the CLUT into the art panel (pieces of at most 120 words: a CBLIT is 127 wide at most).

## 4. Errors (all fall back to the embedded cover, nothing is drawn, no toast)
`TIMG_E_MAGIC` bad magic, `_FORMAT` not palette/8-bit/256 colours, `_SIZE` zero or over 128 or payload != 512 + w*h, `_OPEN` the file is not there,
`_READ` a short read or a mailbox timeout, `_ENGINE` no blit engine (`BLIT_READY()`) or the known-answer mailbox probe failed (the same guard style as
`BLIT_READY()`/`RRECT_READY()`). A failed cover path is remembered (`timg_miss_sig`) so the album's other tracks do not retry it. The last code is in
`timg_last_err` (not on an Info row yet).

## 5. Expected cost (model, not measured)
128 x 128: about 17 KB of SD reads (5 chunks) + 8,192 mailbox writes (about 3 us each, about 25 ms) + one CBLIT: 25 to 67 ms (`tau_image.estimate_ms`).
The embedded path was 2.6 to 15.8 s. Measure on the Pocket only after approval.

## 6. Layout and open items
1. **Layout: resolved (owner, 2026-09-26).** The cover was designed at 128 px; the firmware was wrong. The now-playing screen follows the design shared at 4x: cover 128 px at (8, 8) with rounded corners (no plate), pill y 22 (18 px), text column x 152, meter y 152 (122 px), progress y 295 (6 px), clock y 306, transport y 334, margins 16. `ART_IMG` 128, `ART_PAD` 0. The embedded-JPEG path also decodes to 128 now (its accumulator 15,360 B in the PSRAM section, region raised to 16 KB).
2. **Legacy playlists.** Only library tracks are handled (their absolute paths are known). Playlist-mode tracks need the folder derived from the playlist template.
3. **Data slot 7** is a new declaration in `data.json`: add it to `docs/CROSS_PROJECT_INTERFACE.md` for Tau Omega when this is enabled.
4. Not done from the design: the bottom transport bar's darker background strip, and the diagnostic stress HUD (y 341/344) now overlaps the transport row (Diagnostic Build only).

## 7. Verification
`sim/test_tau_timg.py` (in `make test-host`): real `.timg` files from the Python encoder (128x128, 85x128, 128x83, 91x91, 40x30, 1x5), both mailbox half
orders, pixels compared with the Python decoder plus the crop rule; 128 px wide in pieces <= 120; bad magic / wrong format / bad size / bad payload /
truncated / missing / broken mailbox all refused with the right code and nothing drawn. This proves the logic under emulation, not the RTL or the host's
file behaviour (opening an absolute path with a subfolder into a new slot is the main unverified hardware assumption; B-033 proved absolute-path opens for
the audio slot).
