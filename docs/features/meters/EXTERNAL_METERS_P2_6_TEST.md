# TAU DEV 225: the NORMAL build on a Pocket (P2.6), ONE session of about 15 minutes

**Why:** every Pocket test of the external-meters work (DEV 220 to 224) used the **Diagnostic Build** (`DIAG=1`). The **normal build** (`DIAG=0`: the About page, the support Report, no Check, no Info table) with these features has never run on a Pocket, and it is the build most people get. This session is that first look. **Nothing in the firmware changed since DEV 224** (only host tools, manifests, docs and the release scripts): same sources, same bitstream `wdog-w3` (`4D50331B`), `DIAG=0` instead of `DIAG=1`.

**Package (built, NOT installed):** `work/diagnostics/tau-dev-225/pocket` (zip and `tau-compat.json` next to it). `tau.rom` SHA-256 `da9d9b90...6343`, `bitstream.rbf_r` `4ce03b2c...2245` (the same bitstream as DEV 224), `tau-packs.bin` `2beefdc2...c381`. Version in the ROM: `0.7.1-dev.225+7a82c24d`. Flags: `PLAYER2=1,FLAC2=1,LPC=1,PACKS=1,DIAG=0,RAM_192K=1,CLK66=1,SDRAM_BUSY=1`. Free hot RAM of this build: **5,840 B** (the same as DEV 224's normal build; the Diagnostic Build is 2,880 B).

**Card:** install with `tools/install_dev_core.py` (dry run, your approval, `--card /Volumes/Pock`, `--remove "alfatreze.TAU DEV 224"`). **Before the session take the P2.5 test bin off the card** (the installer never touches `Assets/tau/common/tau-meters.bin`): part A needs the shipped seven only. Part B (optional) puts a small user bin back.

Start a long playlist and keep it playing. **Pause before every screenshot** (a screenshot while playing adds one dropout).

## What the normal build can and cannot show
No Info page, no Check, no Settings > Diagnostics, no `MTOC` / `MWIN` / `MSEL` rows (they are Diagnostic-Build rows). What it has: the **About page** (Settings > About: scrolls with Up/Down, the sections VERSION ... ON THE CARD, HEALTH), and the **support Report** (Select+Start on any screen; the Pocket saves the picture; decode it with `python3 tools/decode_tau_suite.py --grid <png> --table`). The Report carries only the support set of counters. The meter rows of that set: `METER CUR`, `PACKS RUN` (a bit for the row whose pack is in the window now: 0 when Winamp Bars, the built-in, is on screen), `PACKS SEEN` (rows in the table of contents), `PACK FILE` (0 = a bin is in use, 255 = none found, else why the last was refused), `PACK SRC` (**2** = the core's own `tau-packs.bin`, **1** = the user's `tau-meters.bin`), `PACK ERR`, `PACKS STRUCK`; also `SET LAYOUT`, `SET MIGRATED`, `UNDER RING`, `DRY`, `SLOT ERR`, `ST FAILS`, `CAP UNDER`.

## Part A: the seven shipped meters (no user bin)
| Step | What you do | What you should see |
|---|---|---|
| **1. Boot** | Start `TAU DEV 225` from the core list. | Splash, then Now Playing with a track playing as usual. Settings > About: **VERSION `0.7.1-dev.225`**, BUILD `7a82c24d`, CHANNEL **DEV**, HARDWARE **`4D50331B`**. |
| **2. X through the seven** | With music playing press **X** eight times, slowly at first, then quickly. | Winamp Bars, Winamp Scope, Chladni, Layered Wave, VU Master, Waterfall, Sound Map, then **Winamp Bars again** (it wraps). Every meter draws and moves with the music; **no freeze, no black screen, no stray picture left over**, the music does not stutter. |
| **3. The METER list** | Settings > Appearance > METER (A). | The list shows the **same seven in the same order**, a preview each, the title line **`1 / 7`** style indicator, the star on the meter on screen. Pick **Chladni** (A): Appearance reads CHLADNI, Now Playing shows Chladni. |
| **4. Configure and presets** | Settings > Appearance > METER > CONFIGURE (or Start+Y). Change a value; **Select+X** on Now Playing for the next preset. | Parameters change the live meter; presets cycle (names appear). **Preset names are listed**, not just DEFAULT. |
| **5. Persistence** | Pick **Waterfall** in the list, go to Now Playing, **wait 10 s**, Quit the core from the Pocket menu, start it again. | It comes back on **Waterfall**. (A pick followed by a Quit within 3 s keeps the previous meter: the commit rule, unchanged since DEV 222.) |
| **6. About and the Report** | Pause. Settings > About, Down to the end (ON THE CARD, HEALTH). Photograph the About page; then Select+Start and photograph the Report. | see below |

## Part B (optional, 5 more minutes): a user bin
Build a 20-row bin on the host (it is already at `work/p26/user-bin-20/tau-meters.bin`, SHA-256 `d7107ac3...a2e7`; made with `python3 tools/make_synthetic_meter_bin.py work/p26/user-bin-20 --rows 20`: the seven shipped meters, then TEST 08 ... TEST 20 that run as real meters under new ids). **With your approval** copy it as `Assets/tau/common/tau-meters.bin` and restart the core.
| Step | What you do | What you should see |
|---|---|---|
| **7. The list grows** | Settings > Appearance > METER. | **20 rows**, the list scrolls (position `n / 20`), X on Now Playing walks the same order. Pick **TEST 12** (a Waterfall): it runs. |
| **8. Restart on a user meter** | Wait 10 s on TEST 12, Quit, start again. | Back on TEST 12. |
| **9. Remove it** | Delete the file again, restart. | The seven shipped meters; **a saved TEST 12 falls back to Winamp Bars** (the meter is not in the list any more), nothing stuck. |

## What I read from the photos
| Row (Report) | Good (part A) | Good (part B) |
|---|---|---|
| `PACK FILE` | **0** | **0** |
| `PACKS SEEN` | **7** | **20** |
| `PACK SRC` | **2** (the core's own `tau-packs.bin`) | **1** (the user's file) |
| `PACK ERR`, `PACKS STRUCK` | **0**, **0** | **0**, **0** |
| `PACKS RUN` | one bit set for the meter on screen (`00000004` Chladni, `00000020` Waterfall ...), `0` on Winamp Bars | the same, by row |
| `METER CUR` | the table index of the meter on screen | the same |
| `SET LAYOUT` | **2**; `SET MIGRATED` **0** (DEV 224 already converted the settings) | same |
| `UNDER RING`, `DRY`, `CAP UNDER` | 0 (`CAP UNDER 1` is the screenshot itself) | 0 |
| `SLOT ERR`, `SLOT OPEN FAIL`, `ST FAILS` | 0 | 0 |
About page: METER = the meter on screen, SETTINGS = **OK**, AUDIO **NO DROPOUTS** (before any screenshot), CARD READS **NO ERRORS**.

## Known wart you will see (not a failure of this session)
The About page's **ON THE CARD > METERS** row reads **`0 OF 7 LOADED: SOME FAILED`** on Winamp Bars and **`1 OF 7 LOADED: SOME FAILED`** on any other meter. **Cause (code, not yet seen on a Pocket):** the row counts the packs resident in the window and compares with the rows in the table; since P2.2 only the meter on screen is resident, so it can never equal the row count. It is a wrong sentence, not a fault (`PACK FILE 0`, `PACK ERR 0` in the Report say everything is fine). It needs a one-line firmware change (what the row should say is the owner's call); it is **not** fixed in DEV 225 so that this build is exactly DEV 224's firmware as the normal build.

## Bad signs (tell me which step)
A black screen or a hang at boot (the thing this session exists to rule out); a meter that does not draw or leaves part of the previous picture; a freeze while pressing X quickly; the list order differing from X; a preset list that shows only DEFAULT; a meter that does not come back after the restart; `PACK FILE` not 0; `PACK ERR` or `PACKS STRUCK` above 0; `SET LAYOUT` not 2; `UNDER RING` or `DRY` above 0 before the screenshot; About showing SETTINGS other than OK.

## What is not visible in the normal build
How long the boot read of the meter list took, how long each meter load took, the selection commit and abort counters, the hash of the saved meter, the strikes per meter, the queue wait before a load: all of that is `MTOC`, `MWIN`, `MSEL` in the Diagnostic Build and was measured there (B-1064, B-1065, B-1069). Part B's 20 rows are too few to say anything about the 64-row boot cost; that was DEV 224's job.

## When you are done
Tell me. If part B ran, delete `Assets/tau/common/tau-meters.bin` (with your approval): a user bin overrides the core's own list in every dev core.
