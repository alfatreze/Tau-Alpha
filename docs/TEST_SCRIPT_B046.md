# Test script: Phase G1 cold image (TAU PSRAM 13)

**Core:** `TAU PSRAM 13` (library build + `TAU_COLD=1`, P4 bitstream; ROM `07d2c264...`, 168,512 B, heap gap 10,960 B). Packaged in `work/diagnostics/tau-psram-13/pocket` with `Assets/tau_psram_13/common/tau-cold.bin` (5,156 B) and data slot 6. NOT installed.
Everything else (library, playlists, history, loader, colours) is as in B-041/B-042.

## Predictions [EST]
- Boot shows nothing new; the cold image loads in about 10-20 ms (4 KiB SD windows + 1,284 PSRAM word writes).
- Info page: row 7 reads `COLD IMAGE  5136 B <n> MS` (the row replaces LIST CLIPPED in this build), n between 5 and 30.
- MENU > SETTINGS > HOW IT WORKS shows the same text as before (12 lines); Settings > Meter preview thumbnails look exactly as before (the earlier grey 8-level previews).
- FREE RAM on Info: about 10.9 KB (was 7.0 KB in core 12).
- Negative cases (each on a copy of the core folder, one at a time): delete `tau-cold.bin` -> Info `COLD IMAGE OFF E10`, previews are plain grey boxes, HOW IT WORKS says "Help text is not loaded."; flip one byte in the file -> `OFF E13`; truncate it -> `OFF E12`; use the `tau-cold.bin` of another build -> `OFF E14`. In every case music plays and the library works.

## Steps
| # | Do | Expect |
|---|---|---|
| 1 | Start 13, Menu > Settings > Info (S) | COLD IMAGE row with size and ms; FREE RAM about 10.9 KB |
| 2 | Menu > Appearance > Meter | Previews identical to core 12 (S) |
| 3 | Menu > Settings > How it works (S) | Library-mode text, 12 lines |
| 4 | Play, browse, change tracks for a few minutes | No audible change; underruns unchanged |
| 5 | Negative cases above | Feature off, everything else normal |
