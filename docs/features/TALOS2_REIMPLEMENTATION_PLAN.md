# Talos 2: reimplementation plan for the 2D draw engine

**Status (updated 2026-09-29, owner decision): P1 shipped as T2-00 (below); the rest (P2-P4, the actual
"Talos 2" front-end/row-job rewrite) is DECLINED for now, revisit only when a genuinely new opcode is
needed** -- the review this plan comes from already recommended this order (`TALOS_REVIEW_2026-09-28.md`
section 7: "do not rewrite Talos now... rebuild the back end only when the next opcode is planned"); P1's
own urgency (the ALM crisis blocking `lpc-b372` at 111%) was the only forcing function tying it to the
rest of the plan, and it shipped independently. This file stays as the design reference for whenever P3
is actually triggered, not as a scheduled project.

Background and evidence: `docs/research/TALOS_REVIEW_2026-09-28.md` (the review this plan comes from).
Current engine: `src/fpga/core/mp3_fb.sv`; reference page: `docs/TALOS.md`; register map:
`docs/MMIO_ALLOCATION.md`.

## 1. Why

| Problem (measured or read from source) | Effect today |
|---|---|
| ~~With `TAU_BLIT_BLEND`, the 128-word row buffer (`glyphbuf`) gets a second read and a second write site, so Quartus builds it from 3,084 registers instead of an MLAB~~ | **RESOLVED, T2-00, 2026-09-29**: single shared write port (`gb_we`/`gb_addr`/`gb_data`), fanned out to two MLAB copies. Fit-confirmed (all four corners positive, both seeds) and hardware-confirmed. Committed to `main`. |
| ~~Six writers with four address sources share the buffer's one write port~~ | **RESOLVED, same T2-00 commit** -- there is now exactly one write site in the whole module, closing the class of bug fixed five times before it (B-111, B-114, B-157, B-231, B-327). |
| `R_FB_GO` reads back "FIFO full" only; tables and sticky fields are read when a command executes | CLUT reload, corner-cut LUT reload (B-349's fix) and blend-on can change under commands still queued |
| `OP_BAR` lit-row count is 7 bits | Fullscreen Winamp Bars (height 323) draw wrong heights above 127 rows -- **now seen on hardware** (B-406, 2026-09-29); firmware clamps to 127 as a stopgap (`fb_bar()`), the real fix (T2-0/P0: split into stacked bars) is still open |
| A burst crossing a 1,024-word SDRAM page hangs on read, wraps on write | Latent: every current caller uses stride 512. **Note (2026-09-29):** the `test/720` branch's native mode uses stride 1024 (one SDRAM page per line) and works around this at the firmware level (two 400-word chunks per line) rather than waiting on a general fix here -- see `docs/features/VIDEO_720_PHASED_SPEC.md` R9. |
| `OP_CBLIT`/`OP_SBLIT` do one full SDRAM transaction per pixel; every SDRAM read is preceded by an auto-refresh | About 20 cycles per pixel (estimate); also slows every CPU-window read |
| One row buffer, serial read-then-write; 127-pixel width limit; firmware splits copies itself | Slower rows, firmware workarounds (`FB_COPY_MAX`) |
| H2 double buffering offsets only non-blit opcodes | Blits would land in the front buffer unless firmware rewrites `DST_BASE` at every flip. **Being fixed independently (2026-09-29) by `test/720`'s `FB_DRAW_BASE`/`FB_DISP_BASE` (section 5.1 note below) -- do not build a second mechanism for this here.** |

## 2. Goals and non-goals

Goals, each with an acceptance test in section 8:
1. `mp3_fb` at or under 1,800 ALMs **with blend enabled**, and no RAM-inference fallback anywhere in it.
2. Every buffer has exactly one registered write port, so a new opcode cannot create a new write source.
3. Commands, sticky fields and table loads execute in the order the CPU issued them.
4. No width limit visible to firmware; no page-crossing hazard; hardware clip rectangle.
5. The same pixels as today for every existing opcode (checked against `tools/host/blit_reference.py`).
6. Same or better timing margin at 100 MHz than the current shipped fit.
7. The same firmware binary keeps working on old and new bitstreams (probe-gated, as `BLIT_READY()`
   and `RRECT_READY()` do today).

Not goals: 8-bit indexed framebuffer, affine rotation, filtering, multiple pixel lanes, a tile or sprite
compositor, a bank-interleaved SDRAM controller (all assessed and rejected in the review, section 5).
No new opcodes in this work; new opcodes come after, on the new structure.

## 3. Constraints (from the current fits)

- Device 5CEBA4F23C8: 18,480 ALMs, 308 M10K, 66 DSP. Current combined build: 17,285 ALMs (94%),
  240 M10K, 17 DSP. **Budget for Talos 2: 0 additional M10K** (row buffers in MLAB), ALMs as in goal 1,
  DSP no more than today's 6 in `mp3_fb`.
- Clocks: engine at `clk_sdram` 100 MHz; CPU side at 66.667 MHz (`TAU_CLK66`); video 12 MHz.
- Scanout must keep absolute priority: a pending line fill may wait for at most one burst.
- One SDRAM (16-bit, 1 pixel per word), shared with the CPU window through `tau_sdram_arbiter.sv`.

## 4. What stays the same (compatibility contract)

- MMIO: `R_FB_ADDR`, `R_FB_SIZE`, `R_FB_COLOR`, `R_FB_GO`, `R_BLT_IDX/DATA`, `R_CLUT_IDX/DATA`,
  `R_RC_IDX/DATA` keep their addresses and meanings. Bit 0 of `R_FB_GO` still reads "FIFO full".
- Opcodes 0-8 keep their field packing and results (including `OP_BAR`'s lit-at-bottom rule and the
  `OP_CHAR` gamma tables).
- Clock-crossing command FIFO (Gray pointers, 256 entries) and the scanout path (line buffer, video
  timing, `painted` blanking, `scan_vc`, vblank toggle, H2 flip logic) are kept as they are.
- Mutation-test parameters keep their names where the concept survives, so `make test-rtl-fb-mutation`
  keeps its meaning.

## 5. Architecture

```
clk_sys                      clk_sdram
MMIO --> [CMD FIFO] --> FRONT END ---------------> ROW JOB FIFO (small, MLAB) --> BACK END
          (existing)    decode, clip, lower            {row job}                   SRC -> OP -> DST
                        every command into                                         2 row buffers
                        row jobs; all math                                         (ping-pong, MLAB)
                        registered                                                        |
                                                            SDRAM port (via arbiter) <----+
SCANOUT FILL (unchanged, highest priority) -----------------------^
```

### 5.1 Front end (`talos_front.sv`)

- Pops one command, keeps the sticky state (bases, strides, key, blend, reindex, clip, target surface).
- Lowers every opcode into **row jobs** and pushes them to the row-job FIFO, one per cycle at most.
  All arithmetic (bar split, scaled extent, glyph base, corner cut and segment address, clip, page
  split, width split) happens here, one registered stage at a time. Nothing in the front end feeds the
  SDRAM port or a buffer write directly.
- **Splits** any row segment at 128 words and at every 1,024-word page boundary (source and
  destination independently: a split happens where either would cross).
- **Clips** against a sticky clip rectangle (default: whole 512 x 1,024 surface, i.e. no clipping,
  so behaviour is unchanged until firmware sets it).
- **Target surface**: one sticky offset added to every destination, including blit opcodes, so H2's
  back buffer applies uniformly. Default 0. **DEFER TO `test/720`'s design if P3 is ever built (decided
  2026-09-29):** `docs/features/VIDEO_720_PHASED_SPEC.md` section 2.2 independently designed
  `FB_DRAW_BASE`/`FB_DISP_BASE` for the exact same gap, scoped to ship on TODAY's architecture (no
  rewrite needed), with `DBUF_*` kept as a compatibility view. If that lands first (the realistic
  order, since P3 has no forcing function), P3's front end should adopt its register semantics as this
  slice's own implementation rather than inventing a second, competing base-offset mechanism. Whoever
  builds `FB_DRAW_BASE`/`FB_DISP_BASE` should add a mutation test proving it also covers the BLIT-class
  sticky `DST_BASE` addressing path, not just the RECT-class `cmd_addr` path that already had H2
  coverage via the old 1-bit `R_DBUF_CPU` selector -- that distinction is the actual gap, not a detail.
- Handles the in-queue control opcodes (5.4) itself; they never reach the back end except the fence.

### 5.2 Row job (the only interface between the two halves)

| Field | Bits | Meaning |
|---|---|---|
| `dst` | 25 | first destination word (already offset, clipped, split) |
| `w` | 7 | 1..128 words |
| `src_kind` | 3 | NONE (constant), BURST (SDRAM row read), FONT (glyph row), GATHER (scaled: read then resample), DSTREAD (blend) |
| `src` | 25 | first source word, when the kind needs it |
| `pix_op` | 3 | COPY, KEY, CLUT, AA, BLEND |
| `colour` | 32 | fill colour / fg+bg for AA / key colour |
| `aux` | 16 | op-specific: glyph row index and Bresenham state for FONT/GATHER, scale ratio |
| `last` | 1 | last job of its command (for the fence and the idle bit) |

About 112 bits; the row-job FIFO is 4 to 8 entries deep in MLAB (an M10K is not needed at that depth).

### 5.3 Back end (`talos_back.sv`)

- Two row buffers A and B, each 128 x 17 bits (16 data + 1 write-mask bit), each in MLAB with **one
  write port and one read port**. Both written only from a single registered stage `{we, sel, addr,
  data, mask}`. A lint check (section 8) enforces this.
- **SRC stage** fills the free buffer: a burst read for BURST/GATHER/DSTREAD, the font ROM for FONT,
  nothing for NONE.
- **OP stage**, a short fixed pipeline between SRC data and the buffer write: key compare (sets the
  mask bit), CLUT lookup (registered M10K read, one extra stage), AA mix (the existing `cov_weight`
  tables and multiply, registered), blend (the B-327 three-stage pipeline, reading the destination row
  that a DSTREAD pass put into the same buffer beforehand).
- **DST stage** drains the other buffer with one streaming write burst. Each beat's `DQM` comes from the
  mask bit (`00` write, `11` skip), so keyed pixels need no destination read. Constant fills keep the
  existing constant-data burst with no buffer at all.
- The two buffers let row N+1 be filled while row N is written. The SRC and DST stages still use the
  one SDRAM port, so they alternate; the gain is that compose and CLUT work hide behind SDRAM work.
- Scaled blits (GATHER): burst-read the needed source row span into the buffer once, then resample from
  the buffer into the other buffer; this replaces one SDRAM transaction per output pixel.

### 5.4 Ordering, fence and idle

- New in-queue control opcodes (use the free `cmd_op` values 9-15; data rides in the existing fields):
  `SET_FIELD` (a sticky field), `CLUT_WR` (one CLUT entry), `RC_WR` (one corner-cut entry), `FENCE`
  (writes a firmware-chosen token to a status register when every earlier job has retired).
- The existing `R_BLT_DATA`, `R_CLUT_DATA`, `R_RC_DATA` registers **stay** as immediate writes for old
  firmware. New firmware uses the in-queue forms, so a reload can never overtake a draw.
- `R_FB_GO` bit 1 becomes "engine idle" (FIFO empty, row-job FIFO empty, back end idle, no burst in
  flight), through a two-flop synchroniser. `R_FB_FENCE` (new, one MMIO word) reads the last retired
  fence token. These two replace `fb_fence()`'s 50 ms mailbox polling.

### 5.5 Arbitration inside `clk_sdram`

One small arbiter in the back end, priority: scanout fill, then DST (drain a full buffer), then SRC.
Every transaction is one burst of at most 128 words plus overheads, so a fill waits at most one burst,
as today.

## 6. Opcode lowering (front end)

| Opcode | Row jobs |
|---|---|
| RUN, RECT | h jobs: NONE + constant colour (the DST stage uses the constant-data burst) |
| BAR | unlit segment rows then lit segment rows, both NONE. Lit count comes from a **new 9-bit field** (sticky `BAR_LIT`, or the free FIFO padding bits), with the old 7-bit field used when the new one is not set, so old firmware keeps its current behaviour |
| RRECT | body rows NONE; then per corner row up to four 1-row NONE jobs from the cut table (the cut and segment address computed here, a cycle ahead, as B-231 does now) |
| CHAR | per output row: FONT + AA; glyph row index and X Bresenham state in `aux` |
| COPY | per row: BURST + COPY, source relative to 0 with stride 512 (as today) |
| BLIT | per row: BURST + COPY; with key: BURST + KEY (mask bits, no destination read); with blend: DSTREAD pass then BURST + BLEND |
| SBLIT | per output row: GATHER + COPY, source row chosen by the Y Bresenham step |
| CBLIT | per row: BURST + CLUT (source words hold the index in the low byte, as today) |

## 7. SDRAM controller changes (`sdram_fb.sv`), same phase as the back end

1. Refresh before a read only when the read could outlast the refresh interval, using the requested
   length (at most 128 words once Talos 2 splits rows; 512 for scanout). **At the same time** make the
   mid-burst refresh branch in `READ_OUTPUT` safe (finish the burst, then refresh), because today the
   unconditional pre-read refresh is what keeps that branch unreachable.
2. Streaming writes take a per-beat mask from the source port (`wsrc_mask`) and drive `DQM` from it.
3. Scanout fill stops at 400 words instead of 512.
4. Measure the CPU-window access cost before and after item 1 with the existing A-094 probe firmware.

## 8. Verification and acceptance

Simulation (all in `make test-rtl`):
- `sim/tb_blit_scene.v` + `sim/test_blit_reference.py`: every opcode, every pixel matches the Python
  reference. Add scenes for: width 128 to 511 (hardware split), a source and a destination crossing a
  page boundary, clip rectangle, target surface offset, BAR with lit rows above 127, keyed blit with no
  destination read, CLUT reload between two CBLITs queued back to back, FENCE token order.
- `sim/tb_mp3_fb.v` kept as the regression for today's behaviour; mutation parameters rewired to the new
  stages and each shown to be caught.
- New scanout-starvation check: the worst-case wait between a fill request and its first beat, under a
  continuous blit storm, is at most one burst plus controller overhead; the testbench fails otherwise.
- Controller: a testbench for the length-based refresh (refresh interval respected under a stream of
  short reads, and the mid-burst refresh path completes the burst).

Synthesis and fit (on the VM, `tools/vm_fit.py`):
- Map report: every Talos buffer appears as `altdpram`/`altsyncram`; no per-index register names for
  any Talos array (script this as a grep; fail the check if found).
- `mp3_fb` (or its replacement entities) at most 1,800 ALMs with `TAU_BLIT_BLEND`; M10K count not
  higher than today's 22 in `mp3_fb`; all four timing corners positive on two seeds.
- Whole combined build (current feature set plus LPC) fits.

Hardware (Pocket, Diagnostic Build, installed with `tools/install_dev_core.py`):
- Blit Test page: every opcode draws as before.
- Check STANDARD and the blit storm: 0 late underruns, SDRAM busy percentage recorded against B-146's
  15.8%.
- Fullscreen Winamp Bars full height; radio buttons correct; scope trail (blend) correct.
- The same firmware on the previous bitstream still runs (probes fall back).

## 9. Phases

Each phase ends with its own simulation pass and, where RTL changes, its own fit. No phase starts
before the previous one is hardware-confirmed.

| Phase | Content | Fit? | Can ship alone? | Status |
|---|---|---|---|---|
| P0 | Firmware only: `fb_bar()` splits bars above 127 lit rows into stacked bars; width guards in `fb_cblit/fb_blit/fb_sblit`; the page rule written into `docs/TALOS.md` | No | Yes | **Open** -- only a stopgap clamp shipped so far (B-406), not the real split-into-bars fix |
| P1 | Interim fix inside today's `mp3_fb.sv`: single registered write port for `glyphbuf` and two MLAB read copies, so blend no longer costs ~6,500 ALMs. Keeps everything else. | Yes | Yes. Do this first if chip space is needed before the rewrite | **DONE, shipped as T2-00, committed to `main` 2026-09-29** |
| P2 | Idle bit, fence register, in-queue control opcodes (on today's engine). Firmware `fb_drain()` used by CLUT load, corner-cut load, blend | Yes | Yes | Open, no forcing function yet |
| P3 | Front end + row-job FIFO + back end replacing the engine body; controller changes (section 7) | Yes | Yes, after section 8 passes | **Declined for now (2026-09-29)** -- revisit only when a new opcode is actually needed |
| P4 | Firmware cleanup that the new engine allows: drop `FB_COPY_MAX` splitting, use the clip rectangle, H2 target surface, 9-bit bar lit count | No (probe-gated) | Yes | Moot until P3 |

P1 and P2 are deliberately done on the current engine: they are small, independently testable and fix
real problems even if P3 is postponed. P3 keeps P2's register interface.

## 10. Risks

| Risk | Mitigation |
|---|---|
| P3 is a large RTL change to a hardware-proven engine | Reference renderer diff for every opcode, existing testbenches kept, old engine kept behind a macro (`TAU_TALOS1`) until P3 is hardware-confirmed |
| Row-job hop adds latency per command | A few cycles per command against rows of tens to hundreds of cycles; measured by the blit storm |
| MLAB inference fails again silently | The map-report grep in section 8 is part of the fit check, not a manual step |
| Controller refresh change affects every SDRAM client, including the CPU window | Separate commit, own testbench, A-094 probe before and after, soak test (existing Diagnostic Build soak) |
| Old firmware relies on execute-time sticky semantics | Immediate registers are kept; only new firmware uses the in-queue forms |
| Timing on the SRC-OP-DST pipeline | Every stage registered; the only arithmetic in the back end is the existing AA and blend blocks, already pipelined |

## 11. Open decisions (owner)

1. ~~Whether P1 goes in as soon as LPC is done (it frees about 6,500 ALMs), or waits for P3.~~
   **DECIDED, 2026-09-29: P1 shipped independently as T2-00; P3 is declined for now.**
2. Where the 9-bit BAR lit count lives: a sticky field (no FIFO change) or the FIFO padding bits
   (per-command, but only 4 spare bits today, so it would need the word widened). Moot unless P3
   revives -- P0 (firmware-only stacked-bar split) is the real near-term fix.
3. Clip rectangle default: whole surface (no behaviour change, recommended) or the visible 400 x 360.
4. New (2026-09-29): if P3 ever revives, adopt `test/720`'s `FB_DRAW_BASE`/`FB_DISP_BASE` as this
   slice's target-surface mechanism rather than the sketch in section 5.1 -- see that section's own
   note and `docs/features/VIDEO_720_PHASED_SPEC.md`.
