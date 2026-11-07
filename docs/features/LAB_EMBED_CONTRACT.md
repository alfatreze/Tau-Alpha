# Meter Lab embed contract, version 1

For pages that embed the pinned lab drop (`docs/interop/lab-drop/<name>/meter_lab.html`; today Tau Omega's Meter Lab). Written for Omega message 014 (R-1 to R-5, F-3). Reference code: `tools/meters/preview/tau_host.js` (the pure parts, tested by `sim/test_lab_host.py`) and `index.html` (the wiring). The number is `contract` in `lab-data.json` and `MANIFEST.json` and in `tau-lab-ready`. **A drop that changes or removes anything below bumps it**; adding a message or a field does not.

Give the page to a sandboxed frame as `srcdoc` with `allow-scripts` only. Every message has a `type` that starts `tau-lab-`; the lab accepts messages only from its parent window.

## Embedder to lab
| message | effect |
|---|---|
| `{type:"tau-lab-set", meter:<manifest id>, values:{<key>:<integer>}}` | choose a meter and its preset values; the firmware's sparse rule (unnamed = default, unknown key ignored, value clamped); answered by `tau-lab-applied` |
| `{type:"tau-lab-embed", on:true\|false}` | embed mode (the page also starts in it when its URL fragment is `#embed`) |
| `{type:"tau-lab-option", id, value}` | set one preview option (below); answered by `tau-lab-option-applied` |
| `{type:"tau-lab-info-get"}` | send `tau-lab-info` now |

## Lab to embedder
| message | when | content |
|---|---|---|
| `tau-lab-ready` | once every module has compiled | `contract`, `meters:[{id, key, wasm_sha256, wasm_sha256_timed?}]` |
| `tau-lab-applied` | after each `tau-lab-set` | `meter, ignored:[key], clamped:{key:applied}, values:{every parameter}`; `error:"unknown meter"` |
| `tau-lab-options` | after ready, `tau-lab-set`, `tau-lab-option` | `meter, options:[{id, label, type, choices\|min,max, value, applies_to}]` for that meter |
| `tau-lab-option-applied` | after each `tau-lab-option` | `id, value, clamped:bool`; or `error:"unknown option"\|"not a number"\|"not for this meter"` |
| `tau-lab-size` | when the meter view's size changes | `view:"small"\|"full", rect:{x,y,w,h}, w, h` (in embed mode the canvas is cropped to `rect` and drawn at `w` x `h` css px) |
| `tau-lab-cost` | every other frame, once the embedder has sent any `tau-lab-*` message | `meter, frame` (commands this frame), `window:{n,mean,max}` (last 120 frames), `recent:{n,mean,max}` (last 30), `budget, class, over` |
| `tau-lab-info` | after setup and each change (about 0.5 s later), and on `tau-lab-info-get` | `meter, cost:{mean,max,budget,class,over,verdict,basis,vs_bars,cell_width,cpu_low,cpu_high,engine_kpx}, table:[{key,label,options:[{value,selected,cpu_low,cpu_high,delta,kops}]}]` |

## Options (`tau-lab-option` ids)
`theme` (enum, the registry's themes), `mode` (Dark, Light), `accent` (enum, the palette), `src` (Demo, Sweep, Silence), `fullfig` (bool: full-screen view; meters with a full view), `barready` (bool, Winamp Bars), `timedg` (bool, Layered Wave), `statsok` (bool, Sound Map), `corr` (range -256..256, Sound Map). `applies_to` lists the manifest ids an option belongs to (`null` = every meter). Enum values are indexes in `choices`; bools are `true`/`false`/`0`/`1`; out-of-range values are clamped and reported.

## The verdict window
The allowance verdict is **the worst of the last 120 frames** (about 3 s at 38 Hz) against the meter's class budget (`TauCost.BUDGET[class]`: 40, 100, 400 commands): `over = window.max > budget`. `recent` (30 frames) is for a fast-moving bar, not for the verdict. The window starts again when a parameter or a cost-relevant option (`src`, `barready`, `statsok`, `corr`, `timedg`, `fullfig`) changes, so the figures follow a change at once. All numbers are model estimates (not measured on a Pocket).

## Views (`lab-data.json` `views`)
`rects.small` and `rects.full` are rectangles of the lab's 400 x 360 canvas; `meters[<id>]` says which meters have a `small` and a `full` view (the firmware's fullscreen view draws whichever meter is selected, so every selectable meter has both; a retired one has neither). Sizes and x are the firmware's (`fw2/ui/regions.h` `NP_METER_*`, `fw2/ui/fullscreen_view.c` `FS_*`, checked by the host test); the small view's y is the lab's own 152 (the firmware's Now Playing meter starts at 154; the lab's golden vectors are drawn in this box, so it is not changed). `schema.meters` is untouched by the views.

## Element ids that stay in contract 1
Kept so a page that still reads the page's controls keeps working; prefer the messages. `#theme #mode #accent #src #fullfig #barready #timedg #statsok #corr #cv #meter #infod`. The canvas is never scaled by the window (a fractional scale of the pixelated canvas made uneven bars: Omega 014 F-4), and in embed mode it has no border.

## Embed mode
No heading, panel, parameter list, split strip or details area; transparent background; no sticky stage; the canvas shows only the current view. The embedder owns the meter and its parameters.
