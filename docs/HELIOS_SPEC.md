# Helios (UI controller / graphics library) over Talos (the 2D blit engine)

**Status:** design only. Nothing in this document is built. It supersedes `PHASE_F_SPEC.md` section 15
(the tearing investigation and UI-controller sketch) with a complete proposal, and gives the project's
existing "blit engine" RTL work (B1-B11, `PHASE_F_SPEC.md` section 5) a name: **Talos**. The library that
will sit on top of it, replacing today's scattered immediate-mode `fb_rect`/`fb_char` call sites, is
**Helios**.

**Origin:** owner asked (2026-09-25) for a lightweight, highly-performant library built around full use of
Talos, optimized for screen-refresh/animation fluidity, able to serve as a base for a future 720p path, and
likely owning typography — with an explicit ask to investigate prior art first rather than design from
scratch. Sections 2-3 are that investigation's findings; sections 4 onward are the design it produced.

## 1. What Talos actually is, and why that constrains Helios

Talos is a single-command-queue blitter coprocessor: one shared SDRAM port that also serves video scanout,
commands execute strictly serially (no parallelism, no concurrent draws), no double buffering today. The
opcode set (`RECT`/`CHAR`/`COPY`/`BLIT`/`BAR`/`SBLIT`/`CBLIT`/`RRECT`, sticky `SRC/DST_BASE+STRIDE` fields, a
256-entry CLUT) is structurally closest to the **Amiga blitter** — not a modern GPU. The CPU is an RV32IM
softcore with no float unit and 192-256 KB of on-chip RAM, decoding MP3/FLAC in the same superloop that
drives the UI. There is **no interrupt controller anywhere in this design** (confirmed by reading
`mp3_soc.v`/`fw/start.S` directly) — everything is cooperative, single-threaded, polling.

Two consequences that rule out entire classes of design:

- **A general retained scene-graph with automatic diffing (React/LVGL-full-widget-tree style) is wrong** —
  there is no spare CPU/bandwidth for a diff pass on this core, and this project has spent its whole history
  fighting for every draw-command reduction it can get (B6/`OP_BAR`, B8/`OP_CBLIT`, B11/`OP_RRECT` all exist
  specifically to replace many small transactions with one).
- **Real preemptive concurrency (threads, an interrupt-driven audio path) to "decouple" UI from playback is
  not worth it** — see section 8. The audio-never-glitches guarantee this project has protected through
  `meter_afford()`, the cold-code CPU-budget checks, and the blit-storm Check test is the single thing that
  must never regress, and introducing real concurrency risks it far more than it helps.

## 2. Root cause this design has to fix: no frame synchronization exists

`PHASE_F_SPEC.md` section 15 (B-232) traced the reported UI tearing to a genuine structural gap: Talos's
video-timing block already pipelines scanout *reads* one row ahead (`do_fill`/`fill_line_req`/`linebuf`,
`mp3_fb.sv`), isolating pixel fetch from live SDRAM read latency — but provides **zero protection for CPU
writes**. A multi-transaction UI update can have its top rows already scanned (old content) while the CPU is
still writing lower rows (new content). Two already-existing, unused hooks were found: a dead `vblank` input
pin at `core_top.v`, and an internally-generated `vs_pulse` (cleaner, same clock domain as scanout itself,
CDC-able into `clk_sys` via the exact proven technique B7's SDRAM busy counter already uses, B-101). Helios's
entire batching/flush model exists to use one of these.

Also found in the same pass: `fb_round_rect`/`fb_round_rect_on` — used for nearly every panel and every
selected list row in the whole UI, almost certainly the single most-executed draw pattern in the firmware —
are pure software, issuing up to ~33 separate small transactions per call. B11 (`OP_RRECT`, timing-fixed
B-231, fit-confirmed clean B-235) already does this exact shape in one hardware command with zero firmware
caller yet. This is Helios's first, obvious adopter.

## 3. Prior art investigated (2026-09-25, background research)

Four areas researched on the owner's explicit ask, live web search, sources cited in full in the research
transcript (`docs/AUDIT_TRAIL.md` B-237):

- **Amiga Copper/blitter.** The Copper is the sync primitive, not the blitter: a `WAIT`-for-beam-position
  instruction lets register writes (including bitplane base-pointer swaps) land exactly at a chosen
  scanline/vblank. **Double buffering was done by swapping a base-address pointer, never by moving pixel
  data** — directly relevant, since Talos already has the identical mechanism (sticky `SRC/DST_BASE` fields).
  AmigaOS's `graphics.library` `BltBitMap()` is a single blocking call with no queue; the existence of
  community tools like CpuBlit (manually routing blits to the CPU when the blitter is idle) shows the lack of
  a real queue/backlog was a genuine gap in that model, not a feature to copy.
- **PS1 Ordering Tables.** A flat/bucketed list of draw-command packets, built by the CPU during the frame and
  handed to the GPU as one DMA transfer; the GPU walks it unattended. Double-buffered OTs (CPU builds the next
  while the GPU drains the current) is the standard "batch once, flush once, don't block" pattern. Talos has
  no depth/overlap problem Tau's UI needs solved, so this simplifies to a plain ordered list, no bucketing.
- **LVGL.** `lv_obj_invalidate()` marks a dirty rectangle; `lv_refr_join_area()` merges overlapping/adjacent
  dirty rects before rendering. Render-then-`flush_cb` loop, with `lv_display_flush_ready()` as the
  hardware-completion hook — structurally identical to what `fb_wait()` already does in this firmware, just
  never tied to a frame-sync point. Confirms the dirty-tracking direction; Tau's fixed-layout UI (a handful of
  named regions per screen, not a general widget tree) means Helios can skip LVGL's general rectangle-merge
  algorithm entirely.
- **u8g2.** Redraws the whole scene into a tiny on-chip strip buffer to solve *RAM-capacity* scarcity. **Does
  not apply to Tau** — Tau's framebuffer already lives in abundant external SDRAM (64 MiB chip, everything
  today under 2 MiB); the actual constraint is SDRAM *bandwidth* contention with audio, which u8g2's model
  does nothing for and would make strictly worse (redraw-everything is the opposite of bandwidth-conscious).
- **MiSTer OSD.** Composites its on-screen-display as a *separate video overlay plane* mixed at pixel output,
  sidestepping the shared-SDRAM/scanout contention problem entirely rather than solving it. **Considered and
  declined for Tau**: it needs a whole new video-mixing RTL stage (bigger than anything scoped for Phase F),
  and doesn't match Tau's use case — Tau's UI (player screen, meters, library) mostly *is* the primary
  display content, not a menu overlaid on top of some other video source. No openFPGA/Pocket-specific
  precedent for this exact problem (single blitter into shared SDRAM with live scanout) turned up in search —
  a real gap, not filled with speculation.

## 4. Helios's architecture

**A flat, per-frame display list (PS1-OT style, no depth bucketing needed).** Each screen owns a small, fixed
enum of named regions (meter box, art panel, title, each list row — already implicit in today's code via
`wviz_force`, `ui_meter_faces_invalidate()`, `pl_ui_dirty`, just hand-maintained per screen instead of
uniform). A region's own `mark_dirty()` call appends its already-resolved Talos command (which opcode, which
registers) to a small fixed-size list — no dynamic allocation, matching every other buffer in this firmware.

**Vblank-gated flush.** The list is drained into real `fb_*()` calls at one sync point per frame, timed to
begin at vblank using a new CDC'd MMIO bit (section 6, Phase H0). This is also what answers section 8's
"don't let a big UI paint block audio" concern for free: the flush is bounded to what fits before the next
scanout deadline, not "draw everything now" — a large repaint self-limits without needing any concurrency.

**Region dirty-tracking, LVGL-simplified for Tau's fixed layout.** No general rectangle-merge algorithm —
each named region is its own dirty flag, and adjacent regions that always redraw together (e.g. a whole list
page) are already one region by construction, not merged after the fact.

**Command coalescing onto Talos's actual opcodes**, not raw `fb_rect` sequences: any panel or selected row
becomes one `OP_RRECT`, any meter becomes one `OP_BAR`, any palette-driven icon becomes one `OP_CBLIT`. This
is where B11's conversion (section 2) lands architecturally — it's not a one-off fix, it's Helios's first
real region type.

## 5. Double buffering: pointer-swap, not pixel-copy (revised from `PHASE_F_SPEC.md` section 15's T2)

The Amiga finding changes the earlier assessment. Instead of a CPU-side "redraw into a back buffer" model
(2x SDRAM traffic, real cost), Talos already has the mechanism: **two SDRAM framebuffer regions, CPU always
writes to whichever one scanout is NOT currently reading, and a single vblank-gated register flip swaps
which region scanout reads from next frame.** SDRAM capacity is a non-issue (64 MiB chip; two full
framebuffers plus every existing off-screen stash region together are still under 4 MiB). The real cost is
RTL: a buffer-select mux on both the CPU write-address path and the scanout prefetch's read-address path,
synchronized so neither flips mid-frame — bounded, well-precedented work, not a research question. Held as
Phase H2 (below) until H0/H1 are built and measured, per this project's own "cheapest lever first" discipline.

## 6. Talos additions worth bundling alongside B11 (checked, not guessed)

The owner asked whether any of the previously-proposed but unbuilt bar-family opcodes (`PHASE_F_SPEC.md`
section 5, B12-B17) should be bundled into the next Quartus fit. Checked against the actual RTL rather than
assumed:

- **B9 (palette re-index)** — already fully built and RTL/sim-verified (B-179). Free to bundle, zero new
  work.
- **B13 (gradient-fill bar via the CLUT) — HELD, real cost found bigger than scoped (2026-09-25).** The CLUT
  read-port "contention" risk was correctly re-assessed as a timing-margin question, not a functional one
  (Talos dispatches one command at a time, so `OP_CBLIT` and a gradient `OP_BAR` never actually compete for
  the CLUT simultaneously). **But reading `OP_BAR`'s actual RTL for the first time while scoping the build
  surfaced a bigger problem**: `OP_BAR` is not a row-by-row iterator at all — it is two stacked solid-colour
  rectangle BURSTS (an unlit segment, then a lit segment), each capable of covering many rows in a single
  SDRAM transaction, which is exactly why it is cheap. A genuinely per-row gradient background needs a
  completely different mechanism — one burst PER ROW instead of one burst per segment, giving up the whole-
  segment efficiency `OP_BAR` exists for. For a tall panel (the stated `WATER`/`SCROLL` target), that could
  mean dozens of transactions instead of two — potentially **worse** than the existing 3-op software sequence
  it was meant to replace, not better. Owner: "let's hold this to the end of Talos/Helios" — held, not built,
  pending a real cost/benefit re-scope once the rest of Helios/Talos has shipped and there is a clearer
  picture of what a gradient-panel region type actually needs.
- **B12 (column-split bar)** — confirmed the flagged risk is real by reading `OP_BAR`'s actual RTL: its
  efficiency comes from one SDRAM burst per row at one fixed color; a column split needs per-row masking that
  does not exist today, so it would cost multiple bursts per row, not one — not free the way B9/B13 are. The
  design doc's own alternative (reorient the one meter that needs this, `VIZ_LEVELS`, to draw vertically
  instead) needs zero new RTL and solves the same problem. **Deferred**, firmware reorientation preferred.
- **B10 (RLE source blit), B14 (floating bar), B15 (segmented bar), B16 (point-list), B17 (line draw)** —
  each already flagged in `PHASE_F_SPEC.md` section 5 as needing its own dedicated design pass (real open
  gaps for B10, unresolved value proposition for B15, explicitly bigger/Tier-3-4 lifts for B16/B17).
  **Deferred** until a concrete Helios region type actually needs one, not spec-built speculatively.

## 7. Typography

Absorbed into Helios as a first-class region type, not rebuilt. The existing glyph-atlas + hardware-blit
approach (`font_rom.v`, the palette-fitted gamma-correction table for anti-aliasing) is already sound and
already close in spirit to what a from-scratch design would choose for this hardware class (bitmap atlas +
hardware blit, not a float-heavy vector/SDF renderer this RV32IM core has no business running). `fb_char()`
becomes one more command a text region emits into the display list, participating in the same dirty-tracking
and vblank-flush discipline as everything else, instead of being issued immediately the way it is today.

### 7.1 Loadable fonts, and the real crispness problem already present today

Owner asked whether Helios should support loading different fonts, preferably at fixed scales so output is
always sharp, and flagged this as a possible future Tau Omega export path.

**Confirmed by reading `fb_char()` directly: today's "scaled" text (1.5x/2x/3x, the `TS_1X..TS_3X` enum) is
NOT separately-drawn bitmaps per size** — it rides the same Bresenham src-pixel-stepping mechanism `OP_SBLIT`
reuses (`sx`/`sy` fields straight into `R_FB_GO`). Integer factors (2x, 3x) stay genuinely crisp this way —
nearest-neighbour stepping at an exact integer multiple reproduces every source pixel as a clean N×N block,
no blur. **1.5x does not** — a non-integer nearest-neighbour step duplicates some source pixels and not
others, which is the class of artifact pixel-font practice has always avoided by shipping discrete bitmap
sizes rather than scaling (the same reason Amiga's own bitmap font sets, e.g. `topaz.font`, shipped multiple
fixed point sizes instead of one scalable outline). This is a real, previously-uncosted gap in the current
UI, not a hypothetical.

**Design: no runtime scaling for loaded fonts, ever — each supported size is its own pre-rendered atlas.**
A "Tau font" is: a fixed-size bitmap glyph atlas (one bitmap per glyph at its exact target pixel size, same
storage shape as `font_rom.v`'s existing embedded font) plus a small metrics table (advance width, bearing,
covered character range) plus a header (family name, point size, checksum/version — same rigor as
`tau-library.tdb`/`tau-cold.bin`'s own header-check conventions). Wanting the same family at two different UI
sizes means two atlases, generated once, not one atlas scaled at runtime. The existing hardware integer
scaling (2x/3x) stays available as a cheap fallback for the one built-in ROM font where a dedicated
larger atlas doesn't exist — it just should not be extended to custom fonts, where the quality bar is higher.

**Rasterization happens offline, never on the RV32IM core** — consistent with every other asset this project
already generates this way (`tools/gen_font_rom.py` for the embedded font, `tools/gen_text_gamma.py` for the
palette-fitted AA weights used to draw it, the meter-thumbnail RLE pipeline). A new font's glyph bitmaps,
metrics, and AA-ready coverage data are all things a real desktop with FreeType/system fonts can produce
trivially and this softcore cannot. The AA gamma table itself is likely reusable across fonts unchanged — it
is fitted to the *palette and background ramp* this core draws (`fw/player.c`'s own comment on the existing
table), not to any particular glyph shapes — but should be verified per new font the same way
`tools/gen_text_gamma.py --check` already asserts fit today, not assumed.

**Storage and loading**: a font atlas is a new data-slot asset (SDRAM or PSRAM cold-data, matching Phase G1's
existing "meter previews and help text live in PSRAM, loaded at boot from a data slot" pattern) rather than
living on-chip — SDRAM/PSRAM capacity is abundant, on-chip BRAM is the genuinely scarce resource this whole
session has been fighting for. The built-in ROM font stays resident as a guaranteed fallback regardless of
what else loads, matching this project's fail-safe convention everywhere else (a missing/corrupt custom font
degrades to the built-in one, never to a blank screen or a hang).

**Scope for a first version**: ASCII/Latin-1 coverage, matching what `font_rom.v` already covers — broader
coverage (CJK, full Unicode) was already researched and deliberately parked (`PHASE_F_SPEC.md` section 13,
against upstream HarpMudd's hardware-verified UTF-8/Japanese work) pending real near-term interest, and
nothing here needs to reopen that scope now. The font *format* should not preclude wider coverage later
(a variable-length glyph table keyed by codepoint range rather than a fixed 128-entry array), but building
that coverage is explicitly out of scope for Helios's first version.

### 7.2 Icons: the same offline-rasterization principle, reusing the existing thumbnail pipeline

Owner asked whether Helios should read SVG source and rasterize it directly on-device, or use a font-like
approach instead. **Font-like — and more specifically, this is already a solved problem in this codebase,
not a new one.** SVG is a full vector format (bezier paths, fills, strokes, transforms); parsing and
rasterizing that at runtime needs a real vector rasterizer (curve flattening, scanline fill, anti-aliasing) —
a large, float-heavy piece of code this RV32IM core with no FPU has no business running, especially while
also decoding audio. SVG is a perfectly reasonable *authoring* format (what a design tool exports), but the
transformation to what actually reaches the card must happen offline, same as everything else in section 7.1.

**Icons should be a specialization of the meter-thumbnail pipeline already built and hardware-proven (B8,
B-146), not a new mechanism.** That pipeline already does exactly this shape of work: an offline tool
(`tools/gen_meter_thumbs.py`) rasterizes source art into a small palette (8 colours today) plus RLE-encoded
bitmap data, decoded once at first use into a flat off-screen SDRAM buffer, then drawn via `fb_clut_load()` +
`fb_cblit()` — one hardware command per redraw instead of dozens of `fb_rect` calls. An SVG-to-icon tool would
do the identical job: rasterize the SVG at the icon's fixed target size(s) (never runtime-scaled, same
principle as 7.1), quantize to a small icon-specific palette, RLE-encode, done offline.

**A genuine connection to B9 (palette re-index, already built and free to bundle with B11, section 6):**
theming an icon set — swapping to the current accent colour, or a full colour theme — would otherwise need a
separate bitmap per theme per icon. B9's palette re-index (an 8-bit offset added to `OP_CBLIT`'s palette index
before the CLUT lookup) means one base icon bitmap plus a small per-theme palette swap covers this instead —
directly useful the moment Helios has more than one theme, and no new opcode is needed for it.

### 7.3 A future Tau Omega export path

Plausible and a good fit for this project's existing division of labour: Omega already runs on a real desktop
with proper font rendering available (system fonts, FreeType), which is exactly the right place to do the
expensive, constrained part (rasterizing a chosen family/size cleanly, fitting it to Tau's palette) rather
than asking the Pocket's own softcore to do it. Concretely, this would need: **a documented, versioned Tau
font file format** (section 7.1's atlas+metrics+header shape) that Omega can target as an export; **the same
AA-fitting verification step available on Omega's side**, not just Tau-Alpha's own tooling, so an exported
font is verified crisp against Tau's actual palette before it ever reaches a card; and **a new entry in
`docs/CROSS_PROJECT_INTERFACE.md`'s interface-surface table**, since a font export is a new boundary between
the two projects, following the same "Tau-Alpha is the source of truth, verify against real captured
artifacts, never share literal files" discipline already governing every other shared surface (the meter
config spec, the diagnostics QR format). Not scoped further here — a real design pass once Helios's own font
format (7.1) actually exists and has shipped at least one real font through it.

The same reasoning extends naturally to icon/theme export (7.2) once that pipeline exists too — Omega already
has a real SVG rasterizer available via any desktop toolchain, making it the natural place to author and
export an icon set or a full colour theme (base bitmap + palette), rather than a second, independent asset
pipeline. Not scoped in detail here for the same reason — worth a real pass once fonts have proven the
export model once.

## 7.4 Colour, palettes, and what Figma colour variables could actually feed

Owner asked how colour works today, whether it's palette-based, whether it could link to Figma colour
variables, and how that would interact with alpha and other blend modes.

**Two separate colour mechanisms exist, not one.** The framebuffer itself is **true-colour RGB565**
(`fw/player.c`'s own header comment: "400x360 RGB565, one word/pixel") — the whole screen is never limited to
a small palette. Separately, B8's **CLUT is a 256-entry RGB565 lookup table** used only by `OP_CBLIT`, for
SOURCE bitmaps that are themselves stored as 8-bit palette indices (today: the meter-preview thumbnails) —
the classic "sprite palette" pattern, not an indexed display mode. **Theme colours today are a small, fixed
swatch list** (`ui_palette[]`, hand-picked RGB565 values, "Analogue Pocket hardware edition colours"), picked
from in Settings — not free-form colour input, and not currently loaded from any external source.

**A Figma colour-variable export is plausible and fits the same pattern as fonts/icons (section 7.1-7.2)** —
Omega already has real colour data at full precision from a design tool; the work is in the offline
conversion, not on-device. Two things that conversion has to get right, both real, not hypothetical:

- **RGB565 quantization is lossy** (5/6/5 bits per channel, 32/64/32 levels) — an arbitrary Figma hex value
  will not always round-trip cleanly, and picking a Figma palette without checking this against the target
  hardware could produce a slightly different theme than designed. A real export tool needs to report the
  quantized result, not just apply it silently.
- **The text anti-aliasing table is fitted to the CURRENT palette and background ramp**, not to glyph shapes
  (the existing gamma-correction comment: "these weights are fitted to the palette this core actually draws,
  over the background ramp it actually draws on ... regenerate with `tools/gen_text_gamma.py` if the palette
  changes materially"). **A new theme is exactly a palette change** — text drawn over a Figma-sourced theme
  needs this table re-verified (`--check`) or regenerated, the same requirement section 7.1 already places on
  a new font, not assumed to still look right.

**Alpha: there is no per-pixel alpha channel anywhere in this hardware.** RGB565 has zero spare bits to store
one. Two genuinely different mechanisms already exist, and it matters which one a Figma layer would actually
need:

- **Text glyph edges already blend per-pixel** — `mp3_fb.sv`'s glyph compositor mixes `fg`/`bg` by the
  glyph's own coverage value, computed from the font atlas, not from any general image alpha channel. This is
  real, live, shipped — but it is specific to text, not a general mechanism other content can use.
- **B5 (shelved, not currently live on any shipped bitstream — a documented timing-cliff risk was never
  fixed) is a single UNIFORM alpha or blend ratio applied to an entire blit command**, not per-pixel varying
  alpha. Modes: DSP-style linear alpha (0-255, an approximate `/256` divide, a known and accepted rounding
  artifact at alpha=255) and four fixed PSX-style saturating ratios (average `B/2+F/2`, additive `B+F` and
  `B+F/4`, subtractive `B-F` clamped not wrapped). This is a small, fixed set — nothing like Photoshop/Figma's
  general blend-mode list (multiply, screen, overlay, etc. do not exist here and each would need its own new
  hardware logic to add, a real but currently unscoped undertaking).

**What this means concretely for a Figma-sourced asset:** a UNIFORM opacity on a whole icon or panel (e.g. "a
disabled row at 60% opacity") maps directly onto B5's real capability once it's un-shelved. A layer with a
smooth per-pixel alpha gradient or a soft drop shadow does **not** — it cannot be reproduced live by this
hardware at all, and must be pre-composited against its actual expected background at export time (bake the
alpha into the exported bitmap), the exact same "pre-render, never compute live" principle sections 7.1-7.2
already established for fonts and icons. An export tool would need to know which case it's looking at and
choose the right path, not assume live alpha will always work.

**Not scoped further here** — same reasoning as 7.3: worth a real design pass once B5 is un-shelved (its own
timing fix, not yet attempted) and once a font/icon export from Omega has proven the cross-project pipeline
once for a simpler asset type first.

## 8. UI/audio decoupling: considered and declined

Owner asked whether UI rendering should be decoupled from background processes (audio decode) so playback
never blocks the UI. Assessed and **declined as a concurrency project**:

- **No interrupt controller exists in this design at all** (confirmed by reading the RTL and `fw/start.S`) —
  this would be new hardware, not a firmware change, competing for the same scarce M10K/timing budget as
  the RAM shrink and Talos itself.
- **Virtually all of this firmware's global state assumes single-threaded, sequential execution** (`pl_*`,
  `viz_mode`, `wviz_*`, `spec_lp`/`spec_lvl`, the draw-engine FIFO shadow state). Real concurrency turns every
  one of those into a potential race condition — a correctness audit of the entire firmware, not a bounded
  change, and this project's own history of narrow CDC/retiming bugs shows how expensive even *small*
  concurrency-adjacent mistakes are here.
- **The audio-never-glitches guarantee is this project's one truly protected invariant** (`meter_afford()`,
  the cold-code budget checks, the blit-storm Check test all exist for it alone). Any scheduler or interrupt
  path that is even slightly wrong under load is a strictly worse failure mode than today's simple model.
- **Section 4's bounded vblank-flush already gets most of the real benefit for free**, from the other
  direction: it prevents a large UI paint from itself becoming a long blocking stretch, using the existing
  cooperative superloop, with none of the reentrancy risk. The remaining case (a genuine decode stall making
  the UI feel briefly unresponsive) is already heavily buffered against (the ring buffer's own measured
  ~18x margin against SD throughput), so its real frequency/severity is likely too low to justify the risk.

**Revisit only if**, after the bounded-flush design ships, real UI unresponsiveness tied specifically to
decode stalls (not general lag) is still observed — at which point the fix would be much narrower (an
interrupt just for PCM refill, not general preemption) than what was proposed here.

## 9. Phased build plan

Matching this project's synthesis-first, measure-before-committing discipline (B-100 precedent):

- **Phase H0 (RTL, cheap, foundational).** CDC the internal `vs_pulse`/scan-position into `clk_sys` (the
  proven B7/`tau_cdc_gray_ctr.sv` technique), expose as new MMIO (vblank-active bit, ideally a "lines
  remaining" figure too). No behavior change by itself.
- **Phase H1 (firmware, Helios itself) — in progress, 2026-09-25.** `fb_round_rect_on()` converted to
  `OP_RRECT` behind a real hardware-capability probe (`RRECT_READY()`, matching `BLIT_READY()`'s precedent —
  the bitstream currently on the card predates B11 entirely, so this cannot be wired in unconditionally
  without a runtime check, see section 10) — done, not yet hardware-tested. **The display-list/dirty-
  region/vblank-flush core itself (section 4) is started**: `fw/helios.inc` (region registration,
  `helios_mark_dirty()`, `helios_flush()`, `vblank_active()`) exists and is wired into the main loop, but
  deliberately inert — no real screen has been converted to it yet, held until H0's vblank MMIO is itself
  hardware-confirmed (the fit that would carry `TAU_VBLANK` to the card, B-239, came back with blend
  re-enabled and blend not closing timing, B-243 — the no-blend bitstream that WILL carry `TAU_VBLANK`,
  B-235, still hasn't been installed). B13's gradient-panel region type is NOT part of this phase any more
  (section 6 — held to the end of Talos/Helios, real cost found bigger than scoped).
- **Phase H2 (RTL, held).** True double buffering via pointer-swap (section 5), once H0/H1 are built and
  measured, not before.

## 10. Fail-safe requirement (not yet built)

**`RRECT_READY()` must exist before `fb_round_rect`/`fb_round_rect_on` are ever unconditionally converted.**
The bitstream currently installed (shared by `TAU_DEV_49`/`50`/`51`, hash `c81b33f9...`) was built before
commit `1278cc7` (B11) — its `cmd_op` decode is only 3 bits wide, and writing `FB_OP_RRECT` (which sets bit 14
to select opcode 8) would silently truncate to `cmd_op=0` (`OP_RUN`), degrading a rounded panel to a single
drawn line, not a hang — but a real, silent visual regression on every currently-shipped/installed core until
the new B-235 bitstream is actually on the card. `blit_probe.inc`'s `BLIT_READY()` (built for exactly this
class of problem, B-126) is the template: issue a small `h=2` `OP_RRECT` command with `radius=0` into the
proven-safe off-screen columns (400-511), and check whether the second row actually got written (old RTL's
`OP_RUN` fallback only ever touches row 0). `fb_rrect()` itself (the primitive, `FB_OP_RRECT`) was started
this session (`fw/player.c`) but the conversion of `fb_round_rect*` and the `RRECT_READY()` probe are not yet
built — this is the concrete first task of Phase H1.

## 11. Cross-references

- `docs/PHASE_F_SPEC.md` section 15 — the original tearing investigation this document supersedes with a
  complete proposal (kept in place as the historical record of B-232's investigation).
- `docs/ARCHITECTURE_ROADMAP.md`'s "UI/UX redesign" placeholder (B-115) — this document is the first real,
  evidence-based answer to its own open "how it interacts with the Phase F blit engine" question.
- `docs/AUDIT_TRAIL.md` B-232 (tearing investigation), B-236 (RAM-shrink link.ld gotcha, unrelated but same
  session), B-237 (this document's own build + the prior-art research).
