# External meters, phase P2.2: the Pocket test (window loader and the v2 bin)

What it answers (plan P2.2 exit): do the shipped seven meters run when selected from a **v2 bin**, one pack at a time in ONE window, with music playing, **no dropout across 100 swaps**, and do the `MWIN` load figures match P0's (typical 1.48 M cycles = 22 ms, worst 6.49 M = 97 ms, flush 804 cycles)? Does the selection, preset and tuning survive Quit and restart?

**Build:** `TAU DEV 220` (version text `0.7.1-dev.220+<commit>`), Player 2, Diagnostic Build (`DIAG=1`), `PACKS=1`, bitstream `wdog-w3` seed 1 (`4D50331B`, the one v0.7.0 and v0.7.1 ship: no FPGA change). Its `tau-packs.bin` is a **v2 bin** of the seven shipped meters (Winamp Bars first, flagged as the built-in fallback). Package: `python3 tools/package_dev_build.py --number 220 --variant player2 --build-flags "PACKS=1 RAM_192K=1 CLK66=1 SDRAM_BUSY=1" --bitstream <the wdog-w3 ap_core_s1.rbf>` (the exact path and SHA-256s are in the audit entry B-1053). **Not installed by the session that built it: the owner approves the card write** (`tools/install_dev_core.py`, dry run first, `--card /Volumes/Pock`).

**What is different from P0:** the pack-swap probe is gone; the player itself loads a pack **when you select its meter** (X on Now Playing, the Settings METER list, or the saved meter at boot). Winamp Bars runs from its built-in code and loads nothing. Nothing is read from a pack at boot, only the bin's header, table of contents and previews.

## 1. Boot and the TOC (2 minutes)
1. Boot `TAU DEV 220`, wait for the player. Open the Info page (Diagnostic Build: Settings > Info) and read the `MTOC` rows. **Pause first**: a screenshot while playing costs one dropout.

| Row | Good | Bad |
|---|---|---|
| `MTOC STATE` | **1** | 0 = no bin found (no `tau-packs.bin` on the card for this core), 2 = every bin tried was refused |
| `MTOC COUNT` | **7** | |
| `MTOC ERR` | **0** | the reason a bin was refused: 2 magic, 3 version, 4 toc_crc (damaged), 6 layout, 9 duplicate id, 12 pack too big for the window, 20 no built-in fallback row |
| `MTOC SRC` | **2** (the core's own bin; 1 would be a `tau-meters.bin` in `Assets/tau/common/`) | |
| `MTOC READ` | the boot cost in cycles (66.667 MHz counter): about 1 M or less for 7 meters | tens of millions: the TOC read is slow, tell the session |
| `PACKS SEEN` | 7 | |
| `MWIN LOADS` | **0** at this point (nothing is loaded before a meter is selected) | |

2. The Settings METER list shows the seven names and their previews as before (names and previews now come from the table of contents).

## 2. About 100 meter swaps with music playing (10 minutes)
1. Start a long playlist (a few albums). Note `DRY`, `UNDER RING`, `HW UNDER EV` and `HW UNDER TK` on the Info page **before** (pause, photograph, resume).
2. On Now Playing press **X** repeatedly, walking through the seven meters: about **100 presses** (a whole walk is 7; do 14 or more walks). Let each meter draw for a second or two now and then; for a few, open Configure and change a parameter (that is the tuning, section 3).
3. Listen at every press for a click or dropout. A **short blank and one full repaint** at each press is expected (the load is about 20 ms; it never happens inside a picture).
4. Pause, read the `MWIN` rows:

| Row | Good | What a bad value means |
|---|---|---|
| `MWIN LOADS` | about the number of presses minus those on Winamp Bars (6 of every 7) | |
| `MWIN LOAD` | the last load in cycles; **typically 1.5 M** (P0: 1.48 M, 22 ms) | |
| `MWIN LOAD MAX` | **at most about 6.5 M** (P0: 6.49 M, 97 ms); Layered Wave is the biggest pack and the slowest | far above: the card read competes with something |
| `MWIN HASH` | the id hash of the last pack loaded (Layered Wave is `58128034`, Waterfall `17E60776`, Chladni `7471CC21`, Scope `143ACB80`, VU Master `0D8C03D3`, Sound Map `0C82D9F0`) | |
| `MWIN ERR` | **0** | 11 pack header against its table row, 22 ABI/tau_min, 24 not linked for the window, 25 sizes, 26 CRC, 40 struck out, 41 descriptor |
| `MWIN REFUSED` | **0** | a pack was refused and Winamp Bars was shown instead |
| `MWIN STRIKES` | **0** | a pack hung (cut by the watchdog) or overran its limit three times |

5. **Decision rule:** `MWIN ERR`, `MWIN REFUSED`, `MWIN STRIKES` stay 0, **`DRY` / `UNDER RING` / `HW UNDER EV` / `HW UNDER TK` do not grow across the 100 swaps** (P0 had 22 dry events in 22 swaps from the probe's own timing; the point of P2.2 is that selection-time loading adds none), and no meter ever draws the wrong picture (a stale instruction cache would show as garbage or a crash on the first tick after a swap). Any growth in the dropout counters: note which presses, the length of `MWIN LOAD MAX`, and tell the session; the fix is to schedule the load around the music reads (plan 3.3).

## 3. Meter, preset and tuning across Quit and restart (5 minutes)
Persistence now stores the selected meter **by its id hash** (layout 2).
1. Select **Chladni**. Open Configure and change **Line width** (or any parameter), back to Now Playing. Select another meter (say Layered Wave), Configure, change one parameter (a second tuned meter).
2. Select **Chladni** again so it is on screen, then **Quit** from the Pocket menu (not a power-off) and start `TAU DEV 220` again.
3. **Good:** Chladni is on screen, with your changed parameter (the Configure value, not the default). X to Layered Wave: it also has your change. A preset file you had chosen for a meter (`tau-presets/`) comes back for the meter on screen only; the other meter starts on its default preset (layout 2 remembers the preset of the meter shown at Quit, plus the two tunings).
4. **Not remembered by design:** tunings of a third meter (only two are kept, the oldest is dropped), and which preset the not-shown meters had.

## 4. Damaged bin (optional, if the second package is prepared)
A second package `TAU DEV 220` with a deliberately damaged table of contents (one byte of `tau-packs.bin` between byte 40 and the end of the previews flipped) is **not** built by default; ask the session to prepare it (`python3 tools/player2/build_packs.py` then flip a byte, same packager). **Good:** `MTOC STATE 2`, `MTOC ERR 4`, only Winamp Bars in the METER list, the player otherwise normal. A damaged pack (a byte flipped inside a pack's image, after the table of contents) is a different case: the player boots normally, and selecting that meter shows Winamp Bars with `MWIN ERR 26` and `MWIN REFUSED 1`.

## Known limits of DEV 220 (so nothing here surprises you)
- **METR user presets (`tau-assets.bin`) apply to Winamp Bars only.** A card whose `tau-assets.bin` carries a METR section (user presets per meter, written by `tools/tau_assets.py` or Omega) still shows those presets for Winamp Bars; the six pack meters show only their own DEFAULT preset, as if the METR section had no entry for them. Preset **files** (`tau-presets/<n>/`) still work for the shipped seven meters. P2.4 decides how METR comes back for pack meters (a pack is not loaded at boot, so its parameters are not known when METR is read).
- **At most 7 meters are selectable** (the first 7 rows of the bin), however many the bin holds; the list is not scrolling yet (P2.5).
- **A tuning of a third meter is dropped** (two tunings are kept), and a `layout 1` card loses its saved tunings on the one-time migration (the meter and its preset survive).
- **No commit rule or safe boot yet:** a meter that hangs is cut by the watchdog and struck out for that session, but the saved selection is not protected (P2.4).

## 5. What to send back
Photographs of the Info page (the `MTOC` and `MWIN` rows and `DRY`, `UNDER RING`, `HW UNDER EV`) before and after section 2, and one sentence for section 3 (kept or not). The session records them in `docs/AUDIT_TRAIL.md` with evidence label POCKET.

## Host side (done in this branch, nothing run on a Pocket)
`sim/test_p2_meter_window.py` (the real `fw2/ui/meters.c` against stubbed PSRAM, window, `fence.i` and call: the order write, then `fence.i`, then run; every refusal; 200 swaps; boot with no bin, a damaged bin, the user bin preferred; persistence), `sim/test_p2_meterbin.py` (the reader), `sim/test_p2_diag_split.py`, and the ported meter tests (`test_p2_mpre`, `test_p2_mpreset_meters`, `test_p2_meter_stats`, `test_p2_meter_cmd_budget`).
