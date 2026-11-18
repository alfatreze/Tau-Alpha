# Performance: budgets, estimates, measuring

## The three limits
| Limit | Where | What happens |
|---|---|---|
| **commands** `p2.cmds` | draw commands per drawn picture | a picture over it raises the meter's own *degrade* level (the player draws every 2nd, then every 4th window and tells the meter through `in->degrade`); 16 clean pictures lower it again. A forced repaint is not judged. 0 means unbudgeted |
| **cycles** `p2.max_cycles` | CPU cycles of one drawn picture (66.67 MHz clock) | over it is a **strike**; the third strike switches the meter off for the session; one strike is forgiven per 64 clean pictures; a forced repaint is not judged. 0 means none |
| **watchdog** | 4 x `max_cycles`, at least 2,000,000, at most 20,000,000 (300 ms) | the tick is cut off and the meter is struck out **at once**; 0 limit gets the 20 M ceiling |
The shipped meters: Winamp Bars 64 commands and 200,000 cycles; Scope 128 and 1.2 M; VU Master 32 and 200,000; Chladni 1,280 and 4.5 M; Layered Wave 640 and 15 M; Sound Map 3 M and Waterfall 5 M cycles.

## What the suite estimates
`rv32run` counts instructions exactly and estimates cycles with **cost model v1**: 1.5 cycles per instruction (the CPU's measured range is 1.3 to 1.5), 253 cycles for each 32-byte line of code the 4 KiB instruction cache has to fetch from PSRAM (measured on a Pocket, 31.6 cycles per word: B-045; the cache is cold at every tick, which is pessimistic), 60 cycles to issue a draw command, 12 for any other table call. **It does not model the draw engine's own time, the audio decoder's share of the CPU, or card stalls.** The one Pocket figure we have for a pack, Layered Wave's slowest tick, was 7.7 M cycles where this model says 0.4 M: the model is a floor, not a forecast. Use it to catch a meter that is *wrong by a lot* (a loop that does not end, 100,000 iterations a picture), not to certify a limit.

## Setting limits honestly
Two rules first: **`cmds` comes from your worst ordinary picture by reading the code, not from the scenario maximum** (16 cells that all change plus a text line is 17 commands even if the scenarios only reached 13), and **until you have a Pocket figure, take the larger of the simulator's slowest tick times ten and the cheapest shipped meter's limit (200,000 cycles)**. Then:
1. Run `tau-meter test`: it prints the slowest drawn tick and the most commands.
2. Put `cmds` a little above the most commands of a drawn tick (not the forced one).
3. Put `max_cycles` at **4 x the figure you measure on a Pocket**, not the simulator's. Until you have measured, pick a limit no smaller than the shipped meter that draws about as much as yours.
4. Raise a limit only when the meter really needs the time; a high limit lets a slow meter delay the audio.

## Measuring on a Pocket
A **Diagnostic Build** of Tau shows, per meter, `METER MAX <name>` on its Info page (the slowest tick since boot, cycles of the 66.667 MHz counter), `PACKS STRUCK`, `PACK ERR` and the draw command counters; the normal build's support Report (Select+Start, decode with `tools/decode_tau_suite.py` of the Tau Alpha repository) carries `PACKS STRUCK` and `PACK ERR`. Select your meter, play loud music for a minute, switch the meter between its parameters, read the figure, set the limit.

## Making a meter cheaper
Draw only what changed (keep the previous picture's values); one `mtr_bar` replaces two `mtr_rect`; use `mtr_scroll_left/down` and blits to move pixels instead of redrawing them; keep code small (a meter that fits the 4 KiB instruction cache avoids the 253-cycle fills); read `in->degrade` and draw less when it is above 0; avoid division (the core has a divider but it is slow; the SDK's 64-bit division is a loop).
