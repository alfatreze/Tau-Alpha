# The meter model and its lifecycle

**What a meter is.** One freestanding C program for a 32-bit RISC-V core (RV32IM), built into a *pack* (`meter.bin`), loaded by the player into **one 64 KiB window in PSRAM** at `0x24970000`. The player calls its entry point `mtr_pack_entry(in, api)` and the meter draws by calling functions in the **host table** (`api`). It has no operating system, no libc, no files, no other access that the player offers; it never names a player function or address (`tau-meter test` checks this).

## Lifecycle
1. **Listing.** At boot the player reads only the table of contents of the meter bin (ids, names, parameter counts, limits, 56 x 32 previews). No meter code is read.
2. **Selection** (X on Now Playing, the METER list, or restoring the saved meter at boot): the player reads that one pack from the card, checks it (header, ABI, `tau_min`, CRC, size, origin, scratch), writes it into the window through the uncached alias, flushes the instruction cache, **copies `.data` into the scratch area, zeroes `.bss` and zeroes the PSRAM state (`TAU_PSTATE`)**, and enters it with `force = 1`. Loading takes about 22 ms typically, 97 ms at worst (Pocket measurement, B-1045). Only the selected meter is in memory; selecting another replaces it, and **a meter that is switched away loses all its state**.
3. **Ticks.** About once per completed spectrum window (about 38 a second at full detail) the player calls the meter with the new inputs ([HOST_TABLE.md](HOST_TABLE.md)). The meter draws what changed, or everything.
4. **Force repaint.** `in->force == 1` means the context changed (the meter was just selected, the rectangle moved or changed size, the screen came back from a menu): repaint the whole rectangle and drop every redraw cache. A forced picture is the costliest by design and is not judged against the cycle limit or the command ceiling.
5. **Degrade.** `in->degrade` is 0, 1 or 2 when the audio's lead is shrinking: the player already skips every 2nd or 4th window and the meter may also draw with less detail (fewer cells, coarser steps). A meter that issues more draw commands than its `cmds` ceiling raises its own degrade level the same way.
6. **Pause.** `in->paused == 1`: levels fall to zero; draw the resting picture.
7. **Leaving.** There is no exit call: the player simply stops calling and loads the next meter.
8. **Failure.** A tick over its cycle limit is a *strike* (three strikes, one forgiven for every 64 clean ticks, switch the meter off for the session); a tick that does not return is cut off by the hardware watchdog and the meter is struck out at once; a refused or struck-out meter is replaced by the built-in Winamp Bars. After a reset or watchdog while a meter was active, the next boot starts on Winamp Bars. A newly installed meter is never selected automatically ([TRUST.md](TRUST.md)).

## The rectangle
The meter owns the rectangle `(in->x, in->y, in->w, in->h)` and never draws outside it. The player gives three today: Now Playing 368 x 122 at (16, 154), the Configure preview 360 x 100 at (20, 26), fullscreen 400 x 323 at (0, 0). Size yourself from `in->w` and `in->h`, not from these numbers. The suite also runs your meter in a 120 x 40 and a 16 x 8 box and warns when it draws outside them.

## Memory
| What | Where | Size |
|---|---|---|
| code, constants, descriptor | the window, `0x24970000`, read through its data alias `0xA4970000` | with the PSRAM state at most **64 KiB** |
| `.data` and `.bss` (`static` variables) | the scratch area at `0x00026C00`, **shared by every meter** | **4 KiB** together |
| `TAU_PSTATE` arrays (histories) | PSRAM, the window's tail after the image, zeroed at load | the rest of the window |
| the stack | the player's, 5 KiB in all with the player's own use | the suite fails above 3.5 KiB, warns above 2 KiB |

PSRAM state takes whole 32-bit word accesses (the player's notes say so): declare it `uint32_t`. A compiler may turn a clearing loop into a byte-wise `memset`; the suite warns when it sees a byte or halfword access to the state.
The inputs, `api->role`, `api->params` are read-only. Static variables start from their initial values at every selection. There is no floating point (soft float would be slow and pull in a library); use integers.

## What the player does with a tick's time
The player runs your tick on its main loop between audio jobs. A tick that takes long delays the audio's refill; that is why the limits exist ([PERFORMANCE.md](PERFORMANCE.md)). The audio ring holds about one second, and the watchdog's ceiling is 20 million cycles (300 ms).
