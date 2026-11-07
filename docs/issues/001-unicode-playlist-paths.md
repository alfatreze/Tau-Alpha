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

## Result 2026-11-07 (B-991): the matrix on a Pocket (TAU DEV 214, owner-run, Check then Y, Report decoded with `tools/tpg.py` + `tools/decode_tau_suite.py`)
Firmware `0.6.1-dev.214+6c2b1b94`, `NM ERR 0`, `NM BAD 10`, `NM DRY 0`, `NM RES A 55555551`, `NM RES B 55551155`. Per variant (1 = opened the right file, 5 = the open failed with a target code other than 4):
* **Open, right file (hardware-observed):** 0 ASCII control; 10 punctuation and spaces (`a b & c (1) [x] 'q' #5.mp3`); 11 an ASCII path of 220 bytes.
* **Open FAILED (nibble 5), hardware-observed, every non-ASCII path tried:** 1 accent NFC; 2 accent NFD; 3 several accents; 4 Cyrillic; 5 CJK; 6 emoji; 7 accented folder (NFC); 8 accented folder and file (NFC); 9 accented folder (NFD); 12 a 213-byte CJK path. **NFD (what this card stores) fails exactly like NFC, and Cyrillic, CJK and emoji fail like the accents: the normalisation form is not the cause.**
* **The negative controls 13, 14, 15 (files that do not exist) also give nibble 5**, the same code as every non-ASCII path: on this call path a file that is not there returns a target code other than 4 (the firmware's "4 = not found" reading in `slot_errcode` did not hold; the diagnostic did not record the code itself).
* **Reading:** with the current mechanism (the 0192 parameter struct from the library template, our path bytes copied in, the byte length patched) the Pocket opens ASCII paths of up to 220 bytes and **no path with a byte at or above 0x80**, whichever the encoding of the letters; it answers them as it answers a missing file. Player 2 is byte-transparent on its side (B-986), so the limit is in what APF does with the struct as we fill it: its encoding (ASCII or an OEM code page, not UTF-8) is the candidate; (the second candidate written here first, the meaning of a length word, was WRONG: the 0192 struct has no length word, see the correction below). **The explicit ASCII-only baseline (the host-side folding, `tools/sync_media.py`) is therefore what the evidence supports today.**
* **Not established:** which error code the Pocket returns (4 is not it); whether the length word counts characters; whether a single-byte OEM / Latin-1 encoding of the path opens a file named with that letter; whether APF can open such a file by any path.
* **Next round (proposed, needs a build):** record the target error code per variant; retry each failed variant with the length word as characters / UTF-16 units / bytes + 1; and probe single-byte encodings (Latin-1 0xE9, CP437/CP850 0x82) of an accented name.

## Correction and research 2026-11-07 (B-992): what the Analogue docs say, and what that changed in this work
I designed steps 1 to 3 from this repository's own notes without reading the Analogue documentation (the `analogue-pocket-dev` skill's snapshot of https://www.analogue.co/developer/docs). Reading it afterwards (`references/docs-snapshot/host-target-commands.txt`, `references/host-target-commands.md`, the project's own knowledge-base entries KB-041 and KB-043):
* **The 0192 struct has no length word.** `open_dataslot_file_t` is a **256-byte NUL-terminated path at offset 0**, flags at 0x100 and a size at 0x104; `get_dataslot_file_t` (0190) is the same 256-byte path. The "length word before the path" that `fw2/stream/slot.c` patches is a heuristic inherited from the old player (`fw/playlist.inc`), checked "rather than assumed" and never true for the documented struct. **So the round-2 retries with other length-word values that I built first would have changed nothing; they are removed (never shipped).** The B-986 capacity of 222 bytes belonged to my assumed template shape: the documented limit is **255 bytes** (path field 256 B), and variant 13 (252 bytes) was never over it: it is now 262 bytes.
* **The result codes of 0192: 0 opened, 1 created and opened, 2 slot undefined, 3 FILE NOT FOUND, 4 MALFORMED PATH, 5 general error.** The firmware's old comment ("4 = file not on the card") and my nibble naming were wrong. Round 1's nibble 5 means "a code other than 4", which fits **3, file not found, for the missing files and for every non-ASCII path alike: APF did not call them malformed, it did not find them.** (The exact code was not recorded; round 2 records it: `NM COD`.)
* **The documentation says nothing about the path's character encoding or normalisation.** Only hardware can tell, which is what the matrix does. KB-041 (this project's own earlier note, community-reported, low) already had the symptom; it is now backed by B-991.
* **What stands:** the Pocket result of round 1 (ASCII opens, no non-ASCII path does), Player 2 copying the path bytes unchanged (B-986), the ASCII-only baseline as the supported contract today.
* **Round 2, as now built:** per-variant error codes (`NM COD A/B/C`), six single-byte-encoding probes (variants 16 to 21: `ß` U+00DF and `ø` U+00F8, which have no decomposition so NFC = NFD on disk, named in UTF-8, Latin-1 and code page 437/850 bytes), the over-limit control made really over (262 bytes). If APF rejects all of them with code 3 the encoding question is closed for good (ASCII only); if a single-byte form opens, the path encoding APF expects is known.

## Update 2026-11-07 (B-994): the matrix runs from the Report, no Check run needed
The Diagnostic Build runs the name matrix **once per boot at the first Select+Start** (the Report the owner photographs anyway; `name_matrix_auto()` called from the app's Select+Start handler; a run refused for audio pressure does not use the turn), so the Report carries `NM RES A/B/C`, `NM COD A/B/C`, `NM RUNS`, `NM BAD`, `NM ERR`, `NM DRY` without opening the Check page. The Check page (A: every test, ending with NAME MATRIX; Y: only the matrix) still runs it again; **whether Y reaches the Check page on a Pocket was never verified** (round 1 ran the full A-run). The manifest must be the round-2 one (`tools/name_matrix.py write`), else `NM ERR 5`.

## Result 2026-11-07 (B-996): round 2 on a Pocket (TAU DEV 215, owner-run, Check then Select+Start, Report `20261107_120614.png` decoded): the Pocket decodes the path as code page 437
`NM ERR 0`, `NM BAD 10`, `NM DRY 0`; `NM RES A/B/C 55555551 55651155 00555155`, `NM COD A/B/C 33333330 33A30033 00333033`. **Error codes (hardware-observed):** every failing open returned **3, file not found** (never 4, malformed path): APF accepts a non-ASCII path as well-formed and does not find the file; the 262-byte control was refused by the firmware (nibble 6, code 10); the 250-byte ASCII path opened. **The six probes (variants 16 to 21), letters with no decomposition so the stored name is the same on any OS:**
* `Straße.mp3` named in **UTF-8** (c3 9f): code 3. In **Latin-1** (0xDF): code 3. In **the single byte 0xE1**: **OPENED, the right file.** (0xE1 is ß in code page 437 and 850, á in Latin-1.)
* `Søren.mp3` in UTF-8 (c3 b8): code 3. In Latin-1 (0xF8): code 3. In **0x9B (ø in code page 850, ¢ in 437): code 3.**
* **Reading (inference from one positive and five negatives, but a coherent one): the Pocket decodes the 0192 path as code page 437** (the FatFs OEM code page of its file system layer), not as UTF-8, Latin-1 or code page 850: 0xE1 is ß in 437; 0xDF (▀), 0xF8 (°), 0x9B (¢) and the UTF-8 byte pairs (├▀, ├©) are not the letters. **So only characters that exist in code page 437 can be named, each as its single 437 byte, and only against a stored name that holds the precomposed code point; Cyrillic, CJK and emoji cannot be named at all.** This also explains round 1: every UTF-8 path failed because the bytes were read as 437 characters.
* **What is still open:** a decomposable letter (é = 0x82, ü, ñ, ç...) in code page 437 against a name macOS stored: macOS listed those names decomposed (e + U+0301), and code page 437 has no combining accent, so on a card macOS wrote they may be unreachable by any path; whether macOS really STORES them decomposed or only lists them so is not known. Round 3 tests exactly that (`é` 0x82 against the files of variants 1 and 2, and the other 437 letters), needs more than the 24 variants the firmware holds (it will be raised to 32) and the Select+Start auto-run, i.e. a `TAU DEV 216`.
* **The contract this points to, to be confirmed by round 3:** (1) ASCII names are always safe; (2) a name that uses only characters of code page 437 can be opened if the library carries its path encoded in code page 437 (not UTF-8) AND the card stores the precomposed form; (3) macOS-written cards decompose the common accents, which round 3 will show; (4) everything outside code page 437 needs ASCII folding as today. The index and the sync tool would need a code page 437 path encoder and a check that the stored name is precomposed.

## Update 2026-11-07 (B-997): round 3 built (host-tested, not on a Pocket): the code page 437 probes
`tools/name_matrix.py` now plans **32 variants** (the firmware holds 32: `NM_MAX` 32, four result words, `NM RES D` and `NM COD D` added): variants 0 to 21 as before and ten new probes, all 'p' (the result is the information, none counts in `NM BAD`):
* **22, 23: é as the single code page 437 byte 0x82** against the files of variant 1 (written NFC) and variant 2 (written NFD): do they open on a card macOS wrote? 24: ö (0x94), Ü (0x9A), ï (0x8B), ñ (0xA4) and ß (0xE1) together against the file of variant 3. 25: a FOLDER named with 0x82 (variant 9's folder).
* **26 to 31, three new files with characters that have no decomposition and that code page 437 holds, each a file (UTF-8 path, expected not found) and a 437 path:** `Sæm.mp3` (æ, 0x91), `Cost¢.mp3` (the cent sign, **0x9B: it opens under 437 and would not under 850**, whose 0x9B is ø), `Pi π.mp3` (Greek pi, 0xE3: 437 reaches more than Latin letters).
**How to read it:** 18, 27, 29, 31 are the positive controls (expected 1 if the Pocket reads code page 437; 29 vs round 2's 21 separates 437 from 850); 22 to 25 answer the open question: if they open, accented names are reachable on a macOS-written card (macOS stores them precomposed); if they give code 3, macOS stored them decomposed and no path can reach them. The Report carries all of it at the first Select+Start of a `TAU DEV 216` (the auto-run), with the round-3 matrix on the card.

## Result 2026-11-07 (B-1000): round 3 on a Pocket (TAU DEV 216, owner-run: Select+Start once, Report `20261107_141908.png` decoded): the Pocket opens code page 437 paths, accents included
`NM RUNS 1` (the auto-run worked), `NM ERR 0`, `NM DRY 0`, `DRY 0`, `UNDER RING 0` (**no audio dropout**; the owner pressed Select+Start 6 s after boot), `NM BAD 10` (the same ten UTF-8 variants as before), `NM RES A..D 55555551 55651155 11555155 15151511`, `NM COD A..D 33333330 33A30033 00333033 03030300`. **Hardware-observed, every code page 437 probe opened THE RIGHT FILE (code 0):**
* **22, 23: é named with the single byte 0x82 opened both the file written precomposed (variant 1) and the one written decomposed (variant 2)**: macOS stores the precomposed form on this ExFAT card (the decomposed names the Mac lists are only its view), so accents ARE reachable once the path is in code page 437. **24: ö 0x94, Ü 0x9A, ï 0x8B, ñ 0xA4, ß 0xE1 in one name opened. 25: a FOLDER named with 0x82 opened.**
* **27: æ named 0x91 opened. 29: the cent sign named 0x9B opened (and round 2's ø named 0x9B did not): code page 437, not 850. 31: Greek π named 0xE3 opened: code page 437 reaches more than Latin letters.** Their UTF-8 forms (26, 28, 30) returned code 3 as always.
* **Conclusion:** the Pocket decodes the 0192 path as **code page 437**. KB-236 (hardware-validated, one card: ExFAT written by macOS) records it.
**The contract this gives (to implement):** a name is reachable if every character is in code page 437 (the accented Latin letters of 437, Greek letters, symbols, box drawing): the library index must carry its path **encoded in code page 437** (one byte per character, not UTF-8), against the precomposed stored name. A name with a character outside 437 (Cyrillic, CJK, emoji, Latin letters such as Á, Ø, Č) is not reachable by any path tried: it keeps today's ASCII folding (per name, not per library). Nothing changes in the firmware: `slot_open_path` copies bytes unchanged (B-986); the work is in the index builder (`tools/tau_library.py`), the sync (`tools/sync_media.py`: keep NFC names that 437 can encode, fold the rest) and Omega's equivalents. **Not tested:** FAT32 cards, cards written by Windows or Linux, the long-name behaviour beyond 255 bytes with 437 text, case-insensitive matching of 437 letters (é against É), a real library played end to end.


## Update 2026-11-07 (B-1001): the code page 437 contract implemented in the index builder and the sync tool (host-tested; not yet played on a Pocket)
`tools/tau_library.py` writes folder and file names into the string pool as code page 437 bytes (NFC first), counts the path length in those bytes, leaves a file with a character outside code page 437 out of the index with a warning, and reads playlist lines in NFC so a macOS (decomposed) listing matches. `tools/sync_media.py` keeps every character of a name that exists in code page 437 (precomposed) and folds only the others (accent to the plain letter, O-slash to O, else removed); `--ascii-only` is the old behaviour, `--keep-names` unchanged. Firmware needed no change (`lib_path` copies pool bytes verbatim). Tests: `sim/test_cp437_names.py`. **Still to run on a Pocket:** a real library with accented names played end to end; FAT32 and Windows/Linux-written cards; case matching; paths over 255 cp437 bytes.

Addendum (B-1001, same day): the sync tool no longer stops on a name collision (` (2)`, ` (3)` in sorted order, case-insensitive per folder) and transliterates what code page 437 lacks (anyascii; pykakasi for Japanese; emoji dropped; `track` only when nothing is left). Rules and vectors: `docs/features/CARD_NAME_RULES.md` (Tau Omega must follow them).

Update 2026-11-07 (B-1002): the full design for keeping the originals is `docs/features/NAME_SIDECAR_SPEC.md` (card-safe name opens the file, the sidecar holds the lossless UTF-8 name and tags, the firmware shows the original when its font can draw every character). Host-tested with a 62-case corpus; firmware reader and a CJK font are not built.
