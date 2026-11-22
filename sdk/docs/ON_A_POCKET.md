# Trying a meter on a Pocket

**Status, honestly:** the loader, the user bin and the window were proven on a Pocket with the project's own test cores (the window P0 spike, B-1045; the loader, B-1057), and the host side of this SDK is tested hard. **A meter built with this SDK has not yet been run on a Pocket by the project.** The test is prepared (a sheet and a ready-made bin in the Tau Alpha repository: `docs/features/meters/SDK_POCKET_TEST.md`, `work/sdk-export/pocket-test/`); run it yourself and look at the things listed below.

## Steps
1. **Build and test** your meter ([GETTING_STARTED.md](GETTING_STARTED.md)).
2. **Compose the user bin.** The player reads the list of meters from one file. A user bin **replaces** the core's own list, so put in everything you want to see, the shipped meters included:
   ```
   tau-meter compose tau-meters.bin dist/<yourid>.tmeter ../tau-alpha-meters/dist/*.tmeter
   ```
   The shipped Winamp Bars is the player's fallback and must be in every bin; `tau-meter compose` adds it for you from the SDK's `share/` folder if you did not list it. Rows keep the order you give (Winamp Bars first). `compose` refuses what the player would refuse, in plain words: more than 64 meters, two ids with the same id hash, a package that fails `verify`, a bin over 4,456,448 bytes. It warns when two ids share their low 16 bits (those two never save tuning).
3. **Copy it to the SD card** as `Assets/tau/common/tau-meters.bin` (data slot 10; every Tau core on the card shares it). Keep a copy of the card's files first. **Deleting this file brings back the core's own seven meters.**
3b. **Put the factory presets on the card (a step `compose` does NOT do).** The bin holds the meters only. A meter's factory presets live inside its `.tmeter` (the `presets/` folder of the zip) and the player reads them from `Assets/tau/common/tau-presets/<the first 8 hex digits of the meter's id>/`. Tau Omega does this copy when it composes; by hand:
   ```
   mkdir -p "<card>/Assets/tau/common/tau-presets/<first 8 hex digits of yourid>"
   unzip -j dist/<yourid>.tmeter 'presets/*' -d "<card>/Assets/tau/common/tau-presets/<first 8 hex digits of yourid>"
   ```
   The folder then holds `index.tpi` and `p0.tpr`, `p1.tpr`, ... Without it, **Configure lists only DEFAULT** for your meter. Same card-write care as the bin: keep a copy of the card's files first.
4. **Start Tau.** Pick your meter: press **X** on Now Playing to walk the list, or the METER list in Settings (Settings > Appearance > METER today; the 0.7.5 release plan moves it to a METERS entry). **A new meter is never selected automatically.**
5. **Look.** The picture draws inside its box and moves with the music; Configure shows your parameters, and your presets once step 3b is done; the list shows your 56 x 32 preview. Play loud music for a minute, switch meters quickly ten times, quit and restart: the player should come back on your meter once it ran for a few seconds.

## What the player tells you
| Where | Row | Healthy |
|---|---|---|
| About (Settings > About > ON THE CARD) | `METERS` | `N METERS`; `BIN REFUSED (CODE n)` if the bin was refused at boot; `WINAMP BARS ONLY` with no bin; ` (A METER FAILED)` appended after a meter was refused or struck |
| support Report (Select+Start; decode with `tools/decode_tau_suite.py --grid <png> --table` of the Tau Alpha repository) | `PACK FILE` | 0 (a bin is in use); 255 none found; else why it was refused |
| | `PACKS SEEN` | the number of meters in the table |
| | `PACK SRC` | **1** your `tau-meters.bin`, **2** the core's own `tau-packs.bin` |
| | `PACK ERR` | 0; the first refusal code otherwise ([TROUBLESHOOTING.md](TROUBLESHOOTING.md)) |
| | `PACKS STRUCK` | 0; above 0 means a meter ran over its limit three times or hung |
| | `PACKS RUN` | one bit: the row whose pack is in the window now |
| Diagnostic Build, Info page | `MTOC ERR`, `MWIN ...`, `METER MAX <name>` | the table check result, the loader's counters, the slowest tick per meter ([PERFORMANCE.md](PERFORMANCE.md)) |
A screenshot while playing adds one audio dropout: pause before photographing.

## If it does not show up
No bin read: the file name or folder is wrong (the path is exactly `Assets/tau/common/tau-meters.bin`). The bin was refused: About says `BIN REFUSED (CODE n)`; look n up. Your meter is in the list but greyed: it needs a newer ABI or Tau than the player has. It falls back to Winamp Bars when selected: its pack was refused (`PACK ERR`) or struck.
