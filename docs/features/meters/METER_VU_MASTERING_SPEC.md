# Mastering VU meter: segmented dB ladder + technical info overlay

**Status (2026-09-27):** design only. No firmware, RTL, tool or card change. Follows the manifest
conventions of `docs/features/meters/METER_MODULE_SPEC.md` and reuses the role/theme mechanism of `docs/features/THEME_SPEC.md`.
Extends the existing meter family (`meters/*/meter.json`) with one new selectable meter; does not touch
`meters/vu/meter.json` (see section 1).

## 1. Does this replace the `VIZ_VU` stub?

No. `meters/vu/meter.json` is not a placeholder — `fw/player.c` already wires `VIZ_VU` (index 5,
sel_index 7, "VU") to a real, fully-built analogue two-needle VU movement: a drawn arc face, tick marks,
and swinging needles per channel, driven by `peak_l`/`peak_r` through its own attack/decay constants
(`VU_ATT`/`VU_DEC`), confirmed by reading the drawing block around `fw/player.c:4797-4870`. It is a
classic "analogue" meter, cosmetically and mechanically unrelated to what this spec designs. The manifest
file is just thin (no `params`, no `caps`) because nobody has back-filled it to the fuller schema other
meters (`bars`, `winamp_bars`, `led`) now use — that is a documentation gap in the existing meter, not an
open slot for a new one.

This design is therefore a **new, separate meter**, key `vu_master` (`VIZ_VU_MASTER`), sitting alongside
`vu` in the selectable list, not replacing it. Two meters can both claim the letters "VU" in casual
conversation; the choice-list names stay distinct (`VU` for the existing needle pair, `PEAK/RMS` or
`MASTER` for this one — see section 3.3 for the exact on-screen name).

## 2. What "mastering-style segmented meter" means, and the numbers behind it

### 2.1 Rejecting a generic bar meter

`VIZ_BARS` and `VIZ_WINAMP_BARS` already exist and already use `OP_BAR`. Building a third "bars that go
up and down" meter would not meet the brief — the ask is specifically the **look and behaviour of a
professional peak/loudness ladder**: a horizontal row of discrete, evenly-gapped segments, each one a
fixed dB step, colour-coded by zone, with the far end unambiguously marked as 0 dBFS (digital full
scale, the point past which a sample clips). That is a different object from a smooth continuous bar:
the segmentation IS the readout — a mastering engineer reads "meter is sitting at segment 14 of 20,"
not "meter is at 71% height."

### 2.2 Peak meter, not VU, not PPM

Three ballistics conventions exist in real audio gear:

- **VU (classic):** 300 ms integration time, ballistic overshoot/undershoot by design, calibrated to
  +4 dBu = 0 VU. Built for analogue broadcast level-setting, not digital clip avoidance. The existing
  `VIZ_VU` meter already covers this territory (needle ballistics, `VU_ATT`/`VU_DEC`).
  the classic VU experience already exists in this project.
- **PPM (peak programme meter):** fast attack (a few ms), slow decay (multi-second), designed to catch
  and hold transients a VU needle would smooth over — the broadcast tool for "did that peak too hot."
- **Digital peak meter (dBFS ladder):** the mastering/DAW-plugin convention this brief is actually
  describing — fast attack (as close to instant as the source allows), a held peak indicator, 0 at the
  top/end of scale = digital full scale, red zone at the top warns of clipping. This is what every DAW
  channel strip, and every "mastering plugin," shows.

**Decision: digital peak meter, PPM-like fast attack + held peak, not VU ballistics.** The existing
`peak_l`/`peak_r` accumulators (`fw/player.c:1872`, computed at `fw/player.c:4500-4502` from
`MTR_HEADROOM_NUM`/`DEN = 3/4`) are already **true peak-hold-over-window** values — max magnitude over
the last audio-report window, not a mean — so they are the correct signal source for a peak meter with
zero new instrumentation. No RMS accumulator exists anywhere in `fw/player.c` (checked: only
`peak_acc`/`peak_acc_l`/`peak_acc_r`, all fed by `max(|sample|)`, confirmed at the read site
`fw/player.c:4493-4502` and the hardware wave-block path `R_WAVE_PK`, which is also documented as
"tracks max |L|/|R|"). A true RMS/loudness reading is not available without new firmware work (a running
sum of squares) — flagged as an open question in section 8, not built here.

### 2.3 dB range and segment count

Existing meters already establish a working peak-to-pixel mapping: `peak_amp * UI_WAVE_H / 32768` (linear
16-bit full-scale to pixel height, e.g. `fw/player.c:4781`). A segmented dB meter should show the same
underlying signal on a **logarithmic (dB) scale**, because that is the entire point of a "technical"
meter over a linear bar — it makes 6 dB read as one fixed number of segments everywhere in the range,
not a shrinking fraction near the top.

Decision: **-48 dBFS to 0 dBFS, 24 segments, each segment = 2 dB.**

Reasoning:
- 0 dBFS at the top segment is non-negotiable for a digital meter — it is the clip boundary and the
  `MTR_HEADROOM_NUM/DEN = 3/4` scaling already treats "3/4 of full scale" as effectively the ceiling a
  real track should approach (see the measured-headroom comment at `fw/player.c:4472-4491`: real
  material runs 25-53 of 72 px at 3/4 headroom, i.e. roughly -8 to -2 dBFS most of the time). A floor of
  -48 dBFS comfortably covers everything from that headroom-scaled ceiling down through quiet passages
  and silence, without wasting segments below the noise floor of a 16-bit-equivalent playback signal
  (dynamic range concerns of the source format aside — this is a UI floor, not a bit-depth claim).
  -60 dBFS (the "standard" digital meter floor cited in most DAW documentation) was considered and
  rejected: at 24 segments that is 2.5 dB/segment, an awkward non-integer step, and the bottom third of
  the range would sit almost permanently dark on typical mastered material, wasting screen real estate.
- 2 dB/segment is the standard "fine" mastering-meter step (many hardware bargraph meters, e.g. classic
  digital peak meters, use 1-3 dB steps in the top 20 dB and coarser below; this design uses one uniform
  step for simplicity, matching this project's own precedent of documented-but-simple choices over
  hardware-accurate multi-slope curves — see the CHLADNI spec's own "estimates, not measurements"
  honesty).
- 24 segments at ~360 px usable horizontal width (see section 4) gives ~13-14 px per segment including a
  1 px gap — a comfortably legible block size on the Pocket's 400x360 panel, similar in visual weight to
  `VIZ_LED`'s spectrum bank (10 bars) but finer-grained, which is appropriate: this meter's whole purpose
  is finer resolution than the glanceable bars.

dB-to-segment mapping (per channel, per frame):
```
db = 20 * log10(peak / 32768)         # peak is the existing 16-bit-scale accumulator
db = clamp(db, -48, 0)
lit_segments = round((db + 48) / 2)   # 0..24
```
No floating point exists in this firmware (rv32im, no hardware float — confirmed by the FLAC/MP3 profiler
work explicitly avoiding float this week, B-343/B-345). `log10` must be a small fixed-point lookup table
over peak's 16-bit range (256 or 512-entry table mapping magnitude to a pre-quantized segment count
directly — collapsing "compute dB then quantize" into one table lookup, the same offline-table pattern
already used for the AA gamma table and the MP3 polyphase ROM). This is the one new piece of firmware
math this design needs; it is a table lookup, not a runtime transcendental, so it costs one small
`static const uint8_t db_to_segment[N]` ROM table and a shift/index, in the same spirit as
`tools/gen_mp3_poly_rom.py`'s offline-generated tables. No RTL needed.

### 2.4 Colour zones

Following the same 3/4-headroom logic (section 2.3) and standard mastering-meter convention:

- **Green** — segments 1-16 (bottom 32 dB, -48 to -16 dBFS): normal operating range.
- **Yellow** — segments 17-21 (-16 to -6 dBFS): getting hot, mastering engineers watch here.
- **Red** — segments 22-24 (-6 to 0 dBFS): near or at clipping risk.

These thresholds are a judgement call stated plainly as such (not derived from a measurement); they
follow the widely-used "yellow starts around -12 to -6 dBFS, red in the last few dB" convention common to
DAW meters, adjusted so the yellow band starts a little earlier (-16) given this meter's fast peak-hold
response will flash yellow/red more readily than a slower VU needle would, which is correct and desired
behaviour for a clip-warning tool.

**A held peak indicator** (a single 1-segment-wide brighter marker at the highest segment reached in the
last N ms, decaying after a hold time) is included — the standard "peak-hold LED" on every hardware
ladder meter. Reuses the exact peak-hold/fall design (`peak_hold_ms`, `peak_fall`, `peak_gravity`)
`winamp_bars` already ships as parameters (`meters/winamp_bars/meter.json`), so no new ballistics design
is needed — this meter borrows that parameter shape directly (see section 6).

### 2.5 Mono-summed vs stereo (L/R)

**Decision: stereo, two independent horizontal ladders stacked (L above R), each full width.**

Reasoning: `peak_l` and `peak_r` are already tracked independently and are exactly what a mastering
engineer needs — mono-summing them would throw away the one piece of information a "technical" meter
exists to show (channel balance, one channel clipping while the other is fine, mono-compatibility
issues). The existing `VIZ_VU` meter already proves stereo is the right call for "serious" meters in this
project (it is two needles, not one combined dial), and `winamp_bars`/`led` are the project's
already-established mono-style "glanceable" meters — this new meter is deliberately positioned as the
*more* technical alternative, so stereo is the differentiator, not a redundant third mono option.

## 3. Layout

### 3.1 Normal (in-box) mode

Uses the existing meter box geometry (`UI_MARGIN = 16`, `UI_WAVE_Y = 152`, `UI_WAVE_H = 122`, width from
`ui_wave_w()`, typically ~246 px with the art panel showing or ~360 px with it hidden — same box every
other meter uses, read live each frame per the project's own established lesson in `fw/player.c:4797-4801`
about not assuming a fixed width).

```
y  UI_WAVE_Y +  0..12   "L" label, then the 24-segment ladder, held-peak marker
y  UI_WAVE_Y + 14       thin dB scale line: tick marks at -40/-30/-20/-12/-6/-3/0, small labels
y  UI_WAVE_Y + 26..38   "R" label, then the 24-segment ladder, held-peak marker
y  UI_WAVE_Y + 40..H    technical info overlay (section 5), if enabled; otherwise left blank/idle
```

At ~246-360 px usable width minus a small label gutter (~20 px for "L"/"R"), 24 segments fit with a
legible per-segment width (roughly 9-14 px depending on panel state) — narrower than the 13-14 px assumed
in section 2.3's reasoning at full width, still comfortably readable, consistent with how `VIZ_LED`
already handles the same width variability.

### 3.2 Fullscreen mode (Select+Y)

`fw/fullscreen.inc` currently lists only Chladni and the two Winamp meters as fullscreen-capable, and
says explicitly why: "the other meters use fixed constants for the normal box in hundreds of places."
This meter's drawing plan is written from the start to avoid that trap — every coordinate is derived from
a passed-in box rect (`x0, y, w, h` — exactly the `mtr_in_t` contract's own fields from
`docs/features/meters/METER_MODULE_SPEC.md` section 3), not from `UI_WAVE_*` literals — so it is fullscreen-ready by
construction, not by a later retrofit. In fullscreen (`FS_FIG_H = 323`), the ladder simply scales to the
full 400 px width and a taller per-segment height, and the technical info overlay (section 5) gets much
more room to show every field instead of a truncated subset. This is recorded as a design intent for
whoever builds it (M-series build order, section 7); it is not proven until real firmware exists.

### 3.3 Choice-list name and thumbnail

On-screen name: **`MASTER VU`** — distinct from the existing `VU` entry, signals "the technical one."
Thumbnail: a small static rendering of the segment ladder (green/yellow/red blocks), generated offline
the same way `tools/gen_meter_thumbs.py` already renders every other meter's 56x32 preview — no new
mechanism needed.

## 4. Hardware primitive choice and draw-command cost

### 4.1 Why not `OP_BAR`

`OP_BAR`'s RTL contract (`src/fpga/core/mp3_fb.sv:578-590`, `fb_bar()`) is a **vertical**, two-tone
fill: `h` rows total, `lit` of them lit from the bottom in one foreground colour, the rest in one
background colour — one fg/bg pair per call. It is the right primitive for `VIZ_BARS`/`VIZ_WINAMP_BARS`
(vertical columns, two colours: lit/unlit). It is the wrong shape for this meter for two independent
reasons:

1. **Orientation.** This meter is horizontal (lit from the left), and `OP_BAR`'s hardware sequencer only
   knows "lit rows from the bottom of a column" (confirmed by reading the RTL comment and the
   `cmd_glyph`-as-lit-row-count convention). There is no horizontal equivalent opcode.
2. **Three colours, not two.** A segmented meter's lit portion is not one flat colour — it is
   green-then-yellow-then-red depending on how far it has climbed. `OP_BAR` has exactly one fg colour
   per command.

### 4.2 Chosen primitive: `fb_rect()` (plain `OP_RECT`), one call per contiguous colour run

Because the meter is **discrete segments with a gap between them** (the whole visual point, per section
2.1), not a continuous fill, the natural and cheapest draw plan is: draw the *background/unlit* colour
for the whole ladder box in one call (`fb_rect`), then draw each **lit segment** as its own small
`fb_rect` call in its zone colour, skipping unlit segments entirely (they're already background). This
is the same "erase whole background once, draw only what's lit" economy `winamp_bars`' peak-cap already
uses, and it means cost scales with how loud the material is, not with the segment count — quiet passages
(most program material, per the headroom analysis in section 2.3) draw fewer commands, not more.

Per channel, per frame, worst case (peak at 0 dBFS, all 24 segments lit):
- 1 background rect (whole ladder, one call, covers all unlit space at once — cheaper than 24 unlit
  rects)
- up to 24 lit-segment rects (one per lit segment; could be coalesced into up to 3 rects, one per colour
  zone, if the lit run within each zone is contiguous — see the coalescing note below)
- 1 held-peak marker rect

**Coalescing optimisation (recommended, not optional):** since lit segments are always a *contiguous run
from segment 1 up to the current level*, and each zone (green/yellow/red) is a fixed contiguous range of
segment indices, the lit portion can always be drawn as **at most 3 rects** (one per zone that has at
least one lit segment in it), not up to 24 individual rects — exactly the same principle `OP_BAR` itself
uses (one fg span, one bg span) generalised to three colour spans instead of two. This is a firmware-only
optimisation (no new hardware primitive) and is the recommended implementation, not a stretch goal.

**Cost estimate, worst case (0 dBFS on both channels, all zones lit), coalesced:**
- 2 channels x (1 background rect + up to 3 coalesced zone rects + 1 peak marker) = 2 x 5 = **10 rects**
- Plus the dB scale line/ticks (drawn once, cached like `VIZ_VU`'s face — see section 4.3), not per-frame.

**Typical case (quiet-to-moderate material, green zone only lit):**
- 2 channels x (1 background rect + 1 lit-green rect + 1 peak marker) = **6 rects**

Compared against this codebase's own measured baselines (`tools/meter_cost_estimate.py`): `VIZ_BARS`
(the project's own cost-class-0 reference) budgets 100 commands and the doc cites its real baseline as
36-49; `winamp_bars` budgets 100; `winamp_scope` (a structurally more expensive column-per-pixel shape)
budgets 250. At a worst-case 10 rects/frame plus a one-time cached scale line, this meter is
**cost_class 0** (<=40 commands/frame budget), comfortably cheaper than every existing bar-family meter,
because the discrete-segment shape with zone coalescing is inherently cheap — this is one of the reasons
a segmented ladder is a *good* fit for this engine, not just a stylistic choice.

### 4.3 Static elements drawn once (face-caching, `VIZ_VU`'s own proven pattern)

The dB scale line, tick marks and "L"/"R" labels never change frame to frame — only the lit segments and
the peak marker do. `VIZ_VU` already establishes exactly this discipline (`vu_face` cached, only the
needle erased/redrawn each frame, `fw/player.c:4803-4819`) after learning the hard way that redrawing
static geometry every frame caused visible flicker during scanout. This design reuses that same
`*_face`/`force`-driven cache pattern rather than re-deriving it, per the meter-module contract's own
rule 4 ("`open` and `force` both mean assume nothing about the pixels" — i.e. draw the face fresh only
on `open`/`force`, never on an ordinary `tick`).

## 5. Technical info overlay

### 5.1 The toggle (setting 1)

`params`: `{ "key": "info", "label": "Tech info", "type": "bool", "default": 1 }`. When off, the meter
box below the two ladders (section 3.1's `y +40..H` region) is left as background/theme colour — the
ladders themselves are always shown; only the overlay is optional, since the overlay is the "extra" and
the segmented ladder is the meter's core identity.

### 5.2 Fields, and exactly where each one already exists

Every field below is confirmed present in `fw/player.c` today (not assumed), with its exact source and a
note on any availability constraint:

| Field | Source | Availability |
|---|---|---|
| Format | `track_fmt` (`FMT_MP3`/`FMT_FLAC`, `fw/player.c:817-818`) | always |
| Sample rate | `track_hz` (MP3) / `fl.rate` via `fl_rate_hz` (FLAC, `fw/player.c:1028`) | always |
| Bit depth | `fl.bps` (FLAC only — `fw/player.c:8510-8512`, 8/16/20/24; MP3 has no bit-depth concept, show "—" or omit the row for MP3) | FLAC only |
| Encoder string | `track_encoder[12]` (`fw/player.c:927`, e.g. LAME/Lavf-style tag, may be empty) | when tagged |
| Bitrate | `track_kbps` (`fw/player.c:772`, computed differently for CBR MP3 vs FLAC's derived-from-size estimate at `fw/player.c:8619`) | always (0 briefly at track start) |
| CPU load | `ui_cpu_pct()` (`fw/player.c:1217`, `100 - fl_idle_pct` when playing) | always |
| SDRAM busy % | `R_SDR_BUSY` (`fw/player.c:99`, gated on `TAU_SDRAM_BUSY`; see 5.3) | diagnostic-build/bitstream only |
| Decode-stage profile | `mp3_huff_cyc`/`mp3_imdct_cyc`/`mp3_sub_cyc`/... under `#if MP3_PROFILE`, `flac_res_cyc`/`flac_unary_calls` under `#if FLAC_PROFILE` (`fw/player.c:5716-5748`) | diagnostic-profile build only |

Proposed overlay layout (6-8 text rows, matching the existing small-text info-row style used on the Info
page and Check pages, not a new font/size):

```
MP3  320 kbps  44.1 kHz              (format, bitrate, rate — one line)
LAME3.100                            (encoder, if present; omitted if empty, not a blank row)
CPU  38%                             (always)
SDRAM 12%   (release/diagnostic bitstreams that lack TAU_SDRAM_BUSY: this row omitted, not shown as 0%)
H 4% I 61% S 29%  (MP3_PROFILE builds only; FLAC builds show R xx% U xx% instead)
```

**Decision: the overlay ALWAYS uses the release-safe field set (format/rate/bit-depth/encoder/bitrate/CPU)
and treats SDRAM-busy and decode-profiler rows as conditionally-present extras, not core fields.** This
answers the brief's own open point ("decide whether your design wants \[the profiler\] in the release
meter too and what that costs"): the profiler counters are compiled out entirely on the release firmware
target (`#if MP3_PROFILE` / `#if FLAC_PROFILE`, both diagnostic-only macros per `fw/player.c:59,1984`),
so including them unconditionally is not possible without turning them on for `release`, which nobody has
proposed and which this design does not propose either — that would cost ROM/heap on every user's build
for a diagnostic-only readout. Instead, the overlay's row list is itself conditional on which macros are
compiled in, exactly the same pattern the Info page and Check page already use for the same counters
(`fw/player.c:5716` `#if MP3_PROFILE`, `docs/AUDIT_TRAIL.md` B-089's "two independent accumulator sets"
design). On a plain release build, the overlay simply has 2-3 fewer rows and reclaims that vertical
space for the segmented ladders — never a blank/placeholder row.

### 5.3 SDRAM-busy honesty

Same rule the Blit-storm Check test already applies (`fw/suite.inc`'s `CT_BLT`, per AUDIT_TRAIL B-127):
`R_SDR_BUSY` reads a hardwired 0 on any bitstream without `TAU_SDRAM_BUSY`. The overlay must not show a
literal "0%" in that case (a false, misleadingly-precise reading) — it omits the row entirely when the
counter is not known-present, using the same boot-time capability probe already established
(`tau_sdram_busy_present()`-equivalent check other diagnostic-gated UI already performs).

## 6. Settings (both required by the brief)

Both settings follow the generic Configure-page pattern already used for Winamp Bars/Scope
(`wviz_bars_cfg_t`/`wviz_scope_cfg_t` in `fw/player.c`, reachable from Settings > Appearance > Meter >
Configure) and are expressed as plain `params` in the manifest — no new UI mechanism, per
`docs/features/meters/METER_MODULE_SPEC.md` section 4's "parameters are deliberately limited to u8/u16/bool/enum... no
meter can require a UI widget nobody built."

1. **Info overlay toggle** — `info: bool`, default on (section 5.1).
2. **Segment colour mode** — `color_mode: enum ["THEME", "CUSTOM"]`, default `THEME`.
   - `THEME`: green/yellow/red segments read `role[TR_OK]`, `role[TR_WARN]`, `role[TR_DANGER]` — the
     exact roles `docs/features/THEME_SPEC.md` already names for this purpose ("Status colours are themeable
     (ok/warn/danger, **the VU ladder's green/amber/red**)" — THEME_SPEC.md line 9 already anticipates
     this meter by name). No new role is needed; this design is the first real consumer of that
     documented-but-previously-unused intent.
   - `CUSTOM`: three additional `u16` (RGB565) parameters, `color_green`/`color_yellow`/`color_red`,
     shown only `"when": {"color_mode": 1}` (the manifest's existing conditional-field mechanism, used
     identically by `winamp_bars`' `peak_gravity`/`peak_hold_ms`/`peak_fall` fields being conditional on
     `peak_on`). RGB565 is the framebuffer's native format (confirmed true-colour throughout, no
     per-pixel alpha, per `docs/features/HELIOS_SPEC.md` section 7.4's colour audit) — no format conversion is
     needed, the Configure page's existing numeric-field editing already handles a raw 16-bit value the
     same way other colour-ish fields would.
   - Held-peak marker colour: always follows the zone it currently sits in (reads the same three roles
     or custom colours, brightened/tone-shifted the same way `VIZ_VU`'s own peak zone already is —
     "the accent at full strength" pattern at `fw/player.c:4857-4860` — no fourth colour setting needed).

Manifest sketch (`meters/vu_master/meter.json`, following the exact shape of `meters/winamp_bars/meter.json`):

```jsonc
{
  "schema": 1,
  "key": "vu_master", "id": <next free>, "name": "MASTER VU",
  "flags": ["selectable", "stereo"], "cost_class": 0,
  "considered": "Segmented ladder + zone colouring is 3 coalesced OP_RECT spans per channel per frame, cheaper than OP_BAR's own vertical-bar meters; horizontal orientation and 3-colour zones rule OP_BAR out (section 4.1).",
  "params": [
    { "key": "info",       "label": "Tech info",    "type": "bool", "default": 1 },
    { "key": "color_mode", "label": "Segment colour", "type": "enum", "values": ["THEME", "CUSTOM"], "default": 0 },
    { "key": "color_green",  "label": "Green",  "type": "u16", "min": 0, "max": 65535, "default": 0, "when": { "color_mode": 1 } },
    { "key": "color_yellow", "label": "Yellow", "type": "u16", "min": 0, "max": 65535, "default": 0, "when": { "color_mode": 1 } },
    { "key": "color_red",    "label": "Red",    "type": "u16", "min": 0, "max": 65535, "default": 0, "when": { "color_mode": 1 } }
  ],
  "roles_used": ["ok", "warn", "danger"]
}
```

(`color_*` defaults of 0 are placeholders standing in for "copy the current theme's ok/warn/danger at
first switch to CUSTOM" — the exact seeding behaviour is an implementation detail for the build phase,
not a design decision that changes this spec.)

## 7. Build order

Following this project's own established phased-build convention (Chladni's M-series, Winamp's B-series):

- **V0 (design):** this document. Done.
- **V1 (offline lab, no firmware):** a browser lab (`tools/lab/vu_master_lab.html`), mirroring the
  existing Chladni/Fluid-Bars-lab pattern — draws the real 400x360 RGB565 framebuffer, the dB-to-segment
  table, zone colouring and peak-hold, against a synthetic or real audio trace, so the exact segment
  thresholds and colours can be tuned before any firmware is written (same value the Chladni lab and
  Fluid Bars lab already proved: catching wrong-feel ballistics before a card round-trip).
- **V2 (firmware, host-tested only):** `fw/vu_master.inc` module against the `mtr_desc_t` contract
  (`docs/features/meters/METER_MODULE_SPEC.md` section 3), `db_to_segment[]` table generated by a small offline tool
  (same pattern as `tools/gen_mp3_poly_rom.py`), unit-tested against the Python/JS reference the same way
  `sim/test_chladni_module.py` checks `fw/chladni_core.h`. No RTL needed — `OP_RECT`/`fb_rect()` already
  exists and is already used everywhere.
- **V3 (manifest + generator wiring):** `meters/vu_master/meter.json`, regenerate via
  `tools/gen_meters.py` (if M0's generator work has landed by then) or hand-wire into the six places
  `docs/features/meters/METER_MODULE_SPEC.md` section 2 names as the current manual cost, matching whatever the meter
  system's build state is at the time this is picked up.
- **V4 (card, hardware read):** install, confirm segment/colour/peak-hold behaviour against real
  playback, confirm the info overlay's conditional rows behave correctly on both a release-shaped and a
  diagnostic-profile-shaped build (two separate hardware checks, since the overlay's row count differs
  between them by design).
- **V5 (fullscreen):** verify the box-rect-relative drawing plan (section 3.2) actually holds up
  fullscreen without further changes; add to `fw/fullscreen.inc`'s capable list if so.

No RTL work is required at any phase — the entire design is built from `fb_rect()` (existing `OP_RECT`),
matching the brief's own stated preference.

## 8. Open questions (for the owner)

1. **No RMS/loudness signal exists today.** This design uses true-peak (fast, per section 2.2), which is
   the correct convention for a clip-warning ladder meter, but a "mastering" context sometimes also wants
   a loudness/RMS reading (a second, slower-moving reference mark or a dedicated LUFS-style meter). Out
   of scope here — flagged as a possible V6/future meter, not folded into this one, so this meter stays
   a clean single-purpose peak ladder rather than growing two ballistics models in one ticker.
2. **Exact dB thresholds (section 2.3/2.4) are a judgement call**, not derived from a real-music
   measurement the way the `3/4` headroom constant was. Recommend validating them against real tracks in
   the V1 lab (same method the headroom constant used) before locking the firmware table, and treat the
   numbers in this document as a starting point, not a final spec.
3. **Custom-colour seeding (section 6):** should switching `color_mode` from THEME to CUSTOM pre-fill the
   three custom fields with the theme's current ok/warn/danger values (nicer first-edit experience) or
   start at a fixed default (simpler firmware, matches how other meters' `when`-gated fields behave
   today)? Left as a build-time call, not a design blocker.
4. **Peak-hold parameter reuse (section 2.4):** this design proposes reusing `winamp_bars`' exact
   peak-hold parameter shape (`peak_hold_ms`, `peak_fall`, `peak_gravity`) rather than exposing them as
   new settings on this meter — i.e., peak-hold behaviour would be fixed/opinionated for this meter
   (mastering meters conventionally have simple, non-configurable peak-hold), not user-tunable. Confirm
   this is acceptable, or whether the owner wants the same three knobs exposed here too.
5. **Bit-depth row for MP3:** MP3 has no bit-depth concept (it's a lossy transform-coded format, not
   PCM-at-a-depth). This design omits the row for MP3 rather than showing a misleading "16-bit." Confirm
   that reads correctly rather than looking like a missing field.
