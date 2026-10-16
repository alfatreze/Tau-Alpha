# Player 2 bring-up firmware (archived 2026-10-16)

The stage firmwares that proved Player 2 piece by piece before the player existed. They are not part of any build any more; `fw2/build.sh` builds the player only.

| File | What it was |
|---|---|
| `main.c` | The S2/S3/S4 main loop: boot checks, the counter page, and one of three transports (the tone generator, the card list walker, or the first MP3 player). Replaced by `fw2/app/main_player.c`. |
| `listplay.c`, `listplay.h` | S4: walked the library queue reading files with no audio, to prove the card path (opens, reads, hash checks, size checks). Its counters (`LP RATE`, `LP DONE`, `LP OK`, `LP HASH *`, `LP KB/S`) are gone with it. |
| `dead_functions.c.txt` | Functions removed from `fw2` as unreferenced: the old key handler, `library_title`, `slot_busy`, `platform_version_string`. |

`fw2/transport/tone.c` stays in place: `sim/test_p2_aout.py` drives the real aout module with it. To bring a stage firmware back, restore the file from this directory (and the tag `archive/player2-bringup`) and build it against the then-current `fw2` tree.
