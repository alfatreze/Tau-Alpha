# Test script: media library first Pocket run (TAU PSRAM 07)

**Core:** `TAU PSRAM 07` (library build: settings + Info + `TAU_LIBRARY`, bitstream = the v0.3.0 P4 build, ROM 171,180 B, heap gap 8,320 B). Packaged in `work/diagnostics/tau-psram-07/pocket`; NOT installed.
**Media (planned):** copy of the `TAU_DIAGNOSTIC` test media (Soundtrack, Bird Man, flac-tests, root playlist) via `sync_media.py --from-core alfatreze.TAU_DIAGNOSTIC --core alfatreze.TAU_PSRAM_07 --library`, which also writes `tau-library.tdb`. The count and names below are filled in from the tool's report when the index is built on the card copy.
Screenshot (Menu+Start) stops playback (KB-031): take them at the marked steps only. Quit the core to the menu before pulling the card.

## Predictions (written before hardware) [EST]
- Boot shows "LOADING LIBRARY" briefly; index load about 0.2-0.3 s for a 30-track library (7,180 tracks: 1.1-1.4 s).
- Info page, last row LIBRARY: `<N> TRK 0.<x> S`, N = number of audio files in the media copy (30 expected).
- Select (tap) opens LIBRARY with 5 rows (ARTISTS, ALBUMS, TRACKS, SHUFFLE ALL, PLAYLIST); Start still opens Settings.
- Track rows show a theme-colour tile with a black two-digit track number (01, 02, ...).
- Long titles in track rows scroll on the selected row; the now-playing title shows in full (BUG-002: Bird Man track 4, "Giant Warrior ~ Torumekian Army ~ Her Royal Highness Princess Kushana", 69 characters, was cut at "Her Royal H" before).
- No late underruns while browsing during playback; a track change from the browser costs about the same as a playlist pick.

## Steps
| # | Do | Expect |
|---|---|---|
| 1 | Start 07 | Splash, LOADING LIBRARY, then the player as usual |
| 2 | Start > Diagnostics > Info (S) | LIBRARY row `N TRK 0.x S`; FREE RAM about 8.3 KB |
| 3 | Select tap (S) | LIBRARY home, 5 rows |
| 4 | ARTISTS: browse, L1/R1 jump by letter | Alphabetical; jump lands on first entry of the next/previous letter |
| 5 | Open an artist, then an album (S) | Track rows with tiles 01.. in track order |
| 6 | A on a track | Plays it; overlay closes; Next/Previous follow the album; at the last track with repeat off it stops |
| 7 | ALBUMS, X on an album | Plays from its first track |
| 8 | TRACKS (all A-Z), A on one | Plays; Next goes to the next title alphabetically |
| 9 | SHUFFLE ALL, press Next 30 times | Every track once, no repeats; then stops (repeat off) or wraps reshuffled (repeat all) |
| 10 | Bird Man track 4 | Now-playing title complete (scrolls), not cut at "Her Royal H" |
| 11 | PLAYLIST row (library home) | The old playlist overlay; A plays a playlist track; Next now follows the playlist, not the library |
| 12 | Pocket menu > Load MP3 with a single file | Plays it; Select opens LIBRARY again; Next does not follow the old library queue |
| 13 | Start (settings) while the library is open | Library closes, settings open |
| 14 | Listen 10 min, browse while playing | No audible glitches; Info UNDERRUNS unchanged |
Fail signs: no LIBRARY row/Select does nothing (index not loaded: read the Info row for `OFF Ennn`), a tile without a number or with a single digit, wrong order, a repeated or missing track in shuffle, freeze on track change, title cut, playlist/library queues mixed up.

## Negative tests (after the above passes; separate index copies on the same core)
Missing file, flipped body byte, wrong version, truncated file, index from a larger library: expect `OFF E10/E13/E11/E12` and normal playback of single files and playlists.
