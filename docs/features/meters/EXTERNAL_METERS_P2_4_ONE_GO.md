# TAU DEV 222: the whole Pocket test in ONE session (about 25 minutes)

One card visit: the card has **TAU DEV 222** and the 64-row user bin `Assets/tau/common/tau-meters.bin` (`work/p24/one-go/tau-meters-hang.bin`: Winamp Bars, Scope, Chladni, Layered Wave, VU Master, Waterfall, Sound Map, then TEST 08 ... TEST 63, and **HANG TEST as the LAST row**; rows TEST 09 and TEST 10 deliberately share their tuning key). Start a long playlist and let it play for the whole test. **Pause before every screenshot** (a screenshot while playing adds one dropout). Photograph the **Report** page (Select+Start, refresh, then Menu+Start); I decode every row from the picture.

**Which real meter each TEST row runs as** (the synthetic rows reuse the six real packs in this order, then repeat every six): TEST 08 = Scope, 09 = Chladni, 10 = Layered Wave, 11 = VU Master, **12 = Waterfall**, 13 = Sound Map, **14 = Scope** again, 15 = Chladni, 16 = Layered Wave, 17 = VU Master, 18 = Waterfall, 19 = Sound Map, 20 = Scope ... (so step 3's tuning at TEST 12 is a Waterfall parameter; the factory presets exist only for the seven REAL rows, a TEST row lists only DEFAULT).

X on Now Playing is the next meter, in table order, and wraps. Counters restart at every boot.

| Step | What you do | Photo |
|---|---|---|
| **1. Boot** | Start `TAU DEV 222`, start the playlist, stay on Now Playing. | **P1** |
| **2. Commit window** | Press X once (Scope). **Within 3 seconds** Quit from the Pocket menu. Start the core again. **Expect: Winamp Bars** (Scope never ran 5 s, so it was not saved). | |
| **3. Seven meters, presets** | Press X through Scope, Chladni, Layered Wave, VU Master, Waterfall, Sound Map, TEST 08 ... TEST 12 (**11 presses**, about a second apart). On Scope open Configure > Preset: the factory presets are listed. At **TEST 12** open Configure, change one parameter, go back, **wait 10 s** watching it, then Quit and start again. **Expect: TEST 12 on screen with your change kept.** | |
| **4. The long walk** | Press X **51 times** to reach **TEST 63** (names TEST 13 ... TEST 63; a few presses fast, in bursts; music playing). **Stop at TEST 63, do not press X again.** | **P2** |
| **5. Safe boot** | On TEST 63 **wait 10 s** (it is saved). Press X **once**: HANG TEST. It freezes the picture after about 200 pictures; the hardware watchdog cuts it, three strikes put it out and **Winamp Bars takes over** (up to about 10 s). | **P3** |
| **6. Restart after the hang** | Quit from the Pocket menu and start again. **Expect: TEST 63** (the last meter that ran cleanly), **not** HANG TEST. | **P4** |

If the player stays frozen for more than 10 s in step 5, hold the power button and start again: no Quit happened, so the card still holds step 3's saved state and **TEST 12 (with your change)** is the expected start. Say so in your note.

## What I read from the photos (good values)
| Photo | Rows | Good |
|---|---|---|
| **P1** (boot) | `MTOC STATE 1`, `COUNT 64`, `ERR 0`, **`SRC 1`** (the user bin), `READ` | `MSEL ROWS 64`, **`MSEL NOTUNE 2`** (the two collision rows), `MWIN LOADS 0`, `DRY`, `UNDER RING`, `HW UNDER EV` all 0. `MTOC READ` is the 64-row table read: DEV 221's 7-row read was 3.0 M cycles. |
| **P2** (after about 52 swaps) | `MWIN LOADS`, `ERR`, `REFUSED`, `STRIKES`, `READ FAIL`, `LOAD MAX`, `Q MAX` | `LOADS` about 52; `ERR`, `REFUSED`, `STRIKES`, `READ FAIL` **0**; `DRY`, `UNDER RING`, `HW UNDER EV` **0**; **`LOAD MAX` below DEV 221's 9.7 M (146 ms)** (this is the 4 KB read change); `MSEL SAVED` follows TEST 63 once it has run 5 s. |
| **P3** (after the cut) | `MWIN STRIKES`, `MSEL ABORTS`, `PACKS STRUCK` | `STRIKES` 1 or more, `ABORTS` 1, the player alive on Winamp Bars, **`MSEL SAVED` still TEST 63**. |
| **P4** (after restart) | `MSEL SAVED`, `MTOC` | the start was on TEST 63; `MTOC STATE 1`, `COUNT 64`. |

## Bad signs (tell me which step)
A refused row (`MWIN ERR` not 0), `READ FAIL` above 0, any of `DRY` / `UNDER RING` / `HW UNDER EV` moving, a freeze in steps 2 to 4, the restart in step 6 landing on HANG TEST, step 2 landing on Scope, step 3 forgetting your change, `MTOC READ` of tens of millions of cycles.

## Not covered here (a second visit if you want them)
METR user presets for a pack meter (needs a `tau-assets.bin` with a METR section), the old decimal preset folders beyond the Scope check in step 3, the Settings list for more than 7 rows (P2.5).

## When you are done
Tell me. I remove `Assets/tau/common/tau-meters.bin` (with your approval): a user bin overrides the core's own meter list in every dev core and would show 64 TEST meters in daily use.
