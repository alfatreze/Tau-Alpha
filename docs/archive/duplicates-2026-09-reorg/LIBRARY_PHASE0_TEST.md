# Library phase 0: absolute-path open test (B-033)

**Purpose:** prove on hardware that the core opens a track named by an **absolute path** in another folder (`/Assets/tau/.../file`), which the whole media library depends on (`docs/MEDIA_LIBRARY_0.4_SPEC.md` section 0.1). Source says it works (`pl_open_try`, `fw/playlist.inc`: a name starting with `/` replaces the whole path); no hardware run has used it. **No firmware, RTL or core change**: a new playlist file only.
**Media:** the test media already on the card (`TAU_DIAGNOSTIC`: Soundtrack, Bird Man, flac-tests). Core under test: `TAU_DIAGNOSTIC` (v0.3.0 Diagnostic Build, same open path as the release).
**Staged (host):** `work/diagnostics/library-phase0/stage/Phase0/phase0.m3u`, SHA-256 `4e2d9feb793195884ce15cb3eaacb2b2dd0e40095750166c4e9504c5f99dfb50`. Install is additive (a new folder `Assets/tau_diagnostic/common/Phase0/`); nothing on the card is replaced. NOT installed: needs card-write approval.

## Playlist (line order = play order)
| # | Entry | What it tests |
|---|---|---|
| 1 | relative `Soundtrack/01 ...mp3` | control: relative sub-folder path, already proven (root playlist) |
| 2 | absolute, Soundtrack `02. Stampede of the Ohmu.mp3` | absolute path, first folder |
| 3 | absolute, Bird Man `04. Giant Warrior ~ ... Kushana.mp3` (175 bytes, the longest name on the card) | path length near the descriptor limit; spaces, `~`, `.` |
| 4 | absolute, `flac-tests/t2 stereo 16 midside.flac` | absolute + FLAC |
| 5 | absolute, Bird Man `01. The Legend of Wind.mp3` | third folder |
| 6 | absolute, Soundtrack `06. Battle.mp3` | back to the first folder |
| 7 | absolute, `NoSuchFolder/missing track.mp3` | must fail cleanly and skip |

## Predictions (written before hardware) [EST]
- Lines 1-6 open and play, each showing the right title; the playlist screen lists 7 entries (comment line ignored). Line 3 (175 bytes) opens: the descriptor has room (256 minus the name offset).
- Line 7: a toast or skip, playback continues with the next entry (or wraps to 1); no freeze, no crash; Info underruns unchanged.
- If absolute paths fail for any reason: line 2 fails with an open error (result code 4 or NONAME), and the same for 3-6; line 1 still plays. Then the library design must change (spec section 7) before any firmware: fallback is one playlist-style relative path per album folder, or a per-album index slot.
- Decision rule: PASS = lines 1-6 play and 7 is skipped cleanly. Anything else is recorded as FAIL with the entry number and what the screen showed.

## Steps for the owner
1. Start `TAU_DIAGNOSTIC`, choose `Phase0/phase0.m3u` as the playlist (Pocket menu, playlist slot).
2. Let each track play a few seconds, or press Next. Photo/screenshot (Menu+Start stops playback, KB-031) at: playlist screen, one screen per line 1-6, and line 7 (the toast or the skip).
3. Quit the core to the menu before pulling the card.

## Card procedure (needs explicit approval)
Copy `stage/Phase0/` to `Assets/tau_diagnostic/common/Phase0/` with `rsync -rt --exclude='._*'`, compare with `cmp`/SHA-256, remove any `._*` files, `sync`, `diskutil eject /Volumes/Pock`. No index deletion is needed (no core files change).
