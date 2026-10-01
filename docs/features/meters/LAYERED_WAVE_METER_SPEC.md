# Layered Wave meter

**Status (2026-10-01):** design + Omega-first HTML module done, **no firmware yet**. Branch `meter-layered-wave`.
Everything below marked *model* comes from the preview lab (`tools/meters/preview`), not from a Pocket.

A solid-background analyser drawn as several **nested, mirrored envelope layers** that scroll away from a
pointed head and taper to dots, after the reference image (pink outer wave, violet inner wave, three dots at
the tail). Fullscreen capable, theme-role coloured, cheap enough to sit inside the existing meter budget.

## 1. Where it lives (reuse first)

| Piece | File | Notes |
|---|---|---|
| Registry manifest | `meters/planned/layered_wave/meter.json` | `planned: true`, never selectable, **not** fed to the firmware generator, so it cannot grow ROM or RAM. Reaches `tools/meters_schema.json` (what Omega reads) and the docs. Id 16 reserved. |
| Preview module | `tools/meters/preview/meters/layered_wave.js` | Same `{state, tick(ctx)}` contract as `winamp_bars.js`; draws only through `ctx.fb.rect/copy`, so command counts are exact. Float prototype; the firmware port is fixed point and gets golden frames (M3) before it is selectable. |
| Lab | `python3 tools/meters/preview/build.py --out work/meters/meter_lab.html` | One self-contained file (Omega vendors it). Pick **LAYERED WAVE**. New in the shared lab: fullscreen figure (400x323) switch, live audio file (with a Repeat checkbox) / microphone source, a CPU estimate, a split map under the canvas. |
| Capability | `blit_shift` in `tools/meter_capabilities.json` | **design-only**: a scroll by engine copy (below). Hardware-proven caps: `rect`, `hw_spectrum`, `hw_wave`, `vsync_beam`. |
| Tests | `node tools/meters/preview/test.js` | Per preset: deterministic, inside the box, within cost class; nested layers never cross; groups tile the 16 bands. |

`tools/gen_meters.py` gained `meters/planned/` (schema + docs only). That is how any future meter can reach Omega
before its firmware exists.

## 2. What is measured (hardware first)

Nothing new is needed; both inputs already exist in hardware and `mtr_in_t` carries them.

| Input | Source | Used for |
|---|---|---|
| 16 half-octave band levels, `spec[]` (0..255, bass first) | `tau_spec_bank.sv`, MMIO 0xDC-0xE4, zero CPU cost | the three frequency splits |
| Broadband peak (`peak`, from the capture's min/max) | `tau_wave_meter.sv`, MMIO 0xEC-0xFC | the DYNAMICS split |

Level is the bank's **mean |band|** and a **peak**, not RMS or loudness. Planned, in `AUDIO_METERING_RESEARCH.md`
order, each an optional later parameter: RMS (a `measure` choice), stereo correlation (turn the layers into a
width indicator, or skew the top and bottom halves L over R), a clip counter (flash the outer layer on overs),
a shared onset signal (kick the layers). None blocks this meter.

## 3. Splitting frequencies into layers

`layers` 1-6. Layer 0 is the outermost. Four splits (`split`):

| Split | Boundaries | Why |
|---|---|---|
| **OCTAVES** | equal band counts (16/N bands each; 3 layers = ~94-530, 530-4k, 4k-24k Hz) | predictable, matches the spectrum bars |
| **BASS_FINE** | group widths grow x1.6 with frequency | bass is where music has structure; most layers resolve it |
| **ENERGY** | boundaries at equal cumulative slow-averaged energy, recomputed ~4x a second | layers stay evenly "alive" whatever the track; adapts |
| **DYNAMICS** | no frequency split: every layer follows the broadband peak with its own time constant (outer slow, inner fast, inner height 0.5x) | the "echo" look of the reference; needs only the hardware peak |

`outer` (BASS or TREBLE) decides which end is the big outer wave. `nest`:
**NESTED** = layer k hears its own group *plus every group inside it*, so inner layers can never poke out of outer
ones (tested, all splits, both orders) and inner layers sit a little lower so they stay visible;
**OVERLAP** = each layer hears only its own group, layers may cross.
The lab's split map shows the 16 live bands coloured by layer, with the Hz range per layer.

Per-layer motion: attack is 4x faster than release; `response` sets release 40-600 ms; the DYNAMICS split staggers
the time constants 1.6x per layer.

## 4. Parameters (18: far over the firmware page limit of 12)

`view` (scrolling vs fixed) and the colour source (`color_mode`, `grad`, three `custom_*`) were added after the first cut; the colour block alone is 8 parameters, most hidden by `when` at any time. Before promotion out of `meters/planned/` either `MTR_MAX_PARAMS` is raised or a parameter is merged away (candidates: fold `outer` into `nest`, or drop `speed` into a preset-only constant). Every parameter carries `help` (and enums `value_help`) in the manifest: the lab shows them behind an (i) button, and Omega receives them through the schema.

| Key | Type / range | Default | Meaning |
|---|---|---|---|
| **view** | enum HISTORY, SPECTRUM | HISTORY | **HISTORY** scrolls (x = time, newest left). **SPECTRUM** does not scroll: x = frequency (bass left), layers differ by response time and height, both ends pointed. Split, Outer, Layer mode, Speed and Resolution are ignored in SPECTRUM. Cost: layers x 16 (BLOCKS) or up to layers x 400 (SMOOTH), about 195 commands for TIDE |
| layers | u8 1..6 | 3 | number of layers |
| split | enum OCTAVES, BASS_FINE, ENERGY, DYNAMICS | OCTAVES | section 3 |
| outer | enum BASS, TREBLE | BASS | which end is outermost |
| nest | enum NESTED, OVERLAP | NESTED | section 3 |
| draw | enum BLOCKS, SMOOTH, SCROLL | SCROLL | section 5 |
| **res** | u16 16..400 step 8 | 200 | **Resolution: columns across the width.** 400 = one per pixel, the maximum the screen can show |
| speed | u8 20..240 px/s | 100 | scroll speed |
| response | u8 1..100 | 45 | how quickly layers follow |
| taper | u8 0..100 % | 70 | roll-off toward the tail, pointed head, tail dots; 0 = flat |
| **color_mode** | enum ACCENT, THEME, CUSTOM | THEME | where colours come from |
| grad | enum TINTS, SHADES, ANALOGOUS, COMPLEMENT (ACCENT only) | TINTS | outer to inner steps from the accent: lighten toward white / darken the outer layers / hue +-30 degrees / opposite hue. Background = a tint of the accent over the theme base. Greys (a white accent) have no hue, so the hue options then stay grey |
| color_outer, color_inner, color_bg | enum of 12 **theme roles** (ACCENT, TEXT, TEXT2, OK, WARN, DANGER, PILL, ERROR, SURFACE, TRACK, BASE, BG), THEME only | DANGER, ACCENT, BASE | layer k is a ramp outer to inner; the tail fades toward the background |
| custom_outer, custom_inner, custom_bg | u16 RGB565 (`kind: rgb565`), CUSTOM only | pink, violet, deep purple | your own colours; the lab has a colour picker and a hex field, converted to the Pocket's 16-bit format (the field shows the quantised result) |

THEME and ACCENT sources are portable; CUSTOM is the one deliberate exception to "roles, never RGB" (METER_MODULE_SPEC section 18, same precedent as `vu_master`'s custom colours): a shared preset with CUSTOM colours looks the same on every theme. Otherwise colours are roles: a shared preset recolours with the theme, the
user's accent and Dark/Light for free. The background is a solid role colour filled in **one** command.
Colour is quantised into 8 age bands along the width so neighbouring columns merge into runs (fewer commands).
Not exposed yet (limit of 12): per-layer colours, tail hue shift to an accent-2 role (`TR_ACCENT2` has no theme value
yet), stereo skew, measure choice.

### Presets (8, all within the model budget)

AURORA (the reference: 3 nested layers, danger to accent), SUNSET (5 layers, bass-fine), DEEP OCEAN (4 overlapped
layers, energy split, treble outer), HALO (broadband echoes, long taper), NEON (6 layers, 400 columns, no taper),
PULSE (single chunky layer), TIDE (SPECTRUM view, no scrolling, 4 layers), SILK (smooth full redraw, the cost showcase).

## 5. Drawing and cost (Talos / Helios)

One engine command per layer per run of columns with equal height and colour. Model numbers, demo source:

| Draw | How | Commands/frame (model) |
|---|---|---|
| **SCROLL** (default) | the picture is the history: shift it with `fb_blit` copies (rows are <=127 px bursts, far strip first), clear and draw only the new columns, carve the taper with background wedges, 3 tail dots | AURORA 47, SUNSET 51, NEON 41 |
| **BLOCKS** | full redraw of `res` flat columns, only when a column was pushed | PULSE 57, RIBBON 70 |
| **SMOOTH** | full redraw, Catmull-Rom between history points at every pixel (sub-column scrolling) | SILK 225 avg, 287 worst; up to layers x 400 for lively input |

Reference: `VIZ_BARS` 36 (the baseline), the cost class budget for this meter is 400. The resolution slider is
free in SCROLL (cost follows speed and layers, not `res`), which is how "as much resolution as possible" stays
affordable. SCROLL bakes colour and taper in (clipped, not scaled); a blend-faded taper waits for the
`alpha_blend` capability to leave `shelved`.

- **Talos**: `OP_RECT` (hardware-proven) for every span; `OP_BLIT` as a scroll is the new capability (`blit_shift`,
  design-only): the overlapping copy needs a simulation case in `tb_blit_scene.v` and a hardware check before the
  meter can be selectable. `OP_BAR`/`OP_RRECT` do not fit a centred, symmetric span; a floating-bar opcode (B14)
  would not reduce the count either (one rect per layer-column is already the minimum).
- **Helios**: register as a region; draw only when `helios_rows_safe()` says the beam is clear (meters are gated
  on `R_SCAN` today); fullscreen uses the existing `fullscreen_view` (400x323 figure, `FS_FIG_H`). Planned:
  a degradable region that sheds `res`/layers through the shared `helios_audio_ok()` gate instead of `meter_afford()`.
- **Cymo** (resampler and tempo are built or planned; nothing here depends on them): tap the meter inputs where the
  hardware bank already taps (`pcm_fifo` strobe); once the resampler is live the same taps still see the source
  rate, so band edges scale with 44.1 vs 48 kHz (about 8%, ignored). Planned: scroll speed follows Cymo tempo;
  the soft-clipper's overs counter drives a clip flash.
- **Theme**: roles only; Light polarity inherited from `th_role[]`; text gamma not involved.

**CPU estimate (lab, model).** The cost panel also prints a CPU range: command issue 60-400 cycles each, column/band
evaluation 30-80 cycles, 38 Hz, 66.7 MHz, so a few tenths of a percent up to about 1-2% for the presets. It is a bracket
from assumptions, **unmeasured**; the real figure comes from the meter sweep Check (`CPU LOAD`, `DRAW STALL`) once a firmware
module exists. It also shows engine pixels written per frame (SDRAM bandwidth pressure, not CPU).

## 6. Omega

Omega needs nothing but files: `tools/meters_schema.json` (the meter, params, presets, `planned: true`) and the
lab HTML as a versioned vendor drop. Presets are all-integer vectors, so they ride the existing `.tmeter` /
`tau-meter:` forms unchanged. Live audio in the lab is an **approximation** (band layout of the bank, 54 dB
window); use the deterministic demo source for comparisons against firmware.

## 7. Build order (not started)

1. Sign off the look in the lab (owner): layer colours, taper, defaults.
2. Capability `blit_shift`: simulation case (overlapping rows, far strip first) in `tb_blit_scene.v` + reference renderer; if it fails, SCROLL falls back to BLOCKS.
3. Fixed-point port `fw/layered_wave.inc` (cold code), `mtr_in_t` input, golden frames vs the JS module (M3), promote the manifest out of `meters/planned/` (this is the step that costs ROM/RAM; check the 192 KB heap gap first).
4. Hardware Check: command counts against the model, `DRAW STALL`, fullscreen, audio L0 under the meter sweep.
5. Optional later: RMS/correlation/clip measure, accent-2 tail hue, per-layer colours via the Omega data plugin.

Open questions for the owner: keep the 3 dots and pointed head in SCROLL (they are cheap but cosmetic), and whether
BLOCKS (chunky) is worth a slot in the enum or SCROLL/SMOOTH are enough.
