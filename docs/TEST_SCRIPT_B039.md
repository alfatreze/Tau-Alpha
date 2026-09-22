# Test script: library playlists, MENU/SETTINGS restructure, legacy mode (TAU PSRAM 09)

**Core:** `TAU PSRAM 09` (library build, P4 bitstream; ROM 171,928 B, heap gap 7,568 B). Packaged in `work/diagnostics/tau-psram-09/pocket`; NOT installed (the never-installed 08 is superseded).
**Media:** the same test media as 07 (`sync_media.py --library` rebuilds the index with the new playlist rules: album lists are skipped, your own `.m3u` files become playlists, named by the file). Add one extra test list to the media before indexing, for example `Favourites.m3u` at the media root with entries from two albums (a repeat is allowed).

## Predictions [EST]
- Menu (Start) is titled MENU: APPEARANCE, AUDIO, PLAYBACK, SETTINGS. SETTINGS holds INFO and HOW IT WORKS (the Diagnostic Build adds DIAGNOSTICS > TESTS / STRESS / ALL SPEEDS). B steps back one level.
- With a library loaded: no playlist is loaded at boot and nothing auto-plays; the idle screen says "Library ready" and points to Select. Core-menu playlist picks are ignored; a file picked in the core menu still plays once.
- Library home has PLAYLISTS when the index has any. PLAYLISTS lists them with their length on the right; A opens one (tile = position in the list), A on a track plays the list from there, X plays a list from the start. Next/Previous follow the list; repeat and end-of-list behave as for albums.
- Without a library file (delete `tau-library.tdb` on a copy): the idle screen shows the red LEGACY PLAYLIST MODE alert above the old getting-started card, a playing track shows a one-time LEGACY PLAYLIST MODE toast, and SETTINGS > HOW IT WORKS shows the legacy text (how to load a file, a playlist, and how to make a library).

## Steps
| # | Do | Expect |
|---|---|---|
| 1 | Start 09 | LOADING LIBRARY, then the "Library ready" idle screen (nothing auto-plays) |
| 2 | Start (menu) (S) | Title MENU; SETTINGS row; B backs out level by level |
| 3 | SETTINGS > INFO, HOW IT WORKS (S) | Info incl. LIBRARY row; library-mode text |
| 4 | Select > PLAYLISTS (S) | Names and lengths; A opens; tile numbers 01, 02, ... |
| 5 | A on the 3rd track of a list | Plays it; Next goes to the 4th; at the end with repeat off it stops (END OF PLAYLIST) |
| 6 | X on a list | Plays from its first entry; a repeated track in the list plays twice |
| 7 | Pocket menu > Load Playlist with any .m3u | Ignored (nothing changes) |
| 8 | Pocket menu > Load MP3 with a file | Plays once; Select opens the library again |
| 9 | Long list: scroll with the marquee running | Scroll bar intact (earlier fix) |
| 10 | Copy of the card media without `tau-library.tdb` | LEGACY PLAYLIST MODE alert, old behaviour, HOW IT WORKS legacy text |
Diagnostic Build (later, same bitstream): SETTINGS > DIAGNOSTICS holds Tests, Stress, All speeds.
