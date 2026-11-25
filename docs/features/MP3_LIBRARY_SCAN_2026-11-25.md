# MP3 library header scan (2026-11-25)

**Status: measurement report (work package WP-03 of `docs/features/MP3_DECODER_V2_PLAN_2026-11-25.md`).** Host-only, read-only on the owner's library. Nothing changed in firmware, RTL, a card or a VM. **Evidence label: MEASURED on the host** by `tools/mp3_v2/scan_library.py` (own code, written from the frame layout of the standard and the public Xing/Info/LAME and VBRI tag layouts; no third-party code) over the 59 `.mp3` files of the owner's `test music` folder (the second location, `DEV PROJECTS/test music`, does not exist). Clipping figures come from the float oracle of WP-01 (pristine minimp3, `tools/mp3_v2/oracle.py`); everything else is parsed from frame headers and side information. The scanner is tested by `sim/test_mp3_v2_scan_library.py` against a synthetic stream with known contents. Counts only: no file names; folders are lettered.

## 1. Result in one paragraph

**The earlier review's finding is reproduced:** all 59 files are MPEG-1 Layer III at 44.1 kHz (57) or 48 kHz (2); there is **no** MPEG-2/2.5, **no** intensity stereo, **no** mixed block, **no** CRC (protection) frame, **no** free-format frame, no emphasis, no VBRI file, no header change inside a file and no frame short of bit-reservoir data (largest `main_data_begin` 511, the reservoir limit, so the full 511-byte reservoir is used). What the review did not state and this scan adds: **window switching is in use in 58 of 59 files** (122,111 granules, 42,294 frames: start 33,128, short 55,832, stop 33,151), so short blocks are on the hot path of every real file even though mixed blocks are not; **12 files carry an ID3v2.4 tag with the unsynchronisation flag**, the largest ID3v2 tag is 170,910 bytes (embedded art), and the one tag-less speech file has **417 zero bytes between its ID3v2 tag and its first frame**; 36 of 59 files have samples beyond +-1.0 in the float oracle.

## 2. Totals

| Item | Value |
|---|---|
| files / frames / audio frames | 59 / 479,038 / 478,980 |
| MPEG version (files) | MPEG-1 59 |
| layer (files) | L3 59 |
| sample rate Hz (files) | 44100 57, 48000 2 |
| channel mode (files) | joint stereo 47, mono 1, stereo 11 |
| bitrates kbps seen (files) | 32 37, 40 12, 48 12, 56 11, 64 10, 80 21, 96 22, 112 25, 128 51, 160 48, 192 48, 224 48, 256 48, 320 53 |
| CBR / VBR files | 11 / 48 |
| CRC (protection) frames: files / frames | 0 / 0 |
| free-format files | 0 |
| joint-stereo MS frames: files / frames | 44 / 136,035 |
| intensity-stereo frames: files / frames | 0 / 0 |
| window-switched frames: files / frames / granules | 58 / 42,294 / 122,111 |
| block types, granules: start / short / stop | 33,128 / 55,832 / 33,151 |
| mixed-block granules: files / granules | 0 / 0 |
| frames short of bit-reservoir data: files / frames | 0 / 0 |
| largest main_data_begin seen (bytes) | 511 |
| first frame: Xing / Info / VBRI / none | 48 / 10 / 0 / 1 |
| LAME delay and padding present (files) | 58 (delay 576 in all; padding 768 to 2,304 samples) |
| tag frame count differs from the counted frames (files) | 0 |
| ID3v2: files / with footer / unsync / extended header / largest bytes | 58 / 0 / 12 / 0 / 170,910 |
| ID3v1 footer / APEv2 footer (files) | 26 / 0 |
| junk: leading / between frames / trailing (files) | 1 / 0 / 0 |
| sample-rate, version or channel-count change inside a file (files) | 0 |
| channel-mode change inside a file (files) | 0 |
| emphasis flagged (files) | 0 |
| side information inconsistent with the frame size (files) | 0 |
| Xing TOC worst error vs true frame offsets, seconds: median / 90th percentile / max over 58 files | 1.07 / 1.38 / 1.78 |
| float oracle: files with a sample beyond +-1.0 / total such samples / largest peak | 36 of 59 / 297,131 / 1.1935 |
| 16-bit oracle output at the rail: files / samples | 36 / 298,043 |

Other counts from the same run: encoders named by the Xing/Info extension: LAME 3.90 (11 files), 3.97b (12), 3.98r (12), 4.0 (10), Lavc 59.37 (13), none (1, the speech file); ID3v2 major version 3 on 33 files, 4 on 25, none on 1; longest file 6 min 20 s (379.9 s). The Info or Xing header frame of some files is flagged `stereo` inside a joint-stereo file; it is left out of every distribution above, because it is not audio.

## 3. By folder (lettered; all counts)

| Folder | Files | Frames | Window-switched frames | VBR files | Mono files | ID3v2 unsync files | Files with samples beyond 1.0 |
|---|---|---|---|---|---|---|---|
| A | 12 | 116,955 | 10,791 | 12 | 0 | 0 | 12 |
| B | 12 | 111,742 | 7,735 | 12 | 0 | 12 | 6 |
| C | 11 | 97,264 | 13,704 | 11 | 0 | 0 | 5 |
| D | 13 | 91,597 | 5,386 | 13 | 0 | 0 | 12 |
| E (the project's Test Album) | 7 | 53,026 | 4,645 | 0 | 1 | 0 | 1 |
| F, G, H (loopback, gapless and ReplayGain test files) | 4 | 8,454 | 33 | 0 | 0 | 0 | 0 |

## 4. What each finding means for the v2 plan

1. **Scope (D2).** The release-1 coverage of the plan (MPEG-1 Layer III, mono/stereo/MS, all block types, scfsi, reservoir) is exactly what the library uses. LSF, intensity and mixed blocks stay as the plan has them: supported for no-regression, cold or flagged, tested only by synthetic streams and vectors. The scan found no file that needs any of them.
2. **Short blocks are hot.** 55,832 short-block granules in 58 files: the three-12-point IMDCT path and the reorder are exercised by every album, not only by test vectors (plan sections 3.1 M8/M10, 3.3). The zero-band skip and the cache budget of the back end must be measured on window-switched frames too, not only on long-block material.
3. **Reservoir.** `main_data_begin` reaches 511 in real files: the reservoir buffer must be the full 511 bytes (the plan has 511). No frame in the library starts short, so every WARMUP in practice comes from a seek or a fresh start; a WARMUP rule is untested by any real file.
4. **Driver edge cases that real files have today (inputs for WP-11):** (a) ID3v2.4 unsynchronisation in 12 files, (b) a 170,910-byte ID3v2 tag followed by 417 zero bytes before the first frame (the sync search must skip zeros; no Info/Xing frame in that file, so the frame count comes from the file length), (c) 26 files with an ID3v1 footer, (d) the Info/Xing frame is itself a normal Layer III frame (the first frame of 58 files) and must advance the position by one frame of silence (plan 3.5 trim rule); its mode flag can differ from the audio's. None has an ID3v2 footer, an extended header or an APEv2 tag: those paths are covered only by synthetic tests.
5. **Gapless.** Delay is 576 and padding is 768 to 2,304 in every file with a LAME extension; `tag_frames_vs_counted` is 0 for all 58 (the header frame count equals the counted audio frames), so the header frame counts are trustworthy on this library.
6. **U7 (Xing TOC accuracy).** The TOC's worst error against the true frame offsets is 1.78 s on files of at most 6 min 20 s; the median file is 1.07 s. That is the 1/256 quantisation of the table (0.39 % of the duration, 1.5 s on 6 min), not a defect; on a 10 h audiobook the same quantisation is about 2.3 min, as the review estimated. The limit is the TOC's resolution, so a seek that must be exact needs the frame index, not the TOC.
7. **Clipping potential (input to WP-04 and D11).** In the float oracle 36 of 59 files exceed +-1.0 (297,131 samples in total over about 1.1 billion output samples, 0.027 %; largest peak 1.1935, +1.5 dB); the 16-bit oracle output sits on the rail in 298,043 samples (36 files, the largest single file 98,215). These are upper bounds on clipping created by a decoder (the oracle's own 16-bit conversion clips): the plan's pre-gain mechanism and D11 follow from them, not from this report.

## 5. Method and limits

- **Frame walk.** The walk starts after any ID3v2 tag(s), ends before ID3v1 and APEv2 footers, accepts a frame when its header is valid and the next header is compatible (version, layer, sample rate; bitrate, mode and padding may change), or when the previous frame's confirmed next header is this one, or when it is the last frame; anything else is junk and the scan resynchronises byte by byte. Free-format frames are measured by the distance to the next header.
- **Side information** is parsed per frame as the standard lays it out (MPEG-1: 9-bit `main_data_begin`, 2 granules; LSF: 8 bits, 1 granule; window-switching flag, block type, mixed flag per granule and channel).
- **Short of reservoir** follows the decoder's rule: a frame's `main_data_begin` may not exceed the bytes the previous frame left unconsumed (at most 511, the whole payload of a frame that could not be decoded); the Xing/Info frame counts as an ordinary frame with an empty main data. This is the pinned minimp3's rule; a different decoder could be stricter or looser.
- **Not measured:** Layer I/II frames are only recorded as junk (none exist); ID3v2 frame contents are not parsed; no listening.
- **Unit-test coverage:** `sim/test_mp3_v2_scan_library.py` (22 checks): a synthetic stream with every feature above, an MPEG-2 mono stream, a VBRI file, a free-format file, leading junk, the float oracle agreeing on the frame count, the scan being read-only, and two red mutations. It also records an upstream behaviour of the pinned minimp3: when junk lies between two frames it drops the frame before the junk (its resync needs the confirmation of following headers), and it returns no samples for a truncated last frame.
- **Reproduce:** `python3 tools/mp3_v2/scan_library.py` (uses the two known library paths when present; `--json`, `--markdown`, `--no-peak`).
