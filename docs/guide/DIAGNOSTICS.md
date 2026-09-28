# Diagnostics

Tau can show what it is doing inside, so that a problem can be described with numbers instead of "it stutters". There are two
levels: the **Info page** in every build, and a separate **Diagnostic Build** for testers.

Contents: [The Info page](#the-info-page-in-every-build) · [The Diagnostic Build](#the-diagnostic-build-a-separate-core-for-testers) ·
[Check](#check-one-button-that-produces-a-report-diagnostic-build-only) · [Meter Sweep and other tools](#meter-sweep-and-the-other-diagnostic-pages) ·
[Info QR export](#info-qr-export) · [Sending results](#sending-results)

Item names below were checked against `fw/settingsui.inc` and `fw/suite.inc`. Specification and report format:
[TEST_SUITE_SPEC.md](../TEST_SUITE_SPEC.md).

## The Info page (in every build)

**Start** opens Settings; the last group (titled SETTINGS) holds **Info**. It is read-only and
updates once a second. Rows, in order:

| Row | What it tells you |
|---|---|
| **FIRMWARE** / **FPGA REV** | The player version and the version of the FPGA design under it. They must belong together. |
| **SDRAM WINDOW** | `OK` when the Pocket's SDRAM was found and passed its start-up check. |
| **WINDOW READ** | Cycles for one read of that memory. Normal is well under 400. |
| **FREE RAM** | Spare on-chip memory. |
| **COLD IMAGE** | State of the cold-code image loaded from PSRAM at boot (an `E` code if it could not run). |
| **TRACK** | Format, bitrate and sample rate of what is playing. |
| **COVER** | The last cover-decode reason code, if a cover could not be shown. |
| **UNDERRUNS** | Times the audio ran out of data since start. A track change or a screenshot adds one; a count that climbs while music plays untouched is a problem. |
| **DRAW STALL** | Milliseconds the CPU waited on the screen drawing, since start. |
| **LOAD MS** | The last track load in four parts: header, size probe, album art, total. Tells you if a slow start is the cover. |
| **LIBRARY** | Library load state and error code. |
| **METER YIELD** | Seconds the meters have been held off to protect audio (live and worst since boot). |
| **VBLANK** | Display frame rate the firmware sees (about 60/S on a healthy core). |
| **SPECTRUM** | Whether the hardware spectrum filter bank is present. |
| **BEAM** | Share of meter updates that had to wait for the display beam. |
| **CHLADNI** | Chladni meter's own timing and engine probe state. |
| **CPU LOAD** | An estimate of CPU busy time. **Known limit:** it currently reads 100% in every state, so it cannot show headroom. Use the per-stage decode percentages (Decode Sweep) or the speed at which audio breaks up instead. |
| **TIM1 COVER** | Pre-scaled covers loaded, milliseconds, and an error code: `E0` fine, `E6` no drawing engine found, `E7` mailbox timeout, `E8` the copy never landed. Falls back to the embedded JPEG on any error. |
| **MP3 WINDOW** | The hardware MP3 window unit: `HW <slots computed> SLOTS <n> BAD <n> TMO`. `BAD` and `TMO` must be 0; if the unit ever disagrees with software it is switched off and software decodes. |
| **THEME FILE** / **METER FILE** | `NONE`, `n LOADED`, or `E<code>` for `tau-assets.bin`. |

In the Diagnostic Build, **A** on the Info page shows the values as a QR code (see [Info QR export](#info-qr-export)).

## The Diagnostic Build (a separate core, for testers)

A second core, **TAU Diagnostic Build**, is released beside the normal one (`alfatreze.TAU_DIAGNOSTIC_<version>_<date>.zip`). It is
the same player on the same FPGA design, with the test menus switched on under **Settings > Diagnostics**. It installs next to TAU
and has its own settings; use TAU for everyday listening, because the stress tests deliberately load the memory system.

| Menu | What it is for |
|---|---|
| **Tests > Window test** | Writes and reads back a pattern in the extra memory the player uses. Expect `PASS 89`. |
| **Tests > Read / Write cycles** | Timing of that memory as `fastest/average/slowest` cycles. Expect about 48/57/335 read and 31/38/350 write. |
| **Tests > Cold code test** | Runs the code kept in PSRAM and checks it. |
| **Tests > Clear counters** | Resets the underrun and draw-stall counters. |
| **Stress > Level** | Adds memory traffic (R1 light, R2 and R3 heavy) while music plays, to look for glitches. |
| **Stress > Soak** | Runs that traffic for 5 to 60 minutes and reports pass or fail. |
| **Stress > Status** | Live result: passes, failures, early and **late** underruns, slowest access. Late underruns must stay 0. Early ones follow track changes and are not a fault. |
| **All Speeds** | Adds speeds above 1.20x (up to 2.50x) to the speed list, for experiments. Off at every start. |
| **Check** | One-button report, below. |
| **Blit Test** | Exercises each drawing-engine opcode on a full-screen page, no audio needed. |
| **Meter Sweep** | Measures the draw cost of every meter, below. |
| **Meter Trace** | Records a meter's inputs so a run can be replayed on a computer. |
| **Decode Sweep** | Batch decode-cost measurement over a library queue (present only in the profile build variants). |

**A run:** open the core, start music, open the test or stress page, note the result, then **Quit** to the menu (some results are
only saved when you quit).

## Check: one button that produces a report (Diagnostic Build only)

**Settings > Diagnostics > Check** runs a fixed set of checks while music keeps playing: the SDRAM and PSRAM memory tests and
their speed, the cold-code path, the library, playback counters and what the start-up found. The result page lists each check as
PASS, FAIL or SKIPPED (playback is skipped if nothing is playing) and gives a verdict. **B** stops a run, **Y** runs it again, **A**
shows the report as a **QR code**.

The Diagnostic Build has four profiles, chosen on the first page with the d-pad (Up/Down picks the row, Left/Right changes it):

| Profile | What it runs |
|---|---|
| **USER CHECK** | About 30 seconds: the base checks |
| **STANDARD** | Adds 10 track changes, 20 cold-code runs and three 30-second stress levels, about 6 minutes |
| **FULL** | STANDARD plus a soak |
| **ENDURANCE** | The soak alone, with a cold-code test every minute |

The soak length (5, 15, 30 or 60 minutes) and stress level (R1-R3) can be set; FULL starts at 5 minutes and ENDURANCE at 30. While
a long profile runs you can leave the screen alone; **B** stops it and switches the stress traffic off. A `*` after a PASS or FAIL
on the playback and blit-storm rows means audio was not continuous for the whole window, so treat that verdict with care.

Take a screenshot of the result page and of the QR page (**Menu + Start**). The QR code carries the whole report (build, memory
timings, load times, library, error codes) and is read exactly from the screenshot; the 36-character code under the verdict is a
short fallback. When you **Quit** the core, the Pocket also saves a four-number summary in
`Settings/<core>/Interact/_core/interact_persist.json`.

Known: the **Track changes** check currently fails (0 of 10 done). This is a pre-existing, not yet explained problem with the
test; playback and track changing themselves work.

To decode a report on a computer:

```bash
python3 tools/decode_tau_suite.py --qr screenshot.png     # needs opencv-python (or pyzbar / zbarimg)
python3 tools/decode_tau_suite.py --interact persist.json
python3 tools/decode_tau_suite.py --code <short code>
```

Tau Omega (the companion app) can also decode these reports and list the card's screenshots.

## Meter Sweep and the other diagnostic pages

**Settings > Diagnostics > Meter Sweep** measures every meter in turn with the real hardware counters (about 10 seconds per meter)
and finishes with one QR code carrying a record per meter (draw stall, SDRAM busy share, underruns), so meters can be compared
like for like. Start it with **A**; it uses the whole screen so the meters draw on the real player screen. When it finishes it
reopens on its own result page: **B** closes, **A** shows the QR code. Method and the cost budget it checks against:
[METER_MODULE_SPEC.md](../METER_MODULE_SPEC.md) and `tools/meter_cost_estimate.py`.

**Blit Test** runs each drawing-engine operation in turn on a full-screen page (no audio needed); **Meter Trace** records inputs for
replay (decoded by `tools/decode_tau_suite.py`).

## Info QR export

In the Diagnostic Build, **Settings > Info > A** shows the Info values as a titled QR code (firmware and FPGA revision, MP3 window
timing, free RAM, underruns, draw stall, load time, CPU load). Screenshot it and decode as above. The meter **Configure** page can
export its settings as a QR the same way.

## Sending results

Open an issue at <https://github.com/alfatreze/Tau-Alpha/issues> and include:

1. Which core and version (TAU or Diagnostic Build, and the version on the Info page), and your Pocket firmware version.
2. What you did and what you expected.
3. **Screenshots**: press **Menu + Start** on the Pocket. They are saved as PNG files on the SD card in `Memories/Screenshots/`. Take
   one of the Info page and one of the result page. (The screenshot buttons also reach the player, so a track may pause; tap **A** to
   carry on. That adds one to the underrun count.)
4. For a failed test, the row that failed and the numbers it showed, and how long the test had been running.
5. A short description of the music: format, bitrate, whether it has a cover and its size.

A `PASS` on the Info and Tests pages together with a screenshot is enough for a good report; you do not need to send anything else
from the card. With the Check it is even simpler: screenshots of its result page and QR page are the report.

## Related

[Performance](../PERFORMANCE.md) (how the numbers are measured) · [Technical specification](../TECHNICAL_SPEC.md) ·
[Developers](../DEVELOPERS.md)
