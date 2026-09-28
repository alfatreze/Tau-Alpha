# User test: TAU 0.5.0-alpha.34 (core `TAU_0_5_0_A_34`)

Bitstream: gamma build (MP3 window unit, wave block, spectrum/beam, `text_light`). Firmware: everything since alpha.30 (theme system, meter modules, palette fix, TIM1 covers).
It is a Diagnostic Build. Take a screenshot (the Pocket screenshot combo) of every item marked **[shot]**; screenshots are stored on the card and are the evidence.
Everything is session-only for theme/meter settings (they reset on reboot, by design). Quit the core normally before pulling the card.

Start with a **cold boot** (power the Pocket off, then on, then open `TAU 0.5.0-alpha.34`). Card is already installed and verified byte-identical.

## A. Boot and audio basics (5 min)
1. Loading bar appears, then the player or the empty state. **Pass:** no blank screen, no reset, no hang.
2. Open the library, play a Nausicaa MP3. **Pass:** audio starts within a couple of seconds, no crackle.
3. Play a FLAC (flac-tests or Test Album FLAC). **Pass:** plays, cover shows.
4. Pause, resume, next, previous, seek with the D-pad, volume with left/right in Volume. **Pass:** all respond, no stutter after seek.

## B. Cover art, the main open question (TIM1) (10 min)
The reader failed on every previous build (`E6`). This build should load 128 px palette covers.
5. Open a Nausicaa Soundtrack track. **Pass:** a cover appears within a few seconds. **Fail:** placeholder or no cover.
6. Change track within the same album. **Pass:** cover stays (reused, no reload flash).
7. Switch to `Test Album`, then to the Image Album. **Pass:** each shows its own cover.
8. Open a menu (Start) while a cover is loading, then close it. **Pass:** cover finishes loading, no corruption.
9. Settings > Diagnostics > Info, find **TIM1 COVER**. **[shot]** Expected `N LOADED`, N above 0, no `E6`/`E7`/`E8`. Any error code: note it.

## C. Theme system (10 min)
10. Settings > Appearance: THEME and MODE rows. Switch THEME through all entries (TAU, OCEAN, SUNSET if the sample file loaded). **Pass:** colours change instantly, nothing unreadable. **[shot]** one per theme.
11. MODE: Dark then Light. **Pass:** background, panels and text switch; text stays crisp in Light (this is the new Light text-weight table, fitted in hardware). **[shot]** Light on the now-playing screen and on a settings list.
12. Change the accent Colour (19 palette entries). **Pass:** SILVER, CLEAR, ALUMINUM look like their names, not tinted. **[shot]** two accents.
13. Diagnostics > Info: **THEME FILE** should read `1 LOADED` and **METER FILE** `3 LOADED` (the sample `tau-assets.bin` is installed). **[shot]**

## D. Meters (15 min)
14. Settings > Appearance > Meter: cycle every meter (Winamp Scope, Winamp Bars, Chladni, Bars, Waterfall, Peak Dots, the rest). **Pass:** each draws inside its box, no tearing, no leftover pixels when switching. Note any meter that flickers or freezes.
15. Winamp Bars: **Configure** page. Change Preset, Bands, Easing, Peak options. **Pass:** live preview reacts to the music, presets visibly differ. **[shot]**
16. Winamp Scope in Configure. **Pass:** a clean wave, no gradient patch problem, no tearing.
17. Chladni: cycle presets with Select+X. **Pass:** pattern changes, audio clean.
18. Fullscreen visualiser: Select+Y on Chladni (and Winamp). **Pass:** fills the screen, exits cleanly.
19. Leave a menu while music plays and watch a bar meter for 30 s. **Pass:** bars resume and keep moving (the METER YIELD latch fix). Info > METER YIELD should not show 30 s or more.
20. **Meter presets file:** the sample `tau-assets.bin` adds presets for Winamp Bars, Winamp Scope and Chladni. **Pass:** they appear in Configure preset lists.

## E. Performance: the big claim (10 min)
21. Settings > Playback: Speed 1.25x, then 1.5x, then 1.75x on an MP3. **Pass:** clean at 1.75x (alpha.30 measured this). Note where it first breaks, if it does.
22. Diagnostics > Info: **MP3 WINDOW** shows `HW <big number> SLOTS 0 BAD 0 TMO`. **[shot]** Any BAD or TMO above 0 is a failure.
23. Info rows to read once, **[shot]**: VBLANK (about 60/S), BEAM, SPECTRUM, DRAW STALL, CPU LOAD (100% is known not to mean anything).

## F. Check runs (the evidence suite) (about 20 min)
Play a track first (music must be playing for the audio tests). Settings > Diagnostics > Check.
24. **USER CHECK**. **[shot]** every result page and the QR. Pass: all lines PASS.
25. **STANDARD** profile with music playing, **[shot]** results and QR. Pass: all PASS; `Track changes` is a **known old failure**.
26. Blit storm line: PASS with audio marked continuous (no `*`).
27. **Meter Sweep** (Diagnostics > Meter Sweep), let it finish (about 10 s per meter). **[shot]** the QR. Pass: no meter pinned at the 1000 ceiling except known heavy ones; Winamp Scope should now be cheap.
28. Optional if time: **Decode Sweep** over Test Album. **[shot]** QR.

## G. Library and menus regression (10 min)
29. Start closes any menu from any depth (library, settings, playlist).
30. Left/Right are Back/Forward in menus; L1/R1 page long lists.
31. Open the library, play a whole album to its end. **Pass:** next track loads, no LOAD FAILED (FLAC to FLAC too).
32. Reboot: does the last track/album restore or the empty state show? Note which (known boot-restore quirk, issue 021).

## H. Stress and stability (15 min, optional)
33. Settings > Diagnostics > Stress: R3 for 5 minutes with music. **Pass:** audio never breaks, 0 late underruns.
34. Change track, meter and theme repeatedly during playback for a couple of minutes. **Pass:** no reset, freeze or corruption.

## What to send back
Screenshots of every **[shot]** item, plus for any failure: the step number, what you saw, and whether it repeats after a cold boot. The most valuable answers are B (covers), C (Light text), D19 (bars resume), E22 (MP3 WINDOW) and F (Check and Meter Sweep QR).

## Not in this build
The pipelined alpha blend (fit `blend-pipe-b327` still running on the VM; needs its own package and card run if it closes). No alpha blend is exercised by this test.
