# `meter.json` and the package

A **package** is `<id>.tmeter`, a zip made by `tau-meter build`. The project folder holds `meter.json`, `src/*.c`, `thumb.json`, optional `presets.json`, `README.md` and `LICENSE`.

## The project's `meter.json`
```json
{ "schema": 1,
  "id": "9f3c1e7a52b84d06a1c9e34b7d20f518",
  "name": "MY BARS", "version": "1.0.0", "author": "Your Name", "licence": "MIT", "homepage": "https://github.com/you/my-bars",
  "abi": "1.0", "tau_min": "0.7", "params_rev": 1,
  "params": [ ... ],                       see PARAMETERS.md
  "p2": { "cmds": 24, "max_cycles": 400000, "compose": false },
  "sources": ["src/meter.c"] }             optional: the default is src/*.c
```
| Field | Rule |
|---|---|
| `schema` | `1` |
| `id` | exactly 32 lower-case hex digits, from `tau-meter id new`, never changed ([IDS_AND_VERSIONS.md](IDS_AND_VERSIONS.md)) |
| `name` | 1 to 23 printable ASCII characters (0x20 to 0x7E); shown in the METER list |
| `version` | `major.minor.patch`, each 0 to 65535 |
| `author`, `licence` | non-empty text; the licence is shown before an install |
| `homepage`, `source` | optional links (a text); `source` marks the meter as not binary only |
| `abi` | the host-table minor the meter **needs**: `"1.0"`, `"1.1"` (reads `mtr_in_t` statistics fields) or `"1.2"` (uses `scroll_left`). Declare the lowest that is true: the suite fails a meter that uses more than it declares |
| `tau_min` | oldest Tau that can run it (`"0.7"`); the player greys a meter that needs a newer Tau |
| `params_rev` | 0 to 65535; raise it whenever the parameter set changes |
| `p2.cmds` | draw-command ceiling per picture (0 = none: the suite warns) |
| `p2.max_cycles` | cycle limit per picture (0 = none: the suite warns) |
| `p2.compose` | `false` (draw straight onto the screen) or `true` (the player draws you off screen and copies the whole box: for a meter that repaints its entire box every tick) |
| `signature` | not in the project file; the package carries `null` (reserved: signatures are not checked) |

## What is inside the package
| File | Required | What |
|---|---|---|
| `meter.json` | yes | the package manifest: the fields above plus `files`, a SHA-256 for every other file, and `signature: null` |
| `meter.bin` | yes | the pack: a 48-byte `TMPK` header and the image, linked for the window |
| `README.md`, `LICENSE` | yes | shown before an install |
| `presets/index.tpi`, `presets/p<N>.tpr` | no | factory presets (binary files the player reads) |
| `source/...` | recommended | your sources, `meter.json`, `thumb.json`, `presets.json`; a package without them is marked "binary only" |
| `preview.png` | no | for people only; never authoritative |

## Rules (`tau-meter verify`, the first block of `tau-meter test`)
The package is at most **4 MiB** compressed and its files declare at most 4 MiB; no path outside the package (no `..`, no absolute path, no backslash or empty part); no file beyond the table; every file but `meter.json` is pinned by SHA-256 in `meter.json`; `meter.bin` is a valid pack no larger than the window **including its state**, linked for the window at `0x24970000`, with its CRC right and its ABI equal to the manifest's; the preview in the pack is 56 x 32 pixels in at most 1 KiB; `signature` is null. A violation is reported in plain words with the check id of [TESTING.md](TESTING.md).
The player's own checks when the meter is selected (E21 to E29) are repeated by the suite so that you meet them on your desk, not on the Pocket.

## Limits worth knowing
At most 64 meters in one bin; 48 parameters per meter; the whole bin at most 4,456,448 bytes. A pack is read from the card only when its meter is selected.
