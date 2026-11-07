# Name sidecar spec (original names and tags beside the card-safe ones)

Status: **specified and host-tested (reference implementation `tools/name_sidecar.py`); the Player 2 firmware reader is built and host-tested (B-1003, `fw2/cores/names_core.h`, wired in `fw2/stream/library.c`); not run on a Pocket; no tool writes the files yet.** Written 2026-11-07 (audit B-1002). Tau-native design; it does not derive from any other player.

## 1. Why
The Pocket opens a file only by a path it decodes as code page 437 (BUG-001, `docs/issues/001-unicode-playlist-paths.md`). So the **card carries safe names** (`docs/features/CARD_NAME_RULES.md`: kept CP437 characters, transliterated rest, numbered collisions) and the **index carries Latin-1 display text** (`docs/features/FONT_SPEC.md` 3.6). Both are lossy for Cyrillic, Greek, CJK, Hangul, Arabic, emoji and the Latin letters CP437 lacks. The sidecar keeps the **original, lossless UTF-8** next to them so that
1. nothing is lost (Omega, tools, a future font can show the real text),
2. the card name stays a pure, safe key,
3. the firmware can show the original wherever its font can draw it and fall back to the index text where it cannot.

## 2. Two files, one purpose each
Both live in `Assets/<platform>/common/` beside `tau-library.tdb`, are written by `sync_media.py` and Tau Omega, and are optional: **no sidecar = the library behaves exactly as today.**

| File | Reader | Content |
|---|---|---|
| `tau-names.json` | host tools, Omega, humans | the **manifest**: card path <-> original path, original tags, one record per track |
| `tau-names.bin` | firmware | the **display table**: original UTF-8 strings by track / album / artist id |

The manifest is the source of truth (it can rebuild the binary, and the card names, from an original library). The binary is derived from it.

## 3. Text rules (both files)
- UTF-8, **NFC**, no BOM, no NUL, no C0/C1 controls (they become a space), no lone surrogates (invalid input becomes U+FFFD).
- Bidirectional text is stored in logical order, unmodified (no direction marks added, none removed).
- Emoji and variation selectors are **kept** in the sidecar (the card name drops them, the original does not).
- Length caps (characters, not bytes): title, artist, album 127; file name 255. Longer text is cut at a character boundary, never inside an escape or a combining sequence start (the cut also drops a trailing combining mark or joiner).
- A field with no original text is **absent** (binary offset 0; JSON key omitted), never an empty string.

## 4. Manifest `tau-names.json`
```json
{
  "format": "tau-names", "version": 1,
  "index_build_id": 305419896,
  "tracks": [
    {"id": 0, "card": "Cafe/01 Cafe.mp3", "orig": "Café/01 Café.mp3",
     "title": "Café", "artist": "…", "album": "…"}
  ]
}
```
- `card` is the path relative to `common/` exactly as written to the card (the bytes the index holds, as text); `orig` is the source path relative to the sync source root, NFC. `id` is the track id in the index the manifest was built against.
- `index_build_id` = the `build_id` of the `tau-library.tdb` it was built with (index header offset 16, the CRC32 of the index body: the firmware has it after loading the index, no extra pass). **A reader that finds a different build id treats the sidecar as absent** (the library was rebuilt and the ids moved); tools may re-bind by `card` path instead.
- `title`, `artist`, `album` are the original tag text (UTF-8 after the host tool decoded UTF-16 / Latin-1 / Windows-1252 / **Shift-JIS and the other legacy code pages**, which the firmware never has to understand).
- Optional per track: `"orig_file"` (the original file name alone) when it differs from the file name of `orig` (not used today; reserved).
- Unknown keys are ignored; a reader rejects `version` above its own.

## 5. Binary `tau-names.bin`
Little endian, sections 16-byte aligned, offsets only (like `docs/MEDIA_LIBRARY_0.4_SPEC.md` 2.1).

| Off | Size | Field |
|---|---|---|
| 0 | 4 | magic `TNMS` |
| 4 | 2 | version = 1 |
| 6 | 2 | flags (bit0 = pool is UTF-8 NFC; others 0, ignored) |
| 8 | 4 | `index_build_id`: the `build_id` of the `tau-library.tdb` it matches |
| 12 | 4 | n_tracks |
| 16 | 4 | n_albums |
| 20 | 4 | n_artists |
| 24 | 4 | pool offset |
| 28 | 4 | pool size |
| 32 | 4 | header CRC32 over bytes 0..31 |
| 36 | 12 | zero |
| 48 | n_tracks x 16 | track records `{file, title, artist, album}`, u32 pool offsets (0 = absent) |
| next (16-aligned) | n_albums x 8 | album records `{title, artist}` |
| next (16-aligned) | n_artists x 4 | artist records `{name}` |
| next (16-aligned) | pool | NUL-terminated UTF-8; byte 0 is a NUL (offset 0 means absent); de-duplicated |

- **Ids are the index's ids** (`track`, `album`, `artist` order of `tau-library.tdb`); `file` is the original *file name* shown in the browser when a track has no usable title.
- Reader checks: magic, `version`, header CRC, `index_build_id` equal to the loaded index's `build_id`, counts equal to the index's counts, the file exactly `pool offset + pool size` bytes long, the pool starting and ending with a NUL, every offset 0 or the start of a string (the byte before it is a NUL). UTF-8 is validated when a field is drawn (a malformed string is never drawn). **Any failure = the sidecar is absent.** It never changes how a track is opened (paths always come from the index).
- Budget: 7,180 tracks x 16 B = 115 KB + strings (CJK titles are about 3 bytes a character; 7,180 x 60 B is under 450 KB). Hard cap 1 MiB; over it the tool drops the `file` field first, then writes none, with a warning.

## 6. Display rule (the part the firmware implements)
For each field (track title, artist, album, file name) the firmware draws text from a **source list**: the sidecar string when present and fully drawable, else the index string.
1. Let `cov(c)` = whether the active font can draw code point `c`. Today the font is Latin-1 (`FONT_SPEC.md`): `cov` is true for U+0020..U+007E and U+00A1..U+00FF except U+00AD. A larger font only changes `cov` (a coverage table in the font file); nothing else in this spec moves.
2. If the sidecar string exists and **every** character is drawable, draw it. One missing character sends the whole string to the fallback (no mixed `?` rows).
3. Else draw the index string (Latin-1 text built by `tau_library.latin1_text`, folded the same way the card name was).
4. Else (empty) draw the card file name stem.
Sorting and letter jumps keep using the index (the tool collates once); the sidecar never affects order.

## 7. Conversion contract (what the tools guarantee)
- `card` obeys `CARD_NAME_RULES.md` and is always openable (every character in code page 437, path <= 200 bytes).
- Every manifest record round-trips: `card` -> `orig` is unique and unambiguous; two originals never share a `card` (the ` (2)` numbering).
- Rebuilding the card and the sidecar from the same source and tool versions gives byte-identical files.
- A source that already is card-safe still gets a record when its tags contain more than the index can show (so "title only in Cyrillic" is kept), and gets none when nothing differs (small sidecars on plain libraries).
- Omega must write both files with the same rules and test against `sim/name_corpus.py` (the shared name corpus).

## 8. Test corpus
`sim/name_corpus.py` (62 cases) holds the shared cases (plain, accented precomposed and decomposed, CP437-only symbols, Latin letters CP437 lacks, Greek, Cyrillic, Chinese, Japanese kana and kanji-only, Korean, Arabic, Hebrew, Thai, Devanagari, emoji and ZWJ sequences, flags, fullwidth, combining, forbidden characters, reserved and edge names, case-only and normalization-only collisions, long names, music punctuation). `sim/test_name_sidecar.py` runs every case through name conversion, the library index, the sidecar (JSON and binary, round trip and corruption), and the display rule at three font coverages (ASCII, Latin-1, everything).

## 9. Not decided yet / next
- **Firmware reader: built (B-1003).** `names_load` reads `<root>/tau-names.bin` at boot, right after the index, through slot 7 opened by path (no new data slot, no data.json change; the same mechanism as the cover and the skip list), into PSRAM at `+0x700000` (1 MiB, below the cold image). `names_display` answers a field only when every character is drawable, converted to Latin-1; `library_meta` and `library_br_text` use it and keep the index text otherwise. Cost: ROM unchanged (the code is in the cold image: +2,372 B code, +16 B data), **+40 B of hot RAM (.bss)**; Diagnostic Build headroom to the 1,024 B floor is 528 B afterwards. **Hardware assumptions still to prove on a Pocket:** a read past the end of a path-opened slot fails (the size check relies on it; if not, the sidecar is refused and the index text shows), the cost of the one failed open on a card with no sidecar (not measured), and the load time of a large file.
- **Writers:** `sync_media.py` and the index builder do not write `tau-names.json` / `tau-names.bin` yet (`tools/name_sidecar.py` has the builders and the tests); Tau Omega neither.
- **A font with CJK coverage:** separate study with a measured SDRAM budget (rules 5 and 6 of `docs/PROJECT_RULES.md`). Until then the reader only adds text the Latin-1 font already draws (tag text longer than the index's 63 characters, characters the index folded, e.g. a typographic quote is not drawable and falls back).
- Whether album art sidecars (`tau-art/`) move into the same manifest (not needed now).
- Display of right-to-left scripts (Arabic, Hebrew): logical order is stored; shaping and direction are a font and UI decision.
