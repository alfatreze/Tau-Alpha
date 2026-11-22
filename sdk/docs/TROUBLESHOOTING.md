# Troubleshooting

## Build errors (`tau-meter build`)
| You see | Cause | Fix |
|---|---|---|
| `clang was not found` / `no riscv32 target` / `ld.lld was not found` | no suitable LLVM | [TOOLCHAIN.md](TOOLCHAIN.md); `tau-meter doctor` |
| `the meter uses names it does not define: printf, ...` | a libc or player function, or a typo | a meter has no libc: use the host table and your own code; `memcpy`, `memset`, `memmove`, `memcmp` and 64-bit divide and shift are provided |
| `the meter's static data is N bytes but the scratch area for every meter is 4096` | big `static` arrays | shrink them, or `TAU_PSTATE uint32_t` arrays (PSRAM) for histories |
| `the meter (N bytes) and its pstate (M bytes) do not fit the 64 KiB window` | code plus `TAU_PSTATE` over 64 KiB | smaller code, smaller history |
| `the id ... is not 32 lower-case hex digits` | the template's placeholder id | `tau-meter id new` |
| `meter.json has no 'author'` ... | a missing field | [MANIFEST.md](MANIFEST.md) |
| `source file src/x.c does not exist` | `sources` lists a missing file | fix the list |
| `the project has no LICENSE file` | a package must carry its licence | add `LICENSE` |
| `abi 1.3 in meter.json ... this SDK knows ABI 1.0 to 1.2` | a newer ABI than this SDK | update the SDK |
| compiler errors | your C | the first message is the one to read |

## The player's refusal codes
The player refuses in three places. What you see depends on the build: the normal build's About page says `BIN REFUSED (CODE n)` for the bin, and its support Report has `PACK FILE`, `PACK ERR`, `PACKS STRUCK`; the Diagnostic Build's Info page has `MTOC ERR`, `PACK ERR`, `PACKS STRUCK`, `MWIN ...`. `tau-meter test` reports the same conditions before you get there (the check id is in the last column).

### 1. The whole bin (`MTOC ERR n`): the player runs on Winamp Bars only
Raised by the composing tool's checks too, so a bin made by `tau-meter compose` should never meet them. They matter if the file was damaged on the card or made by another tool.
| n | Name | Meaning |
|---|---|---|
| 1 | SIZE | shorter than header plus CRC, over 4,456,448 bytes, or the stored total is not the file's size |
| 2 | MAGIC | not a `TPKB` file |
| 3 | VERSION | not version 2 |
| 4 | CRC | the table of contents or previews are damaged (`toc_crc`) |
| 5 | COUNT | more than 64 meters |
| 6 | LAYOUT | the regions (header, table, previews, packs, CRC) are not in order |
| 7 | ID | an id is not 32 lower-case hex digits |
| 8 | HASH | a row's id hash is not its id's first 8 digits |
| 9 | DUP | two rows share an id hash |
| 10 | NAME | a name is not 1 to 23 printable ASCII characters |
| 11 | PACK | a pack's range is outside the packs, or its header and its row disagree on size, ABI, CRC, state or `.bss` |
| 12 | WINDOW | a pack and its state do not fit the 64 KiB window (suite: `LOAD-E25`, `PKG-PACK`) |
| 13 | PREVIEW | a preview is outside its region, over 1 KiB, or not 56 x 32 (suite: `DESC-PREVIEW`) |
| 14 | RESERVED | a reserved byte or flag bit is not zero |
| 15 | ORIGIN | a pack is not linked for the window (suite: `LOAD-E24`) |
| 16 | PARAMS | more than 48 parameters |
| 17 | READ | the card read failed |
| 19 | FILECRC | the whole-file CRC (tools only; the player never reads it) |
| 20 | no fallback | the bin has no Winamp Bars row (the shipped id `7dddfe63121e904843088c075cc28ce9`): `compose` adds it |
A row can also be **unavailable** without the bin being refused: its `abi` or `tau_min` is beyond the player; the list greys it.

### 2. One pack at selection (`PACK ERR n`): the player shows Winamp Bars instead
| n | Name | Meaning | Suite |
|---|---|---|---|
| 20 | no file | no bin to read the pack from | |
| 21 | header | not a `TMPK` pack | `LOAD-E21` |
| 22 | ABI | another ABI major, or it needs a newer minor than the player speaks | `LOAD-E22` |
| 23 | id | the pack's header id is not the row's (a hand-edited pack) | `PKG-PACK` |
| 24 | origin | not linked for the window | `LOAD-E24` |
| 25 | size | image or state does not fit the window, or sizes inconsistent | `LOAD-E25` |
| 26 | CRC | the image is damaged (CRC) | `LOAD-E26` |
| 27 | entry | the entry point is outside the image | `LOAD-E27` |
| 28 | scratch origin | `.data` is not linked at the player's scratch address | `LOAD-E28` |
| 29 | scratch capacity | `.data` and `.bss` over 4 KiB | `LOAD-E29` |
| 40 | struck | the pack overran its cycle limit three times, or hung and the watchdog cut it | `RUN-OVERRUN`, `RUN-STRIKES`, `RUN-HANG` |
| 41 | descriptor | the descriptor is missing or damaged | `DESC-PARSE` |
| 42 | descriptor too big | its parameters or strings exceed the player's caches | `DESC-PARAMS` |
| 43 | card read | the card read failed while loading | not the meter's fault: try again |
Codes 30 and 31 belong to the old bundle format and are not raised by a v2 bin.

### 3. A meter that loads and misbehaves
| You see | Likely cause | Check |
|---|---|---|
| it draws, then Winamp Bars appears and `PACKS STRUCK` is 1 | overran its limit three times, or hung | `RUN-OVERRUN`, `RUN-HANG`; measure on the Pocket ([PERFORMANCE.md](PERFORMANCE.md)) |
| garbage left outside the box | it drew outside | `RUN-BOX` |
| the picture flickers or tears | it repaints its whole box every tick directly: set `"compose": true` | |
| it starts wrong after a switch | static state is not re-initialised: it restarts from initial values each selection and gets `force = 1` | repaint everything when `in->force` |
| the player resets or hangs | a wild pointer: after a reset the next boot starts on Winamp Bars | `RUN-MEM-WRITE`, `RUN-JUMP`, `RUN-DETERMINISM` |
| the music stutters | the tick is slow | `RUN-OVERRUN`, `PERFORMANCE.md` |

### 4. Things that surprise a first meter
| You see | Likely cause | Check |
|---|---|---|
| Configure lists only DEFAULT on the Pocket although `presets.json` has ten | the factory presets are not on the card: `compose` writes the bin only | [ON_A_POCKET.md](ON_A_POCKET.md) step 3b (copy `presets/` into `tau-presets/<first 8 hex digits of the id>/`) |
| `tau-meter thumb` fails with "has no thumb.json" (and a traceback) | the thumbnail command builds first, and a build needs a `thumb.json` | copy `template/thumb.json` into your project, then run `thumb` ([GETTING_STARTED.md](GETTING_STARTED.md) step 4) |
| `in->wave` is NULL | the player does not provide a waveform today | [LIMITS.md](LIMITS.md): build the picture from the levels, the correlation and the bands |
