# TAU DEV 224: the METER list on a Pocket (P2.5), ONE session of about 15 minutes

What changed: Settings > Appearance > METER now **scrolls through every visible row of the table of contents** (up to 64), seven at a time, in the **same order Now Playing's X walks**. A row this firmware cannot run is listed **greyed** (`N/A`) and cannot be picked; a hidden row is not listed. The title line shows where you are (`12 / 63`). The METER row of Appearance shows the name of the meter on screen. Nothing else changed (no bitstream change, same `CORE_VERSION`).

**Card:** `TAU DEV 224` and the 64-row user bin copied by you as `Assets/tau/common/tau-meters.bin` (built in `work/p25/one-go/tau-meters.bin`; its twin `tau-meters-no-hang.bin` is not needed). Its rows: Winamp Bars, Scope, Chladni, Layered Wave, VU Master, Waterfall, Sound Map, then TEST 08 ... TEST 63, and **HANG TEST as row 64 (last)**. **TEST 20 is UNAVAILABLE** (asks for a newer TAU), **TEST 21 is HIDDEN**, TEST 09 and TEST 10 share a tuning key (as in DEV 222/223).

**Which real meter each TEST row runs as** (the synthetic rows reuse the six real packs in this order and repeat every six): TEST 08 = Scope, 09 = Chladni, 10 = Layered Wave, 11 = VU Master, **12 = Waterfall**, 13 = Sound Map, then 14 = Scope, 15 = Chladni ... In general TEST n runs as (n - 8) mod 6: 0 Scope, 1 Chladni, 2 Layered Wave, 3 VU Master, 4 Waterfall, 5 Sound Map. **TEST 19 is Sound Map, TEST 20 (greyed) would be Scope, TEST 21 (hidden) would be Chladni, TEST 22 is Layered Wave.**

**What the list should hold (63 positions, not 64):** position 1 Winamp Bars, 2 to 7 the six real meters, 8 = TEST 08 ... 20 = TEST 20 (greyed), then **21 = TEST 22** (TEST 21 is hidden), ... 62 = TEST 63, **63 = HANG TEST**. From TEST 22 on, the position is the TEST number minus 1.

Start a long playlist and keep it playing. **Pause before every screenshot** (a screenshot while playing adds one dropout). Do not select HANG TEST in this session (it is the safe-boot test of DEV 222/223, already passed).

| Step | What you do | What you should see |
|---|---|---|
| **1. Open** | Now Playing is on Winamp Bars. Menu > Appearance. The METER row's value reads **WINAMP BARS**. Open METER (A). | The list opens on Winamp Bars (first row, highlighted, `*` at the right), title line **`1 / 63`** at the right, seven rows with a preview each, hints at the bottom. **No strip of the old screen** under the last row or between the rows and the hints (the B-1044 check). |
| **2. Scroll the whole list** | Hold or tap Down through the list to the end (63 positions), then once more. | The window moves one row at a time, the highlighted row never leaves the screen, the indicator counts `1 / 63` ... `63 / 63`. At the end the next Down **wraps to `1 / 63`**; Up from the first row wraps to `63 / 63` (HANG TEST: only look at it). Previews appear for every row (TEST rows show the preview of their real meter). Note any visible lag when the wrap redraws seven previews at once. |
| **3. Greyed and hidden** | Go to positions 19 to 21: TEST 19, **TEST 20, TEST 22**. | TEST 20 is **greyed, its name faint, `N/A` at the right, an empty frame instead of a preview**. There is **no TEST 21**: TEST 22 follows TEST 20 directly. Press **A on TEST 20**: nothing changes (the star stays where it was), the first hint line becomes **NEEDS A NEWER TAU: CANNOT PICK**; the next key clears it. **X on TEST 20** does nothing either. |
| **4. Pick a row below 8** | Highlight **TEST 12** (position 12), press **A**. | Back on Appearance, the METER value reads **TEST 12**. Go to Now Playing: the **Waterfall** is running. |
| **5. Same order as X** | In the list pick **TEST 19** (Sound Map), go back to Now Playing, press **X**. Then X again. | **Layered Wave** (TEST 22), then **VU Master** (TEST 23): the greyed TEST 20 (Scope) and the hidden TEST 21 (Chladni) are skipped by X exactly as the list skips them. Open the list again: the highlighted row and the `*` are the meter on screen. |
| **6. Restart persistence of a list pick** | Menu > Appearance > METER, pick **TEST 30** (position 29, Waterfall). Back to Now Playing, **wait 10 s** watching it, Quit from the Pocket menu, start the core again. | Now Playing starts on **TEST 30 (Waterfall)**. Settings > Appearance > METER reads **TEST 30**, and the list opens on position 29 with the star. (Picking and quitting within 3 s still keeps the previous meter: the commit rule of DEV 222/223, unchanged.) |
| **7. Configure from the list** | Open the list, highlight **TEST 12** (not the meter on screen), press **X**. | The Configure page opens **for TEST 12** (the Waterfall's parameters): X first selects the row (the registry loads only the active meter), then opens Configure. Go back (B): you are on the Settings page again; the list has the star on TEST 12. |
| **8. Report** | Pause. Select+Start, refresh, Menu+Start: the Report page. Photograph it. | see below |

## What I read from the photo (Report page rows `MSEL`, `MTOC`, `MWIN`)
| Row | Good |
|---|---|
| `MTOC STATE` / `COUNT` / `ERR` / `SRC` | `1` / **64** / **0** / **1** (the user bin) |
| `MSEL ROWS` | **62** (64 rows less the greyed TEST 20 and the hidden TEST 21) |
| `MSEL SKIPPED` | **1** (TEST 20 is visible but this firmware cannot run it; the hidden TEST 21 is not counted) |
| `MSEL NOTUNE` | **2** (TEST 09 and TEST 10) |
| `MSEL SAVED` | the hash of the meter that has run 5 s (TEST 30 after step 6; later steps leave it on the last clean meter) |
| `MSEL ABORTS` | **0** (no refusal, strike or hang: you never selected the greyed row, A on it is refused before any load) |
| `MWIN LOADS` | about the number of picks and X presses (about 8 to 12), `ERR 0`, `REFUSED 0`, `STRIKES 0`, `READ FAIL 0` |
| `MWIN LOAD MAX` | below DEV 223's 7.4 M cycles (111 ms) |
| `DRY`, `UNDER RING`, `HW UNDER EV` | 0 (unchanged by the list: it reads nothing from the card) |

## Bad signs (tell me which step)
A row off screen while highlighted; the indicator not matching the row you are on; a strip of the old screen anywhere; a name overlapping the `*`, `!` or `N/A` marks; the list and X disagreeing (X lands on TEST 20 or TEST 21, or skips a row the list can pick); A on the greyed row selecting anything or `MSEL ABORTS` above 0; TEST 21 anywhere in the list; the METER row of Appearance showing a name that is not the meter on screen; a long pause or a dropout when the list redraws; a restart that does not come back on TEST 30.

## What the host could not test (so this session is the first look)
The real font drawing and the exact pixels (the host used the fonts' advance tables and a model of the screen); how the previews look greyed or at the wrap (seven previews are rebuilt at once: unmeasured time); the feel of the key repeat while scrolling; the real redraw time of a page of 7 rows; anything with the real card, PSRAM or the Pocket's save at Quit.

## When you are done
Tell me. Remove `Assets/tau/common/tau-meters.bin` (with your approval): a user bin overrides the core's own meter list in every dev core and would show 63 TEST meters in daily use.
