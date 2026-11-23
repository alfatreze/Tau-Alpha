# 0.7.5 release text: DRAFT for review (nothing here is live)

Written 2026-11-23 for the owner's approval (decision 2 of the P5 step). **Nothing in CHANGELOG.md, README.md or the user guide has been changed by this file.** On the owner's go, the sections below are applied just before the rehearsal build (P6) so the rehearsal ships the real text. Facts used: D-M25 to D-M29, B-1130 to B-1144; every claim is something a Pocket did or the host proved; what is not yet tested is marked.

## A. CHANGELOG.md: replace `## Unreleased` and its two-line intro by

```
## v0.7.5 — <date of the release>

### New
- **Every meter keeps its own settings.** Change a parameter in any meter's Configure page and it is still there after you quit, power off or restart: not just two meters any more, every meter you tune, up to 64. The settings live in one small file, `Saves/tau/alfatreze.TAU/tau-meter-settings.bin`, that the Pocket creates and writes by itself; never delete or edit it (deleting it only puts the meters back to their defaults). TAU Diagnostics keeps its own file.
- **A METERS menu.** The list of meters has moved out of Settings > Appearance to its own entry in the main menu, directly below APPEARANCE. It opens on the meter that is on screen, scrolls (with a position like "12 / 64") and shows a small gear on every meter you can configure. **A** picks a meter, **X** opens its Configure page, even for a meter that is not the one playing (it shows live in the preview, and leaving brings back the one you chose).
- **More than seven meters.** TAU can read one optional file of extra meters, `Assets/tau/common/tau-meters.bin` (up to 64), that you put on the card; it is never part of an update, and without it TAU uses the seven it ships with. A meter that needs a newer TAU is shown greyed and cannot be picked.
- **Safe start.** If the player resets or hangs while a meter is on screen, the next start begins on Winamp Bars instead of the same meter, and the meter you chose is only remembered after it has run cleanly for a few seconds. A meter you add is never chosen by itself.
- **Settings file status on the About page.** A new row, SETTINGS FILE, says whether the file is in use ("ON: SAVED WHEN YOU QUIT") or why not ("NEWER VERSION: NOT USED", "NOT LOADED: 2 METERS ONLY", "NOT AVAILABLE: 2 METERS ONLY"; the last two mean only two meters keep their tuning, as before).
- **Anyone can build a meter.** The meter SDK and its template are public (https://github.com/alfatreze/tau-alpha-meter-sdk), with the seven shipped meters as examples in https://github.com/alfatreze/tau-alpha-meters. A meter is one `.tmeter` file; Tau Omega installs them.

### Improved
- **Meter presets are listed reliably.** (keep the existing text)
- **Meters load when you pick them.** (keep the existing text)
- **The meter you chose is remembered by its identity.** (keep the existing text)
- **The platform category is `Media`** (it was `Media Players`), so TAU sits with the other media apps in the Pocket's core list. The old group disappears when you update.

### Technical
- (the existing `- Omega:` lines stay, including the three added on 2026-11-23: the settings file, no seed, the category)
```
Notes: the date is the owner's call at release time. The existing "Not a release" intro goes away. The line "Settings > Appearance > METER shows seven meters at a time" in the old text is stale (the list is the METERS menu now) and is replaced above.

## B. README.md
1. Line 40 ("**Eleven meters.** Winamp Scope, ... Spectrum"): stale; Tau ships **seven** meters (Winamp Bars, Winamp Scope, Chladni, Layered Wave, VU Master, Waterfall, Sound Map) and can load up to 64. Proposed: "**Seven meters, up to 64.** The seven that ship are Winamp Bars (the fallback), Winamp Scope, Chladni, Layered Wave, VU Master, Waterfall and Sound Map; add your own with the public meter SDK. Every meter keeps its own tuned settings, and has a Configure page with presets."
2. Install, after step 4: "**Updating by hand is safe for your settings.** The release zip contains no `Saves/` folder, so copying `Cores`, `Platforms` and `Assets` over the card never touches `Saves/tau/alfatreze.TAU/tau-meter-settings.bin`, where the Pocket keeps every meter's settings."
3. Documentation table: nothing new (the user guide carries the rest).

## C. docs/guide/USER_GUIDE.md
1. Lines 62 to 63 ("the **meter Configure settings** are *not* [kept]..."): replace by "Every meter's Configure settings are kept too, in `Saves/tau/alfatreze.TAU/tau-meter-settings.bin` (TAU Diagnostics keeps its own). The Pocket creates and writes it by itself, at Quit and at power-off; leave it alone."
2. Section "Meters": describe the METERS menu (below APPEARANCE; gear icon; **A** picks, **X** configures any meter; B back, Start straight to Now Playing); "Reset" = the preset row of Configure; to forget a meter's tuning pick its default preset, to forget all of them delete the settings file (the Pocket recreates it at your next change).
3. "Known limitations": add "A settings file of a newer TAU than this one is not used (About says so); only two meters keep their tuning then. Deleting the file loses the saved tunings, nothing else."
4. About page paragraph (if any): the SETTINGS FILE row and its four wordings.

## D. To decide / check before applying
- The release date and whether the "Anyone can build a meter" bullet should wait until the SDK has had its first Pocket-tested third-party meter (LISSAJOUS LOCAL ran, B-1132; the public SDK docs still say no SDK-built meter had run on a Pocket: update them at publish).
- The sentence "deleting it only puts the meters back to their defaults" is host-proven and was shown on a Pocket on 2026-11-23 (B-1140: delete, boot, edit, Quit recreates the file); the **normal build and both release cores are not yet tested** (P7).
- The About row wordings are host-proven; DEV 233 is the first Pocket look.
