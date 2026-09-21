# Media library (Tau 0.4): design brief

**Goal (owner):** move away from playlists. The player should browse and play a library (artist / album / track, and later more) instead of loading an `.m3u`. Playlists may remain as an optional queue format; they stop being the way to find music.
**Status:** not started. This brief collects what is already known so the first step can be a design, not a re-investigation. Nothing here is decided; section 5 lists the decisions the owner must make.

## 1. Hard constraints (established, with evidence)
| Constraint | Consequence for a library | Source |
|---|---|---|
| **A core cannot list a directory.** APF's 0192 opens a file *by name*; there is no enumeration command. | Something that can see the filesystem must build an **index** ahead of time (the sync tool, on the host). The core reads the index and opens files by the paths in it. `tools/make_album_playlists.py` and `make_playlist_index.py` are the first two examples of that pattern. | `tools/make_album_playlists.py` header, `fw/playlist.inc` |
| **Paths must be plain ASCII** (BUG-001, KB-041). Accented names fail; the 0192 path template is built from the longest printable-ASCII run. | The indexer normalises names (`tools/sync_media.py` already does: accent to plain letter, drop the rest) and stores display strings (titles) separately from paths. Firmware fix is optional but would let titles be UTF-8. | `docs/issues/001-unicode-playlist-paths.md` |
| **Path descriptor is a fixed 256-byte struct** (`DT_WORDS` 64), and a new path must fit (`PLO_TOOLONG`). | Long `Artist/Album/Track` paths need a length budget; the tool can shorten folder names (ASCII, short) and record the final path. | `fw/player.c`, `fw/playlist.inc` |
| **Playlist limits today:** 256 tracks or 12 KB of text; 4 bytes per index entry; buffers (13,312 B) live in SDRAM at `0xA0100000`. | A library of thousands of tracks needs tables in PSRAM/SDRAM, not these buffers. The owner's real library is about **7,180 files** (ROADMAP). | `fw/player.c` (`PL_MAX`, `PL_TEXT_MAX`), ROADMAP |
| **On-chip RAM is the scarce resource.** Release heap gap is 18.7 KiB (art buffer in PSRAM); 7.8 KiB with it in BRAM. No BRAM growth is a stated exit criterion. | All large tables live off-chip, loaded on demand. | `docs/ARCHITECTURE_ROADMAP.md` phase E |
| **Off-chip memory:** PSRAM window `0xA4000000` (32 MiB, uncached, **32 cycles read / 26 write, fixed**, CPU only); SDRAM window `0xA0100000..` (about 48 avg, **worst 373 cycles** while playback runs; 1 MiB region partly used). Real-time masters never touch PSRAM. | PSRAM is the natural home for the index (fixed cost, roomy). SDRAM stays for what already lives there. Both are proven under playback (B-022, B-023). | KB-030, KB-040 |
| **SD read is about 736 KB/s sequential** (measured); one 4 KB window read per `target_read_slot`. | A 1-2 MB index loads in a few seconds if read whole, or on demand by page; design for on-demand pages with a small cache. | `fw/player.c` comments, A-120 |
| **Cover art is read only from inside the track** (APIC / FLAC PICTURE), and decode time follows the compressed bytes (5.3 s for a 255 KB, 455 px cover; 15.8 s for 1.5 MB; 2.6 s for a 36 KB, 707 px cover). The screen shows **92 px**. | A library browser cannot decode a JPEG per row. The indexer should **pre-scale thumbnails** (92x92, or smaller for lists) into an art file the core reads directly (raw RGB565 or a tiny baseline JPEG), so browsing costs a memory copy. This is also what a blit engine would consume. | B-027, KB-042, `fw/art.inc` |
| **Persistence is tiny:** interact persist words (a handful free) hold settings; resume uses a hash of the playlist name plus a position. | Library state (last track id, position, shuffle, queue) needs a defined small encoding, or a state file the core can write (nonvolatile slot / `0184` writes are **not** proven; see KB-022, KB-005: the persist path is interact.json). | KB-022, KB-025, `docs/SETTINGS_ARCHITECTURE.md` |
| **Do not write into the user's music folder** from the core; the earlier data-loss lesson (ROADMAP "destroyed the user's music library") applies. Test against a throwaway card. | The index and thumbnails live in their own folder under `Assets/tau/common/`; the tool never modifies sources. | ROADMAP |

## 2. What exists that can be reused
- **`tools/sync_media.py`:** copy, verify, ASCII names, cover embedding/re-encoding (`--cover-quality`, `--cover-max`), generated playlists, manifest JSON, multiple destination cores. Written to grow into the library sync; the manifest is a starting index.
- **`tools/library_check.py`:** predicts what the core does with each track's cover (APIC search, size caps, reduce vs full decode); constants read from the firmware.
- **Firmware:** tag parsing for ID3 and FLAC, full-screen list overlay (`ov_frame`, `PL_UI_*` rows, scrolling, paging), the playlist browser, settings menu framework (`settingsui.inc`), `pl_open_name()` (open by path), the playlist-in-SDRAM pattern and its fail-safe (`pl_sdram_prove`), the art-in-PSRAM pattern (`art_psram_prove`), Info page, Diagnostic Build tests.
- **Test infrastructure:** host snapshot renderer for UI fixtures (`tools/ui_snapshot_renderer.py`, 50 fixtures), `make test-host`, `make test`, the numbered test-core packager, the two-zip release tool, the diagnostic build for on-Pocket checks.

## 3. Candidate design (to be challenged, not assumed)
- **Index file** built by the sync tool, e.g. `Assets/tau/common/library/library.tdb`: header (version, counts, checksum), sorted string pool (ASCII display strings, UTF-8 later), artist / album / track tables with ids, per-track path (or path parts) and duration, per-album thumbnail offset, sort keys precomputed on the host. Fixed-size records so the core can seek by index number (`PSRAM word = base + id * size`). Loaded into PSRAM at start (or paged), validated by checksum; **feature off, not fallback, on any failure** (the pattern used for the playlist and the art buffer).
- **Thumbnail file** `library/art.bin`: pre-scaled covers, one per album, plus the ordinary embedded cover for the now-playing view.
- **Browse UI:** Artists > Albums > Tracks using the existing full-screen list overlay; A plays, B goes back; jump-to-letter; now-playing keeps its screen. Play order = the album or a queue built from the browse position; shuffle over the library needs a no-repeat random sequence that fits in a few KiB.
- **Playlists:** keep `.m3u` as a file type the indexer can import as a named queue; no longer the entry point.
- **Incremental sync:** the tool compares sizes/mtimes/hashes with the previous index and only re-encodes changed covers.
- **Diagnostics:** Info page row for library size and load time; Diagnostic Build test that walks the index and checks every path resolves.

## 4. Suggested phases (each with predictions written before hardware, evidence-labelled audit entries)
1. **Design + format spec** (docs only): record layout, size budget for 7,180 tracks, load/latency targets, memory map (PSRAM offsets), failure modes, sync tool CLI. Owner reviews.
2. **Indexer + validator on the host** (Python): reads tags (ID3v1/v2, FLAC Vorbis comments), builds index and thumbnails from a folder, round-trip test parser, size report for the real library on a throwaway copy. Golden-file tests.
3. **Firmware reader behind a flag** (`TAU_LIBRARY`, default off): load/prove the index in PSRAM, browse UI fixtures in the host renderer, open-by-index playback; simulation/host tests first.
4. **Pocket runs:** small library (3 albums) then a large synthetic library (thousands of entries; `tools/make_large_playlists.py` shows the approach), load time, browse latency, playback under the stress pump (0 late underruns), Info/Diagnostics checks.
5. **Release 0.4** through `tools/make_release.py` (two zips), README section for the library and the sync tool, CHANGELOG.

## 5. Decisions for the owner (ask early)
1. Index and thumbnail **format ownership**: binary custom format (compact, fast) vs something readable; how much display text may be non-ASCII.
2. **Thumbnails pre-scaled by the tool** (recommended) vs decoding embedded covers on demand; thumbnail size(s) and storage (RGB565 raw vs tiny JPEG); whether now-playing still decodes the embedded cover (needs the art-buffer decision and the blit engine comparison).
3. **Where the index lives at run time:** PSRAM (recommended) vs SDRAM; loaded whole vs paged.
4. **Browse model:** artist / album / track only, or also genre, year, recently played, favourites, search; what "shuffle all" means.
5. **State persistence:** what must survive a restart (last track, position, shuffle/repeat, queue) and the encoding, given the small persist budget.
6. **Sync tool distribution:** Python script for now, or a packaged app for Windows/macOS; who runs it (owner only in the test phase).
7. **Firmware path fix (BUG-001)** now or later: decides whether titles/paths may contain non-ASCII.
8. **Compatibility:** how long the playlist mode stays supported; behaviour when no library file exists (fall back to the current start screen).

## 6. Acceptance criteria (proposal)
- 7,000+ tracks browse with no BRAM growth; index load under a target the owner sets (suggest 3 s or on-demand with under 200 ms per screen); every list screen renders without decoding a JPEG.
- 0 late underruns at the stress levels while browsing and playing (Diagnostic Build gates); library feature turns itself off cleanly on a missing or corrupt index.
- Sync tool never modifies sources, verifies every written file, and is idempotent; host tests cover the index round trip.
- Release notes and README explain the library, the sync tool, naming limits and how to send diagnostics.

## 7. Read before starting
`docs/SESSION_HANDOFF_2026-09-21_RELEASE_0.3.md` (rules and procedures), `docs/ARCHITECTURE_ROADMAP.md` (phase E, F), `docs/PLAYLIST_SDRAM_MOVE_SPEC.md` (the pattern for moving a data structure off-chip), `docs/PSRAM_IMPLEMENTATION_PLAN.md` sections 7-8, `docs/MMIO_ALLOCATION.md`, `docs/SETTINGS_ARCHITECTURE.md` and `docs/SETTINGS_RUNTIME_BUDGET.md`, `fw/playlist.inc`, `fw/art.inc`, `fw/settingsui.inc`, `tools/sync_media.py`, `tools/library_check.py`, `docs/issues/001-unicode-playlist-paths.md`, audit entries A-103..A-112 (playlist in SDRAM), B-022..B-027.
