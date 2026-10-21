# Developing a new meter through the framework: the Waterfall grid (2026-10-21)

A quick test of the Player 2 meter framework: build a meter that looks like a grid of small cells lit by the soundscape (a heat map), as a pack-only addon, and write down every issue, limit and improvement found. The meter is `meters/waterfall_grid/` (key `waterfall_grid`, shown as WATERFALL; the old player's `waterfall`, index 1, is a different, retired meter and was left alone).

## 1. Ways to analyse the sound for a grid (proposals)
What a meter can read today (`mtr_in_t`): 16 spectrum bands after the shared ballistics, the loudest sample of each channel since the last picture, the time step, pause. Anything else needs a new input (see 4).

| # | Mapping | Reads | Looks like | Cost |
|---|---|---|---|---|
| A | **Spectrogram** (built): frequency across, time down; each new row is the current spectrum | bands | the classic waterfall | one engine scroll plus about 20 cells a step |
| B | **Zones / heat map** (built): every cell belongs to one band, scattered over the box, lit by that band's level | bands | a still dust that swells with the sound; low and high regions are mixed over the whole grid | only changed cells, a quarter of the rows a tick |
| C | **Onset sparkle**: a transient (a band rises sharply against its own average) lights a burst of cells around that band's column, which then fades in three steps | band differences | fireflies on the beat; silence is black | cells only on events |
| D | **Stereo field**: left half of the grid from the left channel's level, right half from the right, centre cells from what they share | peak L and R (or L and R spectra: a new input) | a width meter | needs L and R separately |
| E | **Character map**: rows are not frequency but descriptors: loudness (RMS), brightness (spectral centroid), noisiness (flatness), crest factor, onset density; each is a row of cells whose count lit follows the value | derived from bands and peaks | a "what kind of sound is this" panel | tiny |
| F | **Long memory**: one column per second for the last minute, brightness = loudness, hue = centroid | derived, history | a track overview | a ring of history in PSRAM |

A and B are built; C is a small addition on B's cell machinery; D and E need the audio-statistics inputs of the roadmap (RMS, correlation, centroid; `AUDIO_METERING_RESEARCH.md`).

## 2. What was built
`fw2/ui/meter_waterfall_grid.c` (one source, real code only under `P2_PACK`; the firmware carries a stub that says WATERFALL NEEDS ITS PACK), `meter_pack_waterfall_grid.c`, `meters/waterfall_grid/{meter.json,thumb.json}`. Parameters: mode (SPECTROGRAM / ZONES), scroll speed, gain, cell size; four presets. As a pack: 1.85 KB image, 1.95 KB working state. Equal to itself under rv32sim in four scenarios (the degrade levels and the fullscreen box included), no draw outside its box.

## 3. Framework changes made for it (the rules the user set)
1. **Helios methods, the efficient way to draw.** New in `gfx`: `gfx_bar` (a bar column in one command: OP_BAR, which nothing in Player 2 used), `gfx_scroll_down` (a box moves down by rows through the stash, a few copies instead of a redraw; found because the engine's copy must not overlap itself), the stash map written down in `gfx.h`. Both are in the pack host table (ABI 4). Winamp Bars still draws two rectangles per bar: moving it to `gfx_bar` halves its commands (a follow-up with its own picture test).
2. **Standard degrade, always subservient to audio.** `gov_degrade()` (governor core, tested with a twin and a mutant) turns the ring level into 0..3: above the 50% line split in thirds (0 full, 1 every second picture, 2 every fourth), SHED_UI and worse = 3 (meters off). `meters_tick` applies it to EVERY meter (windows are skipped after the ballistics have run, a forced repaint is never skipped); `mtr_in_t.degrade` and `mtr_degrade_div()` let a meter also thin its own detail (ZONES does). Counted in `METER DEGRADED` on Info.
3. **Fullscreen always.** Every meter is drawn in the fullscreen box (400x323) by the generic fullscreen view; the pack test now enters that box mid-run for every meter and fails on any draw outside the box. It found one: Master VU's labels were drawn 2 px above its box (fixed: the ladders sit 2 px lower).

## 4. Findings: issues, limits, improvements
Found by doing it, in the order met.
1. **A pack-only meter still needs its manifest in the firmware.** The parameter descriptors, presets, preview and registry row are generated into the firmware from `meter.json`; only the drawing is in the pack. A meter that exists only on the card (the goal of `PACKS_ONLY`) needs the descriptors, presets and preview to travel in the pack file and the registry to become a runtime table. This is the biggest limit.
2. **Pack slots were a fixed 5** (ids hard-coded in two places); now 8 and generated from the registry. The registry order is the saved meter index: a pack-only meter must be appended, never inserted.
3. **Tests hard-coded the five meters** (`test_p2_mpre`, the stats expectations, the pack harness, the bundle slot lines): each had to learn about a sixth. They now derive from the registry, except the per-meter preset counts of `test_p2_mpre`.
4. **The old generators silently owned the manifests**: `tools/gen_meters.py` (old player) refused the new meter (index gaps) and an existing manifest of a retired meter with the same key was overwritten by mistake (restored; the new key is `waterfall_grid`). A `p2_only` flag now keeps a Player 2 meter out of the old player's tables but in the schema Tau Omega and the preview lab read. A `make new-meter` scaffold would remove the manual steps (manifest, thumbnail, source, pack source, harness trace, test entries).
5. **No scroll primitive existed** and a plain copy cannot scroll itself (hardware copy must not overlap): solved once in `gfx_scroll_down` instead of in every scrolling meter. Similar missing methods: a cell-grid helper (delta draw of lit cells with a palette), a history ring helper for time-series meters (Layered Wave has its own).
6. **The stash had no map.** Five users already (art, icons, scope strip, previews, Chladni) and the rows were found by grep; the map is now in `gfx.h`.
7. **Draw cost is only counted, not budgeted per frame.** A meter can issue thousands of commands; `METER MAX` shows it after the fact. A per-meter command budget the host enforces (and degrades against) would make "always subservient to audio" a guarantee rather than a convention.
8. **The preview lab has no twin for this meter** (nor for Chladni). Omega's preview of it would need a JS twin; for a pack-only meter the twin could run the same C through WebAssembly instead of being ported by hand.
9. **Inputs are the limit for the best ideas** (D, E, F): RMS, correlation, centroid, flux. They belong in the hardware audio-statistics block of the roadmap.
10. **Only the pack's hot state is bounded** (4 KB scratch shared by all packs): a pack that needs more (Chladni 2.7 KB, this one 1.9 KB) fits alone, a future one with a history would use its PSRAM state region.

## 5. The default fallback meter
Agreed direction: ONE built-in meter that is always present, Winamp Bars (small, no engine dependencies, drawn with `gfx_bar`), and every other meter pack-only. That needs finding 1 (descriptors and presets in the pack, a runtime registry) and a rule for what the list shows when the card has no packs (Bars only, plus a line saying where to get more). Not built: it is the next framework step, after the Pocket confirms the Waterfall pack.
