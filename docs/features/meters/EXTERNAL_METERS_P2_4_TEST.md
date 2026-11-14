# External meters, phase P2.4: the Pocket test (registry rewrite, commit rule, 64 rows)

> **TAU DEV 222 finding (2026-11-14):** Configure > Preset on Scope listed only DEFAULT although the decimal preset folders were on the card (B-1059). **TAU DEV 223** retries the preset files when Configure opens. Everything below applies to 223 (version `0.7.1-dev.223+622298c4`, package `work/diagnostics/tau-dev-223/`, not installed).

## 0. Preset files, the retry (2 minutes, first)
With the card's decimal folders `tau-presets/12..18/`: X to Scope, open Settings > Configure > Preset: **FLUID, CLASSIC, BOUNCY ... are listed** (not only DEFAULT), for each of the seven shipped meters, with the 64-row bin active and with music playing. Walk X quickly through several meters first (the case that failed), then open Configure on each. Read `MPX` rows on the Info page: `MPX FAILS` 0, `MPX ERR` 0 (10 = the governor was busy, 11 = open failed, 12 = read failed). A meter with no folder (TEST rows) lists DEFAULT only, and opening Configure costs two failed opens (a few tens of ms), not a hang.

**Build (222; use 223, see above):** `TAU DEV 222` (version `0.7.1-dev.222+3da6034e`), Player 2, Diagnostic Build, `PACKS=1`, bitstream `wdog-w3` (`4D50331B`, no FPGA change). Package: `work/diagnostics/tau-dev-222/` in the worktree `p24-registry-0a8b56` (zip `alfatreze.TAU DEV 222_0.7.1-dev.222_dev.zip`). **Not installed:** the owner approves the card write (`tools/install_dev_core.py`, dry run first, `--card /Volumes/Pock`; evidence B-1058). Stress bins: `python3 tools/make_synthetic_meter_bin.py work/p24/synthetic` writes `tau-meters.bin` (64 rows) and `tau-meters-hang.bin` (row 8 hangs) and a README. **The user bin is copied by the owner, by hand, to `Assets/tau/common/tau-meters.bin` (exactly that name); it is in no package.** Delete it to go back to the seven shipped meters. Pause before photographing the Info page (a screenshot while playing costs one dropout).

## 1. Seven meters, nothing user-installed (5 min)
Boot with no `tau-meters.bin`. Info: `MTOC STATE 1`, `COUNT 7`, `SRC 2`, `MSEL ROWS 7`, `MSEL SKIPPED 0`, `MSEL NOTUNE 0`, `MSEL SAVED` = the id hash of the meter on screen once committed (Winamp Bars `7DDDFE63`). Press X through the seven, 3 laps with music: no dropout counter moves, `MWIN ERR 0`. **New load-time reference:** `MWIN LOAD MAX` against 146 ms (DEV 221, 1 KB reads) = 9.7 M cycles: with 4 KB reads expect clearly lower; `MTOC READ` (boot) lower than DEV 221's 2.8 M.

## 2. 64 rows (10 min)
Copy `tau-meters.bin` (64 rows). Boot. Info: `MTOC COUNT 64`, `MSEL ROWS 64`, `MTOC READ` (write it down: the 80 KB TOC and preview read), `MTOC ERR 0`. X: Winamp Bars, Scope, Chladni, Layered Wave, VU Master, Waterfall, Sound Map, then TEST 08 ... TEST 64, then back to Winamp Bars (TOC order, wraps). Each TEST row draws like one of the real meters. About 130 presses with music: `MWIN READ FAIL 0`, `MWIN ERR 0`, `DRY`, `UNDER RING`, `HW UNDER EV` flat. **Settings > Appearance > Meter** lists only the FIRST SEVEN rows (scrolling is P2.5): it must not break; with TEST 30 on screen the list has no star and the row shows the meter's name.

## 3. Restart persistence and the commit rule (10 min)
1. X to TEST 12, **wait 10 s watching it**, Quit from the Pocket menu, start again: TEST 12 is on screen (`MSEL SAVED` = its hash, `MSEL COMMITS` at least 1). Same for a tuned meter (Configure, change one parameter, wait 10 s, Quit, restart: still tuned).
2. **Commit window:** X to another meter and Quit within 3 s: the next start is on the previous meter (the saved word follows only after 5 s: `MSEL CLEAN` counts 0..5, `MSEL PEND` shows the waiting hash).
3. Choose Winamp Bars: it commits at once (Quit immediately: next start on Winamp Bars).

## 4. Safe boot with the hang row
Copy `tau-meters-hang.bin`. Select Scope, wait 10 s (saved). X until row 8 (HANG TEST): after about 200 pictures the hardware watchdog cuts it, three strikes put it out, Winamp Bars takes over (`MWIN STRIKES` 1 or more, `MSEL ABORTS` 1, row marked `!` in the list if it is among the first seven; it is row 8, so only `MWIN LAST ROW`/`MWIN ERR 40`). Quit and start: **Scope**, not Winamp Bars and not HANG TEST. A power cycle after a hard hang proves nothing (nothing is saved then); say what happened.

## 5. METR user presets for a pack meter (only if cheap)
With a `tau-assets.bin` that has a METR section for Scope or Chladni (`tools/tau_assets.py`), selecting that meter shows the METR presets (Configure > Preset), as Winamp Bars already did.

## 6. Preset folders
Existing cards keep `tau-presets/13/` ... `18/` (decimal): presets still list for the seven. A folder named by the hash (`tau-presets/143acb80/`, written by `install_factory_presets.py`) wins when both exist.

## Info rows (good and bad)
| Row | Good | Bad |
|---|---|---|
| `MSEL SAVED` | hash of the last clean meter | stays on an old meter after 10 s of clean running |
| `MSEL PEND`, `MSEL CLEAN` | 0 or the waiting hash, 0..5 s | stuck below 5 while the meter draws |
| `MSEL COMMITS`, `MSEL ABORTS` | commits grow with each clean choice; aborts only for hang, strike, refusal | aborts without a failure |
| `MSEL ROWS`, `MSEL SKIPPED` | rows X reaches; 0 skipped | |
| `MSEL NOTUNE`, `MSEL CUR NOTUNE` | 0 (a collision bin: `--collide`, 2 and 1 on TEST 09/10) | |
| `MTOC READ`, `MWIN LOAD MAX` | below DEV 221's 2.8 M and 9.7 M | above: tell the session |

## What is not in this build
Settings list scrolling (P2.5); crashed-last-time across a RESET (needs a value that survives one, unverified); the unavailable-row greying in the list.
