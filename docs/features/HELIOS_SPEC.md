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

**Status 2026-09-27 (B-340): built and simulation-verified; not yet fit or on hardware.** H0 and H1 are
both hardware-confirmed (B-266/B-267), meeting this section's own precondition. Resolved one ambiguity the
paragraph above left implicit: **the CPU write-side buffer select (`dbuf_cpu_buf`) is independent of the
scanout display buffer (`dbuf_disp_buf`), not automatically its opposite.** They must be independent,
because H1's own incremental beam-gated draws (the meter block) still target a SINGLE buffer directly —
whichever one is currently displayed — and must keep doing so; only a genuine full-frame redraw (the
chrome, a menu open) sets `dbuf_cpu_buf` to the *other* buffer for the duration of that redraw, then
requests a flip. A second correctness point traced by hand rather than assumed: the offset that selects
buffer 1 (`DBUF_BASE1`, 1,048,576 words / 2 MiB above buffer 0) applies **only to addresses below
`DBUF_VISIBLE_WORDS`** (`V_ACT*STRIDE` = 184,320 words) — every off-screen stash region this project already
has packed just above row 360 (the art panel, meter thumbnails, Chladni's plane, TIM1's index plane, every
probe cell) is reached through the exact same plain `FB_BASE`-relative addressing as ordinary on-screen
draws (`ui_art_mount()`'s `fb_rect(0, ART_STASH_Y, ...)` is the clearest example), so an *unconditional*
buffer-1 offset would silently corrupt every one of them the moment `dbuf_cpu_buf` is set for a redraw.
Blit-mode opcodes (BLIT/BAR/SBLIT/CBLIT/RRECT) are untouched by any of this — they already address through
the fully firmware-programmable sticky `blt_*_base` fields, so a caller wanting one of them to target the
back buffer sets `blt_dst_base` itself.

Built: `mp3_fb.sv`'s `DBUF_ENABLE` parameter and `dbuf_addr()` function (applied at all four plain
FB_BASE-relative sites: the CHAR/COPY write, the RECT write — which also covers BAR and RRECT, both of
which reuse `rect_addr` — the non-blit COPY-source read, and the scanout prefetch read), the vblank-gated
flip (`vblank_tgl` generated once per frame at `VS_ST` in `clk_vid`, synchronised into `clk_sdram` the same
toggle-and-sample way `fill_req_tgl` already is, applied only on that detected edge and only if a flip was
requested), and two new MMIO registers (0x118 `DBUF_CPU`, 0x11C `DBUF_DISP`, `docs/MMIO_ALLOCATION.md`)
behind a new macro `TAU_DBUF`, independent of `TAU_BLIT`. `DBUF_ENABLE=0` (every bitstream before this one)
is a proven byte-for-byte no-op, checked in simulation with the flip/cpu_buf ports actively driven, not just
left untouched (`sim/tb_helios_dbuf.v`, `make test-rtl-helios-dbuf`, in `make test-rtl`). Not done: no
Quartus fit, no firmware integration (a `DBUF_READY()`-style probe, and `ui_draw_chrome` actually using it,
are the natural next steps once a fit proves the RTL), no hardware run.

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
`docs/features/CROSS_PROJECT_INTERFACE.md`'s interface-surface table**, since a font export is a new boundary between
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

## 8.1. A generalized cooperative audio-priority gate (proposed, design only, 2026-09-28)

**Origin:** owner asked (after the day's real MP3/FLAC hardware-decode measurements) whether offloading
decode to hardware frees CPU for the UI, and separately whether audio and UI should be more deliberately
separated so the UI always feels fluid. Both are real, and both point at the same gap rather than at
concurrency (section 8 above already covers why real concurrency is declined).

**The sharing is real, not theoretical, and cuts both ways — two pieces of hardware evidence:**

- The MP3 hardware window unit measured `S` (Subband/filterbank) dropping from 55-59% to 22% of decode
  time (`docs/AUDIT_TRAIL.md` B-087, B-309) — that reclaimed ~35% of a shared, single-core CPU budget
  doesn't disappear, it becomes available to whatever else the same cooperative loop does next, UI included.
- Going the other direction, B-296..B-299 is a real, hardware-confirmed case of the UI costing audio: a
  scope meter drawing too many commands per frame measured `DRAW STALL` at 34,663 ms cumulative and
  `CPU LOAD` pinned at 100%, and it caused actual audible jitter. The fix at the time was narrow (a
  software fallback for that one meter, B-302) — the *general* problem it exposed was never addressed.

**What already exists, narrowly scoped:** `meter_afford()` (`fw/player.c`) is a real, hardware-validated
cooperative priority mechanism — it reads `pcm_level()` (the PCM FIFO fill, sampled at the trough of its
fill/drain cycle) against a two-threshold hysteresis band (`METER_STOP`/`METER_GO`, B-260's tuned values)
plus a hard `METER_YIELD_MAX_S` cap, and lets a caller skip optional work when audio is running low. Today
it has exactly **two callers**, both gating only the spectrum octave-cascade cost inside `VIZ_LED` and
`VIZ_WINAMP_BARS`. Nothing else in the firmware — the Library list's full redraw, the Configure page's live
preview tick, a Settings page's fade-in repaint, Blit Test/Meter Sweep's diagnostic draws — checks audio
headroom before spending CPU/draw time at all. This is a real, checkable gap (`grep meter_afford`), not a
guess: it is exactly the class of cost B-296..B-299 showed can hurt audio, generalized.

**Proposal: promote the mechanism, don't invent a new one.**

1. Move the FIFO-headroom check out of `meter_afford()`'s meter-specific home into `fw/helios.inc` as a
   plain shared gate, e.g. `helios_audio_ok(void)` — same `pcm_level()`/hysteresis/cap logic verbatim (it is
   already hardware-tuned, B-260; this is a relocation, not a redesign), returning whether the caller may
   spend *optional, deferrable* CPU on top of what it must do this pass. `meter_afford()` becomes a thin
   wrapper calling it, so its two existing callers are unaffected.
2. Give a Helios region a `degradable` bit alongside its existing `y0`/`y1` row extent
   (`helios_region_register_rows`). `helios_flush()` already has the exact mechanism needed: a row-scoped
   region that fails `helios_rows_safe_counted()` (the beam-safety check) stays dirty and is retried next
   pass rather than drawn now. Gating a `degradable` region on `helios_audio_ok()` the same way — skip this
   pass, try again next, never block — reuses that control flow directly instead of adding a second kind of
   wait.
3. **Scope this to continuously-repeating, cosmetically-driven work, not one-shot navigation the user is
   actively waiting on.** A live meter's next redraw, the Configure page's live preview tick, or a marquee
   scroll step are all fine to skip a pass — invisible or nearly so. Closing a menu the user just pressed B
   on, or opening Settings, must still complete promptly; gating those on FIFO health would make input feel
   unresponsive to save a UI cost that is not the actual expensive part of those transitions anyway (the
   B-296..B-299 jitter bug was a *repeating per-frame* draw cost, not a one-shot transition). Candidate
   degradable regions, once real Helios regions exist for them beyond today's one (the chrome): the Library
   browser's marquee tick, the Winamp Configure page's live preview, and any future continuously-animating
   view.
4. **Never spins, never blocks — same discipline as beam-safety.** This is a skip-and-retry, not a wait.
   The cooperative loop must never stall on this check; it degrades what gets drawn, not when the loop moves
   on. This preserves exactly the property section 8 above protects (no new blocking/reentrancy surface)
   while still making "audio wins" a system-wide rule instead of a two-meter special case.

**Why this and not concurrency:** it is the same shape of fix as the already-adopted beam-safety design
(section 9's H1) — a cooperative, skip-and-retry gate consulted by the existing single-threaded flush loop
— extended to a second real constraint (FIFO headroom) instead of just the video beam. No new hardware, no
new reentrancy surface, and it directly closes the gap the day's hardware-decode measurements and the
B-296..B-299 bug both point at: freeing CPU on the decode side only helps the UI if something also stops
the UI side from being able to spend that freed CPU back into audio's margin uncontrolled.

**Not built.** This is a design proposal, sized to be a natural extension of `helios_view_changed()`
(B-350/351, already shipped this session) and the beam-safety mechanism (already shipped, section 9) —
not a new subsystem. First real build step, when taken: relocate `meter_afford()`'s logic into
`helios_audio_ok()` with zero behavior change (verify byte-identical build), confirmed on hardware, before
adding the `degradable` bit or a second caller.

## 9. Phased build plan

**Status 2026-09-25 (B-267): beam-aware drawing built, waiting for its bitstream.** H0 is hardware-proven (the frame counter reads about 60/S, B-266). A vblank *pulse* alone cannot schedule drawing (it is 167 us wide and the blanking interval only 1.7 ms, against tens of ms for a full repaint), so H1 now uses the scanning beam's position instead ("racing the beam"): `tau_cdc_gray_bus.sv` carries the video line counter to the CPU (MMIO `R_SCAN`, macro `TAU_BEAM`), and `helios_rows_safe(y0, y1)` (fw/helios.inc) lets a draw through only when the beam is in blanking, has already passed the region (it shows next frame), or is safely ahead of it (draw runs about 8x faster than the beam). Regions register their row extent (`helios_region_register_rows`); the full-screen chrome stays immediate (a full repaint needs double buffering, H2). First adopter: the meter block in `ui_draw_dynamic_cold` (rows 126..259), which waits instead of drawing across the beam. Info > BEAM shows the share of updates that had to wait. Rule verified exhaustively on the host (`sim/test_helios_beam.py`, 837,600 cases). Without a `TAU_BEAM` bitstream everything is safe (old behaviour). Next adopters: progress/clock/transport rows, toasts.

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

## 11. No View abstraction exists yet — every overlay hand-rolls its own invalidation (B-349 follow-up)

**Origin:** owner reported (2026-09-28) the "closing the menu leaves visual leftovers" symptom (B-349)
is also visible leaving the fullscreen visualiser, and asked whether this is a structural problem with
Helios — menus/bars/lists should be reusable components so switching views is less bug-prone even if the
firmware itself has to be more rigid.

**Finding: yes, structural.** Helios today (`fw/helios.inc`) is not a view manager — it is a deferred-draw
wrapper around exactly ONE region, the player chrome (`ui_chrome_paint()`). Every other screen (Settings,
the library browser, the fullscreen visualiser, Blit Test, Meter Sweep, Meter Trace, the Winamp Configure
page) is its own independent state machine with its own open/close flag (`set_open`, `lib_ui_open`,
`ui_fullscreen`, …) and its own hand-written "what to invalidate on the way out" logic:

- **`pl_ui_restore`**, the shared "an overlay just closed, repaint the player" flag, is set from **6
  separate call sites** (`fw/settingsui.inc`, `fw/library.inc`, `fw/fullscreen.inc` x3, `fw/suite.inc` x2)
  — each written at a different time by whoever built that feature. `set_close()` simply never got one
  (B-349) because nothing enforces that every overlay-close path sets it; the fix was one more manual line,
  not a structural guarantee.
- **What the flag triggers is a fixed, hand-maintained checklist**, not a real invalidation query: `pl_ui_restore`'s handler
  (`fw/player.c` ~9005) always calls `ui_chrome_paint()`/`ui_art_draw()`, always clears `ui_bg_ready`, always
  forces `wave_drawn[]`/`spec_drawn[]` stale. Every new visual subsystem this project has added (the B-256
  meter-background-strip rebuild, `ui_wave_force`, fullscreen's own `helios_excl[]` exclusion rects for its
  corner label) had to be individually remembered and folded into that one list by hand. A leftover after
  fullscreen despite it correctly setting `pl_ui_restore` on both transitions (`fw/fullscreen.inc:154-155`)
  is exactly the failure mode this predicts: fullscreen owns pixels and a label-exclusion mechanism the
  fixed checklist doesn't know about, so "restore" is only ever as complete as the last person who edited it
  remembered to make it. Not independently confirmed against hardware this pass (no card mounted) — the
  mechanism-level gap is real regardless of whether this specific instance is the same root cause.
- **Helios's own region registry could carry this today and doesn't.** `helios_region_register()` already
  supports per-region dirty tracking and beam-safe deferred redraw; nothing about it is chrome-specific.
  Settings/library/fullscreen/Blit-Test/etc. never register as regions — they draw directly, gated on their
  own boolean, invisible to Helios entirely.

**Proposed fix (design only, not built): a real `View` layer on top of Helios's existing region primitive.**

```
typedef struct {
    void (*enter)(void);     /* called once, becomes the active view                      */
    void (*exit)(void);      /* called once, leaving — owns its OWN cleanup, nothing more  */
    void (*draw)(void);      /* registered as a Helios region automatically on enter       */
    void (*input)(uint32_t edge, uint32_t keys);
    uint8_t invalidate_mask; /* which shared caches this view's pixels can touch: WAVE, SPEC, BG_STRIP, ART, CHROME, EXCL */
} helios_view_t;
```

`helios_view_switch(to)` becomes the ONE place that closes the outgoing view (`exit()`), invalidates
exactly the union of both views' `invalidate_mask` bits (not a fixed list — a real declared contract per
view, checked once at registration time the way `_Static_assert` already checks `HELIOS_VACT`), and enters
the new one. `set_close()`, `lib_ui_close()`, the fullscreen toggle and the diagnostic pages' own close
paths all collapse into calls to this one function instead of each independently setting `pl_ui_restore`
and hoping the fixed checklist happens to cover what they drew. A missing case becomes a registration-time
gap (an obviously incomplete `invalidate_mask`) instead of a silent, hard-to-spot leftover-pixel bug three
sessions later — the exact failure class both this entry and B-349 hit.

**Scope, honestly:** this touches `fw/settingsui.inc`, `fw/library.inc`, `fw/fullscreen.inc`, `fw/suite.inc`
(Blit Test/Meter Sweep/Meter Trace), `fw/player.c`'s main dispatch (`SET_INPUT`, `UI_OVERLAY_UP`, the
`pl_ui_restore` block itself) and `fw/helios.inc`. It is a real multi-file refactor of working, shipped
UI-dispatch code, not a bolt-on — and there is no card mounted this session to verify it against hardware
before or after. Recorded here as the owner-requested structural answer; **not started**, pending a scope
decision (full refactor now vs. continuing point fixes and taking this as a Phase H1.5 follow-up once H0/H1
are hardware-confirmed, per section 9's existing phasing).

## 12. No shared dialog/alert primitive exists — three independent mechanisms (proposed, design only, 2026-09-28)

**Origin:** fixing the misleading "PLAYS 48kHz MAX" hi-res-FLAC refusal wording (`ui_rate_unsupported()`,
owner-reported as "cryptic" after a real 96kHz FLAC test track) surfaced that this project has **three
separate, independently-built "tell the user something went wrong" mechanisms**, confirmed by reading each
one directly rather than assumed:

1. **`ui_toast_msg()`/`ui_toast_set()`** (`fw/player.c`) — a transient, auto-dismissing banner, non-blocking,
   drawn and cleared through the normal main-loop pass. The most-used of the three (~20 call sites).
2. **`ui_rate_unsupported()`** — a persistent message written into the now-playing card's own
   album/format row (`track_album`, reusing `fb_text_boxed()` with no marquee/scroll of its own, which is
   exactly why the just-fixed wording clipped: a long string has nowhere to go on this row). It has no
   explicit dismiss at all — it clears only as a side effect of the next track loading successfully.
3. **`ui_failed_msg()`/`ui_load_failed()`** — a full-screen, **blocking** takeover: a literal `for (;;) {
   poll_input(); ... }` spin loop that bypasses the main loop and Helios entirely. Worse than "no button
   dismiss" — reading the code directly shows it has **no button dismiss path at all**: the only way out is
   picking a different file from the Analogue Pocket's own Core menu (`slot_changed()`), which is external
   to this firmware. This is the single most severe error state the core can reach, and it is also the
   least dismissible one.

**Proposal: one shared Helios primitive, message + dismiss action(s), usable from anywhere.**

```
typedef struct {
    const char *title;    /* e.g. "LOAD FAILED", or NULL for a one-line dialog */
    const char *message;  /* the actual text -- NOT hand-assembled into a fixed-size row buffer per
                              caller the way track_album/ui_toast currently are; owns its own layout */
    const char *dismiss_label;   /* e.g. "OK", "B TO CLOSE" -- shown on screen, not assumed by the caller */
    void (*on_dismiss)(void);    /* optional; most callers need nothing beyond "close it" */
} helios_dialog_t;

static void helios_dialog_show(const helios_dialog_t *d);
static void helios_dialog_dismiss(void);
```

Built as one more Helios view (section 11's `helios_view_t`, once that lands) rather than a bespoke draw
routine: `enter()` marks it the active view and registers its own dismiss key handling through the normal
input dispatch (B/Start, matching every other overlay's existing convention — B-145 already made this
consistent for Settings/Library), `exit()` calls `on_dismiss` if given and hands control back to whatever
view was active before. Text layout is centralized once (word-wrap or a real fixed-width box with
overflow handling, not each caller building a string into a 64-byte row buffer by hand and hoping it fits
the width available at that draw position — the exact bug class the FLAC wording fix just hit).

**What this replaces, concretely:**

- `ui_load_failed()` gains a real dismiss (B or Start), and stops being a blocking spin loop -- the dialog
  view's `enter()` just marks itself active; the normal main loop keeps running underneath (reload watching
  moves from ui_failed_msg's own private poll loop into ordinary input dispatch, the same place every other
  view's B-press-to-close already lives).
- `ui_rate_unsupported()`'s message moves out of the now-playing card's cramped album row into the same
  shared dialog surface, with a real text box sized for it instead of `fb_text_boxed()` fighting for space
  next to the art panel -- fixing the clipping this session's wording fix could not (see section 11's own
  scope note on that gap).
- `ui_toast_msg()` stays separate on purpose -- it is deliberately non-blocking and auto-dismissing, a
  different shape of notification (a hint, not an error the user must acknowledge), and conflating the two
  would make routine toasts feel like errors. The dialog primitive is for anything that needs a real
  acknowledgement; toast stays as-is.

**Not built.** Design only, sized as a natural sibling to section 11's `helios_view_t` (it IS one, once that
exists) rather than a new subsystem — the two should land together or in either order, but a dialog view
needs the view-switch machinery section 11 describes to have a real "hand control back to the previous
view" step. First real build step, when taken: `ui_load_failed()` alone (highest-value target, currently
has no button dismiss at all), verified it doesn't regress the reload-watching behavior it currently has.

## 13. Cross-references

- `docs/features/PHASE_F_SPEC.md` section 15 — the original tearing investigation this document supersedes with a
  complete proposal (kept in place as the historical record of B-232's investigation).
- `docs/features/ARCHITECTURE_ROADMAP.md`'s "UI/UX redesign" placeholder (B-115) — this document is the first real,
  evidence-based answer to its own open "how it interacts with the Phase F blit engine" question.
- `docs/AUDIT_TRAIL.md` B-232 (tearing investigation), B-236 (RAM-shrink link.ld gotcha, unrelated but same
  session), B-237 (this document's own build + the prior-art research).
