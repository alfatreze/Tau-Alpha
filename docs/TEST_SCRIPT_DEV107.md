# User test script: TAU_DEV_107 and the alpha.4 cores (written 2026-10-07, B-648)

A lot has been built since the last hardware pass and **most of it has never run on a Pocket**. This script covers every change since the alpha.3 / DEV 93 / DEV 91 results, in the order of risk. Anything marked **FIRST RUN** has no hardware evidence at all. Tick the box, write a number or a word in the Notes column, and send the screenshots listed at the end.

Estimated time: P0 and P1 about 60 minutes; P2 about 60 minutes; the soak (section 14) runs 30 minutes by itself and can overlap with listening.

## 0. What is on the card and what it can do

| Core | Bitstream | Firmware | Use it for |
|---|---|---|---|
| `TAU_DEV_107` (**main target**) | `halcyon-b639` seed 2: shipped Cymo bundle + hardware gain stage + 16-bit I2S switch + Halcyon engine | Diagnostic Build, current main (Menu change, Configure fix, Halcyon page, PRST presets, tempo row) | sections 1 to 14 |
| `TAU` and `TAU_DIAGNOSTIC` (alpha.4) | alpha.3 bitstream (`clut-rtl-b576` seed 2): **no** gain stage, **no** 16-bit switch, **no** Halcyon | alpha.4 firmware, built before the menu change | section 15 only |
| `TAU_DEV_105` | `gain-b615` (hardware gain stage only), old firmware | superseded by 107 | do not test; remove when you like |

**Expected differences between the cores (these are not bugs):**
- On `TAU` and `TAU_DIAGNOSTIC`: the rows **HW GAIN, 16-BIT OUTPUT, HALCYON** (Diagnostics) read **NO UNIT**; Settings > Audio has **no** HALCYON row; the Info export is reached through **Menu > Settings > Info > A** (the Settings group still exists there, Diagnostics sits inside it in the Diagnostic Build; the menu change is only in DEV 107).
- On `TAU_DEV_107` none of those rows may read NO UNIT. If one does, you are not on the 107 bitstream: stop and tell me.

## 1. Ground rules

- **Screenshots stop the track for a moment** (Menu+Start). The underrun counter then rises by 1 each time. Take every Info reading **before** the screenshots of a section, and write down the numbers in the notes.
- Start each section from a stopped or freshly started track unless the step says otherwise. Use headphones for the listening tests; a line out into a recorder is even better but not needed here.
- **Test media** (all on the card, carried across): the *Test Album* (MP3 and FLAC pieces, a 48 kHz variant, a speech clip: Mark Twain), *Hyperion* FLACs (real music, LPC-coded), the *Nausicaa* albums (covers, accented names), `cymo_loopback` (1 kHz tone, silence and inter-sample-peak files), the 96 kHz FLAC tracks 14 and 15 of the Test Album (refused by design) and track 13 (48 kHz), a speech FLAC (LPC), and `flac-tests`. A **24-bit FLAC** is needed for 9.1: if none of those is 24-bit, say so and I will make one.
- Controls reminder: **Start** (let go) opens the menu; **Start + Y** jumps to Meter > Configure; **Select** library; **X** cycles meter; **Y** cycles the *old* EQ preset (see 6.9); **L/R** accent colour; **Up/Down** volume (hold repeats); **Select + Y** fullscreen meter; **Select + X** next preset of the meter.
- **Do not skip a failure to carry on.** Write what you saw (the exact text on screen helps most) and continue with the next step.

## 2. P0 smoke on TAU_DEV_107 (10 min). Everything else depends on this

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 2.1 | Power on, start `TAU_DEV_107` from the core list | the core appears in the list with its name (the catalog caches were rebuilt); loading bar; player screen; no "Bridge not responding", no black screen | ☐ | seconds to player: |
| 2.2 | Look at the player screen | cover (TIM1 fast), title/artist/album, meter running, no leftover pixels | ☐ | |
| 2.3 | Start playback (A) on an MP3, then on a FLAC | clean audio, no clicks, time advances, meter moves | ☐ | |
| 2.4 | Up/Down volume: single presses, then hold | steps even in loudness (no jumps), hold repeats about 12 steps/s after a short delay, no clicks or zipper noise | ☐ | |
| 2.5 | Left/Right tap, then hold | previous/next track; hold seeks faster the longer you hold | ☐ | |
| 2.6 | Pause and resume | no click, no pop on resume | ☐ | |
| 2.7 | Open the menu (Start, let go), then close with Start again | opens on release, closes at once, **no leftover pixels** on the player screen afterwards | ☐ | |
| 2.8 | Menu > Info: read FIRMWARE, FPGA REV, COLD IMAGE, FREE RAM, UNDERRUNS | FIRMWARE 0.6.0, FPGA REV **4D50331A**, COLD IMAGE OK, UNDERRUNS about 0 | ☐ | rev: |

If 2.1 or 2.3 fails, stop and report: nothing else is meaningful.

## 3. The new main menu (5 min). FIRST RUN

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 3.1 | Start: read the menu rows | **APPEARANCE, AUDIO, PLAYBACK, DIAGNOSTICS, INFO** in that order; **INFO is the last row**; no SETTINGS row | ☐ | |
| 3.2 | Open each row and leave with B | Appearance, Audio, Playback, Diagnostics, Info each open; **B returns to the menu** (not to a "Settings" page) | ☐ | |
| 3.3 | From Diagnostics open Tests, Stress; leave with B twice | Tests and Stress return to Diagnostics; Diagnostics returns to the menu | ☐ | |
| 3.4 | In Info: Up/Down, hold Down, then B | wraps at both ends, hold repeats, B returns to the **menu**; the selection on the menu is still on INFO | ☐ | |
| 3.5 | Info > Stress status (Diagnostics > Stress > STATUS) then B | returns to Stress | ☐ | |
| 3.6 | Look at the hint line at the bottom of every page you opened | text is whole, never overdrawn by list rows | ☐ | |
| 3.7 | Left/Right on menu rows | Right opens like A; Left goes back like B | ☐ | |

## 4. Meter Configure page (15 min). FIRST RUN of the new layout and the Helios preview

Reach it with **Menu > Appearance > METER: CONFIGURE**, and again with **Start + Y** from the player screen.

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 4.1 | Open it. Look at the bottom | the list stops **above** the bottom action bar; the action bar text ("L/R ADJUST   B BACK" or "UP DOWN SCROLL ...") is whole; **6 rows** visible | ☐ | screenshot C1 |
| 4.2 | Look at the sides of the live preview | the preview box is exactly as wide as the row highlights; **no strip of different background beside it** | ☐ | screenshot C1 |
| 4.3 | Scroll the list to the end (Down) | scrolls, wraps, never draws into the action bar, scroll track visible at the right | ☐ | |
| 4.4 | Change the PRESET and METER rows (Left/Right) | preview changes at once; rows below change with the meter | ☐ | |
| 4.5 | For **each** meter (Winamp Scope, Winamp Bars, Chladni, VU Master, Layered Wave, and the others in the list): open it in Configure with music playing | the preview animates with the music; no frozen bars; no flicker; no garbage outside the box | ☐ | list the ones that fail: |
| 4.6 | Change a parameter of the Scope (trail) and of Bars (bands) | preview responds immediately and keeps animating | ☐ | |
| 4.7 | Watch the preview for 30 seconds on Layered Wave and Chladni (the full-repaint meters) | no tearing line moving through the preview, no flashing | ☐ | |
| 4.8 | B from the page opened from the menu; B from the page opened with Start+Y | menu route returns to Appearance; the Start+Y route **closes Settings** | ☐ | |
| 4.9 | Start while in the page | closes Settings | ☐ | |
| 4.10 | Leave the page, then return to the player screen | the meter on the player screen is correct (not blank, not old preview pixels) | ☐ | |
| 4.11 | Diagnostic only: last row EXPORT QR, press A | a report code appears (grid by default, X cycles to the QR code), any key closes | ☐ | |

## 5. Halcyon page (20 min). FIRST RUN, never drawn on a real screen

Open **Menu > Audio > HALCYON**. Play something with full-range content (a Hyperion track) before opening it.

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 5.1 | Open it | title **HALCYON**; header right shows **OFF**; a graph area on top; six rows WARMTH, BASS, VOCAL, PUNCH, SIBILANCE, AIR; a footer line; action bar with "UP DOWN ROW  L R ADJUST  L1 R1 PRESET  X A/B  Y RESET" | ☐ | screenshot H1 |
| 5.2 | Look at the graph | 0 dB line, fainter lines at ±6 and ±12 dB, vertical lines at 100, 1K, 10K, labels +14 / 0 / -14 and 100 / 1K / 10K; the **selected row's band is lit** (a lighter vertical stripe) | ☐ | |
| 5.3 | Look at the bottom | footer text fits on one line; nothing overlaps the action bar | ☐ | |
| 5.4 | Row WARMTH: Right x3 | value shows +3; header changes to **CUSTOM**; the curve **moves** (low end up against the top end, whole curve at or just under the 0 dB line); footer PREAMP changes to a small negative number (about -2 to -3 dB); **music continues without a click** | ☐ | |
| 5.5 | Down to BASS, Right x4 | bar fills to the right of centre, value +4; curve rises at 100 Hz | ☐ | |
| 5.6 | Down to SIBILANCE; Left at 0; Right x5 | cannot go below 0; stops at 5; the bar fills **from the left** (dip depth); the curve dips around 6.5K | ☐ | |
| 5.7 | Up from WARMTH | selection wraps to AIR | ☐ | |
| 5.8 | Y (reset) | all six back to 0; header CUSTOM or FLAT; the curve is (nearly) the flat line; footer reads PREAMP -0.0 DB (on this card it still says "17 HZ HIGH-PASS ON" and the curve may show a small dip, about -2 dB, at the far left although FLAT is a true bypass: known cosmetic, fixed in source, not on this card) | ☐ | |
| 5.9 | R1 repeatedly, reading the header each time | FLAT, **WARM, CLEAR, BASS, VOCAL, SPEECH, LOW VOLUME, SMOOTH, CLARITY, TEST IEM**, then FLAT again; L1 goes backwards | ☐ | |
| 5.10 | On each built-in preset read the sliders and the PREAMP footer | WARM +3 warmth +1 bass, about **-3.0 dB**; CLEAR -3 warmth +1 punch +1 air, **-2.5 dB**; BASS +1/+4/+1, **-4.7 dB**; VOCAL bass -1 vocal +3 sibilance 1, **-2.7 dB**; SPEECH -1/-3/+3/+1/2, **-3.0 dB**; LOW VOLUME +1/+5/sib 1/air +2, **-5.7 dB**; SMOOTH punch -2 sib 2 air -2, **0.0 dB** | ☐ | write any that differ: |
| 5.11 | Each preset step: listen | one short fade (about 10 ms dip) at the step, then clean audio: **no click, no burst, no level jump other than the tonal change** | ☐ | |
| 5.12 | **TEST IEM** (raw preset, 3 biquads): look at the sliders | the sliders keep their last values (raw presets have no controls); header TEST IEM; audio changes tone (bass lift, presence dip, air lift) | ☐ | |
| 5.13 | X | header **A/B: EQ**, the curve turns grey, audio goes back to the old EQ path (FLAT unless you cycled Y on the player); X again returns | ☐ | |
| 5.14 | B, then reopen the page | sliders show what you left; Start inside the page closes Settings | ☐ | |
| 5.15 | Quit the core, restart, reopen the page | **nothing was saved yet** (expected): controls back to 0, header OFF | ☐ | known gap |

## 6. Halcyon audio quality and robustness (25 min). FIRST RUN, most important listening

Use headphones. Compare against Halcyon OFF (Diagnostics > HALCYON cycles to OFF, or X on the page).

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 6.1 | FLAT preset ON (bypass) vs OFF, same passage | **identical**, no level change (FLAT is a true bypass) | ☐ | |
| 6.2 | WARM, CLEAR, BASS, VOCAL, SPEECH, LOW VOLUME, SMOOTH on a bright and on a bass-heavy track | tone changes as named; **loudness about the same** (the preamp gives back the boost); no distortion on loud passages | ☐ | which presets distorted: |
| 6.3 | Slider moves while playing, **fast** (hold Right on BASS from -5 to +5 and back) | smooth, no zipper noise, no clicks | ☐ | |
| 6.4 | LOW VOLUME and BASS at high volume on a loud, dense track | no clipping crackle; if crackle, note preset and volume | ☐ | |
| 6.5 | Silence between tracks and at pause with Halcyon ON | **dead silent**, no hiss, hum or motorboating (compare with OFF) | ☐ | |
| 6.6 | Start of a new track with Halcyon ON | no thump or click at the start; no residue of the previous track | ☐ | |
| 6.7 | Seek around (hold Right, then Left) with Halcyon ON | no pops | ☐ | |
| 6.8 | Pause for 10 s, resume | no click | ☐ | |
| 6.9 | While Halcyon is ON press **Y** on the player screen several times (old EQ preset) | **expected: no audible change** (the old EQ is out of the path while Halcyon is on); tell me if the old EQ still changes the sound | ☐ | |
| 6.10 | Infrasonic filter: play a track with strong sub-bass (below 30 Hz) with Halcyon on (any non-FLAT preset) vs FLAT | rumble below about 20 Hz is gone, music bass (40 Hz and up) unchanged | ☐ | |
| 6.11 | Halcyon ON during **44.1 kHz** and **48 kHz** files | clean on both; Info CYMO RESAMP shows LIVE for 44.1 kHz | ☐ | |
| 6.12 | Halcyon ON with speed 1.20x (MP3) | clean, no stutter | ☐ | |
| 6.13 | Info after 10 minutes of Halcyon ON: UNDERRUNS and the `ALL` count (Diagnostic Build) | **UNDERRUNS 0 ALL 0** (the engine uses about 34% of the clocks per sample, no CPU) | ☐ | numbers: |
| 6.14 | Switch Halcyon OFF and ON 10 times quickly with X on the page | no clicks, no hang, page still responds | ☐ | |

**First hardware result (2026-10-07, TAU_DEV_107):** every non-FLAT Halcyon setting sounded like fluttering wings (a 30 Hz glitch train). Root cause found and fixed in RTL (B-649: the engines' sample tick was 48,030.7 Hz instead of 48,000 on the 66.667 MHz clock); **the cores on the card still have the bug**, so steps 6.2 to 6.14 are expected to fail on `TAU_DEV_107` for non-FLAT settings. Re-run section 6 on the build that carries the fix. The old preset EQ (Y on the player screen) has the same fault at 66.667 MHz; on the new build compare a Y preset (not FLAT) on a 1 kHz tone: it should be clean.

## 7. Diagnostics > HALCYON row and the user-preset file (10 min). FIRST RUN

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 7.1 | Menu > Diagnostics: find the row **HALCYON** (below HW GAIN) | value OFF (never NO UNIT on 107) | ☐ | |
| 7.2 | Press A repeatedly | OFF, FLAT, WARM, CLEAR, BASS, VOCAL, SPEECH, LOW VOLUME, SMOOTH, **CLARITY, TEST IEM**, OFF; short dip at each step | ☐ | |
| 7.3 | Choose CLARITY, open Menu > Audio > HALCYON | sliders adopt CLARITY: warmth -2, vocal +2, punch +1, air +1 | ☐ | |
| 7.4 | Choose a slider move, then the Diagnostics row | row shows **CUSTOM**; next A goes to OFF | ☐ | |
| 7.5 | **The preset file was refused?** If CLARITY and TEST IEM are missing from the cycle | the file `tau-assets.bin` was not read; Info > THEME FILE should say what it found. Report the Info text | ☐ | |

## 8. Hardware gain stage (volume path) (20 min). FIRST RUN (DEV 105 never ran)

On the 107 bitstream the **hardware gain stage owns the volume** by default. This is a big change to the whole volume path, tested nowhere on a Pocket.

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 8.1 | Menu > Diagnostics > **HW GAIN** | value **ON** (not NO UNIT) | ☐ | |
| 8.2 | Info > row **HW GAIN** | owner, target, current gain and fading fields read sensibly (target near the volume you set) | ☐ | text: |
| 8.3 | Volume from 100 down to 0 in single steps (listening to a steady tone or music) | every step is the same size in loudness; **no click, no zipper noise**; at 0 silence | ☐ | |
| 8.4 | Hold Up and Down across the range | smooth ramps, no steps | ☐ | |
| 8.5 | Toggle HW GAIN OFF and ON (Diagnostics) at a mid volume | **loudness unchanged** between the two (same taper); no click at the switch | ☐ | |
| 8.6 | Start a new track at a high volume | the start is a gentle fade-in (about 40 ms), never a loud click | ☐ | |
| 8.7 | Pause and resume at high volume | no click | ☐ | |
| 8.8 | Track change with Left/Right at high volume | no click between tracks | ☐ | |
| 8.9 | ReplayGain (Menu > Audio > REPLAYGAIN): OFF / TRACK / ALBUM on tagged tracks | loudness steps between tracks levelled; changing the mode ramps, no click | ☐ | |
| 8.10 | ReplayGain track mode with volume at max | no distortion | ☐ | |
| 8.11 | Save and restore: set the volume, quit the core, restart | volume restored; **the first note is not loud** (restore uses the snap, not the ramp) | ☐ | |
| 8.12 | With HW GAIN OFF, repeat 8.3 | same behaviour (software gain) | ☐ | |

## 9. FLAC (15 min)

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 9.1 | A **24-bit FLAC** (a quiet passage and a fade-out) | clean; **no hiss or grain** on fade-outs (the reduction to 16 bits now rounds instead of truncating) | ☐ | file: |
| 9.2 | Hyperion FLACs (LPC) for 5 minutes | clean, no clicks; Info FLAC fields sensible | ☐ | |
| 9.3 | The 48 kHz track 13 | plays, clean | ☐ | |
| 9.4 | The 96 kHz FLAC (track 14) | **refused**: text begins "NO PLAY: ..." and fits the screen; the core does not hang | ☐ | text seen: |
| 9.5 | Diagnostics > ACCEPT ALL RATES ON, then the 96 kHz file | plays but may stutter (known: about 99% of real time); toggle OFF afterwards | ☐ | |
| 9.6 | Mono FLAC and a FLAC with a cover | plays, cover shows (TIM1 or embedded) | ☐ | |
| 9.7 | Seek inside a FLAC | no pops | ☐ | |

## 10. Speed and tempo (15 min)

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 10.1 | Menu > Playback: rows | REPEAT, SPEED, **TEMPO** (Diagnostic Build only) | ☐ | |
| 10.2 | SPEED 1.20x, 1.50x, 2.00x on the Twain speech MP3 | pitch rises with speed (varispeed); intelligible at 2.00x | ☐ | |
| 10.3 | TEMPO on, speed 1.25x, 1.50x, 1.75x on the speech MP3 | **pitch stays natural**, voice faster; clean | ☐ | |
| 10.4 | TEMPO at 2.00x | clicks expected (known: at the CPU limit); note how often | ☐ | |
| 10.5 | SPEED on a FLAC | stays 1.00x, toast "SPEED: MP3 ONLY" | ☐ | |
| 10.6 | Switch the speed while playing | no hang | ☐ | |

## 11. Cymo resampler, 16-bit output and the toggles (20 min)

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 11.1 | Diagnostics > CYMO RESAMPLER on a 44.1 kHz file at 1.00x | row **ON**; Info CYMO RESAMP reads **LIVE** with S1 D0 and a small Q (S counts stale events: 1 at a track restart only) | ☐ | |
| 11.2 | A 48 kHz file | row shows **WAIT** and Info CYMO RESAMP reads **READY** (the resampler is not used for 48 kHz) | ☐ | |
| 11.3 | A 44.1 kHz file at 1.20x | row **WAIT** (Info READY); back to 1.00x returns to ON (Info LIVE) | ☐ | |
| 11.4 | Toggle CYMO RESAMPLER OFF/ON during a 1 kHz tone (`cymo_loopback`) | **no level step, no click** (toggle dip), pitch unchanged between the two | ☐ | |
| 11.5 | Diagnostics > **16-BIT OUTPUT**: value | OFF, **not NO UNIT** | ☐ | |
| 11.6 | Turn it ON during music | level rises about **+6 dB** after a short dip (expected: it uses the full 16-bit slot); no click | ☐ | |
| 11.7 | Turn it OFF | level returns, no click | ☐ | |
| 11.8 | Volume at 100, 16-BIT ON, loud track | no clipping crackle at normal levels; the inter-sample-peak files (`cymo_loopback`) may clip: that is the headroom question, note which | ☐ | |
| 11.9 | Combine: Halcyon ON + Cymo ON + 16-bit ON + HW GAIN ON, 10 minutes of music | clean, no stutter, Info UNDERRUNS 0 ALL 0 | ☐ | numbers: |

## 12. Info page and report codes (20 min)

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 12.1 | Menu > Info: scroll all rows | **37 rows**, the last is **GAP LATENCY**; wraps; hold repeats; labels not cut off | ☐ | screenshot I1 and I2 (top, bottom) |
| 12.2 | Rows to read and note: FIRMWARE, FPGA REV, COLD IMAGE, FREE RAM, UNDERRUNS, DRAW STALL, CPU LOAD, VBLANK, BEAM, SPECTRUM, STRIP/BASES/ALPHA if present, MP3 WINDOW (HW n SLOTS 0 BAD 0 TMO), FLAC unit, REPLAYGAIN, HW GAIN, CYMO RESAMP, I2S JITTER, HEADROOM | all populated, no garbage, MP3 WINDOW BAD 0 | ☐ | values: |
| 12.3 | **A on the Info page** (normal path: Menu > Info > A) | a report code appears: **pixel grid by default**, caption with "INFO EXPORT", the view line, "X VIEW   B BACK" | ☐ | screenshot R1 |
| 12.4 | X on the code screen | cycles robust grid, lossless grid, QR code; QR scans with a phone | ☐ | |
| 12.5 | B or Start | closes the code screen; Info page redraws correctly | ☐ | |
| 12.6 | Take a Menu+Start screenshot of the grid and decode on the Mac (`decode_tau_suite.py --grid shot.png --table`) | decodes, all rows present | ☐ | later, with me |
| 12.7 | GAP LATENCY: let **three tracks end naturally** (use a short track or seek near the end), then read the row | a number of milliseconds for the last track change (time from end of file to the first new sample) | ☐ | ms: |

## 13. Check and the diagnostic suite (25 min, mostly waiting)

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 13.1 | Menu > Diagnostics > **CHECK**, run the USER CHECK with music playing | passes: SDRAM, PSRAM, cold code, library, playback counters, startup; result code shown as a grid | ☐ | screenshot K1 |
| 13.2 | STANDARD profile | all PASS except **Track changes** (known failure, unexplained) | ☐ | |
| 13.3 | Blit Test | completes in about a minute, no hang, no leftover noise on the screen | ☐ | |
| 13.4 | Meter Sweep | runs through the meters (10 s each), one result code | ☐ | |
| 13.5 | Pixel grid test | patterns exact | ☐ | |
| 13.6 | Stress R1, R2, R3 for 1 minute each (Diagnostics > Stress) | **no noise on the display** (the stress memory no longer overlaps the screen buffer), audio clean | ☐ | |

## 14. Soak (30 min, can run while you do other things)

Play an album with **Halcyon on (WARM), Cymo on, HW GAIN on, a meter running**, volume moderate, screen on.

| # | Check at 0, 15, 30 min | Expect | OK | Notes |
|---|---|---|---|---|
| 14.1 | Audio | clean throughout, no dropouts | ☐ | |
| 14.2 | Info UNDERRUNS and ALL | 0 / 0 or a very small stable number | ☐ | |
| 14.3 | The Pocket | warm, not hot; no reset | ☐ | |
| 14.4 | Open and close the menu, Configure and Halcyon pages 10 times during the soak | no leak of pixels, no slowdown, audio unaffected | ☐ | |

## 15. The alpha.4 cores `TAU` and `TAU_DIAGNOSTIC` (15 min)

These were installed before the menu change and are the release candidates.

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 15.1 | Both boot, play, volume, ReplayGain row present (Settings > Audio), pixel grid and QR through X | as on alpha.3 plus the changes below | ☐ | |
| 15.2 | **TAU (normal core): Menu > Settings > Info > A** (the Info path of this build) | **now exports a report code** (the thing that was missing) | ☐ | screenshot |
| 15.3 | TAU: Settings has only INFO (no Diagnostics, CHECK or TESTS) | correct, the normal core has no test menus | ☐ | |
| 15.4 | TAU_DIAGNOSTIC: Settings > Playback has a **TEMPO row** | present; works as in section 10 | ☐ | |
| 15.5 | Both: HW GAIN, 16-BIT OUTPUT (Diagnostic), HALCYON do **not** exist or read NO UNIT | as listed in section 0 | ☐ | |
| 15.6 | TAU_DIAGNOSTIC: free RAM on the Info page | about 1.4 KB less than before (tempo costs RAM); no instability | ☐ | |
| 15.7 | Volume: hold Up/Down, ReplayGain on a tagged track | as confirmed on DEV 93/103 | ☐ | |
| 15.8 | Both: quit and relaunch | volume, accent colour, theme and mode are remembered | ☐ | |

## 16. Regression checks on everything else (15 min)

| # | Do | Expect | OK | Notes |
|---|---|---|---|---|
| 16.1 | Library: Select, browse Artists/Albums/Tracks, hold Down, Start closes from any depth | works, no underrun while browsing | ☐ | |
| 16.2 | Covers: tau-art albums (fast), an embedded-JPEG album (Avalon), a FLAC with a cover | correct colours, no wrong colours, quick when swapping | ☐ | |
| 16.3 | Themes: Menu > Appearance THEME and MODE (Dark/Light) | both switch; Light text readable; persists after restart | ☐ | |
| 16.4 | Colour list: A selects, swatches correct, 19 colours | as before | ☐ | |
| 16.5 | Select + Y fullscreen meter, Select + X preset | works; Start or B leaves; the player screen repaints cleanly | ☐ | |
| 16.6 | Screen blank (Select + Down) | blanks, any button wakes without acting | ☐ | |
| 16.7 | Shuffle/repeat (Select + L) | cycle correctly | ☐ | |
| 16.8 | Start + Y while a track plays | jumps to Configure; B closes Settings | ☐ | |
| 16.9 | Rapidly open and close the menu 20 times | no pixel residue, no crash | ☐ | |

## 17. Expected behaviours and known issues (do not report these as new)

- **The 30 Hz flutter on every non-FLAT Halcyon setting and every non-FLAT old-EQ preset is a known bug on every core installed so far** (fixed in source, B-649, waiting for the fit).
- Switching HW GAIN OFF gives a pop at high volume (the software gain resumes on samples already queued): by design, a diagnostic toggle.
- Halcyon settings are **not saved** across a restart yet; a raw user preset (TEST IEM) has no sliders.
- The FLAT curve on 107 may show a small dip at the far left and the footer still says "17 HZ HIGH-PASS ON" although FLAT is a true bypass (cosmetic; fixed in source, not on this card).
- `Track changes` in Check fails (old unexplained issue).
- The 96 kHz FLAC is refused with "NO PLAY: ..." by design; TEMPO at 2.00x clicks; stereo FLAC at 2.00x speed is not offered (FLAC always plays at 1.00x).
- 16-BIT OUTPUT ON is about +6 dB louder; inter-sample peaks may clip on hot masters.
- The old EQ (Y) has no effect while Halcyon is ON.
- The accented-name playlist bug (BUG-001) is not fixed; names are ASCII on the card.
- No wide-input (18-bit) Halcyon path in this bitstream: that fit (`wide-b646`) was still running.
- A screenshot stops the track briefly (one underrun).

## 18. What to send back

1. For every row with a ☒ or a surprise: the step number and what you saw (the exact on-screen text is the most useful part).
2. Screenshots: **C1** (Configure page), **H1** (Halcyon page with WARM), **H2** (Halcyon page after slider moves), **I1/I2** (Info top and bottom), **R1** (an Info report code), **K1** (Check result), anything that looks wrong.
3. The numbers asked for in the Notes columns: seconds to player (2.1), preamp values (5.10), UNDERRUNS and ALL after 10 minutes (6.13) and after the soak (14.2), GAP LATENCY ms (12.7), CYMO RESAMP S/D/Q (11.1).
4. A word on the **listening verdicts**: which Halcyon presets sound good, which sound wrong, anything that clicks.

If something hangs: note the last thing you pressed and whether the screen kept animating; power-cycle; do not remove the card while the core is running.
