# Phase F spec — blit engine, M10K release, spectrum, and the kernel/3D decisions

Written 2026-09-22, before any code. Covers the next RTL build and everything competing for the block RAM it
needs. Companion to `docs/ARCHITECTURE_ROADMAP.md` (which carries the phase ordering) and
`docs/PHASE_G_SPEC.md` (cold code, which this unblocks and is unblocked by — see section 4).

Evidence labels follow the project convention: **[HW]** measured on a Pocket, **[FIT]** from a real Quartus
fit report, **[SRC]** read out of this repo's source, **[EST]** estimate, **[EXT]** external source.

## 0. The short version

Five decisions settle the scope. Each has its reasoning recorded because reversing one later without the
reasoning is how this list gets re-derived from scratch.

| # | Decision | Why |
|---|---|---|
| **D1** | **No FFT. A hardware octave filter bank instead.** | The software octave cascade already ships and looks right at **1.5% CPU**; the same firmware comment records why FFT lost: **1024-point FFT 13.1% CPU, 12 parallel biquads 31.9%** [SRC]. In hardware the cost inverts differently than expected: a Goertzel/one-pole bank needs **~0 M10K** (two state registers per band, one time-shared DSP MAC) while even a lean FFT needs real buffers [EXT]. Intel's FFT IP for Cyclone V is **not license-free** for distributed bitstreams [EXT], so it would have to be hand-built anyway. The upstream roadmap reached the same conclusion independently: "the filter bank is affordable... but lands in the budget that keeps the decoder fed" [SRC]. |
| **D2** | **The 3D GPU is dropped as a goal.** | A full-screen 16-bit Z-buffer at 400x360 is **2,304,000 bits = ~281 M10K** [EST] on a device with 308 total. 8-bit Z is still ~141. Only tile-based rendering is feasible (~20-25 blocks plus heavy ALM plus a real SDRAM-bandwidth risk), and the actual want — visualiser eye-candy — is reachable from 2.5D primitives on the 2D engine for ~2-4 blocks. See section 8. |
| **D3** | **Audio kernels stay gated on a profile that has never been run.** | Phase D step 1 says "profile first, and if it's fast enough, stop here" — it has now run (B-087). **Correction, same table:** this row originally extrapolated FLAC's bit-reader dominance (`FLAC.md`, R 64-76%) to MP3 as well. The MP3 measurement (B-087, `docs/ARCHITECTURE_ROADMAP.md` section 2) shows the opposite: Huffman/bit-reading is MP3's *smallest* stage (3-5%), Subband (the synthesis filterbank) is its largest (55-59%) — so for MP3 the pre-D3 roadmap order (filterbank first) was right, and this row's inference did not hold outside FLAC. The two codecs need separate kernel orderings, not one shared conclusion. FLAC's own R could not be refreshed (the available test vectors are VERBATIM-only, see B-086/B-087) so its bit-reader-first case stands on the original measurement, unrefreshed, not this one. |
| **D4** | **The EQ is shipped, not planned.** | `eq_biquad` is instantiated in `mp3_soc.v`, listed in `ap_core.qsf`, wired to `R_EQ` (0x68), confirmed on hardware [SRC]. `EQ_DESIGN.md` still says "Nothing here is built" — corrected. It also set the precedent this whole spec leans on: *"the EQ must not use M10K"*, solved with `ramstyle = "MLAB, no_rw_check"`. |
| **D5** | **M10K is released cheaply first; the main-RAM shrink comes last.** | MLAB migration and a font-ROM repack are near-zero-risk and fund the blit engine on their own. The 64-block main-RAM shrink depends on the meters going cold, which depends on the blit engine — so it cannot come first. Section 4. |

## 1. The M10K map (roadmap Phase B item 1 — now done)

Derived from a real fit report, `work/diagnostics/psram-diag/reports-b018-prod/ap_core.fit.rpt` (the B-018
product-configuration build), whose per-instance Block Memory table sums to exactly the 300 in
`ap_core.fit.summary`. Cross-checked against seven other saved fit reports — all show 300/308, so this is
stable, not a one-off. [FIT]

| Consumer | Declared | M10K |
|---|---|---|
| `mp3_soc` `ram0..ram3` — main system RAM, 256 KB as four byte-lane arrays | 65536x8 each | **256** |
| `mp3_fb` `font_rom` — 95 glyphs, 16x16, 4bpp | 3040x32 | **16** |
| `mp3_soc` `pcm_fifo` — "2048 entries = ~43 ms at 48 kHz" | 2048x32 | 7 |
| VexRiscv I-cache (banks + tags) — 4 KiB, 128 lines, direct-mapped | 1024x32 + 128x22 | 5 |
| VexRiscv D-cache (4 byte lanes + tags) — 4 KiB, same geometry | 1024x8 x4 + 128x22 | 5 |
| `mp3_fb` `cmd_mem` — draw command FIFO | 256x82 | 3 |
| `core_bridge_cmd` `mf_datatable` — APF boilerplate | 256x32 TDP | 2 |
| `mp3_fb` `linebuf` — scanout line buffer | 1024x16 | 2 |
| VexRiscv register file (2 banks) | 32x32 each | 2 |
| `mp3_fb` `glyphbuf` | 128x16 | 1 |
| `sound_i2s` `sync_fifo` (dcfifo minimum) | 4x32 | 1 |
| | | **300** |

Three things this settles:

- **85% of all block RAM is one thing** — the 256 KB main RAM.
- **Diagnostic and probe RTL costs nothing in the product.** SignalTap and `TAU_PHASE2_PROBE` are confirmed
  macro'd out of the checked-in `ap_core.qsf`; SignalTap's ~8 blocks of sample RAM only ever appear in a
  separately staged VM build [SRC][FIT].
- **The PSRAM instruction-fetch line-fill path costs zero blocks** — it reuses `tau_psram_bus` and ACKs each
  of the eight beats straight into the I-cache line; there is no buffer [SRC].

`tools/quartus_fit_summary.py` does not parse this table (it only reads the top-line total). Extending it to
emit the per-instance breakdown is a small, worthwhile follow-up so this map regenerates itself per build.

## 2. MLAB — the resource nobody is using

MLAB usage across every saved fit report is **zero**, while ALMs sit around 35% free [FIT]. On Cyclone V each
MLAB is **10 ALMs = 640 bits**, natively **32 words x 20 bits, simple-dual-port only** (there is no true
dual-port MLAB) [EXT]. Syntax is the one the EQ already uses:

    (* ramstyle = "MLAB, no_rw_check" *) reg [W-1:0] mem [0:D-1];

`no_rw_check` matters: MLAB read-during-write only cleanly supports old-data behaviour, and without it Quartus
inserts bypass muxing [EXT].

**Migration candidates**, all simple-dual-port and therefore MLAB-legal:

| Memory | Blocks freed | Note |
|---|---|---|
| `cmd_mem` draw command FIFO (256x82) | 3 | ~40 MLABs / ~400 ALMs [EST]. The riskiest of the four — see the Fmax caveat below. Consider whether 256 entries is actually needed before chaining that deep. |
| VexRiscv register file (2 banks, 32x32) | 2 | Each bank stores 1,024 bits in an 8,192-bit block — ~12.5% utilised. But it lives in the **generated** `VexRiscv_Full.v` netlist with no SpinalHDL source in-repo, so this means hand-patching a `ramstyle` attribute on two `altsyncram` instances. Lowest priority, highest awkwardness. |
| `glyphbuf` (128x16) | 1 | Clean fit, ~4 MLABs. |
| `sound_i2s` dcfifo (4x32) | 1 | Holds 128 bits in a whole block purely because a `dcfifo` won't go smaller. Needs an `lpm_hint` RAM_BLOCK_TYPE change. |

**Caveat [EST], not cited:** deep MLAB chaining (the 256-deep FIFO needs ~8-deep chaining) carries an
unquantified Fmax penalty, and this design already has a documented timing cliff at 100 MHz (section 11). Do
the three easy ones first and treat the command FIFO as its own decision with a timing check.

Expected: **+7 blocks** for essentially no functional change.

## 3. Font ROM — two independent wins

The ROM holds 3,040 words x 32 bits = 97,280 bits but occupies 16 blocks [FIT]. At 32-bit width an M10K is
256x32, so 3040/256 = **12 blocks is the theoretical fit** — it is packing at ~74% efficiency.

1. **Repack (+4 blocks, near-zero risk).** Split it into four byte-wide arrays — exactly the trick `mp3_soc`
   already uses for main RAM, and for the same reason. No behaviour change; one build confirms it. **[EST]** —
   Quartus inference is not fully predictable from source, so this is a hypothesis a fit report settles.
2. **Move it to PSRAM entirely (+12-16 blocks, medium risk).** It is read-only and not on the real-time
   scanout path, and the PSRAM data window is proven [HW]. Costs PSRAM latency per glyph fetch plus bus
   contention with scanout, so it wants a small BRAM glyph cache in front — call it **+12 net**. Only do this
   if the blocks are actually needed; it is the first item here that can hurt something.

## 4. The M10K ledger and the ordering constraint

**Corrected 2026-09-22 (B-102) against a real fit, not an estimate.** Section 2's "+7 MLAB / +4 font repack"
were both pre-fit predictions. Only two of the four MLAB candidates were in scope for this build (`cmd_mem` and
the VexRiscv regfile deliberately excluded, see section 2) and the font repack was measured, not assumed —
**it delivered +0, not +4.** Real result, both seeds: 298/308 used (was 300/308), a net **+2 blocks**, all of it
from `glyphbuf` + the `sound_i2s` dcfifo resolving to MLAB. `TAU_FONT_REPACK` is confirmed functionally inert
(as designed) but is *also* fitter-inert — declared content bits are unchanged by construction (section 10's
correction already covered this), and it turns out the fitter does not find fewer physical M10K primitives for
four narrow ROMs than one wide one either. **On-chip repacking is not a lever for the font ROM; PSRAM (row
below) is now the only real one if those blocks are needed.**

| Step | Frees | Running free | Risk |
|---|---|---|---|
| Today (measured baseline, B-018/A-114 product fit) | — | **8** (300/308 used) | — |
| MLAB: `glyphbuf` + `sound_i2s` dcfifo (B-101/B-102, fit-confirmed, both seeds) | +2 | 10 | very low; one seed showed a real -0.001 ns setup violation on the glyphbuf write path, the other closed positive on all four corners — pick that seed |
| Font ROM repack (B-102, fit-confirmed) | **+0** (not +4 — corrected) | 10 | none (inert, just does not help) |
| MLAB: `cmd_mem` (still needs its own timing check per section 2) | +3 (estimate, unverified) | 13 | medium — the Fmax caveat on deep MLAB chaining is still untested |
| MLAB: VexRiscv register file (generated netlist, needs hand-patching) | +2 (estimate, unverified) | 15 | low risk, high awkwardness — still deferred |
| *Blit engine consumes (CLUT + working)* | −2 | 13 | — |
| *Spectrum filter bank consumes* | ~0 | 13 | — |
| *FLAC bit-reader accelerator, if the profile justifies it* | −2 | 11 | — |
| Font ROM to PSRAM — **now the real path to font-related blocks, not a fallback** | +12 | ~23 | medium |
| **Main RAM 256 KB -> 192 KB** | **+64** | **~87** | see below |

**The ordering constraint, which is the important part of this document.** Main RAM cannot shrink first.
`RAM_WORDS` must be a power of two — a 48 KB attempt once exploded the fitter into LUTs [SRC] — so the options
are 256 KB or 128 KB. Halving needs ~93 KB more headroom and only ~34.7 KB is free, so it is unreachable.

**Proposed workaround:** instantiate **two** power-of-two arrays (128 KB + 64 KB = 192 KB), address-decoded as
one contiguous region. Each array stays a clean power of two, sidestepping the irregular-depth problem that bit
them before. That frees **64 blocks** and needs only ~29 KB more headroom.

Where that ~29 KB comes from: the cold set was estimated at 35-45 KB and only ~24 KB has moved. The two named
remaining pieces are **the meters (~17 KB) and picojpeg (~8 KB)** — almost exactly the gap. And the meters are
deliberately still hot *because they are waiting on the blit engine* (`PHASE_G_SPEC.md` says so in as many
words). So:

> **blit engine → meters go cold → ~29 KB freed → main RAM 192 KB → 64 blocks released**

You cannot fund the blit engine out of the RAM shrink. It runs the other way, and that is what fixes the
ordering of everything below.

**A caution on the shrink itself.** It trades one scarce resource for another. Heap gap has repeatedly been
driven to its floor as features landed (4,096 B at worst, before Phase G). It is reversible only by another
45-minute build.

### 4.1 How to decide the shrink is safe — measure peak, not the static gap

**The link-time heap gap (34,752 B) is the wrong number.** It is a static figure and does not capture peak
*stack* depth, which is what actually decides whether 192 KB holds. Method:

1. **Paint the stack** with a known pattern at boot, then read back the high-water mark — the standard embedded
   technique. It drops straight into the existing Check infrastructure as a reported line (peak stack, peak
   heap, and the resulting true free figure), so it is measured the same way everything else here is.
2. **Measure under genuine worst case**, not idle: ENDURANCE profile + a full library + a large playlist + a
   fresh large cover decode (the uncached path, not the signature-reuse path) + browse-while-playing + every
   meter mode. Browse-while-playing matters most — it is the case G4 introduced and the one not otherwise
   regression-tested.
3. **Write the margin down before the measurement**, per the standing prediction discipline. Proposed:
   **16 KB** (~8% of 192 KB). Pick and record it before seeing the number, not after.
4. **Gate:** shrink only if `measured peak + margin < 192 KB` under the worst profile above.

`RAM_WORDS` is already a `localparam`, so reverting is a one-line change plus a build.

## 5. Blit engine — feature spec

Existing engine, for reference [SRC]: opcodes `RUN`/`RECT` (flat fill), `CHAR` (AA glyph, coverage-blended
against an explicit bg, Bresenham 1x/1.5x/2x/3x per axis), `COPY` (raw SDRAM->SDRAM move); 256-entry x 88-bit
async command FIFO; scanout-first dispatcher decomposing every operation into single-burst units so a pending
scanline fill waits at most one burst (~500 cycles) against ~4,167 cycles of per-scanline slack; single-buffered
400x360 RGB565 at stride 512.

`COPY` is already load-bearing for three separate jobs — the cover-art slide panel, the waterfall meter scroll,
and an off-screen gradient stash used to erase cheaply. **Generalising COPY is therefore most of the ask, and
it is already proven in software.**

### Tier 1 — this build

| ID | Feature | M10K | Notes |
|---|---|---|---|
| **B1** | **Generalised blit** — arbitrary rect, independent source and destination stride/base | 0 | Extends `COPY`. Removes the current `FB_COPY_MAX=127` chunking quirk (a 7-bit `char_w` field where 128 truncates to 0). |
| **B2** | **Colour-key transparency** — one reserved RGB565 value, one comparator | 0 | Right default for a true-colour framebuffer. Palette-indexed hardware (Genesis, Neo Geo) used index-0 keying precisely because it is nearly free; PSX needed a real transparency bit only because it had no fixed palette [EXT]. |
| **B3** | **Sub-pixel skew + first/last column masks** | 0 | The Amiga/Atari ST mechanism: a barrel shifter on the source channel plus first/last-word masks AND'd before the write. Costs tens to ~100 LUTs [EXT]. **This is the fix for the marquee**: a firmware comment states the engine "does not clip one partially off the left edge", which is why long titles scroll by whole characters. |
| **B4** | **Scaled blit, nearest (Bresenham/DDA)** | 0 | **Key finding:** because the decoded cover already sits in a randomly-addressable buffer, *no line buffer is needed* — one read per output pixel (or four, if bilinear is ever wanted). Line buffers are a streaming-source problem, and this source is not streaming [EXT]. The block-RAM cost I originally assumed here is zero. |
| **B5** | **Alpha blend** | 0 | Two paths. A **DSP multiply** path for real 0-255 alpha — affordable, 55 of 66 DSPs are free — and a **shift-add** path for the PSX fixed ratios (`B/2+F/2`, `B+F`, `B-F`, `B+F/4`, all shifts and adds with clamping) [EXT] as a cheap mode. Note the constraint that forced PSX's design does not bind us; we choose fixed ratios for speed, not necessity. |
| **B6** | **Meter column primitive** | 0 | A bar is `(x, base_y, height, lit, unlit)`. One opcode replaces ~72 `fb_rect` calls per frame on the hottest per-frame path in the firmware. |
| **B7** | **SDRAM busy-cycle counter** | 0 | Bundled here per the standing decision (B-083). **Prerequisite, not a nice-to-have**: without it there is no way to show a new SDRAM client did not eat the audio margin. |

### Tier 2 — if it fits in the same build

| ID | Feature | M10K | Notes |
|---|---|---|---|
| **B8** | **CLUT / palette blit** | 1 | Unifies glyph, thumbnail and icon rendering into one pixel path. Also enables B9. |
| **B9** | **Palette re-index for dim/highlight** | 0 | The Genesis shadow/highlight trick: do not blend, just re-index to a shadow or highlight palette [EXT]. Near-free dimmed/selected UI states, and a nearly free theme or dark-mode swap once a CLUT exists. |
| **B10** | **Hardware RLE source blit** | 0-1 | Cover-art rows and meter thumbnails are *already* RLE-encoded in firmware and pushed one command per run. Reading `(run, value)` pairs from a source buffer collapses that. Architecture model is the 3DO Cel Engine's DUP/PDC split — a streaming decompressor stage ahead of a conventional pixel pipeline [EXT]. No adaptable RTL exists; design it fresh. |
| **B11** | **Hardware rounded-rect** | 0 | Replaces `fb_round_rect_on`'s software corner-overpaint, used on every selection highlight. |

### Tier 3 — after this phase (see section 13)

2.5D primitives, an overlay compositing layer, double buffering.

## 6. Prior art studied, and what may actually be used

**Correction, 2026-09-22:** an earlier draft of this section claimed the repository had no LICENSE file. That
was wrong — the claim came from a shell-globbing artifact, not from the tree. **`LICENSE` exists, is MIT, and
has been tracked since the first commit**, carrying two copyright lines (HarpMudd, and alfatreze for the Tau
modifications). `NOTICE.md` and the README's Credits section already document the third-party boundaries
thoroughly and accurately.

That settles the constraint rather than removing it, and it settles it in the direction that matters here:
**because Tau's own code is MIT, copyleft RTL cannot be copied in without relicensing the whole project.** So
the licence column below is a hard constraint — GPLv2/GPLv3 sources are study-and-reimplement only, not
copy-and-adapt. Techniques are decades-old silicon and safe to reimplement independently; code is not.

**Decision: keep MIT.** It matches the ecosystem this project draws from and gives back to (agg23's utils,
VexRiscv and PocketQuake are all MIT), the two real obligations below are per-file and already quarantined
under `third_party/`, and changing it would need HarpMudd's agreement anyway since their copyright line is in
it. No action required — this is recorded so the question is not reopened without reason.

**Licence hygiene checklist** (small, not urgent, none of it blocks Phase F):

1. **Verify VexRiscv's licence properly.** The README asserts MIT but the generated `src/fpga/rtl/VexRiscv_Full.v`
   carries no licence header of its own — the claim is credit-only, not in-tree evidence. Check it against the
   upstream VexRiscv/SpinalHDL project and record the result in `NOTICE.md`. **Not done here** because it needs
   an external source, and asserting a licence without verifying it is exactly the failure mode this checklist
   exists to prevent.
2. **Add a provenance line for `assets/branding/` and `assets/ui/tau-loading-source.jpg`** in `NOTICE.md`.
   Currently it is only implicit that these are owner-authored. **Not done here** — only the owner knows.
3. **Optional: SPDX identifiers** (`// SPDX-License-Identifier: MIT`) on the project's own source files. Cheap,
   machine-readable, makes any future audit trivial. A bulk edit across many files, so left as a deliberate
   choice rather than done in passing.

Two in-tree obligations are already correctly recorded in `NOTICE.md` and stay relevant to anything new:
**Helix MP3 is RPSL 1.0** (per-file source-disclosure, vendored unmodified in `third_party/`, not relicensed by
Tau's MIT) and **Inter is SIL OFL 1.1**, with the generated `font_rom.v` treated as a derivative font work under
the same terms — worth remembering in section 3, since moving the font ROM to PSRAM changes how it is stored
but not what licence it carries. Separately, `src/fpga/apf/` is under **Analogue's proprietary EULA**, not an
open-source licence, which the existing don't-edit rule already handles.

| Implementation | Licence | Verdict |
|---|---|---|
| **Minimig / Minimig-AGA Amiga blitter** (`agnus_blitter.v`) | **GPLv3** | **Study, reimplement.** Two barrel shifters, a 256-minterm generator from `bltcon0`, BLTAFWM/BLTALWM first/last-word masks, fill-mode carry latch, ~13-state FSM. The single best architectural reference for B3. The *technique* is 1985 silicon and safe to reimplement independently; the *code* would pull GPLv3 in. |
| **PSX_MiSTer GPU** | **GPLv2** | **Study.** The semi-transparency ALU is the direct reference for B5's shift-add path. |
| **Atari ST blitter** (AtariST_MiSTer) | **none stated** | **Study only — do not copy.** No LICENSE file found; treat as all-rights-reserved. Architecture mirrors the Amiga's anyway. |
| **Saturn VDP1** (Saturn_MiSTer) | **none stated** | **Study only — do not copy.** Framebuffer-based quad renderer; less relevant than expected. |
| **Neo Geo LSPC** | docs only, no clean RTL | **Technique only.** Two things worth taking: a **ping-pong double line buffer** scanline renderer (we already have the line buffer), and **vertical shrink via a lookup table** rather than an accumulator — a real alternative to Bresenham if table-driven zoom ever fits better. |
| **Genesis/MD VDP** | study | **Technique only.** Colour-key compositing at the line buffer (B2) and the shadow/highlight palette trick (B9). |
| **3DO Cel Engine** | vendor docs, no RTL | **Model only.** The DUP/PDC decompressor-ahead-of-pixel-pipeline split, for B10. |
| **agg23/analogue-pocket-utils** | **MIT** | **Usable**, and already used for PSRAM/CDC/I2S plumbing — but it contains **no 2D/draw IP**. Nothing to borrow for this. |
| **PocketQuake** | **MIT** wrapper | **Directly relevant structurally.** Same chip class, same problem: partition memory so a latency-sensitive buffer (its z-buffer, in a dedicated SRAM) stays off the bus carrying bulk assets, with an accelerator absorbing the hot per-pixel loop, alongside real-time audio. Read it for the partitioning, not the rasteriser. |

## 7. Spectrum — a hardware filter bank, not an FFT

Per D1. What ships today is an **octave cascade of one-pole low-passes**, gated to run only while its meter is
on screen, at ~1.5% CPU [SRC]. The firmware comment is explicit that this is "NOT an FFT, and not a bank of
parallel band-passes — both are far too expensive here", and that a real filter bank was the one addition that
could reintroduce audio glitches.

**The plan is to move that same structure into RTL**, not to replace it with something grander:

- **Cost: ~0 M10K.** Per band, a Goertzel or one-pole IIR needs two state registers and 2 MACs per sample; a
  single DSP-based MAC time-shares across all 16-32 bands inside one clock at 48 kHz [EXT]. State lives in
  ALM/MLAB, exactly as the EQ's does.
- **It removes the reason the feature was constrained.** At 0% CPU it can run continuously instead of only
  while its meter is visible, and it stops competing with the budget that keeps the decoder fed — which is what
  both the upstream roadmap and the firmware comment identified as the risk.
- **Precision:** for a display-only path, 10-12 bits is ample [EXT] — far below the 36-bit Q20.16 the EQ needed
  after 32-bit caused limit-cycle rumble [SRC]. Different problem, different budget.
- **If linear bins are ever genuinely wanted**, the cheap route is a streaming **R2SDF** FFT whose delay-feedback
  stages are mostly shallow enough to live in MLAB, with **CORDIC-generated twiddles** instead of a ROM —
  reportedly near-zero block RAM at these sizes [EXT]. Parked, section 13.

## 8. 3D — the verdict, and what to build instead

Dropped per D2. Recorded so it is not re-derived:

- Full-screen 16-bit Z at 400x360 = ~281 M10K; 8-bit Z = ~141. The device has 308 **in total** [EST].
- Z in SDRAM is possible but read-modify-write per pixel multiplies framebuffer bandwidth on a bus already
  shared with scanout, audio and the CPU window — and Phase H already projects scanout alone rising to 35-45%
  of SDRAM cycles at 720 [EST].
- Tile-based rendering is the only feasible form: ~8 blocks tile Z + ~8 tile colour + bin lists + texture cache
  ≈ **20-25 blocks**, plus substantial ALM for the rasteriser and a real threat to the **zero-late-underrun**
  record that every stress run has held so far.
- PocketQuake proves 3D *is* possible on this chip alongside real-time audio — but it spends a dedicated
  external SRAM on the z-buffer to do it, which this design does not have free.

**What the 3D ambition is actually for is visualiser eye-candy**, and that is reachable without any of the
above. Tier 3 adds, at ~2-4 blocks total: **per-row scaled blit** (perspective floors and tunnels), **rotated
blit**, and an **affine texture-mapped quad**. This is the MilkDrop/Winamp lineage; none of it needed a 3D GPU
either. If true 3D is still wanted after the 192 KB shrink frees 64 blocks, it can be revisited honestly rather
than squeezed in now.

## 9. MMIO — decided: split engine state from per-command fields

Only **0xBC-0xFC is free** (17 words), in a page where **8 offset bits are decoded** (0x100+ aliases) [SRC].
The blit engine alone wants source base, destination base, both strides, alpha level and mode, colour key,
palette select and per-axis scale factors; the spectrum bank and any future kernel want their own. One
register per parameter exhausts the page.

**Decision: do not add a register per parameter, and do not widen the FIFO word either.** Widening `cmd_mem`
from its inferred 82 bits to the ~200 a full descriptor needs would take it from 3 M10K to **~7** at 256 deep
[EST] — eating most of what the MLAB migration frees. Instead, split the two kinds of parameter apart:

- **Sticky engine state** — written rarely, lives in flops, never enters the FIFO: source/destination base and
  stride, colour key, alpha mode and level, palette select, per-axis scale factors.
- **Per-command FIFO word** — varies every operation: opcode, x, y, w, h, source offset. Stays near the
  current width, so the FIFO does not grow (and could be narrowed).

**Three MMIO registers total, regardless of how many state fields exist:**

| Register | Behaviour |
|---|---|
| `R_BLT_IDX` | Write selects a state field by index; auto-increments on each `R_BLT_DATA` write. |
| `R_BLT_DATA` | Writes the selected state field, then auto-increments the index. A burst of stores loads a whole state block with one index write. |
| `R_BLT_GO` | Opcode + flags + the per-command fields; latches and enqueues. Same trigger pattern as today's `R_FB_GO`. |

That is **3 of the 17 free words for an unbounded number of state fields**, leaving ~14 for the spectrum bank
and any kernel. Precedent: this is exactly the Amiga split — `BLTCON`/`BLTAFWM`/`BLTALWM` are persistent
registers and only the size write triggers the blit [EXT].

**Do this before any Phase F RTL is written.** Retrofitting after the blit engine, the spectrum bank and a
kernel have each claimed registers ad hoc is the expensive version.

## 10. Build plan

**Macros, one per separable piece** — this is what makes a failed build bisect instead of rebuild:

| Macro | Covers |
|---|---|
| `TAU_BLIT` | The new opcodes and the descriptor/state register file (section 9). |
| `TAU_BLIT_BLEND` | The DSP blend and scale pipeline — **kept separate on purpose**: it is the deepest new pipeline and therefore the documented -1.888 ns timing-cliff risk. Droppable without losing the rest of the engine. |
| `TAU_MLAB_MIGRATE` | Section 2. Functionally inert. |
| `TAU_FONT_REPACK` | Section 3 item 1. Functionally inert. |
| `TAU_SDRAM_BUSY` | The busy-cycle counter (B7). |

**Step 1 — synthesis only, no fit (~5-6 min, not ~45). Done 2026-09-22 (B-100); corrected from this
paragraph's original claim.** Ran `TAU_MLAB_MIGRATE` + `TAU_FONT_REPACK` through `quartus_map` (compared
against a same-tree baseline with both macros off) and read the RAM summary. The result is **not** simply "the
block-count drop is the entire result" — that holds for one of the two pieces but not the other:

- **MLAB migration: confirmed at this stage, and this stage is sufficient.** The RAM Summary table's `Type`
  column is a direct report of what Quartus resolved each RAM to, and `glyphbuf` and the `sound_i2s` dcfifo both
  resolved to `MLAB` (baseline: 0 MLAB bits; step 1: 2,176 MLAB bits, and the M10K-pool bit total dropped by
  exactly that much). A `ramstyle`/`lpm_hint` request either resolves to the requested type or it doesn't — the
  SignalTap proof build hit the *doesn't* case (an MLAB request silently fell back to M10K over capacity), so a
  synthesis-stage type check is a real, load-bearing thing to confirm before spending a fit.
- **Font ROM repack: synthesis-only is the wrong tool for this question, and cannot confirm it.** `quartus_map`
  reports each RAM's *declared content size* — 4x 24,320 bits (97,280 total), identical to the original single
  3,040x32 array's 97,280 bits, because it is the same content in different lanes. Whether four 8-bit-wide ROMs
  actually pack into *fewer physical M10K primitives* than one 32-bit-wide ROM (the entire point — going from
  ~74% packing efficiency to something tighter) is decided by the **fitter's block-allocation pass**, which
  synthesis does not run and does not preview. Checked the full `quartus_map` log for any packing/physical-block
  hint; there is none. **This piece's win is genuinely unconfirmed until Step 2's fit.**

So step 1 de-risks the MLAB piece (go ahead with confidence) but does not de-risk the font repack the way this
document originally claimed — that risk transfers to step 2 unchanged. Evidence: `docs/AUDIT_TRAIL.md` B-100.

**Step 2 — one full build, multi-seed**, with everything bundled (the counter is needed to validate the engine,
so they belong together):

1. Blit engine Tier 1 (+ Tier 2 if it fits)
2. MLAB migration
3. Font ROM repack
4. SDRAM busy-cycle counter

**If timing fails:** first bisect is dropping `TAU_BLIT_BLEND`. Keep the inert items on — they change no
behaviour, so any trouble they cause is a packing/routing effect that the multi-seed convention should absorb.

**Timing failed on both seeds (B-107/B-109, 2026-09-23) — but not on the path this bisect assumes.**
`report_timing` shows the violation is entirely inside `glyphbuf`'s existing MLAB write-data arithmetic, the
same path `B-102` already flagged as near-zero-margin *before* the blit engine existed, not on the new
`TAU_BLIT_BLEND` pipeline. Dropping `TAU_BLIT_BLEND` is still the cheapest next experiment (less logic overall
may relieve the congestion pushing this path over), but it is not a proven fix for the actual failing path — see
section 11's corrected row. If it doesn't recover positive slack, the real fix is pipelining that specific
`glyphbuf` write-data path (an extra register stage on the address-to-write-port arithmetic), independent of
the blit engine's own macros.

**Bisect run (B-110, 2026-09-23): recovered almost everything, but the worst path moved.** Both seeds went from
-2.5/-2.6 ns to +0.02/-0.11 ns (seed 2) and +0.05/-0.10 ns (seed 1) — consistent across seeds, so a real
structural gap remains, not noise. The `glyphbuf` violation itself is gone; the new worst case is a *different*
pre-existing path, B6/BAR's `cmd_q` -> `char_fg` clamp+subtract chain (same anti-pattern: combinational logic
off a BRAM-registered value, straight into another register, same cycle). **Fixed by retiming (B-111)**: computed
the same arithmetic off the raw BRAM read `cmd_q` itself registers from, on the same clock edge, instead of
after it — same function, same timing relative to everything else, verified bit-for-bit in simulation. Not yet
re-fit to confirm it closes the gap.

**The full audit (B-112) found this bug shape recurs and should be fixed proactively, not one path at a time.**
`OP_SBLIT`'s dispatch (`sblit_ext` clamp/shift off raw `cmd_q`, registered same-cycle) has the identical
structure to the just-fixed BAR bug and was not caught only because BAR happened to be what Quartus reported as
worst first. `OP_CHAR`'s `char_base` compute is a milder variant. **Recommended before the next fit: apply
B-111's exact retiming technique to `OP_SBLIT` (and optionally `OP_CHAR`) at the same time**, rather than
discover each one via another failed multi-seed build. Separately: the blend write-back into `glyphbuf`
(`A_COPYRD`, `TAU_BLIT_BLEND`-only) lands on the same write port as the original violation — meaning B-110's
"congestion relief" theory may be incomplete; a direct fix (one fewer input to that port's write-data mux) is at
least as plausible and hasn't been distinguished from the congestion theory yet. Full detail: `docs/AUDIT_TRAIL.md`
B-112, `docs/FULL_AUDIT_2026-09-23.md`.

Compare the product-config build against the shipped RBF as B-018 did, noting B-021's finding that shared-RTL
changes make bit-identity unattainable even with macros off — it is a review aid, not a gate.

Seeds: follow the existing rule (multi-seed, pick by the pre-set criterion). Compare the product-config build
against the shipped RBF as B-018 did — noting that B-021 already found shared-RTL changes make bit-identity
unattainable even with macros off, so the comparison is a review aid, not a gate.

## 11. Risks

| Risk | Why it is real here | Mitigation |
|---|---|---|
| **Timing cliff — RESOLVED for the no-blend configuration (B-109..B-117)** | The real multi-seed fit failed setup on both Slow corners, both seeds (-2.5 to -2.9 ns). `report_timing` traced it to `glyphbuf`'s MLAB write-data arithmetic (`Add32~8` -> `Selector222~1` into the write port, B-102's pre-existing near-zero-margin path), not the new `TAU_BLIT_BLEND` pipeline. Dropping `TAU_BLIT_BLEND` (B-110) recovered nearly all of it but exposed a *different* pre-existing path as new worst case: B6/BAR's `cmd_q` -> `char_fg` clamp+subtract chain, fixed by retiming (B-111); `OP_SBLIT`'s dispatch had the identical shape and `OP_CHAR`'s `char_base` a milder variant, both also fixed by retiming (B-114). Blend/`glyphbuf` theory resolved (B-116): congestion relief, not a direct fan-in fix. **The re-fit combining no-blend + B-111 + B-114 (B-117, seed 2) closed cleanly on every corner: Slow 85C +0.727 ns, Slow 0C +0.597 ns, both comfortably positive** — the first build in the whole B-107..B-117 sequence with zero known timing violations. | **Resolved for this configuration.** Confirm with a second seed before final adoption (this session's own convention, though the margin here is large enough that seed variance alone is very unlikely to flip it). `TAU_BLIT_BLEND` itself remains shelved — the `glyphbuf` write-port chain (`Add32~8`/`Selector222~1`) has never been retimed and would need it (or `KB-045`'s `DSP_BLOCK_BALANCING` idea) before blend could safely return. Full detail: `docs/AUDIT_TRAIL.md` B-109..B-117 (final), `docs/FULL_AUDIT_2026-09-23.md`. |
| **The L0 invariant** | Every stress run to date reports **zero late underruns**. It is the strongest quality signal this project has, and a new SDRAM master is exactly what threatens it. Confirmed still true of the full history in the B-112 audit (~59 mentions, one explained early false-positive, no confirmed contention-caused late underrun ever recorded). | The busy-cycle counter (B7, built, RTL-only — **no firmware consumer yet**, confirmed by B-112) is the instrument; the blit-storm Check test (section 12.1, **not built yet**, deliberately deferred per B-101 since no firmware issues blit commands to generate the traffic pattern it would test) is the regression net. Current exposure is low precisely because nothing exercises the blit engine's SDRAM traffic yet — revisit urgency once firmware starts issuing real blit commands during playback, not before. |
| **MLAB Fmax on deep chains** | The 256-deep command FIFO needs ~8-deep MLAB chaining; the chain depth is now confirmed exactly (a Cyclone V MLAB is a fixed 32x20/640-bit block, so 256/32 = 8 chained instances is precisely right, per Intel's Embedded Memory Blocks docs, 2026-09-23), but the **Fmax penalty of chaining them is still undocumented [EST]**. | Do the three easy migrations first; treat the FIFO separately, and consider reducing its depth instead. |
| **`FITTER_EFFORT` is `AUTO FIT`, not `STANDARD FIT` (found 2026-09-23, `KB-048`)** | Auto Fit explicitly stops optimizing once it estimates "good enough" and skips optimizations that affect timing/routability, specifically to save compile time -- confirmed as Tau's actual current qsf setting. Given this project has spent B-107..B-117 chasing sub-nanosecond violations by hand, it's plausible the Fitter itself has been leaving real margin unclaimed the whole time. | Try `STANDARD FIT` on the next timing-marginal build before further manual retiming -- if it closes a gap on its own, it's a strictly better fix (applies automatically to any future marginal path too), at the cost of a build that may run 2x+ longer. Full validation plan: `KB-048`. |
| **RAM shrink trades scarcity** | Heap gap has hit its floor repeatedly as features landed. | Measure the hot set with margin and write down a floor before shrinking. Reversible only by another build. |
| **720 (Phase H) invalidates bandwidth assumptions** | Scanout goes from ~12% to 35-45% of SDRAM cycles [EST]. | Parametrise width/height/stride/base now, as Phase F already requires. |
| **Licence** | Tau's own code is **MIT**, so copyleft RTL cannot be copied in; the best references are GPLv2/GPLv3 (Minimig, PSX_MiSTer) or carry no stated licence at all (AtariST_MiSTer, Saturn_MiSTer). | Reimplement from technique; never copy from a GPL or unlicensed repo. Verify VexRiscv's licence properly — the README asserts MIT but the generated `VexRiscv_Full.v` carries no header. |

## 12. Verification and fail-safe

**Verification, following the pattern this project already uses** (the library loader, cold code and the QR
encoder were all validated against host-side references before hardware):

- **Done, 2026-09-23 (B-125).** `tools/host/blit_reference.py` (a from-scratch Python reimplementation of every
  opcode — RUN/RECT/COPY/BLIT with key+blend/BAR/SBLIT/CHAR with the real gamma-fitted anti-aliasing table,
  CHAR's glyph data parsed from the shipped `font_rom.v` rather than re-rasterised) plus `sim/tb_blit_scene.v`
  (an 11-command scene through the real `cmd_push` interface) and `sim/test_blit_reference.py` (the diff
  driver), wired into `make test-rtl` as `test-rtl-blit-reference`. All 4 existing mutation hooks
  (`BUG_IGNORE_BLIT_STRIDE`, `BUG_IGNORE_KEY`, `BUG_SBLIT_NO_SCALE`, `BUG_BLEND_ALWAYS_SRC`) confirmed caught
  by the pixel-diff, reused rather than reinvented, satisfying the injected-fault requirement below. Two real
  testbench-modelling bugs found and fixed along the way (a keyed pixel re-writes the pre-read destination,
  it doesn't skip the write; the scene needed a non-default sticky stride for `BUG_IGNORE_BLIT_STRIDE` to have
  anything to diverge on) — see `docs/AUDIT_TRAIL.md` B-125 for the full account.
- Put it in `make test-rtl`, with an injected-fault case that must be caught, matching the PSRAM/G3 mutation
  tests. **Done above.**
- The `tools/host` harness already exists and is the natural home for the reference renderer. **Done above.**

**Fail-safe — done, 2026-09-23 (B-126), but not the way this row originally assumed.** Reading the RTL to
build this found that `TAU_BLIT` doesn't actually gate the opcodes at all — `mp3_fb.sv` has no
`` `ifdef TAU_BLIT `` and `mp3_soc.v`'s own comment says the register file exists "regardless of
`BLIT_ENABLE`." There is no bitstream feature bit the way PSRAM's `PS_ID` or cold code's `IF_CFG` provides;
every bitstream from B-103 onward already has the full opcode set unconditionally. The real distinction is
"pre-B-103 bitstream" vs everything since, detected via `fw/blit_probe.inc`'s `BLIT_READY()`: exploits the
one real difference an old bitstream shows — its 2-bit opcode decode silently truncates `OP_BLIT` to
`OP_RUN` — by issuing a 2-row, custom-stride `OP_BLIT` into never-displayed framebuffer padding (columns
400-511) and checking via the CPU's uncached SDRAM window whether the second row actually got written.
Gated behind a new `TAU_BLIT_PROBE` macro (default off, byte-identical product ROM confirmed); nothing
calls `BLIT_READY()` operationally yet since no feature uses the blit engine. The "real BLIT honours a
custom stride" half is indirectly verified by B-125's own scene test; the "old 2-bit decode truncates to
RUN" half rests on B-103's documented claim, not a fresh RTL-in-the-loop test — full detail and the exact
limit of what's verified: `docs/AUDIT_TRAIL.md` B-126.

Firmware ships from the SD card independently of the bitstream, so new-firmware-on-old-bitstream is a real
configuration that has already bitten this project once (the E18 cold-code refusal, which behaved correctly).

**Split:** the blit engine, the spectrum bank and the M10K work are all **shipping** features. Only the
busy-cycle counter's readout and any new Check tests are Diagnostic-Build surface.

### 12.1 The blit-storm Check test — the L0 regression net

The zero-late-underrun record is this project's strongest quality signal, and it has so far been *observed*
rather than *defended*. A new SDRAM master is exactly what threatens it, so it needs a test, not a habit.

**Done, 2026-09-23 (B-127), with a scope correction from this row's original wording.** New `CT_BLT` in
`fw/suite.inc`: full-height `OP_BLIT` commands into framebuffer columns 400-511 (the never-displayed strip
B-126's `BLIT_READY()` probe already proved safe), re-issued the instant the draw engine goes idle — a
non-blocking poll rather than `fb_wait()`, so the test cannot itself manufacture an underrun by blocking the
main loop. **Added to STANDARD, FULL and ENDURANCE**, as specified. Verdict is late underruns only (same rule
as CT_R1-3); the SDRAM busy permille over the window rides along as the test's reported value — the first real
consumer of the B7 counter, gated behind a new firmware macro `TAU_SDRAM_BUSY` (default off, matching whichever
bitstream is actually installed; reports N/A rather than a false 0% when the counter isn't wired). **Scope
actually shipped is narrower than "full-screen scaled and blended":** this is B1 (generalised blit) load only,
across 112 of the 512-word stride (the safe off-screen strip, not the full 400-column display width), and does
not exercise B4 (scaled) or B5 (blended) traffic — blend is shelved pending its own timing fix (section 11) and
a scaled-blit firmware helper doesn't exist yet. Widen this test once either lands. **Deliberately kept as one
fixed test in all three profiles, not scaled like `CT_R1`-`CT_R3`'s three intensity levels or `CT_SOAK`'s
level/duration options** (owner question, 2026-09-23, `AskUserQuestion`: decided "not now — decide after the
first hardware run," rather than guess at intensity levels with no real busy-percentage number yet to reason
from). `make test-host` passes, the release (`player`) ROM is confirmed byte-identical (CT_BLT is entirely
`#if CHK_DEV`).

**Hardware result: PASS, 2026-09-23 (B-146) — MILESTONE.** After B-134's timing closure and a run of card
bugs unrelated to the engine itself (B-136, B-141, B-142, B-143), `TAU_0_5_0_A_4` ran STANDARD and FULL twice
each independently: **`Blit storm (30 s)` PASS both times, SDRAM 15.8% busy over the window (`busy_permille`
158), audio confirmed continuous the entire 30 s (B-139's `audio_full` flag true both runs) — zero late
underruns.** This is not ambiguous the way B-138 was: the audio-continuity check exists specifically to rule
out "passed because nothing was actually contending," and it reads clean. The predicted-busy-percentage
convention this row asked for was never actually recorded before the first run (a process gap, not backfilled
retroactively now that the real number is known) — 15.8% is the first real number to reason from for any
future scaling decision. Real margin remains before the SDRAM port would be a concern. `Track changes (10)`
failed in both runs, same as B-138, still unexplained and not investigated. **This closes the loop section 12
always intended: correct in simulation (B-125), timing-closed on real hardware (B-134), and now load-tested
on real hardware with real audio (this result) — the blit engine's foundation is proven, not just built.**

## 13. Parked — revisit after this phase

Kept deliberately, with reasoning, so none of it has to be re-invented. None of these are commitments.

**2D engine ideas (the wild list, preserved):**

- **Cover-art crossfade** — dissolve old into new instead of sliding, once B5 exists.
- **Theme swap via palette rewrite** — with a CLUT (B8), a dark mode or per-track accent recolour is ~256 word
  writes instead of a full UI redraw.
- **Screen-transition dissolve** — render the next screen off-screen, hardware-ramp alpha over a few frames for
  a genuine cross-dissolve between library/settings/playlist, at near-zero CPU.
- **Autonomous multi-frame animate descriptor** — hand the engine a start state, end state and frame count once
  and let it run unattended; also drives idle-screen animation without repeated CPU wakeups (a battery angle).
- **Icon/badge overlay layer** — generalising the openFPGA template's hardware-cursor pattern (the one real
  compositing primitive in Analogue's own example corpus) to play/pause/shuffle badges that composite without a
  row redraw.
- **Independent multi-row tickers** — per-row scroll offsets once B3 exists, for a persistent artist/album
  ticker.
- **Overlay compositing layer** via a second line buffer, Neo Geo LSPC ping-pong style (~2 blocks).
- **Double buffering** — ~288 KB in SDRAM plus a vsync pointer swap; simpler and safer than single-buffer beam
  fencing, but only worth it once drawing is complex enough to tear visibly.
- **Bilinear cover scaling** — free of line-buffer cost for the same reason B4 is; four reads per output pixel.
- **Neo Geo-style zoom lookup table** as an alternative to Bresenham if table-driven scaling ever fits better.

**Type/font system, broader than today's fixed ASCII-only M10K atlas:**

- **Broader glyph coverage (Latin diacritics through CJK).** Upstream HarpMudd v1.5.0 shipped and hardware-
  verified this already: `tools/gen_font_ext.py` builds one SD-card-loaded binary (4bpp Inter for Latin-1/Ext-A/
  Greek/Cyrillic, matching the ROM's own AA style; 1bpp Unifont-JP for kana + the full CJK Unified Ideographs
  block — a bitmap face reads sharper than a downscaled outline font at 16 px, their reasoning, not assumed
  here), read through the existing `CHAR` opcode via a glyph-index sentinel into the same row registers the ROM
  path already fills. Comes with full UTF-8 string-pipeline hardening (ID3 UTF-16, FLAC tag boundaries,
  filenames, `.m3u` BOM) that would also close this project's own open BUG-001 (accented filenames skipped).
  **The one deliberate deviation from their design, not a copy:** they stream glyph rows live from SDRAM; this
  project would want the asset in **PSRAM** instead, through the already-proven data window, to avoid the exact
  SDRAM-contention risk the rest of Phase F exists to protect. Their timing note — compose "tipped to -1.888 ns,
  fixed by splitting into two registered stages, now +2.093 ns" — is the same upstream cliff `PHASE_F_SPEC.md`'s
  own risk section (11) already cites; the fix (an extra pipeline stage) is already proven upstream if this is
  ever picked up.
- **Crispness at scale.** Today's 4bpp coverage atlas is baked at one fixed cell size (16x16) and read pixel-
  for-pixel — there is no scaling path for text today. Once B4 (scaled blit) exists, the same nearest-neighbour
  approach used for cover art would work for text too, but coverage-based AA that looks right at 1x can look
  wrong scaled up (blocky edges) or down (lost fine strokes, especially CJK stroke detail at 1bpp) — worth a
  real look rather than assuming it transfers, particularly for any eventual UI scale setting.
- **Multiple font support.** Today there is exactly one typeface (Inter SemiBold) baked into one ROM. A second
  face (a monospace variant for tabular/diagnostic screens, or a CJK-appropriate face distinct from Latin) is
  architecturally a second atlas plus a font-select bit somewhere in the glyph index — cheap in concept once any
  atlas is PSRAM-resident (SD-card assets are easy to add to), but each additional face multiplies the storage
  and glyph-generation-tooling surface, and font mixing/fallback rules (which face wins for a given code point)
  need an actual policy, not just "whichever loads."

**Beyond the 2D engine:**

- **True FFT via R2SDF + CORDIC twiddles**, if linear frequency bins are ever genuinely wanted (section 7).
- **Tile-based 3D**, revisited honestly after the 192 KB shrink frees 64 blocks (section 8).
- **JPEG hardware acceleration** — the dominant user-visible cost today is decode time (multiple seconds for a
  fresh large cover), not draw time. A much larger project, parallel to the parked MP3 acceleration idea, but
  it is the item with the biggest perceived-performance payoff if load time matters more than draw effects.
- **MP3 synthesis filterbank / IMDCT kernels** — only if the Phase D profile justifies them (D3). The V[] buffer
  alone is ~8 blocks and must be on-chip.
- **PSRAM-vs-BRAM album-art comparison** — parked since B-028, waiting on this engine.
- **`quartus_fit_summary.py` extension** to emit the per-instance memory table automatically, so section 1
  regenerates per build instead of being hand-derived.

## 14. Recommended order — and what to pick up next

Steps 3 and 4 are strictly ordered; the rest have some freedom.

| # | Step | Needs a Quartus slot? | Gated on |
|---|---|---|---|
| **1** | **Profile the software decoder** | No | **Done — B-086..B-098, on hardware** |
| **2** | Decide the MMIO descriptor model in RTL terms (section 9) | No | **Done — B-085** |
| **3** | Blit engine Tier 1/2 (opcodes + MMIO register file) | Yes | 2 — met; **Tier 1 RTL/sim complete, verification/fail-safe/Check-test work all done (B-125/B-126/B-127, section 12/12.1 closed). B-130's caveat RESOLVED by B-134: the real full-G3-macros + blit-engine fit closed cleanly on both seeds (seed 1: setup +0.634/+0.501 ns, hold +0.322/+0.305 ns; seed 2: +0.395/+0.310/+0.311/+0.302 ns), same 298/308 RAM and 11/66 DSP as the blit-only fits — B-111/B-114's retiming survives the real product configuration with no further RTL change. Packaged as 0.5.0-alpha.1 (B-131's semver convention), not yet installed.** Real multi-seed fit failed timing (B-107/B-109), bisect (drop `TAU_BLIT_BLEND`, B-110) recovered nearly all of it, both exposed retiming bugs fixed (B-111 BAR, B-114 SBLIT/CHAR), the blend/`glyphbuf` theory independently verified as congestion relief not a direct fix (B-116); the re-fit combining all three fixes closed cleanly on every corner (B-117 final, seed 2: Slow 85C +0.727 ns, Slow 0C +0.597 ns). **B-130 found that every one of B-100 through B-117's fits, including this one, was built with ONLY `TAU_MLAB_MIGRATE`/`TAU_FONT_REPACK`/`TAU_SDRAM_BUSY`/`TAU_BLIT` on top of the bare `USE_SDRAM=1` base — none of them ever included the shipped product's own `TAU_PHASE2_WINDOW`/`TAU_PSRAM_PROBE`/`TAU_PSRAM_WINDOW`/`TAU_PSRAM_IFETCH` macros.** Packaging B-117's RBF with real product firmware (`player-library-diagnostic-profile`) made this concrete: on hardware, TAU DEV 43 loaded nothing and had no menu access at all, because that firmware needs the window/PSRAM paths this bitstream never had, and the "no BRAM fallback" playlist path (since A-105) has nothing to fall back to. **The recorded timing margin is not proven to survive combining with the full product configuration** — a genuinely new, not-yet-run "full G3 macros + blit engine" fit is required before this bitstream family can be trusted as a product candidate. Software reference renderer (B-125), `BLIT_READY()` fail-safe (B-126) and the blit-storm Check test + busy-counter consumer (B-127) are all built and host-verified, and remain correct RTL/firmware — only the *bitstream pairing* was wrong. B3 analysed, needs firmware coordination, not RTL-only. |
| **3a** | MLAB migration (`glyphbuf` + dcfifo) + font repack + busy-cycle counter, scoped out from 3 as everything not needing the blit opcodes | **Done — B-101/B-102, real multi-seed fit, both seeds Successful** | none |
| 4 | Meters to cold code | No (firmware) | 3 |
| 5 | Main RAM 256 -> 192 KB | Yes | 4, and the peak-usage gate in section 4.1 |
| 6 | Spectrum filter bank in RTL (section 7) | Yes — can ride a later build | Nothing; cheap in blocks |
| 7 | Audio kernels, smallest first (FLAC bit reader) | Yes | **1** — done, unblocked, but stays ordered after 3-5 (owner decision, 2026-09-22) |

### Timing-experiment backlog (added 2026-09-23, B-118..B-120)

Zero-RTL-change experiments to try before any further manual retiming, cheapest/least-disruptive first. None
of these are tried yet; each has a KB entry with a concrete validation plan.

1. **`FITTER_EFFORT` to `STANDARD FIT`** (`KB-048`) — `ap_core.qsf` currently reads `AUTO FIT`, which
   Intel's own docs say explicitly stops optimizing once "good enough" and skips timing-affecting
   optimizations to save compile time. Try this on the next timing-marginal build (e.g. re-run B-117's
   no-blend + B-111 + B-114 combination) before assuming more manual retiming is needed — if it closes a gap
   on its own, it's a strictly better fix, since it applies automatically to any future marginal path too.
   Real cost: builds may run 2x+ longer (already 50 min-1h45m today).
2. **Per-instance `DSP_BLOCK_BALANCING`** (`KB-045`) — only relevant if/when `TAU_BLIT_BLEND` is revisited.
   Forces the specific `Add32~8` adder off DSP-block mapping without touching the real blend/EQ multiplies,
   targeting the exact placement collision B-116 confirmed. Cheaper than the RTL-restructure fallback also
   listed there.
3. **Quartus Rapid Recompile** (`KB-046`) — a workflow/iteration-speed item, not a timing-closure fix; see
   the roadmap's Tooling track item 5. Worth enabling once build-iteration count becomes the bottleneck again
   rather than timing itself.
4. **M10K native-width check** (`KB-047`) — not a Phase F item, belongs to the still-unexecuted Phase G RAM
   shrink; listed here only so it isn't missed when that work starts (also cross-referenced in the Phase G
   section of `docs/ARCHITECTURE_ROADMAP.md`).

### Item 1 result (for the record)

Measured on hardware, real content (the permanent "Audio Test Suite" test album, source outside the repo,
synced via `tools/sync_media.py`): **MP3 is filterbank-dominated (IMDCT+Subband, not Huffman)**, matching the
roadmap's assumed ordering. **FLAC's bit-reader share measured 7-15%**, not the 64-76% `docs/FLAC.md` reported
on different content — that gap is open, not resolved. A real cross-format measurement bug (FLAC accumulators
leaking into the MP3 reading shown right after a FLAC track) was found and fixed (B-097) before trusting the
numbers. Full detail: `docs/ARCHITECTURE_ROADMAP.md` section 2, `docs/AUDIT_TRAIL.md` B-086..B-098.

### Item 3, step 1 result (for the record)

**B-100, 2026-09-22.** Ran the synthesis-only pre-check (`quartus_map`, no fit) with `TAU_MLAB_MIGRATE` +
`TAU_FONT_REPACK`, compared against a same-tree baseline with both off. **MLAB migration confirmed and
de-risked** — `glyphbuf` and the `sound_i2s` dcfifo both resolved to `MLAB` in the RAM Summary table (0 -> 2,176
MLAB bits, an exact 1:1 move out of the M10K-bit pool); only 2 of the 4 candidates from section 2 are in scope
(`cmd_mem` and the VexRiscv regfile deliberately excluded, per their own risk/awkwardness notes there).
**Font ROM repack's actual win is still unconfirmed** — synthesis reports declared content bits, which are
identical before and after by construction (same content, different lanes), so the real question (does the
fitter pack four 8-bit ROMs into fewer physical M10K blocks than one 32-bit ROM) needs step 2's fit. This is a
correction to section 10's original framing of what synthesis-only would prove — see that section for detail.
Evidence: `docs/AUDIT_TRAIL.md` B-100.

### Item 3a result: the real fit, scoped to everything except the blit opcodes (for the record)

**B-101/B-102, 2026-09-22.** Owner scoped step 2 down to "everything except the blit engine" (asked after being
told the spec's literal step 2 bundles blit RTL that does not exist yet). Added B7 (the SDRAM busy-cycle
counter, section 5) since it has no RTL dependency on the opcodes, then ran a real multi-seed fit (not
synthesis-only) of `TAU_MLAB_MIGRATE` + `TAU_FONT_REPACK` + `TAU_SDRAM_BUSY` together.

**Font repack's real answer, settled: +0 blocks, not +4.** Both seeds fit to identical **298/308 RAM blocks**
(baseline 300/308 — a net +2, all of it the two MLAB items). Corrected in section 4's ledger. On-chip repacking
is not a usable lever for the font ROM; PSRAM is now the only real path to those blocks if they are ever needed.

**Timing: real difference between seeds, and it matters.** Seed 1 closed with a genuine (if tiny) violation —
**setup slack -0.001 ns / -0.101 ns** on the two slow-silicon corners, traced to one exact path: `mp3_fb.sv`'s
`Mux3~4` (the px_color arithmetic feeding `glyphbuf`'s write-data port) into `glyphbuf`'s newly-MLAB-mapped
write port. Seed 2 closed **positive on all four corners** (setup +0.091/+0.005/+5.552/+5.790 ns; hold
+0.315/+0.301/+0.138/+0.127 ns) — same RTL, same macros, different placement. **Seed 2 is the build to carry
forward** (RBF sha256 `a0942341...1a465`); seed 1's result is recorded because it is a real, reproducible
finding (not just a bad seed to discard and forget) — the glyphbuf MLAB write path has effectively zero margin
in the worst corner, so any future change that adds even a few picoseconds there (routing shift, a nearby logic
change) could reopen it. Worth a note if `glyphbuf` or its feeding arithmetic changes again.

**Process note, also worth keeping:** the first seed-2 attempt showed 3 errors from a corrupted run — two
`make fpga` invocations had raced on the same project directory (traced via impossible log timestamp ordering,
Assembler starting before the Fitter that must precede it), an artifact of a launch-script mistake, not an RTL
problem. Redone cleanly with a verified single process before trusting the result.

### Item 3 progress: MMIO descriptor register file + B1 (generalised blit), simulation-verified (for the record)

**B-103, 2026-09-22.** Started item 3 itself. Built, in order:

- **The MMIO descriptor register file (section 9), as designed there** — `R_BLT_IDX`/`R_BLT_DATA` (0xC0/0xC4) in
  `mp3_soc.v`, four sticky fields (SRC_BASE, SRC_STRIDE, DST_BASE, DST_STRIDE), index auto-increments on each
  DATA write. **One deliberate deviation from the spec's literal "three registers":** no new `R_BLT_GO` — the
  existing `R_FB_GO`/`fb_cmd_op` per-command path (already proven, already tested) was widened from 2 to 3 bits
  instead, using a bit that was already unused padding in `R_FB_GO`'s word layout, and the new opcode rides that.
  Reusing proven infrastructure over adding a parallel one, not a spec violation without reason.
- **`OP_BLIT` (B1), the first Tier 1 opcode** — a genuine generalisation of `OP_COPY`, not a parallel code path:
  same row-at-a-time streaming-write datapath (`A_COPYRD`/`A_WRWAIT`/`glyphbuf`), but destination and source are
  each `sticky_base + flat_offset`, stepping by the sticky `STRIDE` per row instead of `OP_COPY`'s fixed
  `FB_BASE`=0/512. No multiplier needed — the per-row step was already a plain add; swapping a register in for a
  constant cost nothing extra. Both addresses are full 25-bit SDRAM addresses (not the 19-bit FB_BASE-relative
  window `OP_COPY`/`OP_RECT`/`OP_CHAR` stay confined to), so a blit can reach anywhere in SDRAM.
  **Deliberately not fixed here, a separate follow-up:** `OP_COPY`'s existing row-buffer width limit
  (`glyphbuf` is 128 entries, so widths above 127 silently truncate) — carries over unchanged to `OP_BLIT`
  because fixing it is an M10K/MLAB cost decision (a wider row buffer), not an addressing one, and bundling it
  in would have obscured which change caused what.
- **Fail-safe, for free rather than built:** `q_op` is 3 bits now but old RTL only ever reads 2 (`R_FB_GO`'s
  `dDAT_MOSI[1:0]`), so new firmware sending `OP_BLIT` (value 4) to an old bitstream is truncated to 0 (`OP_RUN`)
  before it even reaches the FIFO — the existing "unknown opcode degrades to a RUN" behaviour, not a hang. A
  full `COLD_READY()`-style feature-bit check (section 12) is still worth doing once more opcodes exist to gate,
  not for one opcode alone.

**Verification, following section 12's pattern:** extended `sim/tb_mp3_fb.v` rather than writing a parallel
testbench, since `OP_BLIT` extends `OP_COPY`'s own machinery. Two properties checked: (1) **equivalence** — with
the sticky registers left at their power-up defaults (base 0, stride 512), `OP_BLIT` reproduces `OP_COPY`'s
existing passing test byte-for-byte; (2) **independence** — with `SRC_BASE=0x8000/STRIDE=64`,
`DST_BASE=0x9000/STRIDE=96` (neither matching `FB_BASE`=0/512), every address and every per-row step matches
hand-computed expected values, not the old hardcoded ones. A `BUG_IGNORE_BLIT_STRIDE` mutation parameter
(reverting the stride step to a fixed 512, reproducing the exact bug this feature exists to prevent) is
confirmed caught — `make test-rtl-fb-mutation`, a real functional mutation test, not the kind of physical-timing
hazard B-101's CDC counter found it could not meaningfully mutation-test. `make rtl-lint`, `make test-host` and
`make test-rtl` (now including this) all pass, 0 failures; `mp3_soc_sim.v` regenerated correctly and the
unrelated PSRAM testbenches confirmed unaffected.

**Not done, at B-103:** `TAU_BLIT_BLEND` and B2-B6 — B1 alone was scoped as a real, complete, verified foundation
rather than shallow progress across all six. No Quartus slot spent yet; RTL/simulation only.

### B2 (colour key) and B6 (meter column), simulation-verified (for the record)

**B-104, 2026-09-22.** Both built on B1's `OP_BLIT`/`R_BLT_IDX`/`R_BLT_DATA` foundation.

- **B6, `OP_BAR`** (section 5: "a bar is `(x, base_y, height, lit, unlit)`") is two chained `RECT` fills, not a
  new burst mechanism — the existing `rect_active`/`A_WRWAIT` row loop runs twice per command, the second
  segment queued (`bar2_pending`/`bar2_addr`/`bar2_rows`/`bar2_fg`) and re-armed the moment the first segment's
  last row retires. `cmd_addr` is the span's top-left (the convention every other opcode already uses);
  `cmd_glyph` (otherwise unused outside `CHAR`) carries the lit-row count, clamped to the span height; colours
  reuse `cmd_fg`/`cmd_bg`, the same "no other use for these fields" reasoning `OP_COPY` already established for
  its source address. **One convention decided here, since nothing upstream pinned it down:** lit rows are the
  *bottom* of the span (the usual meter-fills-from-the-floor reading of "base_y"), unlit rows the top —
  documented in the code, not just assumed.
- **B2, colour-key transparency**, needed more than "one comparator" to be *correct*: showing the destination
  through a keyed source pixel means the destination has to be read at all, which `OP_BLIT`/`OP_COPY` never did
  before (write-only). Added a genuine destination pre-read phase (`A_KEYDST`, structurally identical to the
  existing `A_COPYRD` read-into-`glyphbuf` loop) that runs before the source read whenever `blit_mode &&
  blt_key_en`; the source read (`A_COPYRD`, one line changed) then simply *skips* writing into `glyphbuf` for any
  word equal to the sticky `KEY` colour, leaving the pre-read destination pixel already sitting there — no
  separate per-pixel select/blend stage needed. `OP_COPY` is untouched and never keys, matching B1's own
  precedent of leaving `COPY` as the simple case. New sticky field 5 (`R_BLT_IDX`=4: bit16=enable,
  bits[15:0]=colour); `blt_idx` widened 2->3 bits, wraps 4->0 instead of counting to 5, so a burst of exactly 5
  `R_BLT_DATA` writes loads the whole state.

**Verification:** both extend `sim/tb_mp3_fb.v`. BAR: three cases (a split bar, a lit-clamped-to-height fully-lit
bar with no phantom second phase, a fully-unlit bar with no phase 2 firing at all) checking row count, exact
per-segment addresses/stride and exact colours. B2: a keyed blit where one of four words in row 0 matches KEY
(confirmed it keeps the destination's own pre-read value, not the source's) while the other three and all of
row 1 take the source normally (confirming the key does not universally suppress writes), plus the same
command with keying disabled (confirms the previously-keyed word reverts to plain source, i.e. `A_KEYDST` never
even ran). Two new mutation parameters, both confirmed caught by `make test-rtl-fb-mutation`:
`BUG_IGNORE_BLIT_STRIDE` (from B-103, still passing) and new `BUG_IGNORE_KEY` (disables the colour-key compare
entirely — the keyed-word check fails as expected). `make rtl-lint`, `make test-host` and `make test-rtl` all
pass, 0 failures.

**Not done, at B-104:** `TAU_BLIT_BLEND` and B3-B5 — still RTL/simulation only, no Quartus slot spent.

### B3 (analysed, not built as RTL-only) and B4 (scaled blit, `OP_SBLIT`), simulation-verified (for the record)

**B-105, 2026-09-22.** Owner said "continue with B3 and B4." B3 turned out to be a real scoping finding, not a
straightforward build; B4 is a complete, verified new opcode.

**B3: the literal mechanism does not translate to this architecture, and the real gap needs firmware
coordination first — not built as RTL-only.** Section 5 describes an Amiga/Atari ST mechanism: a barrel shifter
plus first/last-*word* masks, built for a format that packs many 1bpp pixels per word. This engine is one pixel
per SDRAM word — there is no sub-word packing here for a shifter or a mask to act on, so the literal mechanism
has nothing to translate to. What it would *buy* — an arbitrary source column offset, and an output width
independent of the source rectangle — `OP_BLIT` already has, for free, from B1's own generalised addressing; no
new hardware needed for blits. The one genuine gap is CHAR-specific: the marquee's own comment ("does not clip
one partially off the left edge") is about sub-*glyph* clipping, which is real and would fix the marquee's
whole-character scroll. **Checked rather than assumed that retrofitting it is safe, and found it is not:**
`fw/player.c`'s `fb_char()` never writes `R_FB_SIZE` (`cmd_w`/`cmd_h`) at all — those registers hold whatever an
earlier, unrelated `fb_rect()`/`fb_copy_span()` call left in them by the time a `CHAR` command is pushed.
Repurposing `cmd_w`/`cmd_h` as CHAR clip fields, as originally considered, would silently feed garbage leftover
RECT/COPY dimensions into every existing glyph draw. This needs a firmware change (dedicated clip fields
`fb_char()` actually sets) before it is safe, which is real coordination work outside an RTL-only delivery's
scope — recorded here so it is not re-attempted the same way, not because it is unimportant.

**B4 (`OP_SBLIT`, scaled blit, nearest):** built as a genuinely new opcode, so none of B3's compatibility risk
applies — no existing caller to break. Reuses CHAR's own Bresenham registers directly
(`char_num`/`char_den`/`acc_x`, `char_numy`/`char_deny`/`acc_y`) rather than duplicating them, since CHAR and a
blit are never in flight at the same time; the ratios come from the same `nd_x`/`nd_y` wires CHAR's own dispatch
already computes. The one real generalisation: `sblit_ext()` computes the output extent from a *variable* source
width/height (`cmd_w`/`cmd_h` — safe here, brand new opcode) and the same four scale factors, instead of CHAR's
fixed-16px-cell lookup, using only a small multiply-by-constant (x3, for 1.5x/3x) plus a shift, not a general
divider. Matches section 5's own "no line buffer needed... one read per output pixel" finding directly: every
output pixel issues its own single-word SDRAM read at the Bresenham-selected source column, returning through
`A_IDLE` between pixels exactly like every other transaction in this engine (new state `A_SBLIT`) — scanout can
preempt between *any* two words, not just between rows, which the module's own header comment establishes as
the whole point of routing everything through one dispatch point. The destination still advances by the sticky
`DST_STRIDE` every output row, reusing B1's `blit_dst_addr`/`blit_mode` unchanged; only the *source* row
advances, and only when the Y-Bresenham condition says to (a new conditional step in `A_WRWAIT`, `sblit_mode`
selecting it over `OP_BLIT`'s own unconditional per-row stride step). Same 128-entry `glyphbuf`/127-word output
limit as `OP_COPY`/`OP_BLIT`, same reasoning, same "not silently widened here either" note.

**Verification:** two cases in `sim/tb_mp3_fb.v`. 1x (no scaling): confirms the per-pixel read path agrees with
the row-burst path pixel-for-pixel for the trivial case, including the source row correctly stepping by the
sticky stride every output row. 2x: a 2x1 source region doubles to 4x2 output — hand-computed expected values
(every source pixel repeats twice per axis) matched exactly on the first run, a good sign the Bresenham reuse
is genuinely correct rather than coincidentally close. New mutation parameter `BUG_SBLIT_NO_SCALE` (forces the
X step to fire every pixel regardless of the accumulator, i.e. silently drops back to an unscaled 1:1 copy) is
confirmed caught — the 2x test's doubling checks fail exactly as expected. `make rtl-lint`, `make test-host` and
`make test-rtl` (now 3 mutation cases for this file) all pass, 0 failures.

**Not done, at B-105:** `TAU_BLIT_BLEND`/B5, and the CHAR sub-glyph clipping half of B3 (needs the firmware
coordination described above). No Quartus slot spent yet.

### B5 (alpha blend), behind its own `TAU_BLIT_BLEND` macro, simulation-verified (for the record)

**B-106, 2026-09-22.** Owner said "continue with B5" — the last Tier 1 item, and per section 10's build plan the
one that actually needs care: it is the deepest new pipeline and carries the documented -1.888 ns timing-cliff
risk, so it is kept droppable on its own, separate from B1/B2/B4/B6.

**A real gap caught before it shipped: the macro wasn't wired to anything.** Built the blend datapath first,
then went to add the separate `TAU_BLIT_BLEND` macro the build plan requires — and found `mp3_fb`'s existing
instantiation in `core_game.vh` passes **no module parameters at all**. Every earlier mutation-test parameter in
that module (`BUG_IGNORE_BLIT_STRIDE`, `BUG_IGNORE_KEY`, `BUG_SBLIT_NO_SCALE`) was therefore always silently at
its default regardless of any macro, which was fine for THOSE (test-only, default-off is correct), but for a
real feature parameter like `BLIT_BLEND_ENABLE`, that same silence would have meant the whole feature was
unreachable even with `TAU_BLIT_BLEND` defined. Fixed before it became a real bug: added
`mp3_fb #(.BLIT_BLEND_ENABLE(...))  u_fb (` and the `TAU_BLIT_BLEND` -> `TAU_BLIT_BLEND_EN` derivation
(requires `TAU_BLIT`, matching the existing dependency-check convention). Caught by building the feature
end-to-end and checking the wiring, not by a test that happened to exercise it — worth naming as a real finding
about how easy it is for a "just add a parameter" step to be silently inert.

**The blend itself:** new sticky field 6 (`R_BLT_IDX`=5: enable, 3-bit mode, 8-bit alpha — DSP 0-255 alpha or
one of the four PSX shift-add ratios section 5 lists: B/2+F/2, B+F, B-F, B+F/4, all clamped not wrapped). Shares
B2's destination pre-read phase (`A_KEYDST`) rather than adding a second one — its trigger condition generalised
from "`blt_key_en`" to "`blt_key_en` OR `blend_active`". **Found and fixed a real latent bug while generalising
that condition, not after:** the existing key-match check (`key_dst_done && (p0_q == blt_key)`) relied on
`key_dst_done` implying `blt_key_en` was on, which was true before this change (nothing else could trigger the
pre-read) and stopped being true the moment blend could trigger it too — a blend-only blit could have
accidentally treated a source pixel equal to a stale/leftover `blt_key` register value as keyed, with keying
never actually enabled. Fixed by adding an explicit `blt_key_en` check (new `pixel_keyed` wire) rather than
relying on the old implication. Key takes priority over blend where both apply: a keyed pixel is fully
transparent, so blending it would be wrong, not merely redundant. One blend function (`blend_ch`, parameterised
by the channel's own max value) serves R/G/B alike rather than three near-copies; DSP mode approximates `/255`
as `>>8` (weight 256), the same pragmatic trade CHAR's own `cov_weight` already makes and documents.

**Verification:** two cases in `sim/tb_mp3_fb.v`. DSP mode at alpha=128 reduces exactly to a per-channel average
(128/256 = 0.5, no rounding surprise) — hand-computed R/G/B values matched on the first run. PSX mode 2 (B+F)
deliberately chosen to overflow the 5-bit R channel (20+20=40) to confirm clamping, not wrapping. New mutation
parameter `BUG_BLEND_ALWAYS_SRC` (blend silently does nothing, always writes the source pixel) confirmed caught.
`make rtl-lint`, `make test-host` and `make test-rtl` (4 mutation cases for this file now) all pass, 0 failures.
**Tier 1 is functionally complete in RTL/simulation as of this entry** — B1/B2/B4/B6 built, B3 analysed and
correctly scoped out, B5 built behind its own separable macro. No Quartus slot spent on any of it yet.

### Next item, in enough detail to start cold

**Step 2 of section 10's build plan: the first real multi-seed fit of the blit engine.** Bundle `TAU_BLIT`
(+`TAU_BLIT_BLEND`) with the counter (B7, already fitted and proven in B-102) and re-verify MLAB/font-repack
still hold (they should — this build doesn't touch them, but section 10 bundles everything together precisely
because it is the first real spend of a Quartus slot on this new RTL). Watch specifically for the documented
-1.888 ns risk; if timing fails, the first bisect is dropping `TAU_BLIT_BLEND` per section 10, keeping the rest.
Software reference renderer + pixel-diff fixtures (section 12) and the `COLD_READY()`-style feature-bit fail-safe
are still open items ahead of any card install — this session's RTL testbenches cover functional correctness
per-opcode, not yet the full render-a-scene-and-diff-the-buffer pattern section 12 describes. See sections 3-6
and 9-13 for the rest of the plan.
