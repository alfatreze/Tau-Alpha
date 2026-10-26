# Skip silence: specification (Player 2 and Tau Omega)

Status: **player side built for MP3** (audit B-774, `TAU DEV 149`, not yet run on a Pocket); **Omega side to do**. This document is the contract between the two. Design discussion lives in `PLAYER_V2_SPEC.md` section 10.1; this file is what Omega implements against. Reference implementations: the reader `fw2/cores/skiptab_core.h`, the writer in `sim/test_p2_skiptab.py` (function `make`), both checked against each other.

## 1. What it does

Some tracks begin or end with a long stretch of silence (live albums, hidden tracks, fade tails). With the setting **SKIP SILENCE** on, the player starts the track *lead* sample pairs in and ends it *tail* pairs early. Nothing else changes: the clock, the progress bar and the total time show the shortened track, the sample accounting (the P3 check) expects the shortened length, and the gapless join to the next track still has no gap.

Detection is **offline, in Omega**. The player never analyses audio: finding silence means decoding it, which costs the CPU the feature is meant to save, and an on-device detector cannot know the tail without reading to the end of the file.

## 2. The file

One file per album folder, named exactly **`tau-skip.bin`**, in the same folder as the tracks it describes (the folder that holds the audio files, not `tau-art`). Little endian throughout. At most 4,096 bytes (the player reads it with one read).

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | magic | ASCII `TSK1` (the 1 is the format version) |
| 4 | 2 | count | number of entries, **0 to 340** |
| 6 | 2 | reserved | 0 |
| 8 | 4 | crc32 | CRC-32 of the entries (the bytes from offset 12 to the end), zlib convention: polynomial 0xEDB88320 reflected, initial value and final xor 0xFFFFFFFF (Python: `zlib.crc32(entries) & 0xFFFFFFFF`) |
| 12 | 12 each | entries | see below |

Each entry:

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | name_hash | FNV-1a, 32 bit, of the track's **file name** (no folder), see 2.1 |
| 4 | 4 | lead_pairs | sample pairs to skip at the start (0 = none) |
| 8 | 4 | tail_pairs | sample pairs to drop at the end (0 = none) |

Entries may be in any order. Two entries with the same hash: the first wins. An album with more than 340 tracks needs more than one folder in practice; Omega writes the first 340 and warns.

### 2.1 The name hash

FNV-1a, 32 bit: `h = 2166136261`; for each byte `b` of the file name: `h = (h xor b) * 16777619 mod 2^32`. The bytes are the file name **as it is on the card** after Omega's ASCII conversion (the same name the library index holds, extension included, case as written), not the original tag or file name. Example: `01 Intro.mp3` hashes to `CB8F593C`, `02 Hidden Track.mp3` to `FD521DCE`.

### 2.2 Units

*Source sample pairs at the track's own sample rate*, counted from the first sample of the **music**, that is after the encoder delay of an MP3 has been removed (the gapless trim the player already applies). 44.1 kHz: 441,000 pairs = 10 s. Lead and tail are independent; `lead + tail` must be less than the track's length minus 1 s, or the entry is ignored by Omega (never written).

### 2.3 Byte-level example

Two tracks: `01 Intro.mp3` (skip the first 10 s at 44.1 kHz) and `02 Hidden Track.mp3` (drop the last 60 s). 36 bytes:

```
0000  54 53 4B 31 02 00 00 00 90 08 D0 87 3C 59 8F CB     TSK1, count 2, reserved 0, crc32 87D00890, entry 0 hash CB8F593C
0010  A8 BA 06 00 00 00 00 00 CE 1D 52 FD 00 00 00 00     lead 441000 (0x0006BAA8), tail 0; entry 1 hash FD521DCE, lead 0
0020  F0 5F 28 00                                         tail 2646000 (0x00285FF0)
```

This exact file was read back by the C reader (`01 Intro.mp3` gives lead 441000 tail 0; `02 Hidden Track.mp3` gives lead 0 tail 2646000; a name that is not in the table gives nothing).

## 3. What the player does with it

1. When a track is opened (a manual choice or the natural join), if SKIP SILENCE is on, the player opens `<track's folder>/tau-skip.bin` through data slot 7 and looks the file name up. A folder without the file is remembered, so the rest of the album costs nothing.
2. **The file is ignored, and the track plays whole, when:** the file is missing; the magic is not `TSK1` (including a future version such as `TSK2`: an old player must not guess); the count is above 340 or the file is shorter than 12 + 12 x count; the CRC does not match; the name is not in the table. No error is shown; the Info page counts `SKIP HITS` / `SKIP MISSES`.
3. **Tail** (MP3): the track ends `tail` pairs early. This needs the track's length, which comes from the Xing/Info frame count: a file without one cannot apply a tail and plays whole (counted in `SKIP IGN`). The segment's expected length (the P3 accounting) shrinks by the tail.
4. **Lead** (MP3): the player seeks to `lead` pairs after the first frame is decoded. A seek lands within the accuracy of the file's seek information: exact to the frame on constant-bitrate files, and within about 0.4 percent of the file (the Xing table's 8-bit resolution) on true variable-bitrate files; the tail moves with the landing, so a VBR file can end a fraction of a second away from the numbers.
5. **FLAC:** not supported yet (it needs a seek first); the numbers are ignored. Omega may still write them.
6. The setting is **off by default** and applies from the next track opened; it is not saved across power cycles in this version.

## 4. What Omega should do

**Detection.** Decode the track (any decoder; use the same sample rate as the file), then measure silence on the **mixed-down peak** (the larger of |L| and |R|) in windows of 10 ms:

* a window is silent when its peak is below **-60 dBFS** (about 33 on a 16-bit scale); make the threshold a setting, with -60 as the default;
* the **lead** is the length of the silent run that starts at sample 0, if it lasts at least **2 s**; the **tail** is the silent run that ends at the last sample, if it lasts at least 2 s;
* **keep a margin**: leave 200 ms of silence in front of the first sound and 500 ms after the last (a natural attack and decay are not silence), so subtract these from the run lengths before writing them; a run shorter than the margin plus 1 s writes 0;
* **never skip inside a track.** Only the run touching the start and the run touching the end count; long silences in the middle are part of the music;
* a track that is silent all through writes nothing for that track.

**Writing.** One `tau-skip.bin` per album folder that has at least one entry; no file when nothing was found. Write it next to the tracks in the card layout, after the ASCII file-name conversion (the hash is of the converted name), and rewrite it whenever a track in that folder is added, removed or renamed. Leave other folders' files alone. The file is not signed; the CRC is only a corruption check.

**Preview in Omega.** Show the detected lead/tail per track as a waveform with the two cut marks, editable by hand (drag or type seconds) with a per-track "never skip" override; a hand-edited value is written as typed.

**Tests Omega should keep.** (a) Byte-for-byte equality with this document's example (section 2.3). (b) Round trip through the reference reader's cases in `sim/test_p2_skiptab.py` (the file in section 2.3 plus a 340-entry file, a corrupted CRC, a wrong magic). (c) The hash of non-ASCII input is never computed: names are ASCII by then.

## 5. Open points

* **FLAC** needs frame-accurate seeking in the player first (the new FLAC decoder, spec 8.2).
* **A numbers-in-the-index variant** (put lead/tail in the library details record instead of a side file) was considered and rejected for now: it needs an index version bump that touches the old player and Omega's writer. If the side file proves awkward, it can be revisited with a details v2.
* **Persist the setting** (a persisted word) once the feature is hardware-confirmed.
* **A bigger album than 340 tracks** is cut at 340 in this version.
