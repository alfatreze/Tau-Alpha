# Card name rules (shared by `tools/sync_media.py` and Tau Omega)

Why: the Pocket opens a file by a path it decodes as **code page 437**, one byte per character (BUG-001, `docs/issues/001-unicode-playlist-paths.md`, hardware B-991..B-1000). The library index stores every folder and file name as those bytes (`docs/MEDIA_LIBRARY_0.4_SPEC.md`).

Rules, applied to every folder name and file name (each path piece on its own), in this order:
1. Normalize to NFC (macOS lists names decomposed; the card stores them precomposed).
2. Keep a character that is printable ASCII (0x20-0x7E) or exists in code page 437 above 0x9F (e-acute, o-umlaut, n-tilde, ss, ae, pi, cent ...).
3. A run of other characters is transliterated to ASCII: kana runs with kanji go through pykakasi (Hepburn, words split by a space), every other run through anyascii (Cyrillic -> Latin, Hanzi -> pinyin ...). Emoji, arrows, dingbats and joiners are dropped. Without those libraries the accent is dropped to the plain letter and the rest removed.
4. `<>:"|?*\\` become `_`; leading/trailing spaces are trimmed.
5. A name left empty (or only an extension) becomes `track` (+ extension).
6. Two different originals in one folder that end up with the same name (compared without case: the card is ExFAT) get ` (2)`, ` (3)` before the extension, in sorted order of the originals, so the result is the same on every run.
7. Playlist lines are rewritten with the same map (relative to the playlist's folder).
`--ascii-only` skips step 2's non-ASCII keep (everything non-ASCII is folded); `--keep-names` skips all of it.

Vectors (with anyascii and pykakasi installed; `sim/test_cp437_names.py` checks them): `Café.mp3` -> `Café.mp3`; `Cafe\u0301.mp3` -> `Café.mp3`; `Søren.mp3` -> `Soren.mp3`; `Привет.mp3` -> `Privet.mp3`; `东京.flac` -> `DongJing.flac`; `東京タワー.mp3` -> `toukyou tawaa.mp3`; `😀.mp3` -> `track.mp3`; `Café Привет.mp3` -> `Café Privet.mp3`.

Tau Omega must produce the same names (it writes cards too): port the rules and the vectors, or call `sync_media.py`. Transliteration output depends on the library versions; the shared guarantee is rules 1, 2, 4-7 and the cp437 bytes.
