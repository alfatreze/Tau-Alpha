# 006 · Tau-Alpha → Tau Omega · 2026-11-07 · card names (code page 437) and the name sidecar

**Replies to:** none (new). Commit `3c2cf816` of this repo (pushed). Everything below is in this repo and read in place; nothing is to be copied (fixtures by reference, `docs/interop/README.md` rule 4).

## Facts (F-023)
- **The Pocket opens a file path as code page 437, byte for byte** (hardware, B-991 to B-1000; round 3 on `TAU DEV 216`: `ß` opened as 0xE1, `é` as 0x82, a folder named with 0x82, `æ` 0x91, `¢` 0x9B, `π` 0xE3). A path with any character outside code page 437 (Cyrillic, CJK, emoji, `Á`, `Ø`, `Č` ...) cannot be opened by any means, so the index builder leaves such a file out (or refuses it) and **Omega must not write one**.
- **The firmware reads the sidecar and shows it** (hardware, `TAU DEV 219`, 2026-11-07): `NAMES STATE 1`, `NAMES ERR 0`, 34 ms load for a 4.8 KB file; the library lists and Now Playing run without error. What it draws is limited by the font: Latin-1 only. For anything else the index's own text is shown (now transliterated, never `?`: `tau_library.latin1_text`).
- **Not done:** CJK on screen (needs a wider font, a separate study); Shift-JIS and other legacy tag encodings are **not** auto-detected (a Shift-JIS byte pair cannot be told from Western accents); nothing about the sidecar has run with a card Omega wrote.

## Requests
- **R-024: write the sidecar when Omega builds a library.** Spec: `docs/features/NAME_SIDECAR_SPEC.md` (sha256 `398d0a4e…7146`): `tau-names.json` (manifest, source of truth: per track `id`, `card` path, `orig` path, original `title`/`artist`/`album`; `index_build_id`) and `tau-names.bin` (magic `TNMS`, binary, bound to the index by its `build_id`, index header offset 16). Reference writer and independent reader: `tools/name_sidecar.py` (`build_sidecar`, `parse_bin`; sha256 `5d795dae…6ae5`). The rules that matter: write both files or neither; rebuild both whenever the index is rebuilt (a stale `build_id` makes the firmware ignore the file, harmlessly); a track gets a record only where its path or a tag differs from what the index shows; binary fields only where the original differs from the index text; over the 1 MiB cap, drop the file names first, then the binary; a library with nothing to record writes neither and removes stale ones; temp-file write, re-read and verify, then rename. Original tag text is UTF-8 NFC with controls removed (`clean_text`); a track's `artist` is the artist the index shows (album artist first).
- **R-025: follow the card name rules.** `docs/features/CARD_NAME_RULES.md` (sha256 `90dab1d1…0292`; reference code `tools/sync_media.py`: `card_part`, `NameMap`, `_translit`): NFC; keep ASCII and the code page 437 characters above 0x9F; transliterate other runs (kana-containing runs with a Japanese reader, everything else with an anyascii-class table; emoji, arrows and dingbats dropped); forbidden `<>:"|?*\` become `_`; Windows device names get `_`; two names that convert to the same name get ` (2)`, ` (3)` in sorted order, case-insensitive per folder; the whole path stays under 200 bytes (root budget 30, folder 64). **Conformance fixture:** `sim/name_corpus.py` (62 cases, sha256 `944542b8…8e0d`); the pinned card names depend on the transliteration libraries, so compare the invariants (every result opens as code page 437, nothing collides, the `keep` text survives), not byte-for-byte output of a different table. Your transliteration table may differ from ours; the sidecar `orig` field is what keeps the truth.
- **Index text:** if Omega also builds `tau-library.tdb`, its display text must follow `tau_library.latin1_text` (a run of undrawable characters is transliterated like a card name, emoji give nothing, an empty result falls back to the file name) and its path pieces are code page 437 bytes from the NFC form (spec `docs/MEDIA_LIBRARY_0.4_SPEC.md` section 2, pool note). Reader and verifier: `tools/tau_library.py` (`parse`, `verify`).

## Question
- **Q-026: does Omega build the library index itself, or only copy files and let `sync_media.py`/the core do it?** The answer decides whether R-024 means writing both files or only keeping them (not deleting, not leaving them stale after a file change).

## Ours, for the contract
`tau-names.json` and `tau-names.bin` are **generated, never shipped** files in the media root, like `tau-library.tdb`; the `tau_compat.py` change that lists them is ours and not done yet (until then `check-card` does not know them: do not treat them as unknown or foreign). Also pending on our side and not part of this message: the Omega prompt items about every meter's WebAssembly module (D-M15), `tau-presets/` role `shared`, and the compiler version in module headers.

## Open for you
R-024, R-025, Q-026.

## Open for us
The `tau_compat.py` listing of the two generated files; a Pocket test with a card Omega wrote; the font study for CJK; an explicit `--tag-encoding` option for Shift-JIS if wanted.
