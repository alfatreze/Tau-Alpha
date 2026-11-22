# What a meter can know and what it cannot (today)

Read this **before you design**, not after. A meter is told a few numbers about the music and can draw with a small set of calls; some ideas that look natural are not possible, and the honest answer is to approximate them and say so in your README.

## What the host gives you every tick (`mtr_in_t`, [HOST_TABLE.md](HOST_TABLE.md))
| You get | What it is |
|---|---|
| `spec[0..15]` | 16 spectrum bands, 0..255, already smoothed by the player (fast attack, slow release) |
| `peak_l`, `peak_r` | the loudest sample of each channel since the last tick, 0..24575 |
| `rms_l`, `rms_r`, `corr`, `crest` | RMS per channel, left/right correlation (Q8: -256 opposite, 0 unrelated, +256 identical) and peak/RMS, **only when `stats_ok` is 1** (the hardware statistics block); otherwise 0 |
| `centroid`, `flux`, `onset` | where the energy sits (0..255), how fast the bands rise, and a 1 on a hit |
| `dt_ms`, `frame`, `x y w h`, `bg`, `force`, `paused`, `degrade` | timing, your rectangle, the background colour, repaint and pause flags, and the player's request to do less |

## What it does not give you
* **No waveform.** `in->wave` exists in the header and is **NULL in the player today**; there are no audio samples at all. A real oscilloscope, a stereo XY or phase scope, a Lissajous figure drawn from the actual left and right samples, a spectrogram finer than 16 bands: none of these can be drawn from the true signal. You can build a picture from the levels, the correlation and the bands; call it what it is (an estimate, "driven by levels") in your README and in the preset names.
* **No timing finer than a tick**, about 38 a second at full detail (fewer under `degrade`).
* **No input from the user** beyond your own parameters, and **no files**: a meter cannot read the card, save its own data (the player remembers the user's tuning of your parameters) or talk to the host except through the host table.
* **No drawing primitives beyond filled rectangles, bars, text and block copies.** Lines, circles and curves are built from rectangles (a staircase of rectangles per segment is the usual way). Each rectangle is one draw command, and the commands per tick are limited and counted ([PERFORMANCE.md](PERFORMANCE.md)).
* **No floating point you can rely on:** use integers and small tables ([SNIPPETS.md](SNIPPETS.md) has a sine table, a clamp and a colour mix).

## If your idea needs something that is not here
Design around it, state the limit in your README, and tell the project: a waveform source for meters is a known wish (it is not promised or dated). A meter that says "driven by levels" is honest and can be good; one that claims to be a true scope is not.
