# Test script: album art in PSRAM (B-024) and the diagnostic build

**Cores on the card:** `TAU PSRAM 03` (release-style: Settings + Info) and `TAU PSRAM 05` (diagnostic: adds Tests, Stress, All Speeds). Both run on the same bitstream. Each has the same music copy.
**Music (covers are embedded in the copies; sources untouched):** Bird Man (11 tracks, 455 px cover, playlist auto-created), Soundtrack (13 tracks, 1400 px), flac-tests (6 FLAC, 1499 px), plus the root `playlist.m3u`.
Take a screenshot at every step marked (S). Quit the core to the menu at the end of each core's session, before pulling the card.

## A. Covers and timing on TAU PSRAM 03
| # | Do | Expect |
|---|---|---|
| A1 | Start 03, load the Bird Man playlist, let track 1 play | 455 px cover appears and looks correct (S) |
| A2 | Start > Diagnostics > Info (S) | Info page shows the art decode time. Prediction: about 0.3-0.6 s slower than the earlier 2.6 s (A-120) for a 455 px cover |
| A3 | Load the Soundtrack playlist, play track 1 (S), Info page (S) | 1400 px cover correct; decode time about the same as before (large covers cost little) |
| A4 | Load flac-tests, play t1 (S), Info page (S) | 1499 px cover shown from FLAC; audio plays |
| A5 | Next track 10 times quickly on the Soundtrack | Cover appears each time, no crash, no stuck screen |
| A6 | Listen 10 minutes on Bird Man, seek a few times | No glitches; Info page: underruns 0 |
Fail signs: no cover where one is expected, a toast about art, garbled cover, any freeze. Note which track.

## B. Diagnostic gates on TAU PSRAM 05 (same as B-023)
| # | Do | Expect |
|---|---|---|
| B1 | Start > Diagnostics > Tests > run each row (S) | WINDOW TEST PASS 89, READ about 48/57/335, WRITE about 31/38/344, PLAYLIST CHECK PASS 13 |
| B2 | Play Soundtrack; Diagnostics > Stress > Level R1, R2, R3, about 5 min each (S at the end of each) | Late underruns 0 at every level |
| B3 | Stress > Soak 30 MIN (S near the end, S after) | SOAK PASS, failures 0, late 0, worst access about 373 |
| B4 | After the soak: Tests > PLAYLIST CHECK again (S) | PASS 13 (this check was owed since A-128) |
| B5 | Change track once during a stress level (S) | Cover still decodes, no failure |

## C. All Speeds (05 only)
| # | Do | Expect |
|---|---|---|
| C1 | Playback > Speed: list has 5 entries (0.85x-1.20x) | as before |
| C2 | Diagnostics > ALL SPEEDS ON, then Playback > Speed | list shows 10 entries up to 2.50x (S) |
| C3 | Try 1.30x, 1.50x, 2.00x, 2.50x on the Soundtrack; note what you hear | expect degradation from 1.30x up (A-133); note any crash or freeze |
| C4 | Choose 1.50x, then Diagnostics > ALL SPEEDS OFF | speed returns to 1.00x, list back to 5 entries |
| C5 | Quit and restart 05 | ALL SPEEDS is OFF again |

## Report back
Send the screenshots (they read from the card) and anything odd you heard or saw. Quit before pulling the card.
