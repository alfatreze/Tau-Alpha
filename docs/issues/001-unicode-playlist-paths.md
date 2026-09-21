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
