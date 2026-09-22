# Test script: library history, load-not-play, list counter, Settings-style lists, library switch (TAU PSRAM 10)

**Core:** `TAU PSRAM 11` (replaces 10 before it was run: the counter now uses the existing transport-row `x / y`, see B-041 addendum; everything else as described for 10) (library build on the P4 bitstream; ROM `878c1f8b...`, 172,012 B, heap gap 7,456 B). Packaged in `work/diagnostics/tau-psram-10/pocket`; NOT installed. Adds four persist words (interact ids 24-27, addresses 0x30-0x3C) to the packaged core only.
**Media:** the 09 media copy and index (30 tracks, playlists Favourites and Playlist), moved to the 10 folder by `sync_media.py --library`.

## Predictions [EST]
- **First start (no history):** the first track of the first album opens **paused** (a loaded, not playing, now-playing screen) instead of the idle screen. Nothing plays until A.
- **Later starts:** what you last played is opened paused at the same queue position: an album, an artist, a playlist, all tracks A-Z, or Shuffle All (the same order, because the seed is saved). If the index is rebuilt since (new build id) the history is ignored and the first track opens.
- The card shows title, artist and album from the index as soon as the head of the file is read; the cover art and format details follow when the file has fully loaded (cover art is slow for heavy files; the early frame has no cover panel).
- Lists (library queue or playlist) show the existing transport-row counter `x / y` at the right (`3 / 12`), now also for library queues. A single file shows none.
- Library lists look like Settings: 36 px rows, 7 visible, bar 4 px shorter than the row, ">" on rows that open a deeper level, track tiles inside.
- MENU > SETTINGS > LIBRARY: shows "Library is ON"/"OFF", explains the effect and applies after a restart; A asks, a second A confirms, B cancels. Off: legacy mode after restart (LEGACY PLAYLIST MODE alert, Select opens the playlist, Info LIBRARY row `OFF (SETTING)`); on: the library loads again and the history is still there.

## Steps
| # | Do | Expect |
|---|---|---|
| 1 | Start 10 on a fresh core folder (no saved history) | First track of the first album loaded, paused; x/y shows 1/N |
| 2 | Play a few tracks of an album, quit the core, start again | The same album at the same track, paused |
| 3 | Repeat for an artist (X on an artist), a playlist (Favourites), Tracks A-Z, Shuffle All | Same source and position each time; Shuffle All resumes the same order |
| 4 | Play through a list; watch the counter | x/y increments; playlist and album both show it |
| 5 | Rebuild the index (rerun `sync_media.py --library`) and start | History ignored; first track opens |
| 6 | Browse the library lists (S) | Settings-style rows; scroll bar intact while a long title scrolls |
| 7 | MENU > SETTINGS > LIBRARY (S), A, A | Confirm step, then OFF; restart: legacy mode. Turn back on: library returns |
| 8 | Bird Man track 4 | Title complete, no cut |
