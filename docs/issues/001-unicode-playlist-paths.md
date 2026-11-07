# BUG-001: Playlist paths with Unicode names can fail to open

**Status:** Confirmed; deferred for Tau compatibility work

## Observed behaviour

Tau's HarpMudd baseline reported `No playable tracks in playlist` for a valid
playlist.  Pressing **Left** later started playback, which obscured the cause.

The playlist and its first track used `Nausicaa\u0308` (an `a` followed by a
combining diaeresis).  The album directory was later renamed to an ASCII-only,
shorter name, but the first track and its playlist entry still retained the
combining diaeresis.

## Impact

The player can appear unable to play a valid playlist when a path component's
Unicode byte sequence differs from the spelling stored on the FAT volume.  Two
visually identical names may have different UTF-8 encodings (notably NFC versus
NFD), so this is easy to reproduce unintentionally on macOS.

## Current workaround

Use short ASCII-only folder and media names, and make every `.m3u` entry match
those names exactly.  Prefer a single album-local playlist with bare filenames.

## Investigation to do

1. Create an SD-card test matrix with identical ASCII control files plus NFC,
   NFD, Latin-1-style, and non-Latin UTF-8 path variants.  Test each one both
   as a directly loaded file and as a playlist entry; those routes may have
   different APF and firmware handling.
2. Record the raw UTF-8 bytes of both directory entries and `.m3u` lines on
   macOS and on the Pocket-readable FAT volume; test cold boot and track skip
   independently.
3. Trace the complete path flow: playlist line -> firmware buffer -> APF 0192
   request -> Pocket filesystem result.  Identify whether bytes are preserved,
   normalized, truncated, or interpreted using another code page.
4. Decide the compatibility contract from evidence:
   - **Byte-transparent UTF-8** if the APF stack accepts it reliably.
   - **NFC-normalized UTF-8** if firmware can normalize safely within memory
     limits and APF/FAT behaviour supports it.
   - **Explicit ASCII-only baseline** if neither is reliable, accompanied by
     host-side Sync validation and filename normalization.

## Acceptance criteria

- The supported filename encoding and normalization form are documented.
- A playlist either opens every supported path on cold boot or reports the
  exact failing entry, rather than a generic empty-playlist message.
- The selected path tests run from a real FAT-formatted SD card and are retained
  as a regression checklist.

## Update 2026-09-21 (B-026)
Reproduced on the Pocket with the numbered test builds: tracks whose names contain an accented character (decomposed, as macOS writes them) were skipped as unreadable (Soundtrack track 1 and Bird Man track 2), while ASCII-named tracks in the same playlists played. Firmware side found in `fw/playlist.inc`: the path template for 0192 is the longest printable-ASCII run in the descriptor (`pl_template`), so a non-ASCII byte splits the recorded path. The workaround is now in the host tool: `tools/sync_media.py` writes ASCII-only names and rewrites playlist lines to match. The firmware test matrix above is still to do.

## Update 2026-11-07 (B-986): step 1, the host reproduction (Player 2, host-only)
`sim/test_p2_name_paths.py` compiles the real `fw2/stream/slot.c` against a model of the APF target channel (`sim/p2_slot_open_harness.c`) and runs the host tools on name variants. **What it shows:**
* **Player 2 is byte-transparent on its side:** an ASCII, NFC, NFD (macOS), CJK or Latin-1-byte path reaches the 0192 parameter struct with exactly its bytes and the BYTE length in the length word. The old player's failure mechanism (a non-ASCII byte in the TEMPLATE splits the longest printable-ASCII run, the length word is not patched, APF reads the old length) is reproduced, and Player 2 avoids it only because its template is always the library slot's own ASCII path. So the cause of B-026 is probably not in Player 2's path building; **whether the Pocket's APF accepts non-ASCII bytes in 0192 is still unknown** (that is the card matrix, steps 2 and 3).
* **Capacity:** the struct holds a path of at most 222 bytes (ASCII template shape assumed, no real capture is in the repository): about 73 characters of 3-byte UTF-8 (CJK) or 110 of 2-byte, against 222 of ASCII. A longer path is refused with SLOT_E_TOOLONG, not corrupted.
* **Host tools:** `tools/sync_media.py` folds names to ASCII (Café and its NFD form both become Cafe); two different names can fold to one (Björk / Bjork), and the sync then STOPS ("name collision after ASCII conversion"); a name with no ASCII letter becomes `track.<ext>`, so a CJK or Cyrillic library keeps colliding. `tools/tau_library.py ascii_name` agrees on accented names but returns `.mp3` for a fully non-ASCII name (sync gives `track.mp3`): harmless while the index is built from converted card names.
**Next:** step 2 (the card matrix tool: ASCII control plus one file per variant, directly and as playlist entries) and step 3 (a diagnostic build that reports open OK / fail per name), which need a card write and a Pocket run from the owner.

## Update 2026-11-07 (B-987): step 2, the card matrix tool (host-tested; nothing written to a card yet)
`tools/name_matrix.py` (`plan`, `write --card ... [--yes]`, `verify`, `remove`) writes `<media root>/name-test/`: 16 variants (ASCII control; NFC and NFD accents; several accents; Cyrillic; CJK; an emoji; accented NFC and NFD folders; punctuation; paths of 220 and 213 bytes near the firmware's 222-byte limit; three negative controls: over the limit, no such file, not valid UTF-8), a tiny valid silent MP3 per file whose ID3 title is `NM<id>` (the diagnostic proves it opened THAT file), `matrix.tnm` (the list the firmware reads: `TNM1 <count>`, then `<id>|<o or f>|<absolute path, raw bytes>`) and `MATRIX.txt` (every path with its UTF-8 bytes, for the record). After a write the tool reads every name back under its exact bytes and lists what the OS actually stored: **a name that macOS or FAT changed or refused is itself a finding** (run `verify` on the Mac after the card has been in the Pocket too). The library index builder would list these files as tracks (folded to ASCII): `remove` the folder before rebuilding the index. **Next: step 3**, a diagnostic build that reads `matrix.tnm`, tries `slot_open_path` for each line and reports open OK / failed (with the target's result code) and whether the opened file carries the expected id, in the Report; then the owner runs it on a Pocket.

## Update 2026-11-07 (B-988): step 3, the diagnostic that opens each name on a Pocket (built, host-tested, not yet on a Pocket)
`fw2/diagnostics/name_matrix.c` (Diagnostic Build only, registered in `fw2/diag_features.json`): **Check page, Y** (no track needed; A-run also ends with it as the NAME MATRIX row) reads `<media root>/name-test/matrix.tnm` (2,048 bytes, written by `tools/name_matrix.py`), tries `slot_open_path()` for each of the 16 paths through the cover's slot, reads the first 32 bytes of what it opened and checks the ID3 title `NM<id>` proves it is THAT file. The result is on Info: **NM RES A** (variants 0..7) and **NM RES B** (8..15), one hex nibble per variant, variant 0 the lowest nibble: `1` opened the right file, `2` opened another file, `3` read failed, `4` no such file (target code 4), `5` another target code, `6` refused by the firmware (over its 222-byte limit), `7` timeout; **NM BAD** (unmet expectations: an 'o' variant that did not give 1, a negative control that did not fail), **NM ERR** (0 ran; 1 no library root, 2 matrix.tnm did not open, 3 did not read, 4 audio under pressure, 5 bad header, 6 bad line) and **NM DRY** (audio dropouts during the run). An ideal card reads `NM RES A 11111111`, `NM RES B 11111644`, `NM BAD 0`. The Check page's rows are now 20 px high (13 rows). Cost: free RAM of the Diagnostic Build 1,680 -> 1,632 B (floor 1,024); the normal build is unchanged (4,544 B).
**To run it (needs the owner):** (1) a DEV package of this build on the card, (2) `python3 tools/name_matrix.py write --card /Volumes/Pock --yes`, (3) on the Pocket: the TAU DEV core, Settings > Diagnostics > Check, Y, then the Info page (photograph NM RES A / B, NM BAD, NM ERR, NM DRY), (4) back on the Mac `python3 tools/name_matrix.py verify --card /Volumes/Pock` (what the FAT card kept of the names), (5) `python3 tools/name_matrix.py remove --card /Volumes/Pock --yes`.

## Update 2026-11-07 (B-990): the matrix is on the card (an ExFAT volume); what macOS did to the names
`tools/name_matrix.py write --card /Volumes/Pock --yes` wrote the 15 files and read them all back (15 of 15 found, 0 differing). **Findings from the Mac side, before any Pocket run:** (1) the card is **ExFAT**, not FAT32; (2) macOS lists every accented name in its **decomposed (NFD)** form, including the variants that were written precomposed (NFC): variants 1, 3, 7 and 8 read back as NFD, so **the bytes stored on this card are NFD whatever was written**, and the precomposed paths of the matrix reach the Pocket as names the card does not hold in that form (the lookups on the Mac are normalisation-insensitive, so "found" on the Mac says nothing about the Pocket); (3) macOS adds AppleDouble `._<name>` files beside the files it writes: the tool sweeps them (5 remain as ghost directory entries for the accented names that cannot be unlinked until the volume is remounted; `tau_library`'s scan and `install_dev_core` already treat `._*` as junk). **What this predicts for the Pocket run (a prediction, not a result):** variants 2, 9 (NFD paths) can open and variants 1, 3, 7, 8 (NFC paths) may report nibble 4 if the Pocket's file system compares UTF-16 exactly. That would mean the contract for a macOS-written card is "the path the firmware uses must be the NFD bytes the card holds", which the library index would have to carry (today the tools fold to ASCII, so no accented path is ever built).

