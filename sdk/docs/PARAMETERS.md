# Parameters, descriptors, presets and the preview

A meter's **parameters** are what the Configure page lets the user change. You declare them in `meter.json`; the build turns them into a **descriptor** that is part of `meter.bin`, so the player can show the page without running your code. Your code reads the live values with `mtr_param(i)` (index = the order in `params`, 16-bit values).

## Declaring
```json
"params": [
  { "key": "gain", "label": "Gain", "type": "u8",   "min": 25, "max": 400, "step": 5, "default": 100, "unit": "%" },
  { "key": "mode", "label": "Mode", "type": "enum", "values": ["LEVELS", "HISTORY"], "default": 0 },
  { "key": "peaks", "label": "Peak hold", "type": "bool", "default": 1 },
  { "key": "custom_bar", "label": "Bar colour", "type": "u16", "default": 64512 },
  { "key": "cell", "label": "Cell size", "type": "u8", "min": 4, "max": 16, "step": 1, "default": 8, "when": {"mode": 0} } ]
```
| `type` | Shows as | Value | Rules |
|---|---|---|---|
| `u8`, `u16` | a number | `min` to `max` in `step`s | `min` <= `default` <= `max`, `step` > 0 |
| `enum` | a choice | the index of `values` (0 first) | 2 to 12 values, each at most 23 characters; underscores show as spaces |
| `bool` | on / off | 0 or 1 | |
| `u16` with a key starting `custom_` (or `color_green`, `color_yellow`, `color_red`) | a colour picker | a raw RGB565 value | |
Common rules: at most **48** parameters; `key` is lower case with underscores, unique, and its 16-bit hash (FNV-1a folded, what the preset files store) must not collide with another key's (the build says which); `label` at most 23 characters (shown upper case); `when` hides a parameter until another has a value; `unit` is documentation only. Parameters appear in the Configure page in the order declared.
Checks: `DESC-PARAMS` (ranges, defaults, choices), `RUN-PARAMS` (the meter runs clean with every parameter at its minimum and maximum).

## Presets
`presets.json` holds the factory presets, shown in the order given:
```json
{ "presets": [ { "name": "NORMAL", "values": { "gain": 100, "gap": 1 } }, { "name": "LOUD", "values": { "gain": 220 } } ] }
```
`name` at most 15 characters (shown upper case); `values` keys must be your parameters; a value outside its range is refused at build time; at most 127 presets. The build writes them as the binary files the player reads: `presets/index.tpi` (the list) and `presets/p<N>.tpr` (one preset: a sparse list of key hash and value), CRC-protected, each carrying the low 16 bits of the id hash. They travel in the package; the folder the player reads them from is `tau-presets/<first 8 hex digits of the id>/` on the card, which the composing tool (Tau Omega, planned) creates; copying that folder by hand has not been tested by the project. The suite checks the files (`PRESET`): ids, names, CRC, values in range.
A preset saved by a user on the Pocket is not part of the package.

## `params_rev`
Raise `params_rev` whenever you add, remove, reorder or re-range a parameter: the player keeps a user's tuning only while the meter's `params_rev` is the same, and starts from the defaults otherwise. `tau-meter test NEW.tmeter --previous OLD.tmeter` fails when the parameters changed and `params_rev` did not (`PARAMS-REV`), and warns when `params_rev` changed for nothing.

## The preview (`thumb.json`)
The METER list shows a **56 x 32** picture of your meter before it is loaded, from the descriptor. `thumb.json` holds `w` and `h` (56 and 32), `palette` (8 RGB565 values) and `rle` (hex of bytes `(palette index << 5) | (run - 1)`, row by row, a run never crosses a row, at most 1 KiB). `palette_roles` is optional: per palette entry a theme role name (`"accent"`, `"bg_bottom"`, ...) or a ramp `{"ramp": [role, role, t, n]}`, so the preview follows the user's theme; `null` keeps the fixed colour.
`tau-meter thumb` renders a frame of your meter in the simulator and writes `thumb.json` with at most 8 colours; edit `palette_roles` by hand afterwards if you want theme colours.

## Descriptor, for the curious
The descriptor (`MDSC` v3) is the last section of the pack: name, parameter records (label, type, min, max, step, choice names, `when`, key hash, default), the preview palette and stream, the draw-command ceiling and the cycle limit, and a trailer so the loader finds it from the end of the image. `meter.bin` carries no presets table: the presets are the files above.
