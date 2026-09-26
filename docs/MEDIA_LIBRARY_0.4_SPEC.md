# Media library (Tau 0.4): specification

**Status:** design only (2026-09-21, audit B-031). No code, card or VM was touched. Builds on `docs/MEDIA_LIBRARY_0.4_BRIEF.md` (constraints and reusable parts are not repeated; read it first).
Evidence labels: **[HW]** Pocket result, **[SRC]** read in source, **[DOC]** documentation, **[EST]** estimate to be measured. Numbers marked [EST] are predictions and must be recorded as such before any hardware run.
Items marked **ASSUMED** are recommendations for the owner decisions in section 12; the spec follows them unless the owner changes them.

## 0. What is new relative to the brief
1. **Absolute paths are the mechanism, and they are untested on hardware.** `pl_open_try()` keeps the slot's current directory for bare names and replaces the whole path only when the name starts with `/` [SRC `fw/playlist.inc`]. Every playlist so far used bare names next to the playlist [HW]. A library needs `/Assets/tau/common/<album>/<file>`. **Proven in phase 0 (B-033, [HW] owner report, KB-043):** absolute paths to MP3 and FLAC in three folders, including a 175-byte path, opened; a missing path was skipped cleanly. Side finding: a relative sub-folder line was skipped, so relative names probably resolve against the slot's current directory; the library uses absolute paths only and does not depend on it.
2. **The library must not use the release's art-in-BRAM default** (still true without thumbnails, for code space). Release heap gap is 7,824 B with art in BRAM and 18,736 B with it in PSRAM (B-028/B-029). A library reader needs roughly 8-14 KiB of code plus state [EST], so `TAU_LIBRARY` requires `TAU_ART_PSRAM=1` (the v0.3.0 configuration) and must still leave the 6 KiB floor.
3. **No RTL, no Quartus build.** The index is read through data slots and target reads (existing mechanism), stored in the existing PSRAM window. Only `data.json` gains one slot (a second is reserved for the deferred art file). Bitstream stays the v0.3.0 P4 build. This means 0.4 needs no VM approvals, only card-write approvals.
4. **Owner decision (2026-09-21): thumbnails are deferred to the blit engine.** 0.4 has no art file and no pre-scaled covers. Track rows show a small placeholder tile: theme-colour background, the track number in black, always two digits (`01`, `07`, `12`). Now-playing keeps today's embedded-cover decode (heavy covers stay slow; known limit, B-027). This removes the art file, the T1/T2 readers, the page cache, the image-scaling dependency and the artwork latency targets.
5. **Resume needs two persist words**, not one (section 8).
6. **Formats, budget, memory map, latency targets, failure codes, CLI, tests and predictions** (sections 2-11).

## 1. Scope
In 0.4: browse Artists > Albums > Tracks, All Albums A-Z, All Tracks A-Z, jump-to-letter, play album / play from track, Shuffle All (no repeats), numbered placeholder tiles on track rows, resume of a library track, Info-page row, Diagnostic Build library check, host index builder/validator, `sync_media.py --library`. Playlists (`.m3u`) keep working exactly as today and can additionally be imported as named queues (section 2.7).
Not in 0.4 (format reserves room): thumbnails / pre-scaled covers (blit engine), genre/year/recently-played/favourites/search, non-ASCII display text, cached window or blit engine, writing anything back to the card.

## 2. Index file `tau-library.tdb`
Location: `Assets/tau/common/tau-library.tdb` (data slot 5, deferload, not required). Little-endian, every section 16-byte aligned, no pointers (offsets only). Built by the host tool from the **destination** layout (the names that will actually be on the card, after ASCII conversion and cover embedding).

### 2.1 Header (128 B)
| Off | Size | Field |
|---|---|---|
| 0 | 4 | magic `TLIB` (0x42494C54) |
| 4 | 2 | format_version = 1 |
| 6 | 2 | min_reader_version = 1 |
| 8 | 4 | header_size = 128 |
| 12 | 4 | flags: bit0 art file present, bit1 playlists section present, bit2 string encoding UTF-8 (0 = ASCII) |
| 16 | 4 | build_id = CRC32 of the body (deterministic: same input gives same id) |
| 20 | 4 | file_size |
| 24 | 4 | body_crc32 (bytes 128..file_size-1) |
| 28 | 4 | reserved 0 |
| 32 | 2x4 | n_artists, n_albums, n_tracks, n_playlists (u16 each) |
| 40 | 4 | root_str: string offset of the path root, `/Assets/tau/common/` |
| 44 | 4 | art_id: CRC32 of the art file header (0 = no art file) |
| 48 | 8x8 | section table `{offset u32, length u32}`: artists, albums, tracks, strings, album_by_title, track_by_title, letters, playlists |
| 112 | 12 | reserved 0 |
| 124 | 4 | header_crc32 over bytes 0..123 |

Hard caps (reader rejects above): 16,384 tracks, 2,048 albums, 1,024 artists, 64 playlists, file 4 MiB, strings 3 MiB. Reader ignores unknown flag bits but rejects `min_reader_version` above its own.

### 2.2 Records (fixed size, seek by index)
- **Artist, 8 B:** `name` u32 (string offset), `first_album` u16, `n_albums` u16. Sorted by the tool's sort key; albums of one artist are contiguous. Album artist (TPE2) wins over track artist (TPE1); missing = "Unknown Artist".
- **Album, 20 B:** `title` u32, `dir` u32 (string, path of the folder relative to root, no trailing `/`, empty for root), `artist` u16, `year` u16 (0 unknown), `first_track` u16, `n_tracks` u16, `art` u16 (thumbnail index, 0xFFFF none), `flags` u16. Sorted by (artist key, year, title key). Tracks of an album are contiguous in play order (disc, track number, filename).
- **Track, 16 B:** `title` u32, `file` u32 (file name in the pool, relative to the album `dir`), `secs` u16 (0 unknown, clamped 65,535), `tno` u16 (disc 6 bits << 10 | track 10 bits), `album` u16, `fmt` u8 (1 MP3, 2 FLAC), `flags` u8. Track id = index in this table (0..n-1).
- **String pool:** NUL-terminated printable ASCII, de-duplicated; offsets u32 from the section start.
- **Path of a track** = pool(root) + pool(album.dir) + `/` + pool(track.file) (no extra `/` when dir is empty). Tool refuses paths over **200 bytes** (firmware limit is `256 - name offset in the descriptor`; the offset is small but not fixed, so 200 keeps margin; confirm in phase 3).

### 2.3 Order tables and letters
`album_by_title` u16[n_albums], `track_by_title` u16[n_tracks]: ids in sorted order (tool does all collation: case-insensitive, leading "The "/"A " ignored, digits natural; the core never compares strings). `letters`: for artists, album_by_title, track_by_title, 27 u16 each (`#`, A..Z): first position of that letter, so jump-to-letter is one lookup.

### 2.4 Playlists section (optional)
Record 8 B `{name u32, first_item u16, n_items u16}` then a u16 array of track ids. The tool resolves `.m3u` lines to library ids; lines that resolve to nothing are dropped and counted in the tool report.

### 2.5 Size budget (7,180 tracks, 800 albums, 300 artists) [EST]
| Part | Bytes |
|---|---|
| header | 128 |
| artists 300 x 8 | 2,400 |
| albums 800 x 20 | 16,000 |
| tracks 7,180 x 16 | 114,880 |
| order tables | 15,960 |
| letters | 176 |
| strings (track titles ~26 B, file names ~35 B, album titles, dirs, artists) | ~509,000 |
| **total** | **~675,000 (0.64 MiB)** |
A 16,384-track library is about 1.5 MiB, inside the 4 MiB cap. Strings dominate; if ever needed, file names derivable from titles could cut ~0.25 MiB (not planned).

## 3. Thumbnail file `tau-library-art.bin` (DEFERRED: not in 0.4)
> **Update 2026-09-26 (B-284, `docs/IMAGE_FORMATS.md`):** the pixel format below (raw RGB565, 92 px, letterboxed) is superseded as the starting point. Owner decision: palette 256 (CLUT + 8-bit indices, `TIM1` container from `tools/tau_image.py`) at 128 px on the long side, non-square covers scaled proportionally with no letterbox. Per-album files (`<album>/tau-art/cover_128.pal256.timg`) are already written by `sync_media.py --art-variants`; whether the library packs them into one art file with fixed strides is open (a 128x128 entry is 16.9 KB, non-square ones are smaller). The thumbnails themselves stay deferred to the blit engine / UI-controller work.
**Deferred by the owner to the blit engine.** Nothing below is built in 0.4; the header keeps `art_id` and the album `art` field (0xFFFF) reserved so the format does not change later. The design is kept as the starting point for that work. 0.4 placeholder: a fixed tile per track row (theme background from the current colour setting, black two-digit track number from `tno`, `00` when unknown), drawn with the existing rect/char engine, no memory reads beyond the record.
Location `Assets/tau/common/tau-library-art.bin` (data slot 6, deferload, not required). Raw pixels in the framebuffer format used by the art blit (RGB565 assumed; **verify against `fw/art.inc` before freezing**, phase 2). Fixed stride so a thumbnail is found by album index with no table.
- **Header 512 B:** magic `TART`, version, n_albums, t1 size (32), t2 size (92), sections offsets, `art_id` value = CRC32 of these 512 B (matches the index header, so a stale art file is detected).
- **Section A (T1, list thumbnails):** 32 x 32 x 2 = 2,048 B per album, contiguous, so one page of rows is one 16 KiB read.
- **Section B (T2, detail / now-playing):** 92 x 92 x 2 = 16,928 B, padded to 17,408 B (34 sectors) per album.
- 800 albums: 1.6 MB + 13.9 MB = about 15.5 MB; 2,048 albums about 40 MB. Trivial next to the music.
- Source: the album's cover (folder image, else the first track's embedded art), scaled with area averaging, letterboxed square on the theme background. Missing cover: `album.art = 0xFFFF`, the core draws its placeholder.
- Tool caches thumbnails by cover hash so re-syncs only re-encode changed covers.

## 4. Memory map
PSRAM window `0xA4000000`, 32 MiB, uncached, 32 read / 26 write cycles [HW, KB-040]. Real-time masters never touch it.
| Range | Use |
|---|---|
| `0xA4000000` + 0x0000..0xFFFF | firmware static PSRAM data (art accumulator 11,040 B today); 64 KiB reserved |
| `+0x010000` .. `+0x40FFFF` | library index image (4 MiB cap) |
| `+0x410000` .. `+0x50FFFF` | reserved (thumbnail page cache, deferred) |
| `+0x510000` .. `+0x51FFFF` | queue / shuffle order u16[16,384] (32 KiB) + play history |
| `+0x520000` .. | reserved (blit engine, later features) |
SDRAM is not used. BRAM budget for the feature (state only): under 1 KiB [EST] (browse stack, cursors, counts, a 256 B path buffer, a 64 B string scratch). Strings are read from PSRAM into that scratch for drawing.

## 5. Load and browse behaviour, latency targets
Load happens **once at boot, before playback starts**, next to `pl_load()` (ASSUMED; playback and a 1 s load would fight over the single target-command channel). The core reads the file in 4 KiB windows into the tag buffer, copies to PSRAM (1,024 words, ~0.5 ms), pumps nothing else. Then verifies header CRC, body CRC, section ranges and counts against the caps, then proves the PSRAM window (same pattern as `pl_sdram_prove`/`art_psram_prove`).
| Item | Target | Prediction [EST] |
|---|---|---|
| Index load + verify, 7,180 tracks (0.64 MiB) | <= 1.5 s (hard limit 2.5 s) | 1.1-1.4 s (SD 736 KB/s = 0.9 s, copy 0.1 s, CRC 0.15 s) |
| Library open with no index file | 0 added delay | < 10 ms (one failed slot probe) |
| Text-only list page draw (12 rows) | <= 100 ms | 30-60 ms |
| Track page with 12 placeholder tiles | <= 100 ms | 40-70 ms |
| Jump to letter | <= 50 ms | < 10 ms (table lookup) |
| Shuffle All permutation (7,180) | <= 100 ms | ~25 ms (Fisher-Yates in PSRAM) |
| Track start from browse (open + head parse), no cover decode | <= 0.8 s | 0.4-0.5 s (A-120 head 371 ms) |
Reads while a track plays are done in 4 KiB windows with the existing refill pump between windows (as the playlist overlay does), never as one 17 KiB call.

## 6. Failure behaviour: feature off, not fallback
Any index failure disables the **library UI only** (no Library entry, no partial use); playback of a single file and of playlists is exactly as in 0.3. The Info page shows `LIB OFF Ennn`. No index file present is not an error (`LIB NONE`).
| Code | Cause |
|---|---|
| E10 | slot empty / file missing |
| E11 | bad magic, version above reader, header CRC |
| E12 | file size differs from header or from the slot size |
| E13 | body CRC mismatch |
| E14 | a count above the caps |
| E15 | a section range outside the file or misaligned |
| E16 | PSRAM proof failed (reuse the 0xE1 mechanism), or window bit absent on an older bitstream |
| E17 | string offset / track id out of range found during the load-time walk (sampled, first 256 records plus every 64th) |
A track that fails to open at play time shows a toast, is skipped, and counts in a session counter; three consecutive failures stop the queue (the existing skip-loop rule from A-108).

## 7. Playback integration
- A `play_src` state {single, playlist, library}. Library play builds the absolute path in the 256 B buffer and calls `pl_open_name("/Assets/...")`.
- Queue = u16 track ids in PSRAM plus a position. Play album = album range; play from track = rest of the album; Shuffle All = permutation of all ids (seeded from the cycle counter), history in the same region for Previous. Next/Previous/Repeat call the same functions as the playlist path (inspect `pl_play_dir` in phase 3: this is the main integration risk).
- Cover: unchanged (embedded decode for every track, library or not). Now-playing text keeps using the tags read at open (unchanged).
- Controls are a proposal to reconcile with the current control map in phase 3: Up/Down move, Left/Right page, L/R jump letter, A open/play, B back, Start settings, Select stays as today.

## 8. Persistence
Existing resume word (playlist) is unchanged. Library adds **two** persist words (A-118 recorded 4 free; verify):
- `LIB_POS`: bits 0-13 track id (16,384), bits 14-30 seconds (0-131,071), bit 31 zero (APF stores signed 32-bit; see the resume note in `fw/player.c`).
- `LIB_ID`: low 31 bits of the header `build_id`. A mismatch (library rebuilt) discards the resume silently.
Not persisted: browse position, queue (Shuffle All restarts as a new permutation). Shuffle/repeat use their existing settings. Append-only rule of `SETTINGS_ARCHITECTURE.md` applies.

## 9. Host tool
New `tools/tau_library.py` (stdlib only in 0.4; the Pillow-optional / `sips` decision applies when thumbnails return) plus a `--library` flag on `sync_media.py` so one command copies and indexes.
```
tau_library.py build  <dest-music-root|--core ID>  [--out DIR] [--cache FILE] [--playlists]
tau_library.py verify <index.tdb> [--root DIR]   # invariants, CRCs, every path exists under root
tau_library.py report <index.tdb>                             # counts, sizes, longest path, tag gaps
tau_library.py synth  --tracks 7180 --albums 800 --artists 300 --out DIR [--real-tracks DIR]
sync_media.py ... --library                                 # copy (existing rules), then build + verify
```
Rules: never modify sources or music files; write the index last, each via temp file then rename, verify by re-parse and SHA-256; idempotent (same input gives byte-identical files); non-zero exit on any verify failure. Tags: ID3v2.3/2.4 (TIT2, TPE1, TPE2, TALB, TRCK, TPOS, TYER/TDRC), ID3v1 fallback, FLAC Vorbis comments, transliterated to ASCII with the `sync_media.py` rules; missing tags fall back to file/folder names. Duration from the Xing/VBRI header, else CBR estimate, else FLAC STREAMINFO; unknown = 0 and shown as `--:--`. Warnings, not errors: missing tags, no cover, path over 200 (this one is an error).
Incremental cache keyed by (size, mtime_ns, path) for tags.

## 10. Firmware plan and host tests
- New `fw/library.inc`, `#ifdef TAU_LIBRARY` (default off), enabled by a `player-library` target first (requires `TAU_PL_SDRAM=1`, `TAU_ART_PSRAM=1`, settings UI). `build.sh` enforces the existing heap-gap floor; the size of the added code is a **gate**: if the gap falls under the floor, stop and re-plan (move the two art coordinate maps to PSRAM, 2 KiB, already noted in B-024) before adding features.
- **Split (B-036):** `fw/library_core.h` is portable logic with no MMIO (loader, verifier, record/string access, path builder, jump tables, queues, shuffle); `fw/library.inc` (later) holds UI and integration. The core is built into `tools/host/library_harness.c` and run under `tools/rv32sim.py` by `sim/test_library_fw.py`, which compares it with the Python reference reader (`TAU_BIG=1` adds the 7,180-track run).
- Pieces: loader + verifier + prove; string/record accessors; browse state machine and full-screen list pages (`ov_frame`, `PL_UI_*`); placeholder tile drawing; queue/shuffle; resume words; Info rows (library tracks, load ms, art state, error code); Diagnostic Build page "Library" (load time, walk every record, N random opens, optional long soak of opens).
- Data slot 5 added to `data.json` (slot 6 reserved for the art file); datatable RAM is 256 words so no RTL impact expected (verify in phase 3 that slot ids above 4 need no RTL support, by grep of `core_game.vh` and a sim).
- Host tests (added to `make test-host`): builder golden files on a fixed synthetic tree; parse / round-trip; every invariant in section 2 (ranges, contiguity, order tables are permutations, letters, path lengths); corruption matrix (every byte class flipped must yield the expected E-code in a Python reference reader that mirrors the C logic); synthetic 7,180-track size report; UI snapshot fixtures for library home, artists, albums, tracks with placeholder tiles (incl. single-digit numbers as `01`), LIB OFF; if `tools/host/*_harness.c` can compile `library.inc` as a harness, run the C loader over the same corruption matrix.
- Every change logged in `docs/AUDIT_TRAIL.md` and `CLAUDE.md`; KB entries only after hardware evidence.

## 11. Pocket test plan (predictions written before hardware)
Numbered test core: next free number is **TAU PSRAM 07** (never reuse 03/05/06); each test core gets its media through `sync_media.py`; remove superseded test cores after verified backup. All card writes need explicit approval.
| Step | Test | Prediction [EST] |
|---|---|---|
| 0 (DONE, PASS, B-033) | **Absolute-path open (no firmware change):** on the current release, a playlist in one folder listing `/Assets/tau/common/<album>/<file>` lines for two other folders, incl. a FLAC | Opens and plays all lines. If result code 4 or NONAME: the design fails and section 7 must change (relative-to-slot-directory only) before anything else. |
| 1 | Small library (3 albums, ~30 tracks), `TAU PSRAM 07` | Info: LIB 30 TRK, load < 0.3 s; browse works; play from artist/album/all; tiles show two-digit track numbers in black on the theme colour; Shuffle All visits each track once |
| 2 | Synthetic 7,180-track index (tracks beyond the real ones do not exist) | Load 1.1-1.4 s; page draws within section 5 targets; jump to letter instant; playing a nonexistent entry shows the toast and skips; free RAM equals the build's gap |
| 3 | Browse while playing at stress R1-R3 (Diagnostic Build) | Late underruns 0 (playlist overlay precedent, 4 KiB windows with pump between); worst window access unchanged (~373 cycles) |
| 4 | Negative matrix: missing file, flipped body byte, wrong version, truncated file, cap exceeded, art file from another build, PSRAM proof failure (fault-injection build) | E10, E13, E11, E12, E14, art off with library on, E16; single-file and playlist playback unaffected in each; no crash |
| 5 | Resume: play track in library, Quit, relaunch; then rebuild the library and relaunch | Returns to track and second (within 2 s); after rebuild starts clean |
| 6 | Diagnostic library check on the real ~7,180-track library (owner's copy) incl. 50 random opens | Index validates; 50/50 opens; path lengths within 200 |
Results are recorded with evidence labels; a prediction that fails is recorded as failed, not adjusted.

## 12. Decisions (recommendation for each; see the chat summary for which need the owner)
1. **Format:** custom binary (this spec), ASCII text in 0.4, UTF-8 flag reserved. Recommend as specified.
2. **Thumbnails (owner, decided):** deferred to the blit engine; 0.4 uses the numbered placeholder tile (section 3). Art file format kept as a reserved design.
3. **Index location at run time:** PSRAM, loaded whole at boot. Recommend as specified (lazy load rejected: playback contends for the one command channel).
4. **Browse model:** Artists/Albums/Tracks, A-Z views, letter jump, Shuffle All. Genre/year/search later; fields reserved.
5. **State:** two persist words (section 8).
6. **Tool distribution (owner, decided for the later art work):** Python script, owner runs it; Pillow optional with macOS `sips` fallback. Not needed in 0.4.
7. **BUG-001 firmware fix:** later. 0.4 keeps ASCII-only names and titles; the tool transliterates.
8. **Compatibility:** playlists stay supported indefinitely; no index file means today's start screen.
9. **Decided:** browse scope = Artists/Albums/Tracks + Shuffle; load at boot, missing index = today's screen. **Assumption to confirm:** the placeholder tile appears on track rows only (albums and artists are text rows). `TAU_LIBRARY` requires art in PSRAM (section 0.2).

## 13. Parked (owner, 2026-09-21)
**Enable / disable the library from Settings (advanced), with a confirmation and an explanation.** Not started. Notes for when it is picked up:
- Purpose: lets a user fall back to legacy playlist mode (Load MP3 / Load Playlist) on a card that has an index, without deleting `tau-library.tdb`.
- Where: MENU > SETTINGS > an advanced entry; A opens a confirmation page that states what changes ("Library off: Select opens the playlist, core-menu playlist and file loading work as before, the index stays on the card. Takes effect after a restart") with confirm / cancel; the same page explains what turning it back on does.
- Storage: one persisted flag. Store it inverted ("library disabled", 0 = enabled) so a missing or zero word means enabled, which is the safe default (settings rules in `fw/settings.inc`: next unused `interact.json` id, stable address, never renumber; 2 of the 4 free persist words are already earmarked for library resume, so this takes one of the remaining two).
- Behaviour: read at boot next to `lib_boot_load()`; when disabled the loader is skipped (state NONE-like but without the legacy alert, or with a distinct "LIBRARY OFF" line on Info), so the legacy paths run exactly as without an index. Applying it live is not planned (restart is simpler and avoids unloading the queue mid-playback).
- Also update HOW IT WORKS and the Info LIBRARY row (`OFF (SETTING)`), add a snapshot fixture for the confirmation page, a host test for the flag decoding, and a line in the Pocket test script.

## 14. Parked until more CPU RAM is free (owner, 2026-09-21)
Library build heap gap is 6,976 B against the 6,144 B floor, so these wait. Nothing here blocks the current tests.
1. Library resume by second (persist word budget: the four library words are used; a fifth needs the spare or a repack).
2. Diagnostic Build "Library" check page (index walk, N random opens, load time).
3. Negative-test index copies on the Pocket (missing file, flipped byte, wrong version, truncated, oversize).
4. Snapshot fixtures for the Library settings page, the loading frame and the `x / y` counter.
5. Settings-style look for the legacy playlist overlay.
6. Per-colour text colour and a lifted accent for dark palette entries (BLACK, GLOW, TRANS_SMOKE, CLASSIC_INDIGO); duplicate RGB values between the TRANS and CLASSIC sets.
7. Release 0.4 work: version bump, README and CHANGELOG (library, sync tool, colours, loading screens), two zips.
Where the memory can come from, in the order the roadmap already has: (a) blit engine (Phase F): the two art coordinate maps (2 KiB) and some drawing code, modest; (b) audio hardware (Phase D): the Helix polyphase/IMDCT/DCT kernels are about 15-20 KB of code, but the roadmap keeps the software path selectable, so RAM is only freed if that path is retired after a bit-exact soak; (c) Phase G, running cold code (settings, library, playlist, help) from the cached PSRAM window, is the large lever (tens of KB [EST]) and needs the line-fill adapter (an RTL change and its gates). Cheaper software options meanwhile: move `meter_thumb_rle` (4.6 KB) and the help/explainer text to PSRAM, both read only occasionally.

## 15. Open question (owner, 2026-09-23): does the library index or the cold image need a migration path across Tau version updates?

Not investigated yet — logged so it isn't lost, prompted directly by B-136 (`docs/AUDIT_TRAIL.md`), where a stale `tau-library.tdb` (carried over from one test core to another without rebuilding) silently pointed at the wrong platform folder and broke track opens with no error shown. That was a *test-build* mistake (mine), not a real version update, but it exposed a real coupling worth thinking through properly before a real user ever upgrades a card in place.

**What's already known, not newly discovered:**
- The index has a version field (`fw/library_core.h`: `lib_ld16(win+6) > 1u` -> `LIB_E_HDR`), so a *newer*-schema index is correctly refused by *older* firmware. **Unverified in the other direction:** does current firmware's parser assume only the one schema that has ever existed, or does it actually branch on version to stay compatible with an older-but-still-valid index? Only one schema version has ever shipped, so this has never been exercised.
- The index's `root` string is baked in at build time, tied to the destination core's own platform folder (`/Assets/<platform>/common/`, B-136). **For a real release this is safe** — a genuine version update (v0.4.0 -> v0.5.0 of `alfatreze.TAU`) keeps the same platform id (`tau`), so the root string doesn't change and an index built under the old version stays valid under the new one, *if* the schema itself is otherwise compatible. The risk is narrower than B-136 made it look: it's specifically about a platform-id change (a core rename, or swapping which numbered test build a folder's contents came from), not ordinary version bumps.
- Library resume (section 8, `LIB_ID`) already has its own safety net: a mismatch between the persisted `LIB_ID` and the current index's `build_id` silently discards the resume rather than resuming into the wrong track. This covers the *narrow* case of "index rebuilt with different content, old resume position now meaningless" — it does not cover the browse position, the queue, or anything the index format itself might need to add in a future schema (new fields, new record shape).
- `tau-cold.bin` (`fw/cold.inc`) uses a strict layout-id equality check, not a version range — any firmware change that touches cold code content requires a matching rebuild, and a mismatched pairing is refused cleanly (the same fail-safe class as B-049's `E18`). This is not a version-*compatibility* mechanism, it's a version-*rejection* mechanism: there is no plan for an old cold image to keep working on new firmware, by design, and packaging always regenerates both files together — so the real risk here is purely a packaging-process one (shipping a ROM without regenerating its matching cold image), not a data-format one.

**What genuinely hasn't been thought through:**
1. If a future release changes the index *schema* (new record fields, a different string-table layout, more sections), what's the actual migration story for a user's existing `tau-library.tdb`? Silent refusal (current behavior for a too-new index) is fine for *downgrading*; there's no plan yet for *upgrading* firmware to keep reading an old-schema index, or for firmware to detect an old-but-valid schema and offer/force a rebuild rather than just going to Legacy Playlist Mode.
2. The persisted **library history** (`LIB_POS`'s track id, `LIB_ID`'s build-id guard) only protects the *resume* case. It doesn't cover **playlists** (`tau-library.tdb`'s own playlists section) or the **queue** if track ids or ordering shift after a re-sync (files added/removed/reordered) — a playlist entry that stored track index N could silently point at a *different* track after a rebuild, with no build-id-style guard on that path today. This needs its own check before it's a real risk (does the on-disk playlist section store indices, or something more stable like paths/hashes?) — not yet read for this specific question.
3. No decision yet on whether a real "update the card in place" workflow (as opposed to `sync_media.py`'s dev-only rebuild) should force a library re-scan automatically when the firmware version changes, versus trusting an unchanged index to still be valid. `docs/TEST_SUITE_SPEC.md`/the release process (`tools/make_release.py`) don't currently touch the user's own library index at all — only test builds and dev tooling do.

**Next step, when this is picked up:** read `fw/library_core.h`'s parser to confirm point 1 (branch-on-version vs single-schema-assumed), read `tools/tau_library.py`'s playlist-section encoding for point 2, and decide whether either needs a design change *before* the schema is ever actually revved a second time — cheaper to decide this on paper now than to discover it the way B-136 was discovered.
