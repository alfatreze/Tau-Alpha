# User test suite for TAU DEV 28 and 29 (G4: library, settings, playlist and cover-art code as cold code)

Written 2026-09-22 (audit B-071). Run on the Pocket; screenshots with Menu+Start land in `Memories/Screenshots`; results are read from the card afterwards
(`decode_tau_suite.py --qr` / `--interact`). Estimated time: about 75 minutes for 28 (30 of them the ENDURANCE soak, which you can leave running), 15 for 29.
Screenshots stop the track for a moment: the underrun counter goes up by 1 each time, so take Info readings BEFORE the screenshots of a section.

## What is being tested
G4 moved about 24 KB of firmware into PSRAM (cold code): the whole library UI, the settings menus and Diagnostic tests, the playlist loader and overlay, and the cover-art
decoder glue. Free RAM rose from 4.1 KB to about 30 KB (Diagnostic Build). The risks are: a page that draws slower or wrong, a cover that decodes slower, an audio
underrun while browsing (cold code competes with the decoder for the 4 KiB instruction cache), and the fail-safe when the cold image is missing.

## Core 28 (G3 bitstream, all of G4). Install: TAU_DEV_28
### A. Boot and Info (2 min)
| # | Do | Expect | Record |
|---|---|---|---|
| A1 | Start the core; watch the loading bar | loading bar only, then the player; no error toast | time to the player (about, seconds) |
| A2 | Start > Settings > Info | FIRMWARE 0.3.0, FPGA REV 4D503317, **SDRAM WINDOW OK**, WINDOW READ about 48 CYC, **FREE RAM about 30,000 B**, COLD IMAGE about 59,700 B and `CODE` (no E-code), LIBRARY 30 TRK | screenshot |
| A3 | Note UNDERRUNS and DRAW STALL now | small (1 or 2) | numbers, for section C |

### B. Menus (10 min): every page draws correctly and quickly
Open each with A, leave with B; look for glitches, missing text, slow draws (more than about half a second).
Menu > Appearance (Colour list, Meter list with the previews, Album art, Screen blank), Audio (Equalizer, **Volume with Left/Right: the track must NOT seek**), Playback (Repeat, Shuffle, Resume, Speed),
Settings > Info, Library (on/off with confirmation, do NOT confirm), How it works, Diagnostics > Tests / Stress / All speeds / Check.
Pass: all pages correct, the colour swatches and meter previews show, no page needs more than one press to open. Screenshot: Appearance, Meter list, Library page, Tests.

### C. Browse while playing (10 min): the main G4 risk
Start a track (any), then for 2 minutes each: (1) open the library (Select), scroll Artists, Albums, Tracks with the d-pad, hold Down to fast-scroll a list, use the letter jump; (2) open Menu and page through 10 pages quickly; (3) skip tracks with Left/Right ten times.
Pass: no audible gap or stutter except at a track change; Info UNDERRUNS grows by at most 1 per track change (compare with A3); DRAW STALL grows by less than 50 ms per minute. Screenshot Info after.

### D. Library behaviour (10 min)
Play an album; next/previous; Shuffle All; play a library playlist (Favourites); the x/y counter; Quit the core and start it again: the library must reopen at what you last played (loaded, not playing).
Pass: each works; history restored. Record anything that differs from before (B-041, B-039).

### E. Legacy playlist mode: playlist loader and overlay are cold code now (10 min)
Settings > Library > turn OFF (A, A to confirm) > Quit and restart. Then: Select opens the playlist overlay (legacy); scroll it (Up/Down; on DEV 28 Left/Right page a screenful; from DEV 30 **L1/R1 page, Right plays the row like A and Left closes like B**), Y returns to the playing row, A plays a row; Core menu > Load Playlist (`playlist.m3u` and `Favourites.m3u`) works; the "LEGACY PLAYLIST MODE" toast appears once. Turn the library back ON, Quit, restart, confirm the library returns.
Pass: overlay draws, scrolls and plays; no crash; playlist load time similar to before (about 1 s or less for 13 tracks).

### F. Cover art: the decoder glue is cold code (5 min)
Play a track with a small cover, one with the 455 px cover, the 1400 px cover (heavy) and a FLAC. Open Info each time.
Expect: covers show correctly (also FLAC); LOAD MS art part about 10-15 ms for the small one and 5,000-15,000 ms for the heavy ones (as before: 5.3 s and 15.8 s were the PSRAM-art figures; the release keeps art in BRAM, this build in PSRAM); **not more than 5% slower than the same track on DEV 23**. Record the four LOAD MS lines. Screenshot Info once.

### G. Diagnostics pages (5 min)
Tests: WINDOW TEST PASS 89, READ CYCLES about 48/57/335, WRITE CYCLES about 31/38/345, PLAYLIST CHECK PASS (with a legacy playlist loaded, otherwise NO PLAYLIST), COLD CODE TEST PASS about 31.6 C/W. CLEAR COUNTERS.
Stress: Level R1 then OFF; Status page shows failures 0.

### H. The Check (about 50 minutes; each run: screenshot the result page and the QR page)
Options on the start page: Up/Down chooses a row, Left/Right changes it (profile, soak length, soak level). **Left/Right must not seek the track.** Press Start during a result page: the menu must stay open (Start is ignored there).
| # | Profile | Time | Expect |
|---|---|---|---|
| H1 | USER CHECK | 30 s | seven PASS; QR version 9-11 |
| H2 | STANDARD | about 3 min (measured 2:41) | rows 12; track changes **10/10 or SKIPPED (value = entries)**; cold x20 = 20; R1-R3 PASS late 0; verdict in the title bar |
| H3 | FULL (5 min soak) | about 7-8 min (measured 7:11) | 13 rows fit (20 px lines), soak PASS, playback PASS 0 underruns |
| H4 | ENDURANCE, 30 min soak | about 31-32 min | countdown mm:ss; PASS; QR settings entry [30, 2]; cold runs about 30, fails 0 |
| H5 | B during a stress step | any | the run stops at once; Tests > Stress Status shows STOPPED |
| H6 | Y on a result page | any | starts the same profile again (run counter +1) |
| H7 | Leave the Check with Start while running | any | run aborts, nothing left running |
Then Quit the core (so the settings file is written).

### I. Persistence (3 min)
Change Colour, Meter, Equalizer and Repeat; Quit; start again: all kept. Volume kept.

## Core 29 (OLD bitstream, same ROM): the fail-safe test. Install: TAU_DEV_29 (15 min)
The old bitstream cannot run code from PSRAM, so everything cold must switch off cleanly.
| # | Do | Expect |
|---|---|---|
| J1 | Start the core | player appears; no crash, no reboot loop |
| J2 | Press Start | toast **MENU OFF: NO COLD IMAGE**; no menu |
| J3 | Press Select | no library; the legacy list is empty (no playlist), no crash |
| J4 | Core menu > Load MP3: pick one track | it plays; 2 minutes, UNDERRUNS unchanged after the first |
| J5 | Left/Right, A (pause), Up/Down (volume), Select+Right | all still work |
| J6 | Core menu > Load Playlist | nothing loads or "NO PLAYLIST"; no crash |
| J7 | Track with a cover | plays without a cover (placeholder), no crash |
| J8 | Quit | returns to the Pocket menu cleanly |
The Info page is not reachable here (menus are cold); the toast is the evidence. Screenshot the toast and the playing screen.

## Regression on the shipped cores (5 min)
`TAU` (release v0.3.0) and `TAU_DIAGNOSTIC` (v0.3.0) must behave as before: start, play, Menu pages, Info. They are untouched by G4. (`TAU_PSRAM_12` is an old test core.)

## Sending the results
1. Screenshots: all of the above are already on the card in `Memories/Screenshots`.
2. Quit each core once after its runs, then leave the card as it is: the settings files are the record.
3. Tell me: A1 time, C (sound gaps yes/no), F (LOAD MS numbers), anything odd, and which numbered rows failed. I decode the QR pages and settings files myself.
Pass overall: A-I as expected on 28 (H2 may report track changes SKIPPED; that is information, not a failure), J1-J8 on 29, no regression on the shipped cores.

## Added (B-072): Left/Right as Back/Forward (TAU DEV 30 and later)
Try in every menu, list and page: **Right** opens/selects like A, **Left** goes back like B (on a switch or the Volume row Left/Right still change the value). Library lists: Right enters/plays, Left goes up a level or closes; the letter jump stays on L1/R1. Playlist overlay: L1/R1 page a screenful. The Library on/off page: Right may ask, only A confirms. Check pages: Left goes back from the result and QR pages. Expected: no menu needs the B button any more; nothing seeks the track.

## Added (B-073): fixes from the TAU DEV 28 test round
1. **Left/Right stopped skipping tracks, said "NO PLAYLIST"** in library mode: `pl_load()` is never called there, so `pl_count` stayed 0 forever and the skip gate only ever checked that. Fixed to check the library queue when a library track is playing.
2. **Menu scrolling now auto-repeats** on a held Up/Down, the same as the playlist and library lists (previously one tap moved one row).
3. **Smaller cover not loading** (Nausicaa Image Album - The Bird Man, track 2, a 455x455 ID3v2.3 cover): confirmed on the host that the frame-finding code correctly locates the picture in this exact file's bytes, so it is not a parsing-logic bug. As a step toward finding it, the MP3 cover-finder (`art_find_apic`) is temporarily un-cold (back to fast RAM) in this build, `art_decode`/`art_flush_row`/the FLAC finder stay cold code (they already work, including for the largest covers). If the smaller cover now loads, cold code was the cause and it stays hot for good; if it still fails, the fault is elsewhere and cold code is cleared. Also added: **Info > COVER** shows `OK`, `-` (no picture) or an `E` code (a new, more specific error) so this is visible without guessing next time.
4. **Test-script time estimates corrected** from the actual measured run times above (I had overestimated STANDARD and FULL).
Try again with this same track/cover on `TAU DEV 31` and read Info > COVER; also try it, if convenient, on `TAU_DIAGNOSTIC` (an older build where this code was never cold) for comparison.
