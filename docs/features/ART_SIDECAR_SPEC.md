# Cover art sidecars: always, per album, several sizes (spec, 2026-10-14, B-728)

**Status: DESIGN. Nothing built.** Owner points (2026-10-14): the embedded-JPEG cover decode blocks the loop for seconds (A-120, B-027) and makes gapless meaningless (`GAPLESS_SPEC.md` section 2c); covers should **always come from a sidecar**, with optimised sizes for different uses; the sizes themselves are to be settled later and changing them must be a minimal change.

## 1. What exists today [READ]
- **Format:** TIM1 `.timg` (`docs/IMAGE_FORMATS.md`, `tools/tau_image.py`, firmware reader `fw/timg.inc`): pal256 is a 16-byte header, a 256-entry RGB565 palette (512 B) and one byte per pixel, so a W x H file is **W x H + 528 B**; the hardware CLUT blit draws it, no CPU decode. Load measured about 90 ms for 128 px (B-325, B-330).
- **Placement:** one sidecar per **album folder**: `<album>/tau-art/cover_128.pal256.timg`, found by the fixed name `fw/timg.inc:149`. Every track of the folder shows it.
- **Creation:** `tools/sync_media.py --art-variants` only (opt-in, Pillow); the source is the folder image, else the first track with embedded art, else any image under the folder; non-square covers are scaled by the long side (`fit_long_side`, no crop). Default variant `pal256`, default size 128 (`tau_image.DEFAULT_VARIANTS`, `DEFAULT_SIZE`).
- **Fallback:** no sidecar means the embedded JPEG is decoded in `load_track()` (2.6 s at 707 px, 5.3 s at 455, 15.8 s at 1400), synchronously.
- **Index:** the details section (B-727) records whether the 128 px sidecar exists (flag bit 2) and the embedded cover size.

## 2. Decisions
1. **Sidecars are always made.** `sync_media.py` creates them for every album it copies unless `--no-art` is given (today's opt-in flag inverts); Tau Omega does the same on sync. The embedded JPEG stays in the file untouched and is only a fallback.
2. **One set per album folder, keyed by size.** `<album>/tau-art/cover_<size>.pal256.timg` for each size in the table below (size = long side in px, aspect preserved). A single-file loose track gets the set in its own folder. A per-track override (`<track name>.<size>.timg`) is a later option, not needed now.
3. **Sizes are a table, not code.** One file `tools/art_sizes.json` lists `{use, size}`; the sync tool reads it, and a small generator (as `tools/gen_meters.py`) writes `fw/art_sizes.h` from it, so changing, adding or dropping a size is an edit of that JSON plus a re-sync, no firmware logic change. The firmware asks for a USE (`ART_USE_PLAYING`, `ART_USE_LIST`, `ART_USE_FULLSCREEN`), maps it to the size, and reads only that file.
4. **Fallback chain, in order:** the sidecar of the requested size; else any other size present, scaled by the existing hardware scaled blit (`OP_SBLIT`, B4) to fit; else the embedded JPEG through the **asynchronous, lowest-priority decode job** (GAPLESS_SPEC 2c, step G1b), which writes a 128 px stash and never blocks audio; else the placeholder plate. The sidecar flag in the index lets the UI know before opening the file.
5. **No cover is ever decoded on the main path once G1b exists.** Until then a missing sidecar keeps today's blocking decode (documented limit).

## 3. Sizes (PROPOSED, to be settled; the cost is why a small set is enough)
| Use | Size (long side) | File | Read at 850 KB/s (KB-139) | Where it is shown |
|---|---|---|---|---|
| list tile (library rows, 28x18 today) | 64 | 4.6 KB | about 6 ms | library lists, track details header |
| playing (now playing, 92 px art plate today, 128 reserved) | 128 | 16.9 KB | about 20 ms (90 ms measured with the draw) | player screen |
| fullscreen cover | 360 | 130 KB | about 150 ms | fullscreen view (screen is 400 x 360) |
Intermediate sizes (96, 192, 256) cost 9.7, 37.4 and 66.1 KB. A file is a few percent of a track's size, a whole album adds 150 KB to 1 MB of covers per 100 MB of music; at 10,000 albums the three sizes are about 1.5 GB on the card: worth stating before the set grows. Each cover is read once per track change (the playing size) or once per visible list row; list thumbnails would be read per row, so the library page should prefetch them one frame ahead, not per redraw.
**Open:** whether the list size is needed at all (today's tiles are drawn from the playing size), the fullscreen size (360 square or the 400 x 360 screen), and 4:3 or wide covers (long-side scaling leaves bars).

## 4. Work, in order
1. `tools/art_sizes.json` + `sync_media.py` default-on with `--no-art`, a set of sizes per album folder, idempotent (existing files kept unless the source cover changed: compare a hash stored in the filename or a `tau-art/manifest.json`), tests with real covers (the Test Album, the Hyperion FLAC pictures, an embedded-only MP3).
2. `fw/art_sizes.h` generator and `fw/timg.inc` lookup by use; host test with the real reader (red first: a missing size falls back to another size, then to none).
3. Index details flag becomes a size bitmask (the 32-byte record has room: today's bit 2 stays "128 px present"), written by `tau_library.py`.
4. Library list thumbnails and the fullscreen cover (UI work, after G1b).
5. Tau Omega: same sets on sync (`docs/CROSS_PROJECT_INTERFACE.md` addendum).
**Minimal-change promise:** after step 2, a new size is one JSON edit and a re-sync of the card.
