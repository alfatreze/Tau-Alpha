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
| **D3** | **Audio kernels stay gated on a profile that has never been run.** | Phase D step 1 says "profile first, and if it's fast enough, stop here" — it has never happened. The one real measurement contradicts the roadmap's own ordering: `FLAC.md` reports the **bit reader at 64-76%** of FLAC decode time [HW], not the IMDCT or synthesis filterbank the roadmap names first. A bit reader is a barrel shifter and a table, not a DSP kernel, and costs ~1-2 blocks instead of ~8-12. |
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

| Step | Frees | Running free | Risk |
|---|---|---|---|
| Today | — | **8** | — |
| MLAB migration (section 2) | +7 | 15 | very low |
| Font ROM repack (section 3) | +4 | 19 | very low |
| *Blit engine consumes (CLUT + working)* | −2 | 17 | — |
| *Spectrum filter bank consumes* | ~0 | 17 | — |
| *FLAC bit-reader accelerator, if the profile justifies it* | −2 | 15 | — |
| Font ROM to PSRAM (only if needed) | +12 | 27 | medium |
| **Main RAM 256 KB -> 192 KB** | **+64** | **~91** | see below |

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

**Step 1 — synthesis only, no fit (~5 min, not ~45).** Run `TAU_MLAB_MIGRATE` + `TAU_FONT_REPACK` through
synthesis alone and read the RAM summary. Both are functionally inert, so the **block-count drop is the entire
result** — it confirms the expected ~11 blocks (7 + 4) before a real Quartus slot is spent, and needs no
hardware. This is the pattern the SignalTap proof build already established.

**Step 2 — one full build, multi-seed**, with everything bundled (the counter is needed to validate the engine,
so they belong together):

1. Blit engine Tier 1 (+ Tier 2 if it fits)
2. MLAB migration
3. Font ROM repack
4. SDRAM busy-cycle counter

**If timing fails:** first bisect is dropping `TAU_BLIT_BLEND`. Keep the inert items on — they change no
behaviour, so any trouble they cause is a packing/routing effect that the multi-seed convention should absorb.

Compare the product-config build against the shipped RBF as B-018 did, noting B-021's finding that shared-RTL
changes make bit-identity unattainable even with macros off — it is a review aid, not a gate.

Seeds: follow the existing rule (multi-seed, pick by the pre-set criterion). Compare the product-config build
against the shipped RBF as B-018 did — noting that B-021 already found shared-RTL changes make bit-identity
unattainable even with macros off, so the comparison is a review aid, not a gate.

## 11. Risks

| Risk | Why it is real here | Mitigation |
|---|---|---|
| **Timing cliff** | The roadmap records an upstream **-1.888 ns** cliff from adding a pipeline path at 100 MHz. Alpha blend and scaling both add pipeline depth. | Multi-seed; keep the blend/scale stages behind their own macro so they can be dropped without losing the build. |
| **The L0 invariant** | Every stress run to date reports **zero late underruns**. It is the strongest quality signal this project has, and a new SDRAM master is exactly what threatens it. | The busy-cycle counter (B7) is the instrument; the blit-storm Check test (section 12.1) is the regression net. |
| **MLAB Fmax on deep chains** | The 256-deep command FIFO needs ~8-deep MLAB chaining; the penalty is undocumented [EST]. | Do the three easy migrations first; treat the FIFO separately, and consider reducing its depth instead. |
| **RAM shrink trades scarcity** | Heap gap has hit its floor repeatedly as features landed. | Measure the hot set with margin and write down a floor before shrinking. Reversible only by another build. |
| **720 (Phase H) invalidates bandwidth assumptions** | Scanout goes from ~12% to 35-45% of SDRAM cycles [EST]. | Parametrise width/height/stride/base now, as Phase F already requires. |
| **Licence** | Tau's own code is **MIT**, so copyleft RTL cannot be copied in; the best references are GPLv2/GPLv3 (Minimig, PSX_MiSTer) or carry no stated licence at all (AtariST_MiSTer, Saturn_MiSTer). | Reimplement from technique; never copy from a GPL or unlicensed repo. Verify VexRiscv's licence properly — the README asserts MIT but the generated `VexRiscv_Full.v` carries no header. |

## 12. Verification and fail-safe

**Verification, following the pattern this project already uses** (the library loader, cold code and the QR
encoder were all validated against host-side references before hardware):

- A **software reference renderer** implementing each opcode, plus **pixel-diff fixtures** — render the same
  scene through the reference and through the RTL simulation and compare buffers exactly.
- Put it in `make test-rtl`, with an injected-fault case that must be caught, matching the PSRAM/G3 mutation
  tests.
- The `tools/host` harness already exists and is the natural home for the reference renderer.

**Fail-safe**, following `COLD_READY()`: firmware must detect that the bitstream lacks the new opcodes —
feature bit plus a probe call — and degrade to the existing RUN/RECT/CHAR/COPY paths rather than hanging.
Firmware ships from the SD card independently of the bitstream, so new-firmware-on-old-bitstream is a real
configuration that has already bitten this project once (the E18 cold-code refusal, which behaved correctly).

**Split:** the blit engine, the spectrum bank and the M10K work are all **shipping** features. Only the
busy-cycle counter's readout and any new Check tests are Diagnostic-Build surface.

### 12.1 The blit-storm Check test — the L0 regression net

The zero-late-underrun record is this project's strongest quality signal, and it has so far been *observed*
rather than *defended*. A new SDRAM master is exactly what threatens it, so it needs a test, not a habit.

- **New Check test:** a sustained worst-case blit load during playback — full-screen scaled *and* blended
  blits, issued back to back — counting late underruns and worst-case window access for the duration.
- **Add to STANDARD, FULL and ENDURANCE.**
- **Report the busy-cycle percentage alongside the verdict.** This is the half that matters: a line reading
  "0 late, SDRAM 38% busy" says how much margin remains; "0 late" alone only says the wall has not been hit
  yet. Publishing the busy figure is the entire reason the counter is in this build.
- **Record the predicted busy percentage before the first hardware run**, per the standing convention.

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
| **1** | **Profile the software decoder** | No | Nothing — **this is the next item** |
| 2 | Decide the MMIO descriptor model in RTL terms (section 9) | No | Nothing; do before any Phase F RTL |
| 3 | Blit engine Tier 1/2 + MLAB migration + font repack + busy-cycle counter | Yes, one (plus a ~5 min synthesis-only pre-check) | 2 |
| 4 | Meters to cold code | No (firmware) | 3 |
| 5 | Main RAM 256 -> 192 KB | Yes | 4, and the peak-usage gate in section 4.1 |
| 6 | Spectrum filter bank in RTL (section 7) | Yes — can ride a later build | Nothing; cheap in blocks |
| 7 | Audio kernels, smallest first (FLAC bit reader) | Yes | **1** — do not start without it |

### Next item, in enough detail to start cold

**Profile the software MP3/FLAC decoder** (Phase D step 1). It has never been run, it is free — no RTL, no
Quartus slot, no card write — and it gates all kernel work. The roadmap's own rule is "if it is already fast
enough, stop here", so this step may cancel step 7 outright and save a 45-minute build plus a hardware cycle.

- **What exists to build on:** `docs/FLAC.md` already reports a diagnostic split into D/O/U/R stages, finding
  **R (the bit reader) at 64-76%** of FLAC decode and rising with bitrate. That is the model to follow — and
  there is **no equivalent MP3 profile at all**, which is the real gap.
- **What to produce:** per-stage CPU share for MP3 (Huffman/bit reading, IMDCT, synthesis filterbank, and the
  rest) and a refresh of the FLAC figures, measured on hardware at the bitrates that matter, with the existing
  headroom context (decode breaks down around 1.3-1.5x speed at 128 kbps).
- **Why the answer matters beyond kernels:** it decides whether the roadmap's "synthesis filterbank, then
  IMDCT" ordering survives. The only measurement so far points at a bit reader instead — ~1-2 M10K against
  ~8-12 for the filterbank (D3).
- **Where it lands:** a per-stage resource/cost line in `ARCHITECTURE_ROADMAP.md` section 2, plus an audit
  entry with the measurement conditions recorded.
