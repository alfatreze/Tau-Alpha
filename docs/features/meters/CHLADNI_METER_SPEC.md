# Chladni meters: algorithms, blit-engine use, proposed engine features

**Status (2026-09-25, B-268):** design plus a working preview lab. No firmware, RTL or card change. All
blit cycle figures below are model estimates from the RTL structure, not measurements. Audio figures come from
one synthetic demo track, not real music.

Preview lab: `tools/lab/chladni_lab.html` (single file; open in a browser, or use the published artifact).
It draws the real 400x110 meter box in RGB565, runs the Q14 integer field kernel the firmware would use,
models the Tau 16-band spectrum ballistics, and prints per-frame cost plus a benchmark table.

## 1. What a Chladni figure is, in the terms we can compute

Sand on a vibrating plate collects on the nodal lines, where the plate does not move. For a square plate a
mode is `phi(x,y) = cos(m pi x) cos(n pi y) + s cos(n pi x) cos(m pi y)`, with `s = +1` or `-1` (the two
families in the reference picture). The figure is the zero set of `phi`. Three properties drive the design:

- **Pitch selects the mode.** Mode frequency grows with `m^2 + n^2`, so a low tone gives a simple X or cross
  and a high tone a dense lattice. The viewer can read pitch from the figure.
- **Several tones superpose.** The figure is the zero set of the weighted sum, so it deforms continuously as
  the spectrum changes. This is the motion.
- **Sand escapes only where the plate moves.** A grain jumps with probability proportional to `|phi|`, so
  nodal lines trap it. That is why real figures dissolve when driven hard and re-form when it calms.

Separable modes make the field cheap: per row precompute `a_k = w_k C[n_k][j]` and `b_k = w_k s_k C[m_k][j]`,
then each cell costs `2K` multiply-adds from two small cosine tables. No trig at runtime, no division.

## 2. Three algorithms compared

Lab benchmark: 40 simulated seconds of the demo track (118 bpm: kick, bass, arpeggio, snare, hats), 30 fps,
54x54 cells on a 108x108 plate, row-burst blit path. "Steady change" is the share of cells that toggle per frame
away from triggers (flicker); "punch" is the same share in the 6 frames after a trigger divided by steady
(how clearly a trigger reads).

| Engine | Layout | Cmds/frame | CPU us/frame | SDRAM %/frame | Steady change % | Punch x |
|---|---|---|---|---|---|---|
| Live field | one plate | 1 | 2,914 | 0.6 | 2.3 | 4.1 |
| Live field | three plates | 3 | 7,425 | 1.7 | 2.8 | 2.2 |
| Sand (700 grains) | one plate | 482 | 3,419 | 0.7 | 3.8 | 1.8 |
| Sand | three plates | 1,716 | 8,320 | 2.5 | 5.0 | 1.2 |
| Dictionary | one plate | 1 | 2 | 0.8 | 0.18 | 18.2 |
| Dictionary | three plates | 4 | 11 | 2.3 | 0.39 | 3.4 |

For reference `VIZ_BARS` is 36 commands per frame and the CPU budget at 30 fps is 33,333 us.

**Live field.** Each of the 16 bands weights one mode (low band, simple mode). The strongest K bands (default 3)
are kept and their weights slew toward `level^2`. A per-mode phase moves `s` continuously between the two
families, so even a held note breathes. Best fidelity to the physics and the only engine where a chord looks
like a chord. Costs CPU, not blit.

**Sand.** Grains descend `phi grad(phi)` and are kicked in proportion to `|phi|` and an agitation term that a
trigger raises for about 250 ms. With a steady tone 93 to 99 percent of grains sit on the nodal set within 5 s
(lab check). Most organic behaviour, and a natural memory: a bass hit scatters the sand and it re-forms into the
next figure over half a second. Heaviest engine by far: every grain that moves needs an erase and a draw.

**Dictionary.** Only the dominant band's pure mode is shown, chosen from a bank of figures generated once when the
meter opens, and changes dissolve over 250 ms. Almost no CPU, very stable, the most legible. It cannot show two
tones and it changes only in steps.

### What the tuning showed (this changes the design)

- **Mode weights must be slow.** With weights following the band levels at display rate, the field flickered:
  13 percent of cells toggled per frame in steady passages, because a 16th-note arpeggio moves the strongest band
  every 130 ms. Integrating weights over about a second cut it to 2 to 4 percent and raised punch from 1.4 to 4.
  The rule: **slow signals choose the figure, fast signals only change brightness, line width and sand agitation.**
  Morph rate had no measurable effect on flicker; slew and the number of active modes did.
- **Triggers need a refractory time.** Spectral rise fired 137 times per minute at 350 ms and 86 at 600 ms on a
  track with 118 kicks per minute. One figure change per beat is too busy; the default is 600 ms.
- **Three plates are less stable per plate** (steady 2.8 vs 2.3 percent, punch 2.2 vs 4.1) because each plate
  sees fewer bands and its own noise. The pitch-split triptych (bass, mid, treble plate) is still the most
  striking layout and echoes the reference picture.
- **Constant threshold gives fat crossings.** Lines thicken where the gradient is small (a bright star at the
  X centre in the gallery). An optional even-width mode divides by a gradient taken from neighbouring cells
  already in the plane, about 3 extra operations per cell.

### Trigger rules (all in the lab)

1. Spectral rise: sum of positive band-level increases over the plate's bands exceeds a gain times a running mean
   plus a floor, after a refractory time. Works on drums.
2. Bass rise: the same over the lowest four bands only. Ties the change to the kick.
3. Tone change: the strongest band changes and holds for 4 windows (about 95 ms). Works on melodic music where rise
   is weak.

A trigger advances the scene (a different band-to-mode map), adds a quarter turn to the family phase, flashes the
palette and, for sand, agitates the grains. All are cheap; only the scene change moves lines.

## 3. Blit engine use, and the cost of each path

Three ways to get the figure to the screen, measured in the lab's cost model:

| Path | Commands | SDRAM per frame (one / three plates) | Notes |
|---|---|---|---|
| Rects, one per run | 219 / 608 | 0.9 / 2.4 % | Works today. 6x to 17x the commands of `VIZ_BARS`. |
| `OP_SBLIT` today (one SDRAM read per output pixel) | 1 / 3 | **8.4 / 25.2 %** | Works today. Source plane must already be RGB565. Solo is plausible; three plates is not. |
| Row-burst scaled blit (proposed B18) | 1 / 3 | **0.6 / 1.7 %** | Each source row read once, each output row streamed from the row buffer. |

The path that exists today is workable for one plate and unaffordable for three. Adding B18 is what makes the
triptych and the dictionary engine cheap. With B18 the CPU, not the engine, is the limit: the live field costs
2.9 ms per frame per plate at 54x54 cells (about 9 percent of a frame), so ship at 36x36 cells (3x scale, about
1.3 ms) and let `meter_afford()` yield to audio as the other meters do.

Sand needs a different tool. 480 to 1,700 commands per frame is 13x to 48x `VIZ_BARS`. See B20 and the finding
under B16 below.

## 4. Proposed engine features

Numbered after B17 in `docs/features/PHASE_F_SPEC.md`. Each is general; the Chladni meter is one use among several.
None is built. Every one adds a source into the shared `glyphbuf` write network unless noted, which has cost
timing margin three times (B-109, B-150, B-243), so each needs a fit and probably a retiming register.

**B18. Row-burst scaled blit, with optional CLUT and colour key.** Extend the streaming path that `OP_COPY`/
`OP_BLIT` use: read a source row into the row buffer once, then write output rows by stepping the buffer index with
the existing Bresenham registers (horizontal) and re-reading only when the source row changes (vertical). Stage 1:
RGB565 source, no CLUT. Stage 2: index source through the B8 CLUT, plus B2 keying, so a palette write animates
every pixel of a plane for the cost of 16 stores.
Uses beyond this meter:
- Geiss field: the B-210 lab measured 70 to 100 rect runs per frame; one blit of a small plane replaces them.
- QR pages in Check: a 61-module code drawn per module today; one 1-bit plane through a 2-entry CLUT at 3x.
- Spectrogram or waterfall heat map: an index plane through a colour ramp instead of per-column gradient work.
- Helios transitions: scale-in and scale-out of panels.
- Library grid: album covers downscaled to thumbnails without a software resampler.

**B19. Flip flags on `OP_BLIT`, `OP_SBLIT`, `OP_CBLIT` (horizontal, vertical).** Horizontal: the row buffer is read
in reverse. Vertical: a negative source stride, which the address adder may already handle by wrap.
Uses: same-parity Chladni figures have full square symmetry (every mode has `m` and `n` of equal parity and
`|phi|` is symmetric), so compute one quadrant and mirror it, cutting field cost roughly 4x (about 0.7 ms at
54x54). Also `VIZ_MIRROR`, a left arrow reused as a right arrow, and a faint reflection under the cover art.
Constraint the lab does not model: the symmetric shortcut holds only when all active modes share parity, so the
scene tables would need a parity class per scene.

**B20. Index-plane saturating add and subtract (region).** Read-modify-write a burst: `dst = sat(dst +/- k)`.
Uses: sand density accumulation and decay, scope phosphor persistence (the Fluid Bars lab's trail fade needed
alpha blend, which is shelved; this gets the same look from index arithmetic), fire and plasma, heat decay in a
waterfall, a cheap glow. Highest timing risk of the three because it writes back into the row buffer with an
adder in the path; a separate small buffer would avoid loading `glyphbuf`.

**B16 (point list), finding.** The Chladni sand meter is the first concrete user of a scatter primitive, and it
shows B16 as written would not help. A point list lives in SDRAM and the CPU writes it through the window at
about 0.8 us per word, while a draw command costs roughly 5 MMIO stores, likely under 0.5 us. Listing the points
in memory is slower than issuing them. B16 pays off only if the *engine* generates the points. Recommend
downgrading B16 until a measured command cost says otherwise, and treating B20 as the sand primitive (splat into
an index plane, decay, blit through the CLUT).

## 5. Recommended build

1. **Ship first on today's engine: dictionary meter, tiled across the box (section 5b), `OP_SBLIT` path** (almost no CPU; roughly 8 to 12
   percent SDRAM per frame, the upper figure while a dissolve draws two layers). Appended as `VIZ_CHLADNI` after `VIZ_WINAMP_SCOPE` (append-only rule, `docs/features/meters/METER_CONFIG_SPEC.md`),
   plane generated once when the meter opens, no new persistence.
2. **Then live field, 3 px cells, 4 tiles across, 15 fps.** Same draw path, adds the CPU kernel and the slow-weight rule.
3. **Then B18** (fit first), which unlocks the triptych and moves both engines to under 2 percent SDRAM.
4. **Sand last**, and only after B20 or a measured command cost; it is the most beautiful and the most expensive.

No configurator: the Winamp configurator is parked. Presets are constants; tuning happens in the lab.

## 5b. Filling the whole meter box: tiling (added B-269)

The full draw area is 400x110. Computing one wide plate would cost 4x the CPU (4,921 cells at 3 px against 1,221
for a tile), so the meter computes **one tile and replicates it with plain copies**.

**Why it is seamless.** `phi(x+1,y) = (-1)^m phi(x,y)` for a mode with `m` and `n` of equal parity, and the same
in `y` with `(-1)^n`. If every active mode has the same parity class, the whole field only changes sign across a
tile edge, so the nodal set repeats exactly. Lab check: the next period differs from the tile by 0.003 (Q14
rounding). Consequences:
- Modes are drawn from two classes, both even (2,0) (4,0) (4,2)... or both odd (3,1) (5,1) (5,3)..., 15 modes each.
  A trigger switches class, so a trigger visibly changes the family of figure.
- The lowest mixed-parity figures (1,0) and (2,1) are not available in tiled mode.
- With the flip flags of B19 the parity rule disappears, because a mirrored tile is continuous for any modes.
  That is a second, independent argument for B19.
- Sand tiles too: the grain domain wraps at the tile edge, and one simulation fills four tiles.

**Replication with the engine that exists.** Render the tile once at the left edge, then copy framebuffer to
framebuffer with `OP_BLIT` (hardware-proven, `A_COPYRD` burst path, no new RTL) by doubling: width 100 to 200
to 400, then rows if the tile is shorter than the box. Copies are at most 127 words wide, so 400 px needs 3 to 4
commands per step. Only the changed frames need drawing; the dictionary engine redraws nothing between figure
changes.

**Zoom rule.** Line spacing must stay resolvable. A mode of order `m` has a half-wave of `tile_px / m`, and the
field is sampled in cells of `c` px, so require `m <= tile_px / (3c)` (three cells per half-wave, sand: nine
pixels per half-wave). The lab clamps the mode pool to that. Default **4 tiles across (100x110 px, nearly
square), 3 px cells, so 33x37 cells per tile, modes up to order 11**. Eight tiles across (50 px) leaves only
orders up to 5 and looks like mush; two tiles across looks coarse and doubles the CPU.

**Numbers, live field, demo track, per 30 fps frame (model estimates):**

| Zoom, cell | Tile cells | CPU us | SDRAM % | Commands |
|---|---|---|---|---|
| 4 across, 2 px | 2,750 | 2,752 | 3.0 | 4 |
| **4 across, 3 px (default)** | 1,221 | **1,224** (1,590 with even line width) | **2.9** | 4 |
| 4 across, 4 px | 891 | 703 | 2.9 | 4 |
| 8 across, 3 px | 350 | 315 | 3.3 | 9 |
| 2 across, 3 px | 2,442 | 2,480 | 2.5 | 3 |

Other engines at the default zoom: sand 575 commands, 3.5 ms CPU, 3.3 % SDRAM (about 5 percent steady change,
punch 1.5); dictionary 2 commands, 0.002 ms, 1.5 % SDRAM (0.3 percent steady, punch 12.7). Blit path at the
default: rects 162 commands, 3.0 %; today's `OP_SBLIT` for the tile plus copies 10.4 %; row-burst (B18) 2.9 %.

**Breathing room for audio.** The reference is the hardware-proven blit storm (B-146): 15.8 percent SDRAM busy,
zero late underruns. The lab budgets one third of that and a tenth of the CPU frame:
- SDRAM at most 5 percent per frame. Every tiled figure passes (2.5 to 3.3 percent); today's `OP_SBLIT` does not (10.4).
- CPU at most 10 percent of a 30 fps frame (3.3 ms). The default passes at 1.2 to 1.6 ms.
- **Field rate.** The figure moves slowly by design, so it can update at 15 fps and halve both: 613 us and 1.5 %
  SDRAM at 15 fps, 408 us and 1.0 % at 10 fps, with steady flicker falling from 2.4 to 1.4 percent. Recommend
  15 fps as the shipping default and let `meter_afford()` skip frames when the audio FIFO is low.
- Draw gating: the copies write 44k pixels, so route the whole box through Helios `helios_rows_safe()` and draw
  it only when the beam is clear of the box or below it.
- The tile is flat-coloured, so the gradient no longer shows behind the meter. Draw the box as one rounded
  rectangle (`OP_RRECT`) first; keying the tile to keep the gradient would cost a destination read per pixel.

**Revised build order.** The tiled dictionary meter needs only the tile render plus copies and no CPU beyond the
tile, so it now ships first, ahead of the single plate. Live field follows (adds the field kernel and the parity
classes), then B18 (removes the `OP_SBLIT` per-pixel cost of the tile render), then sand.

## 5c. Templates (added B-270)

Five presets in the lab's Template selector. Each sets engine, layout, zoom, colour, line width, mode integration,
trigger and field rate together. Benchmark on the demo track, blit path row-burst (B18), limits SDRAM 5 % and CPU
10 % of a 30 fps frame. Estimates, not hardware.

| Template | Engine, layout | Character | Cmds | CPU us | SDRAM % |
|---|---|---|---|---|---|
| Lattice | field, 4 tiles across, 3 px, bone, 15 fps | Ornamental mesh, calm; the default | 2 | 796 | 1.5 |
| Sand Table | sand, 4 tiles across, amber, 500 grains | Grains settle and scatter on bass hits | 516 | 2,615 | 3.2 |
| Gallery Plate | dictionary, 2 tiles across, white, tone-change trigger | Large still figures that dissolve when the tune changes | ~0 | 3 | 0.4 |
| Triptych | field, three plates (bass/mid/treble), cyan, bass trigger | The reference picture; each plate triggers alone | 1 | 995 | 0.6 |
| Shimmer | field, 8 tiles across, 2 px, lime, tone trigger, 30 fps | Fine quick mesh that follows melody | 9 | 1,058 | 3.3 |

All five pass the headroom limits. Sand Table is the heaviest (7.8 % CPU) and stays last in the build order.
Gallery Plate changes figure about every 4 s on the demo track, so its steady change reads 0 and punch is n/a.
Colours are constants (bone, amber, white, cyan, lime); real presets should come from the theme system.

## 5d. Multicolour version, and what it changes about the module (added B-271)

Reference: a colour-mapped amplitude plot of the same figures (violet, blue, cyan, green, yellow, red across the
value of F, instead of only the zero lines). It is reasonable, and it is a better fit for the engine than the line
version, for one reason: **colour costs nothing once the tile goes through a plane and a blit.**

Measured in the lab, live field, tile 33x37 cells, 15 updates/s, per 30 fps frame averaged. Rects means one draw
command per run of equal colour, which is what a plain `fb_rect` renderer would do.

| Paint | Rects: commands | Rects: SDRAM % | Plane + `OP_SBLIT` today | Plane + row-burst (B18) |
|---|---|---|---|---|
| Lines, 3 levels | 320 | 2.2 | 2 cmds, 5.2 % | 2 cmds, 1.5 % |
| Heat fill, 4 colours | 122 | 1.6 | 2 cmds, 5.0 % | 2 cmds, 1.4 % |
| Heat fill, 8 colours | 214 | 1.9 | 2 cmds, 5.2 % | 2 cmds, 1.5 % |
| Heat fill, 16 colours | 355 | 2.3 | 2 cmds, 5.2 % | 2 cmds, 1.5 % |
| Heat plus bright lines | 217 | 1.9 | 2 cmds, 5.2 % | 2 cmds, 1.5 % |

Findings:
- **Colour count does not matter on the blit path.** The plane holds the final RGB565 value per cell, so 4 or 16
  colours, or lines over a heat fill, cost the same two commands. On rects the cost scales with colours and
  boundaries: 122 to 355 commands per 30 fps frame-equivalent, about 250 to 710 per update. `VIZ_BARS` is 36.
- **The line version has the same problem.** The native test measures 642 rect commands per update for Lattice
  (mean; 773 peak) and 362 for Shimmer, with 3 line levels. This corrects section 3, which counted one colour.
  **Rect-per-run drawing is not the module's render path.** The tile is written as a plane of RGB565 cells into
  SDRAM scratch and scaled with `OP_SBLIT` (exists today, 5.2 % SDRAM at 15 updates/s), then copied across the box
  with `OP_COPY`. B18 lowers that to about 1.5 %.
- **CPU is cheaper, not dearer.** Heat fill needs no neighbour gradient: one multiply-free lookup per cell, so it is
  about 10 percent cheaper than even-width lines (0.67 to 0.79 ms against 0.87 ms in the lab).
- **Contrast needs auto-gain.** F rarely reaches its bound (weights sum to one and modes cancel), so the palette
  index is scaled by the plane's own peak; without it only about 10 distinct colours appear.
- **Palette animation is not free.** Colour cycling (rotating the palette on the beat) needs the CLUT path, and
  today's CLUT blit (`OP_CBLIT`) has no scaling. It arrives with B18 stage 2. Until then a beat can change the
  palette only by rebuilding the plane, which costs the CPU write (about 0.3 ms) but no extra blit.
- **Look:** a full-box heat fill is bright and busy next to the cover art and the theme colour. Offer it as its own
  meter (or a paint choice inside the meter), with the palette tied to the theme system rather than a fixed rainbow.
  A two-hue signed map (theme colour for positive, its complement for negative) reads as calmer and gives the
  same information.

**Module consequence.** The firmware module renders through a plane and blit for every paint (lines, heat, both),
so multicolour is a palette table plus a paint switch, not a second renderer. The portable core
(`fw/chladni_core.h`, host tests in `sim/test_chladni_core.py`) exists and passes. Not yet done (as of B-271):
the plane write into SDRAM scratch, the `OP_SBLIT` call with its sticky source base (the first firmware use of
that path, which needs its own hardware check), the enum/dispatch hooks and the build switch.

**Update (B-276..B-278, B-347): the plane write, `OP_SBLIT` call, dispatch hooks and build switch above are now
all built and shipping** (`fw/chladni.inc`, `chladni_tick_box()`) -- this list is no longer open. What B-347 adds
on top of that foundation is exactly the "palette table plus a paint switch" this section predicted: a `paint`
enum parameter (`meters/chladni/meter.json`, values `LINE`/`EMBER`/`OCEAN`) selecting which fixed 4-stop
`ctx.pal[]` `chl_row()` looks colours up in -- no change to `chladni_core.h`, `chl_render()`, the plane write or
the blit calls, confirming the "not a second renderer" claim. `LINE` (index 0, default) is the original
background-to-accent monochrome ramp, byte-identical to the pre-B-347 code (checked by `sim/test_chladni_module.py`'s
existing pixel-for-pixel screen compare, which recomputes the expected palette independently and still passes
unchanged). `EMBER` and `OCEAN` are hand-picked warm/cool 4-stop RGB565 palettes (`CHL_PAL_EMBER_*`/`CHL_PAL_OCEAN_*`
in `fw/chladni.inc`), each real hue-to-hue steps rather than a linear mix of two colours, with stop 0 kept dark/muted
per this section's own "full-box heat fill is bright and busy" warning rather than raised to full saturation. Two
new presets (`EMBER` on the Lattice geometry, `OCEAN` on the finer Shimmer geometry) ship alongside the existing
`LATTICE`/`SHIMMER` (both now explicit `paint: LINE` so their look is unchanged). Still open, as this section
already said: true theme-driven or user-custom colour (today's EMBER/OCEAN are fixed constants, not derived from
`th_role[]`/`ui_accent` the way `LINE` is) and the Sand Table/Gallery Plate/Triptych templates, which need a
genuinely different render engine, not a palette swap.

## 5e. Symmetry: what the figures allow and how much of it the engine should do (added B-272)

Until now the meter used one symmetry only: translation (copy the tile), which forces the parity restriction. The
formula has more, and the tests in `sim/test_chladni_core.py` prove each one on the float reference:

| Symmetry | Holds when | What it buys |
|---|---|---|
| Mirror about the tile **edges** (x to -x, y to -y) | always, any modes, any family blend | **Mirror tiling**: neighbouring tiles are mirror images, seamless for every mode mix. Removes the parity restriction, so the simplest figures (1,0) and (2,1) and mixed-parity mixes come back. |
| Mirror about the tile **centre lines** | all modes in one parity class, any blend | **Quarter compute (D2)**: compute a quarter of the tile, mirror the values. Exact (0 to 7 LSB of 16384 in the lab, which is rounding). |
| Mirror about the **diagonal** | every mode shares one family sign s = +-1 (no continuous blend), square tile | **Eighth compute (D4)**: the crisp eight-fold look of the reference photos. Exact, and wrong (33,000 LSB error) if the signs differ. |

The engine can copy and, with B19, flip. It cannot transpose (a diagonal mirror needs a per-pixel column step), so
the diagonal fold is done by the CPU on cells, which is free. Where each job goes:

| Job | Where | Cost |
|---|---|---|
| Fold the field (quarter or eighth) | CPU, cell level, mirror the value into the plane | Removes 75 to 90 percent of the multiply-adds; no extra stores |
| Expand a quarter plane to a full plane | engine, flipped copies (B19) | Removes 75 to 87 percent of the SDRAM window stores (the largest CPU cost) |
| Fill the box | engine, translate copies or mirror copies | 3 to 4 commands either way (mirror needs B19) |
| Transpose, rotate 90 degrees | CPU, cell level | Free at cell count |

Lab model, live field, tile 33x37 cells (D4: 37x37), per figure update (the lab reports per 30 fps frame at 15
updates per second, doubled here):

| Configuration | Commands | CPU per update | Note |
|---|---|---|---|
| Translate, full compute (today's plan) | 2 | 1.59 ms | parity class only |
| Translate, D2 fold on the CPU | 2 | 1.06 ms | no new hardware; **do this first** |
| Translate, D4 fold (square 110 tile, single family sign) | 2 | 1.09 ms | crisp eight-fold look; tile is larger, so the saving is smaller |
| D2 fold and B19 quarter expansion | 3 to 5 | about 0.69 ms (estimate) | stores fall from 0.49 to about 0.12 ms |
| D4 fold and B19 eighth expansion | 3 to 6 | about 0.61 ms (estimate) | |
| Mirror tiling, any modes, B19 built | 2 | 1.59 ms | full variety; no fold available across classes |
| Mirror tiling, any modes, **no B19** | 152 | 1.89 ms, SDRAM 2.7 % | horizontal flips become one column copy per pixel |

SDRAM stays at about 1.5 percent in every row that has B19 or translation; the saving is CPU, not blit.

**Status (B-273):** the quarter fold is in `fw/chladni_core.h` (`chl_render` with a `half` buffer, on in both
presets). It computes cells x < Rx/2 of rows y < Ry/2 and mirrors the rest with sign (-1)^m. Native tests: same figure
as the full path, exactly mirror symmetric, and under 35 percent of the multiply-adds. Extra RAM: one half-tile
buffer (up to 1.6 KB).

Findings:
- **The blit engine is not the bottleneck; the plane store is.** Half the update time is the CPU writing cells into
  SDRAM scratch (611 stores, about 0.49 ms). Folding the maths alone leaves that cost; only an engine-side expansion
  removes it. That is the strongest argument for B19 in this project: one flag pair turns a quarter plane into a full
  one and turns translate tiling into mirror tiling.
- **Mirror tiling beats translate tiling on quality**, not just cost: any mode mix tiles seamlessly, so the meter
  can use the whole pool and keep the classic simple figures. The family blend can stay continuous, because the edge
  mirror needs nothing of it.
- **Symmetry as a trigger effect.** A trigger can change the group as well as the scene: translate to mirror, D2 to
  D4, or a four-fold rotation of the box. All are copies or cell writes, so a change of pattern costs no more than
  a frame.
- **Recommended order:** (1) D2 fold in `chladni_core.h` (CPU only, exact, tested; **done, B-273**); (2) B19 flip flags (also
  serves `VIZ_MIRROR`, reflections and reused icons) and mirror tiling; (3) D4 as an optional crisp variant.
  Meanwhile the module ships with translate tiling and the parity restriction.

## 6. Test plan (same shape as the earlier meters)

1. **Lab** (this doc): visual preview, cost model, benchmark. Done.
2. **Host reference.** Port the Q14 kernel to `tools/host/chladni_ref.py`; `sim/test_chladni_ref.py` checks
   determinism, tile periodicity (the next period equals the tile up to sign), that pure modes have zeros where the formula says (for example `cos(pi x) - cos(pi y)` zeros on
   the diagonal), that quadrant mirroring equals the full computation for same-parity sets, and that no
   intermediate overflows int32 for `K <= 8`. Fixture planes for `tools/ui_snapshot_renderer.py`.
3. **Firmware on the current bitstream**, `player-library-diagnostic-profile`, with the blit-storm Check (CT_BLT)
   extended to draw the plate under audio and publish the SDRAM busy percentage from the B7 counter next to the
   L0 verdict. That measurement replaces every estimate above.
4. **Hardware read** on the Pocket: late underruns 0, meter yield count, per-frame time.

## 7. Open questions

- Real music. The benchmark uses one synthetic track; run the lab on three or four real files (loud master, sparse
  acoustic, electronic, speech) before fixing the trigger gain and refractory time.
- Cell colour: the plate tile is flat because a CLUT is flat; the gradient shows only between tiles. Confirm this
  looks right beside the cover art.
- Whether the spectrum bank's top band (about 3.5 to 7 kHz) is enough for the treble plate to show detail. It
  probably is not; a higher band or a broadband "air" value may be needed.
- The 0.8 us window-store cost is from A-094 at 60 MHz; the 5-store command cost is an estimate.
