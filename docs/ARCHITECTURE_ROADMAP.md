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
| M10K blocks | 300 / 308 **[HW]** | GPU line buffers, EQ, kernel buffers | Size each first (EST). Stay at <= 300 until a consumer is measured to need more. |
| DSP blocks | 11 / 66 **[HW]** | EQ (about 1), kernel accelerator, GPU blend | Track per phase. |
| MMIO offsets | used up to 0xAC (0xA8 + PSRAM window counter) in a 4 KiB page; table in `docs/MMIO_ALLOCATION.md` (corrected 2026-09-21) | EQ extras, accelerators, GPU | Allocate from the table before any new block. Do not widen the page (8 offset bits are decoded; 0x100+ aliases). |
| SDRAM bandwidth | scanout about 17 MB/s (400x360x60x2) **[EST]** of 200 MB/s peak | audio (if ring moves), GPU, CPU window | Add a busy-cycle counter (Phase B). |
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
   what breaks if moved. No hardware, no build.
2. **SDRAM busy-cycle counter** (scanout + CPU window + audio) published via
   `interact.json`; add it to the next RTL build so no extra Quartus slot is spent.
3. **SDRAM map document**: framebuffer, upstream font (byte 2 MiB), Tau diagnostic
   window (2-3 MiB), CPU windows from 1 MiB, PSRAM window, and a **reserved region
   for a 720 framebuffer** (about 1.15 MB **[EST]**, more than the 1 MiB now kept
   free). Upstream flagged the font collision; write this before either side moves.
4. **MMIO allocation table** covering PSRAM, EQ, accelerators, GPU. (Done: `docs/MMIO_ALLOCATION.md`.)
**Exit:** three documents/tables reviewed; counter bundled into the next build.

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
1. **Profile** the software decoder first (CPU share per stage). If it is already
   fast enough, stop here for kernels.
2. **EQ** per `EQ_DESIGN.md`: post-`pcm_fifo`, about one DSP, no CPU, no SDRAM, no
   new CDC. Coefficients generated offline.
3. **One kernel at a time** (synthesis filterbank, then IMDCT; FLAC LPC only if FLAC
   matters). Memory-mapped block with on-chip buffers, completion flag, software
   path kept selectable and bit-exact compared over a long soak. Huffman stays in
   firmware unless profiling says otherwise.
4. Each stage records ALM/DSP/M10K/timing and audio underruns under the existing
   stress matrix.
**Exit:** per-stage resource line added to section 2; no underrun regression.

### Phase E - media library
- Firmware and data layout first: indexes and sorted tables in SDRAM/PSRAM, loaded
  on demand; large-list behaviour already proven to 240-256 entries (A-108).
- Optional small FPGA helpers only if measured: SD-to-memory copy engine, CRC/hash
  for change detection.
- Cover art: decide `art_acc` placement from the Phase C cost, or restructure the
  decode.
**Exit:** library size/latency targets defined and measured; no BRAM growth.

### Phase F - GPU (at the current resolution)
- Extend the `mp3_fb` command set: blit/copy, alpha blend (DSP), scaled blit for
  covers. Or a scanline sprite/tile compositor if bandwidth or M10K is short.
- Source assets from PSRAM/SDRAM through the existing arbiter; separate read and
  write paths if a DMA/GPU master is added (a shared single CPU port serialises
  copies).
- Parametrise width, height, stride and framebuffer base (section 4).
- Carry the two upstream lessons: font/burst starvation and the -1.888 ns timing
  cliff from a new pipeline path at 100 MHz.
**Exit:** GPU commands validated on Pocket under playback with the busy-cycle counter.

### Phase G - cached window / BRAM return (decision gate, not a commitment)
Start only if a measured consumer from Phases D-F needs more than the blocks that
fit today. Sequence: cache-line-decomposing adapter (Phase 2 step 3) -> cold code
experiment (Phase 3) -> 128 KiB RAM only if the hot set fits with margin
(Phase 4). This is a large project; if nothing needs it, skip it.

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

## 4. Decisions to make now so 720 stays cheap later
- No hardcoded 400, 360 or 512-word stride in firmware, GPU or packager; use named
  constants or registers.
- Reserve the 720 framebuffer region in the SDRAM map (Phase B) even though it is
  unused.
- Keep GPU coordinates and clipping parametric.
- Keep per-line pixel work bounded, so a 4x pixel count is a bandwidth question,
  not an algorithm rewrite.

## 5. Stop conditions
- A step that raises M10K above 308, or timing failure over all seeds: stop and
  redistribute buffers before continuing.
- Any underrun or display corruption attributable to a new client: stop, record,
  and fix before the next phase.
- A mailbox test passing while the CPU-window test fails (the A-092 pattern):
  suspect the bus adapter and registered ACK before the memory.
- Do not combine two RTL clients in one change ("do not attempt the port and the
  decoder in the same change" applies to every pairing).
