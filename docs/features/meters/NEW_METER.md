# A new Player 2 meter in one command (B-814)

```
make new-meter KEY=sparks TAG=SPK LABEL="SPARKS"
```

`tools/player2/new_meter.py` creates a working **pack-only** meter and builds its pack, so a new meter starts from something that already passes the pack test:

| file | what |
|---|---|
| `meters/<key>/meter.json` | the manifest: next slot and manifest index, `p2.pack_only`, three parameters (mode LEVELS / HISTORY, gain, cell size), two presets, a command ceiling (`p2.cmds`) and a cycle limit (`p2.max_cycles`) to adjust |
| `meters/<key>/thumb.json` | the 56 x 32 METER-list preview, palette from the theme roles |
| `fw2/ui/meter_<key>.c` | the drawing: `<tag>_load(params)` and `<tag>_tick(in)` under `P2_PACK`; LEVELS lights a cell grid from the band levels, HISTORY scrolls a loudness history |
| `fw2/ui/meter_pack_<key>.c` | the pack wrapper (redirects to the host table, the descriptor, the entry point) |

then runs the generators (`gen_meters_p2.py`, `gen_meters.py`) and `pack_meter.py`. The limits: seven meters (the saved presets have seven fields, `cores/settings_core.h` `SF_MPRE0..6`, and the METER list shows seven rows); a slot beyond that needs a field and list scrolling first.

## What to edit
1. The manifest: parameters (the order is the host's parameter table; `when` hides one until another has a value), presets (every parameter in every preset), the limits. Run `python3 tools/player2/gen_meters_p2.py` after a change.
2. The drawing in `meter_<key>.c`. Draw only inside the box (`in->x, y, w, h`); the host composes, degrades and budgets it (`p2.cmds`, `p2.max_cycles`; three overruns disable the pack, decision D-M20). A meter with its own history keeps it in a ring, a meter made of cells uses the grid helper.
3. The thumbnail (`thumb.json`: `rle` is `(palette index << 5) | (run - 1)` row by row; `palette_roles` names theme roles or ramps between two).
4. `make test-host`: the pack test picks the meter up from its manifest (generic trace: every preset, loud and quiet stretches, a pause, the degrade levels, the fullscreen box) and checks the pack on rv32sim against the source compiled straight in, inside its box, and that its descriptor parses.
5. Deliver the pack to the Pocket: `python3 tools/player2/pack_meter.py <key> --out work/packs-p2/<key>.tmpk`, bundle all packs (`pack_bundle.py`), merge into `tau-assets.bin` (`MPKB`), install with `tools/install_dev_core.py ... --assets`.

## The helpers
* **Cell grid** (`fw2/cores/cellgrid_core.h` pure, `fw2/ui/meter_grid.h` drawing): `cg_fit` fits whole cells into the box, centred, capped; a nibble a cell of state (`cg_get/cg_set`); `mgrid_palette` (the four steps from the background to the accent), `mgrid_clear` (a new context: every cell dim), `mgrid_put` (a cell takes a step, drawn only when it changed). The Waterfall is built on it.
* **History ring** (`fw2/cores/histring_core.h`): a byte ring in word-addressed memory (a pack's `.pstate` in PSRAM takes word accesses only); `hr_back` (the head after a push), `hr_store` (one word read-modify-write), `hr_unwrap` (the newest n samples into a hot array with word reads), `hr_get`, `hr_clear`; several rings may share one head. Layered Wave's six layers are built on it.
