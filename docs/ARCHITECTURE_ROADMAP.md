# Architecture roadmap (720 last)

**Status:** planning document, 2026-09-20. No code, RTL, card or VM change.
Supersedes nothing; it orders the existing plans (`PSRAM_IMPLEMENTATION_PLAN.md`,
`SDRAM_MEMORY_ARCHITECTURE.md`, `EQ_DESIGN.md`, `PLAYLIST_SDRAM_MOVE_SPEC.md`).
Evidence labels as elsewhere: **[HW]** Pocket result, **[RTL]** read in source,
**[DOC]** documentation, **[EST]** estimate to be measured.

## 0. Owner decision

Resolution 720 is the **last** item. Every other feature is prioritised ahead of
it. Consequence: everything built earlier must not assume 400x360 or a 512-word
stride, so that 720 later is a configuration and memory-map change, not a rewrite
(section 4).

## 1. Corrections that shape the order

1. The CPU already has 4 KiB I/D caches (32-byte lines, cacheable by address bit
   31) **[RTL]**. The "cached window" is the `0x4010_0000` alias, which returns a
   bus error today. What is missing is a line-fill adapter (Phase 2 step 3,
   KB-024), not a new cache.
2. Moving data to SDRAM/PSRAM frees address space, **not** M10K blocks. Blocks
   only return if the 256 KiB CPU RAM shrinks (Phase 4), and `.text` alone is
   135,508 B, so that needs cold code executing from a cached window (Phase 3).
3. The SDRAM owner-locking arbiter (scanout wins) already exists. Audio as a
   real-time client would extend it; it is not new work unless the audio ring moves.
4. The draw engine (`mp3_fb`: RUN/RECT/CHAR FIFO, scanout-first dispatcher) is
   already a small 2D engine. The GPU is an extension of it.
5. Partial MP3 acceleration is already a deferred item with a profile-first gate.

## 2. Budgets (shared, tracked across phases)

| Resource | Now | Owner of the next claim | Rule |
|---|---|---|---|
| M10K blocks | 300 / 308 **[HW]**; **full per-instance map in `docs/PHASE_F_SPEC.md` section 1** | GPU working buffers, kernel buffers | Size each first (EST). Stay at <= 300 until a consumer is measured to need more. **85% of the 300 is one thing: the 256 KB main RAM (256 blocks).** Release plan and ledger: PHASE_F_SPEC section 4. |
| MLAB | **0 used** — entirely unclaimed, and ALMs are ~35% free | small SDP FIFOs and delay lines migrated off M10K | The EQ set the precedent (`ramstyle = "MLAB, no_rw_check"`; its design doc: "the EQ must not use M10K"). ~7 blocks are recoverable this way for no functional change. |
| DSP blocks | 11 / 66 **[HW]** (includes the shipped EQ's one) | GPU blend, spectrum filter bank, kernel accelerator | Track per phase. DSP is **not** the constraint here; block RAM is. |
| MMIO offsets | used up to 0xAC (0xA8 + PSRAM window counter) in a 4 KiB page; table in `docs/MMIO_ALLOCATION.md` (corrected 2026-09-21) | EQ extras, accelerators, GPU | Allocate from the table before any new block. Do not widen the page (8 offset bits are decoded; 0x100+ aliases). |
| SDRAM bandwidth | scanout about 17 MB/s (400x360x60x2) **[EST]** of 200 MB/s peak | audio (if ring moves), GPU, CPU window | Add a busy-cycle counter, bundled into the Phase F blit-engine build (deliberately skipped in Phase C/G3 so timing changes there stayed attributable to PSRAM instruction fetch alone; owner decision 2026-09-22). |
| Quartus slots | about 45 min per build, one at a time | every RTL change | Bundle RTL changes into one build where safe; multi-seed each. |

## 3. Phases

### Phase A - close the current product RTL
- A-114 probe-free fits (seeds 1, 2) finish; pick by the pre-set rule.
- Re-run gates 2-4 (`CURRENT_STATUS.md`) on the probe-free RTL: margin/soak,
  contention with real playback, product build.
- Decide promotion of the A-105 playlist-in-SDRAM move (`TAU_PL_SDRAM`, 13 KiB;
  A-096 says this alone leaves about 12.7 KiB spare, enough for the 7-row settings
  menu). The 24 KiB target is not supported by a measured need.
**Exit:** probe-free window RBF passes the gates; playlist move decided.

### Phase B - measurements (mostly no hardware)
1. **M10K map** from the Quartus fit report: per-instance blocks, what is movable,
   what breaks if moved. No hardware, no build. **DONE 2026-09-22** - derived from
   the B-018 product-config fit report, cross-checked against seven other saved
   fits (all 300/308). Table and analysis: `docs/PHASE_F_SPEC.md` section 1.
   Follow-up: extend `tools/quartus_fit_summary.py` to emit this table per build
   instead of it being hand-derived.
2. **SDRAM busy-cycle counter** (scanout + CPU window + audio) published via
   `interact.json`. **Not yet built.** Deliberately left out of every RTL build since
   (A-101/A-114 product fits, B-005/007/018 PSRAM builds, the G3 instruction-fetch
   build) so each build's timing/gate changes stayed attributable to what that build
   actually changed. Now targeted at the **Phase F blit-engine build** (owner decision
   2026-09-22): that build already changes RTL and firmware together and a Quartus
   slot is being spent anyway, matching the "no extra slot" rule below.
3. **SDRAM map document**: framebuffer, upstream font (byte 2 MiB), Tau diagnostic
   window (2-3 MiB), CPU windows from 1 MiB, PSRAM window, and a **reserved region
   for a 720 framebuffer** (about 1.15 MB **[EST]**, more than the 1 MiB now kept
   free). Upstream flagged the font collision; write this before either side moves.
4. **MMIO allocation table** covering PSRAM, EQ, accelerators, GPU. (Done: `docs/MMIO_ALLOCATION.md`.)
**Exit:** three documents/tables reviewed; counter bundled into the Phase F build.

### Phase C - PSRAM (P2-P5 of the PSRAM plan)
**Status (2026-09-21):** P2 and P3 done on the Pocket, read margin measured, P4 (CPU window) built and simulated with two Quartus builds running; see `docs/SESSION_HANDOFF_PSRAM_2026-09-21.md`.
- P2 diagnostic core (bundle with the Phase B counter), P3 Pocket matrix (5 cold +
  5 warm), P4 uncached window plus A-094/A-097-style cost and soak, P5 one cold-data
  move (`art_acc` only if the measured cost keeps the decode delta small).
- Keep PSRAM behind the existing decoder/bus interface (single outstanding request,
  held response, `clk_sys`, no arbiter). Firmware uses ready/wait, never fixed
  delays. Real-time masters (scanout, audio) never touch PSRAM.
**Exit:** PSRAM access cost and worst case measured; no SDRAM gate affected.

### Phase D - hardware audio (partial acceleration)
**Status correction 2026-09-22:** step 2 (the EQ) **shipped some time ago** - `eq_biquad`
is instantiated in `mp3_soc.v`, listed in `ap_core.qsf`, wired to `R_EQ` (0x68) and
confirmed on hardware. `EQ_DESIGN.md`'s "Nothing here is built" was stale and is
corrected. The EQ's one DSP is already inside the 11/66 figure in section 2.

1. **Profile** the software decoder first (CPU share per stage). If it is already
   fast enough, stop here for kernels. **This has never been run**, and it is free -
   no RTL, no Quartus slot. It gates everything below and may cancel it.
2. ~~**EQ**~~ - shipped, see the correction above. It also set the M10K precedent this
   project now relies on: "the EQ must not use M10K", solved with MLAB.
3. **One kernel at a time.** **Ordering corrected 2026-09-22:** this list used to read
   "synthesis filterbank, then IMDCT", but the only real measurement points elsewhere -
   `docs/FLAC.md` reports the **bit reader at 64-76% of FLAC decode time**, rising with
   bitrate. A bit reader is a barrel shifter and a table (~1-2 M10K), not a DSP kernel
   (~8-12 M10K for the MP3 synthesis filterbank, whose V[] buffer must be on-chip). So:
   smallest and best-evidenced first, and only after step 1. There is still **no MP3
   per-stage profile at all**. Memory-mapped block with on-chip buffers, completion flag,
   software path kept selectable and bit-exact compared over a long soak.
4. Each stage records ALM/DSP/M10K/timing and audio underruns under the existing
   stress matrix.
5. **Spectrum filter bank** (moved here from "someday"): port the shipped software
   octave cascade into RTL. ~0 M10K, one time-shared DSP MAC. Rationale and the
   decision *not* to build an FFT: `docs/PHASE_F_SPEC.md` section 7.
**Exit:** per-stage resource line added to section 2; no underrun regression.

### Phase E - media library
- Firmware and data layout first: indexes and sorted tables in SDRAM/PSRAM, loaded
  on demand; large-list behaviour already proven to 240-256 entries (A-108).
- Optional small FPGA helpers only if measured: SD-to-memory copy engine, CRC/hash
  for change detection.
- Cover art: decide `art_acc` placement from the Phase C cost, or restructure the
  decode.
**Exit:** library size/latency targets defined and measured; no BRAM growth.

### Phase F0 - JTAG debug access (immediately before the blit engine)
- Owner decision 2026-09-21. Procedure and background: `docs/JTAG_DEBUG_ACCESS.md`
  (USB-Blaster through UTM works; `quartus_pgm` reloads a `.sof` in about 5 s).
- Add, behind a macro and in the Diagnostic Build only: SignalTap for the memory
  controllers and video timing; a JTAG-to-Avalon master for memory/frame reads and MMIO;
  optional JTAG UART. Check free M10K/ALM and timing first; release stays unchanged.
- First step: the SignalTap proof build (files prepared, `docs/JTAG_DEBUG_ACCESS.md` s5.5).
**Exit:** a frame buffer and a memory range read from the Mac; a SignalTap capture of an
SDRAM window access; fit still closes on several seeds; product config unchanged.

### Tooling track - faster firmware testing (added 2026-09-21, B-034)
Not a phase of its own; each item is scheduled where it first pays off. Basis: firmware (`tau.rom`) is delivered from the SD card at core start, so JTAG alone
does not shorten a firmware iteration (JTAG reload re-initialises the core and re-delivers slots, but the ROM must still be on the card).
1. **Host-side firmware harness** - starts with Phase E firmware (media library phase 2, `TAU_LIBRARY`). Extend `tools/host` and the real-CPU sim (`sim/tb_psram_fw.v`)
   with a file-backed fake bridge (serves `tau-library.tdb`, music files, data slots) and a framebuffer-to-PNG dump. Use it first for the library loader, browse
   logic and the E10-E17 corruption matrix; then reuse for BUG-001 (path handling), settings, and the Phase F blit engine's firmware side. Most valuable while a feature
   is being written, before any Pocket run. Exit: library loader and browse pages run in the harness over the same fixtures as the Python reference reader.
2. **One-command card push** (`make card-fw` style: backup, copy, `cmp`/SHA-256, remove `._*`, sync, eject; stops for owner approval before writing) - ready by the end
   of Phase E firmware, before the first library Pocket install (`TAU PSRAM 07`); useful for every later numbered test core. Removes manual steps, not the approval rule.
3. **JTAG RAM loader** (debug-only bitstream: Virtual JTAG block writes firmware into CPU RAM and resets the CPU; seconds per iteration, no card swap) - a decision gate,
   not a commitment. Earliest sensible point: Phase F (blit engine), where RTL and firmware change together and one Quartus build is being spent anyway; bundle it into that
   build rather than paying for its own. Also reconsider if firmware iteration count is still the bottleneck after items 1 and 2. Rules: separate dev bitstream, never the
   product one (B-021: shared RTL changes even with macros off); needs a host tool to drive the JTAG chain (VM blaster or a host tool); full gates if it ever touches shared RTL.
4. **VM stays RTL-only.** The media library needs no RTL and no Quartus build.

**Parked in Phase E (owner, 2026-09-21):** a Settings (advanced) switch to disable / enable the library, with a confirmation and an explanation of what it does; design notes in `docs/MEDIA_LIBRARY_0.4_SPEC.md` section 13. Also open in Phase E: library resume (two persist words) and a Diagnostic Build library check page.

**Parked behind more free CPU RAM (owner, 2026-09-21):** library resume by second, the Diagnostic Build library page, negative-test indexes, fixtures for the new screens, Settings-style legacy playlist overlay, per-colour text colours, release 0.4 documents; list and memory sources in `docs/MEDIA_LIBRARY_0.4_SPEC.md` section 14. RAM is expected mainly from Phase G (cold code in the cached PSRAM window); Phases D and F give less unless the software audio path is retired.

### Phase F - GPU (at the current resolution)
**Full spec, written 2026-09-22: `docs/PHASE_F_SPEC.md`.** It carries the feature tiers,
the M10K release plan bundled into this same build, the prior-art/licensing review, the
verification and fail-safe plan, and the parked-ideas list. Summary only here.

- Extend the `mp3_fb` command set. Tier 1: generalised blit (arbitrary rect, independent
  stride), colour-key transparency, **sub-pixel skew + first/last column masks** (the
  Amiga/Atari-ST barrel-shifter mechanism - this is what fixes the marquee's
  whole-character scrolling), nearest scaled blit, alpha blend (DSP multiply, with
  PSX-style shift-add fixed ratios as a cheap mode), a meter-column primitive, and the
  SDRAM busy-cycle counter. Tier 2 if it fits: CLUT blit, palette re-index for
  dim/highlight, hardware RLE source blit, hardware rounded-rect.
- **Bundled into the same build** (one Quartus slot): the MLAB migration (~7 blocks) and
  the font-ROM repack (~4 blocks). Both are functionally inert, so a timing failure
  bisects by macro rather than by rebuild - keep each behind its own macro.
- **Scaled blit needs no line buffer**: the decoded cover is already randomly addressable
  in memory, so scaling is one read per output pixel. Line buffers are a streaming-source
  problem and this source does not stream.
- Source assets from PSRAM/SDRAM through the existing arbiter; separate read and
  write paths if a DMA/GPU master is added (a shared single CPU port serialises
  copies).
- Parametrise width, height, stride and framebuffer base (section 4).
- Carry the two upstream lessons: font/burst starvation and the -1.888 ns timing
  cliff from a new pipeline path at 100 MHz.
- **Ordering constraint that drives everything after this phase:** the meters (~17 KB)
  are hot *because* they wait on this engine. Blit engine -> meters go cold -> ~29 KB
  freed -> main RAM 256 KB to 192 KB (two power-of-two arrays, address-decoded) ->
  **64 M10K blocks released**. The RAM shrink cannot come first.
**Exit:** GPU commands validated on Pocket under playback with the busy-cycle counter,
with no regression in the zero-late-underrun record.

### Decision 2026-09-22: no 3D GPU
Recorded so it is not re-derived. A full-screen 16-bit Z-buffer at 400x360 is about
**281 M10K** on a device with 308 in total; 8-bit Z is about 141. Tile-based rendering is
the only feasible form (~20-25 blocks plus heavy ALM) and puts the zero-late-underrun
record at real risk. PocketQuake shows 3D *is* possible on this chip alongside real-time
audio, but it spends a dedicated external SRAM on the z-buffer, which this design does not
have free. What the ambition is actually for - visualiser eye-candy - is reachable from
**2.5D primitives on the 2D engine for ~2-4 blocks**: per-row scaled blit, rotated blit,
affine textured quad. Revisit honestly after the 192 KB shrink if still wanted.

### Phase G - cached window / BRAM return (partly delivered)
**Status 2026-09-22:** the first two steps are done and shipped in v0.4.0 - the PSRAM
instruction-fetch path and about 24 KB of cold code now run from PSRAM (`docs/PHASE_G_SPEC.md`).
What remains is the third step, **returning main RAM to the M10K pool**, now specced in
`docs/PHASE_F_SPEC.md` section 4 with one correction to the plan as written here:

- **192 KB, not 128 KB.** `RAM_WORDS` must be a power of two (a 48 KB attempt once exploded
  the fitter into LUTs), but **two** power-of-two arrays address-decoded as one contiguous
  region (128 KB + 64 KB) keeps each array clean while landing at 192 KB. That frees
  **64 blocks** and needs only ~29 KB more headroom, against ~93 KB for a true halving -
  which is not reachable from the ~34.7 KB free today.
- **Gated on the blit engine**, because the ~29 KB comes from moving the meters (~17 KB)
  and picojpeg (~8 KB) cold, and the meters wait on Phase F.
- Measure the hot set *with* margin and write down a floor before shrinking: this trades a
  scarce resource for another one, and heap gap has repeatedly been driven to its floor.

### Phase H - 720 (last)
1. Check the maximum video mode in the APF docs snapshot (`video.json`); not yet
   verified.
2. Bandwidth budget from the Phase B counter: scanout would take roughly 35-45% of
   SDRAM cycles at 800x720 against about 12% now **[EST]**. Decide 16 bpp vs 8-bit
   palette and any line-buffer/FIFO depth.
3. Apply the reserved memory map (Phase B); raise stride; extend the arbiter so
   scanout has fixed top priority; re-run contention and soak.
4. Re-validate GPU commands and the media/cover paths at the new size.
**Exit:** all earlier gates re-pass at 720; timing closes over several seeds.

## 4. Decisions to make now so later phases stay cheap
- No hardcoded 400, 360 or 512-word stride in firmware, GPU or packager; use named
  constants or registers.
- Reserve the 720 framebuffer region in the SDRAM map (Phase B) even though it is
  unused.
- Keep GPU coordinates and clipping parametric.
- Keep per-line pixel work bounded, so a 4x pixel count is a bandwidth question,
  not an algorithm rewrite.
- **MMIO: split engine state from per-command fields.** Decided 2026-09-22; full form in
  `docs/PHASE_F_SPEC.md` section 9. Only 0xBC-0xFC is free (17 words) and only 8 offset bits
  are decoded. Do **not** add a register per parameter, and do **not** widen the command FIFO
  word either - widening `cmd_mem` to carry a full descriptor would take it from 3 M10K to
  ~7, eating most of what the MLAB migration frees. Instead: sticky state (bases, strides,
  colour key, alpha, palette, scale) lives in flops; only op/x/y/w/h/offset ride the FIFO.
  Three registers total - `R_BLT_IDX`, `R_BLT_DATA` (both auto-incrementing), `R_BLT_GO` -
  give an unbounded number of state fields for 3 of the 17 free words. This is the Amiga
  split (`BLTCON`/`BLTAFWM` persist; only the size write triggers). Do it before any Phase F
  RTL exists; retrofitting after three features have claimed registers is the expensive version.
- **Tau is MIT, so copyleft RTL cannot be copied in.** (An earlier version of this line
  wrongly claimed there was no LICENSE file - there is, MIT, tracked since the first commit,
  with both HarpMudd's and Tau's copyright lines; `NOTICE.md` and the README Credits already
  document third-party boundaries correctly.) The consequence for Phase F: the best reference
  implementations are GPLv2/GPLv3 (Minimig's Amiga blitter, PSX_MiSTer) or carry no stated
  licence at all (AtariST_MiSTer, Saturn_MiSTer - treat those as do-not-copy). Techniques are
  decades-old and safe to reimplement independently; code is not. See `docs/PHASE_F_SPEC.md`
  section 6.

## 5. Stop conditions
- A step that raises M10K above 308, or timing failure over all seeds: stop and
  redistribute buffers before continuing.
- Any underrun or display corruption attributable to a new client: stop, record,
  and fix before the next phase.
- A mailbox test passing while the CPU-window test fails (the A-092 pattern):
  suspect the bus adapter and registered ACK before the memory.
- Do not combine two RTL clients in one change ("do not attempt the port and the
  decoder in the same change" applies to every pairing).
