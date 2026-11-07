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

