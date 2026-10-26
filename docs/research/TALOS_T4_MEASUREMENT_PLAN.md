# Talos T4 gate: what each meter costs, measured on a Pocket (2026-10-26)

Decision (owner, 2026-10-26): measure before building any new graphics primitive (D-T05). The column list of the plan was written for the Winamp scope, but the scope draws 64 rectangles a frame and its larger cost is reading the capture RAM (about 512 register accesses), so the gain from a column list is unproven. This build records the data that picks the primitive.

## What the Diagnostic Build records (rows in the Report, Select+Start on Info)
For each meter, over its ordinary ticks (a forced repaint is left out):
| Row | Meaning |
|---|---|
| `M CMD MAX <meter>` | the most draw commands one tick issued |
| `M CMD AVG16 <meter>` | the average commands per tick, times 16 (a moving average: new = old + (x - old) / 8) |
| `M CYC AVG <meter>` | the average CPU cycles per tick (compose copy not included) |
| `M STALL AVG <meter>` | of those, the cycles spent waiting for a free command-queue slot |
| `M FENCE AVG <meter>` | of those, the cycles spent in fences |
Existing rows used with them: `GFX B1 ISSUE` (cycles per command to issue, Check > GFX ISSUE), `METER MAX <meter>` (slowest tick), `METER TICKS`, `UPTIME`, `GFX Q OVF`, `GFX SCAN LATE` (zero on a bitstream without the T3 block, which this build does not yet have).

## How to read it
Per meter: `commands = CMD AVG16 / 16`. `issue = commands x GFX B1 ISSUE`. Then
* `issue / CYC AVG` close to 1: **command issue limits the meter** (a primitive that draws many things per command pays).
* `STALL AVG` large: the queue was full, the engine limits it.
* `FENCE AVG` large: the meter waits for the engine (a hardware fence, T3, would shorten it).
* none of them large: the meter's own arithmetic or register reads limit it (a graphics primitive will not help; the scope's capture-RAM reads are this case).
The CPU share of a meter is `CYC AVG x ticks per second / 66,666,667`; ticks per second is `METER TICKS` over `UPTIME`.

## The run (about 10 minutes, owner)
1. Install the build (`TAU DEV 195`), start playing music with a lively spectrum.
2. For each meter in the METER list (Winamp Bars, Winamp Scope, Master VU, Layered Wave, Sound Map, Waterfall, Chladni): select it in the window, leave it 30 seconds, then **fullscreen** for 30 seconds.
3. Pause playback, press Select+Start on Info for the Report and photograph it (a screenshot while playing adds a dropout and a 4 s card stall, so the counters are read while paused). One Report after all meters have run holds every meter's rows; the rows hold the numbers of the last mode used, so for window and fullscreen separately take a second Report after repeating the window pass.
