# Media and tools

How to prepare music, covers, themes and cores for the Pocket's SD card. The tools are Python scripts under `tools/` (Python 3;
Pillow and numpy for the image tools). The desktop companion app [Tau Omega](#the-tau-omega-companion-app) wraps the same jobs.

Contents: [The card layout](#the-card-layout) · [sync_media.py](#syncmediapy-copy-convert-verify) · [The library index](#the-library-index-tau-librarytdb) ·
[Cover art (TIM1)](#cover-art-tim1) · [The theme and meter file](#the-theme-and-meter-file-tau-assetsbin) ·
[install_dev_core.py](#installdevcorepy-installing-a-core) · [Other tools](#other-tools) · [Tau Omega](#the-tau-omega-companion-app)

## The card layout

| Path on the card | What it holds |
|---|---|
| `/Cores/alfatreze.TAU/` | The core (`core.json`, `data.json`, `interact.json`, bitstream) |
| `/Platforms/tau.json` and images | The core's entry in the Pocket menu |
| `/Assets/tau/common/` | `tau.rom` (firmware), `tau-cold.bin` (the cold image), your music, `playlist.m3u`, `playlists.m3u`, `tau-library.tdb`, optional `tau-assets.bin`, optional `<album>/tau-art/*.timg` |
| `/Settings/alfatreze.TAU/` | What the Pocket saves for the core (delete to reset) |
| `/Memories/Screenshots/` | Screenshots taken with Menu + Start |

The core's data slots (declared in `data.json`; source `tools/tau_data_slots.py`): **5** the library index (`tau-library.tdb`,
optional), **6** the cold image, **7** pre-scaled cover files (opened by name), **8** `tau-assets.bin` (optional).

## sync_media.py: copy, convert, verify

`tools/sync_media.py` copies music, playlists and images onto the card for one or more Tau cores (every core reads its own
`Assets/<platform>/common/`), makes them player-safe, and verifies every file by SHA-256.

```bash
python3 tools/sync_media.py "My Music" --core alfatreze.TAU
python3 tools/sync_media.py MUSIC_DIR --all-tau --library                       # every alfatreze.TAU* core, plus the library index
python3 tools/sync_media.py MUSIC_DIR --all-tau --library --art-variants        # also the fast cover files
python3 tools/sync_media.py MUSIC_DIR --from-core alfatreze.TAU --core alfatreze.TAU_DEV_109   # clone media into an OLD per-platform dev core
```

Cores of the release channels (TAU, TAU Diagnostics, TAU Preview, TAU Preview Diagnostics) and dev builds (TAU DEV NN) all read the
music in `Assets/tau/common/`: sync it once with `--core alfatreze.TAU`. Only old dev cores with their own platform need a copy.

Rules it follows (from the tool's own docstring):

- Folders keep their name under `common/`; loose files go to `common/`. Audio (`.mp3`, `.flac`) and playlists (`.m3u`, `.m3u8`)
  are copied unchanged; tags are never rewritten.
- The player reads cover art only from inside the track (MP3 APIC / FLAC PICTURE), never from a `cover.jpg` beside it, so
  `--embed-cover` writes a folder's cover into the *copies* of its tracks (the source files are never changed).
- A folder of tracks with no playlist gets a `playlist.m3u` generated (bare filenames, natural track order); `--no-playlist` turns
  that off.
- Names become ASCII-only on the card (the player cannot open non-ASCII paths, see the user guide), and playlists are rewritten
  to match; `--keep-names` turns this off. A collision after conversion stops the run.
- Standalone images are made player-safe (baseline JPEG, long side limited by `--max-image`, under the firmware size cap).
- Files that are not media are skipped; `._*` and `.DS_Store` are never copied. Exit status 1 if any verification fails.

Flags:

| Flag | Meaning |
|---|---|
| `sources` | Files or folders to copy |
| `--card PATH` | Card root (default `/Volumes/Pock`) |
| `--core ID` (repeatable), `--all-tau` | Destination core(s) |
| `--from-core ID` | Also use this core's media as a source (clone) |
| `--mirror` | Delete files in the destination that are not in the source |
| `--dry-run` | Show the plan, write nothing |
| `--max-image N`, `--quality N` | Longest cover side (default 2500) and JPEG quality for converted images (default 90) |
| `--embed-cover`, `--cover FILE` | Embed a cover into the copies of tracks (`--cover` implies it and uses that image for every track) |
| `--copy-images` | Also copy loose images (converted if needed) |
| `--no-playlist`, `--keep-names` | Turn off playlist generation / ASCII naming |
| `--cover-quality N`, `--cover-max N` | Re-encode / shrink embedded covers |
| `--dest-suffix TEXT` | Append text to copied top-level folder names |
| `--art-variants [LIST]`, `--art-size N` | Write pre-scaled cover files (below) |
| `--manifest FILE` | Write a JSON record of the run |
| `--library` | Build and verify `tau-library.tdb` in each destination |

## The library index (tau-library.tdb)

`--library` calls `tools/tau_library.py` to build the index from the *destination* layout (the names as they will be on the card):
Artists, Albums, Tracks, sorted tables, jump-to-letter positions and optional playlists, with CRCs. The player never sorts or
compares strings; the tool does. Format and limits: [MEDIA_LIBRARY_0.4_SPEC.md](../MEDIA_LIBRARY_0.4_SPEC.md). Direct use:

```bash
python3 tools/tau_library.py build [common] [--core ID] [--card PATH] [--out FILE] [--playlists]
python3 tools/tau_library.py verify INDEX [--root ROOT]
python3 tools/tau_library.py report INDEX
python3 tools/tau_library.py synth --out FILE [--tracks N ...]     # a synthetic library for size and speed tests
```

The index records the path root of the core it was built for, so **an index copied from another core is wrong**; rebuild it with
`--library` (the installer does this for you when carrying media across).

## Cover art (TIM1)

Decoding an embedded JPEG costs 2.6 to 15.8 s (it scales with the file's weight). A pre-scaled `.timg` cover shows in about 90 ms.

```bash
python3 tools/sync_media.py MUSIC_DIR --all-tau --library --art-variants     # pal256 at 128 px, the default
```

- Files are written to `<album>/tau-art/cover_128.pal256.timg` (palette-256, 128 px on the long side, scaled proportionally, never
  cropped or padded). Other variants for study: `pal64,pal16,bc1,jpg60,jpg75,jpg85,rgb565,auto`; sizes with `--art-size`.
- **Where the cover comes from**, in order: `--cover FILE`; a named cover image in the album folder (`cover.jpg`, `folder.jpg`,
  `front.jpg`, `cover-*`); art embedded in the first track that has any (MP3 APIC or FLAC PICTURE); any image in the folder or one
  level below (a front/cover name first, then the largest).
- The player uses a `.timg` for **library tracks** only, and falls back to the embedded cover if the file is missing or invalid.
  Info > TIM1 COVER shows the state. Hardware-confirmed for MP3 and FLAC albums (B-330).
- Needs Pillow and numpy. Design and the reader: [COVER_TIMG_READER.md](../COVER_TIMG_READER.md); the format study and results:
  [IMAGE_FORMATS.md](../IMAGE_FORMATS.md). The `TIM1` container is not yet frozen.

## The theme and meter file (tau-assets.bin)

An optional `tau-assets.bin` in `Assets/tau/common/` (data slot 8) can add themes and replace meter presets. Container `TAUA`
with a `THEM` section (up to four extra themes, each with Dark and Light colours for every role) and a `METR` section (per-meter
presets). Every CRC is checked before any byte is used; a bad file leaves the built-in themes and presets.

```bash
python3 tools/tau_assets.py pack themes/user_examples/sunset.json -o tau-assets.bin [--meters meters.json]
python3 tools/tau_assets.py dump tau-assets.bin
python3 tools/tau_assets.py install tau-assets.bin --core alfatreze.TAU
```

Format: [THEME_FILE_FORMAT.md](../THEME_FILE_FORMAT.md); design: [THEME_SPEC.md](../THEME_SPEC.md); meter presets:
[METER_MODULE_SPEC.md](../METER_MODULE_SPEC.md). The two built-in themes come from `themes/tau.json` and `themes/ocean.json`
through `tools/gen_themes.py`, which also checks contrast.

## install_dev_core.py: installing a core

`tools/install_dev_core.py` installs a packaged core onto the card following the documented procedure: verified backup, copy and
SHA-256 check, optional media carry-over with library rebuild, optional removal of superseded cores, deleting the Pocket's five
catalog caches (a new core does not appear without this), junk cleanup, eject. **Default is a dry run**; add `--yes` to write.

```bash
python3 tools/install_dev_core.py PACKAGE_DIR --carry-from alfatreze.TAU --remove alfatreze.OLD_CORE --yes
```

Options: `--card`, `--carry-from`, `--assets`, `--remove`, `--replace`, `--allow-release`, `--backup-dir`, `--no-eject`, `--yes`. The
release cores are never touched without `--allow-release`. Procedure: [CARD_INSTALL_PROCEDURE.md](../CARD_INSTALL_PROCEDURE.md).

## Other tools

| Tool | Use |
|---|---|
| `tools/library_check.py` | What the core would do with a track's cover and tags |
| `tools/decode_tau_suite.py` | Decode Check / Info / Meter Sweep reports from a QR screenshot, the persist file or the short code |
| `tools/tau_image.py` | The TIM1 encoder and decoder |
| `tools/gen_themes.py`, `tools/tau_assets.py` | Themes and the `tau-assets.bin` container |

## The Tau Omega companion app

**[Tau Omega](../../../Tau%20Omega/)** is a separate desktop app (macOS and Windows, Rust + Tauri) that does these jobs with a
graphical interface: building and syncing the media library, managing Tau and other openFPGA cores and their media, decoding
diagnostic QR reports, and browsing the card's screenshots. It is built as the product version of `tools/tau_library.py` and
`tools/sync_media.py`. It lives in its own repository (a sibling folder of this one); the two projects never share files, only
documented formats ([CROSS_PROJECT_INTERFACE.md](../CROSS_PROJECT_INTERFACE.md)).
