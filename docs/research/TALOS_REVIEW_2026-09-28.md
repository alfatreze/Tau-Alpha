# Talos review (2026-09-28): what is slow, what is wrong, what a rewrite would look like

Scope: `src/fpga/core/mp3_fb.sv` (the engine), `src/fpga/rtl/mem/sdram_fb.sv` (the SDRAM controller it
sits on), `tau_sdram_arbiter.sv`, and the firmware side in `fw/player.c` / `fw/blit_probe.inc`. Read from
source on `main` at `f2bdb0a`. Pocket hardware facts come from the `analogue-pocket-dev` skill
(`references/hardware-video-audio-input.md`) and this project's own fit reports. Nothing here was run
on hardware; every item says whether it was read from source, computed, or is an estimate.

## 1. Summary

- **Talos is not what limits audio today.** The blit storm Check held 15.8% SDRAM busy with 0 late
  underruns (B-146).
- **But Talos is now the largest block on the chip, and almost all of it is one inference failure**
  (section 1a, added the same day from the VM fit reports). With `TAU_BLIT_BLEND` on, `mp3_fb` needs
  7,800 of 18,480 ALMs; without it, 1,313. The whole design goes from 57% to 92-94% full, and the
  TAU_LPC fit (`lpc-b372`) overflows at 111%. **Correction:** an earlier draft of this review said
  "about 7,200 ALMs, logic 61% free, Talos about 600 ALMs"; those numbers came from 2026-09-24 fits
  without blend and are wrong for the current build.
- **The biggest waste is below Talos, in the SDRAM controller**: every read request is preceded by an
  auto-refresh, whatever its length (section 3.1). That costs about 10 cycles on every single-word read,
  including every CPU-window read and every pixel of `OP_CBLIT`/`OP_SBLIT`.
- **Two live correctness bugs**, both firmware-visible:
  1. Fullscreen Winamp Bars (height 323) wrap: `OP_BAR` carries the lit-row count in the 7-bit glyph
     field, and `fb_bar()` masks it with `& 0x7F`, so a 200-row bar draws 72 rows lit (section 4.1).
  2. `fb_wait()` only waits for "FIFO not full", but three call sites use it as "engine finished"
     (CLUT reload, corner-cut LUT reload for B-349's radio-button fix, blend on). Those races are not
     closed (section 4.2).
- **One latent hazard**: a burst that crosses a 1,024-word SDRAM page hangs the engine on a read and
  wraps silently on a write. Unreachable with today's firmware (all strides are 512, all spans fit in a
  row), reachable the moment anyone sets a sticky base or stride (section 4.3).
- **The recurring timing-bug class (fixed five times) is structural**: six writers with four address
  sources feed `glyphbuf`'s one write port. A rewrite should have exactly one registered writer.
- **Recommendation**: do not rewrite Talos now. Do steps T2-0 to T2-3 in section 7 (small, each
  independently testable, the first is firmware-only). Rebuild the back end as a single row pipeline
  (section 6) only when the next opcode is planned, since every new opcode so far has cost a fit and a
  retiming cycle.

## 1a. Why Talos is 7,800 ALMs: the row buffer stopped being RAM

Read from the fit reports on the VM (`~/tau-local/*/src/fpga/output_files/ap_core.fit.rpt`,
"Fitter Resource Utilization by Entity"):

| Build | Blend | Whole design | `mp3_fb` | `glyphbuf` implemented as |
|---|---|---|---|---|
| `gamma-b316-s1` | off | 10,494 (57%) | 1,313 | MLAB (`altdpram`, 40 memory ALMs) |
| `blend-pipe-b327-s1` | on | 16,964 (92%) | 7,796 | registers: 3,084 flops, 10,784 LUTs |
| `all6-combined-s1` (alpha.11-15) | on | 17,285 (94%) | 7,830 | registers |
| `lpc-b372-s1` | on | 20,483 (111%, no fit) | 9,207 | registers |
| `clk66-b338-s2` (no `TAU_MLAB_MIGRATE`) | off | 8,722 | 3,222 | registers (not forced to MLAB) |

The map report lists `glyphbuf[64][0]`, `glyphbuf[65][7]`... as individual register bits behind 4:1
multiplexers, so the 128 x 16 buffer (2,048 bits) is a register file with a decoder and read muxes.

Cause (read from source): an MLAB is one write port plus one read port. The blend pipeline (B-327)
gives `glyphbuf` a **second read** (`bl_bg <= glyphbuf[copy_cnt]` in `A_COPYRD`, next to the existing
`glyph_q <= glyphbuf[wsrc_addr]`) and a **second write site with its own address**
(`glyphbuf[bl_i1] <= bl_r`, outside the `case`, so in the same clock as the `case` writers). No RAM
primitive has that shape, so Quartus falls back to registers. B-371 noticed "glyphbuf falls back to
M10K" on the combined build; it is actually registers, and the cost is about 6,500 ALMs.

Fix (not built, estimate): give `glyphbuf` exactly one write port and one read port per copy.
1. Merge every write (compose, copy/key/sblit/cblit reads, blend stage 2) into one registered
   `{we, waddr, wdata}` selected by state. They never write in the same cycle functionally (in blend
   mode `A_COPYRD` only feeds the pipeline, and `bl_drain` blocks other work), so this changes no
   behaviour.
2. Keep two MLAB copies written identically: one read by `wsrc_addr` (the streaming write), one read
   by `copy_cnt` (the blend's destination read). About 80 ALMs of MLAB instead of about 6,500 ALMs.
   Alternatively share one read port (the two reads are never active in the same state) and delay the
   blend's source word by one cycle.
3. Check in the map report that `glyphbuf` appears as `altdpram`/`altsyncram` again, then fit.

Expected: `mp3_fb` back to about 1,400 ALMs with blend on, the design back to about 60%, and the
TAU_LPC build fitting. This is the single largest saving available on the chip and it needs no new
feature work. Simulation must still pass the blend and mutation tests, and the reference-renderer diff.

## 2. How Talos works today (as built)

- One command FIFO (256 x 88 bits, 3 M10K, Gray-coded CDC from clk_sys 60/66.7 MHz to clk_sdram 100 MHz).
- One monolithic FSM (`astate`, 12 states). `A_IDLE` is the only dispatch point and checks, in order:
  scanout fill, pending row write, rect row, rounded-rect corner step, keyed pre-read, copy read,
  scaled-blit pixel read, CLUT pixel read, glyph row fetch, then pops a new command. Scanout always
  wins, and every unit of work returns to `A_IDLE`, so a fill waits at most one burst.
- One 128-word row buffer (`glyphbuf`, MLAB) shared by CHAR, COPY, BLIT, SBLIT, CBLIT and blend.
- Execution per row is strictly serial: fill the row buffer (compose or read), then write it. Nothing
  overlaps.
- Sticky state (`blt_*` base/stride/key/blend/reindex), the CLUT and the corner-cut LUT are read by the
  engine when a command **executes**, not when it is queued.
- Memory: one 16-bit SDRAM (AS4C32M16, 64 MB, 1 pixel per word). The controller is closed-page: every
  burst does ACTIVATE, the burst, then PRECHARGE ALL. Bank = address bits [24:23], so everything Talos
  and the CPU use (framebuffer at 0, H2 buffer at 1 M words, stash rows below 1,024) is in bank 0.

## 3. Unoptimised strategies

### 3.1 Every read pays for a refresh (read from source, arithmetic checked)

`sdram_fb.sv`: `burst_refresh_amount = refresh_counter + 1023 + 16`, compared with
`CYCLES_PER_REFRESH` = 751 at 100 MHz. 1,039 is always at least 751, so `needs_burst_refresh` is
always true, and every new `p0_rd_req` first issues an AUTO REFRESH (tRFC 80 ns, about 9 to 10 cycles)
and then the read. The check was written for agg23's full-page (1,024-word) reads, where it is correct;
here most reads are 1 to 127 words.

Who pays: scanout (360 times per frame, harmless), every CPU SDRAM-window read (this is part of the
measured ~50 cycles per access, KB-030 / A-094), and every pixel of `OP_CBLIT` and `OP_SBLIT`.

Caution before fixing: this unconditional refresh is also what keeps a second hazard latent. If a
refresh comes due in the middle of a read burst (`READ_OUTPUT`, the `needs_refresh` branch), the
controller precharges and ends the burst **without telling the client**, and `A_FILL`/`A_COPYRD` then
wait for data forever. Today every read starts right after a refresh and lasts under 751 cycles, so it
cannot happen. A fix must replace the check with one based on the requested length (reads here are at
most 512 words, about 530 cycles) or make the mid-burst refresh resume the burst, not just delete it.

### 3.2 Single-pixel transactions for CLUT and scaled blits (computed, estimate)

`OP_CBLIT` and `OP_SBLIT` issue one SDRAM read per output pixel, each a full ACTIVATE / READ / CAS /
PRECHARGE round trip through `A_IDLE`, plus the refresh from 3.1. Estimate: about 20 cycles per pixel.
A 56 x 32 meter thumbnail is about 36,000 cycles (0.36 ms) against about 4,500 for burst reads.
The choice was deliberate (keep the CLUT's M10K latency out of the shared burst loop, B-148), but a
registered CLUT read only adds pipeline depth to a burst; it does not require single-word reads.

The CLUT source plane also stores one 8-bit index per 16-bit word, so half of every source read is
unused. Two indices per word would halve source reads.

### 3.3 Serial row processing and a halved compose rate

Row N+1 is never prepared while row N is being written: there is one row buffer. For CHAR, B-157's
retiming made compose two cycles per pixel. Estimate for a 3x glyph row: about 100 cycles compose plus
about 60 cycles write, done one after the other. With two row buffers (ping-pong) the compose would hide
behind the write. Glyph drawing is not a measured problem today; this matters only if text-heavy screens
become one.

### 3.4 Keyed blits read the destination (read from source)

`A_KEYDST` reads the whole destination row so that keyed pixels can be "left alone". SDRAM writes have
a per-beat data mask (DQM, zero write latency; the skill confirms the Pocket's SDRAM exposes byte DQM).
Masking the keyed beats during the streaming write gives the same result with no destination read.
Today streaming writes force `DQM = 00` on every beat. Adding one mask bit per row-buffer word (17 bits
wide) and driving `DQM = {m, m}` per beat removes the pre-read for key-only blits. Blend still needs it.

### 3.5 Scanout reads 512 words per line to show 400 (read from source)

`A_FILL` ends the burst at `fill_cnt == STRIDE - 1`. 112 unused words per line, 40,320 per frame,
about 2.4% of the bus. Ending at `H_ACT - 1` is a one-constant change (check the stop-count alignment
in simulation, as for any burst-length change).

### 3.6 No bank overlap (read from source)

Closed-page with all activity in bank 0 means ACTIVATE and PRECHARGE are never hidden behind another
burst. A bijective address swizzle (for example, bank = row bits [1:0]) is transparent to every client,
but it only helps once the controller can activate the next bank during a current burst. That is a real
controller rewrite, lowest value per effort of the list.

## 4. Correctness issues

### 4.1 Live: `OP_BAR` lit count is 7 bits; fullscreen bars are 323 tall

RTL: `cmd_glyph` is 7 bits, reused as the lit-row count, clamped to `cmd_h`. Firmware: `fb_bar()` sends
`(lit & 0x7F) << 3`. Player-screen meter height is 122, so it fits. Fullscreen Winamp Bars call
`wviz_bars_tick()` with height `FS_FIG_H` = 323, so any bar above 127 rows wraps (200 draws 72, 128
draws 0). Firmware fix now: when `lit > 127`, draw with `fb_rect()` pairs, or split the bar into two
`fb_bar()` calls stacked vertically. RTL fix later: take the lit count from a wider field (`cmd_w` has
9 bits but is already the width; a sticky field or a spare FIFO bit would do). Not yet confirmed on a
Pocket; it should show as bars that drop sharply once they pass about 40% of the fullscreen height.

### 4.2 Live: `fb_wait()` is not a drain

`R_FB_GO` reads back `fb_cmd_full` only. Three places use `fb_wait()` as if it meant "every earlier
command has executed":

- `fb_clut_load()`: its comment says the CLUT "must not be reloaded while an earlier OP_CBLIT command is
  still draining", and `fb_wait()` does not ensure that.
- `fb_round_rect_on()` (B-349's radio-button fix): the added `fb_wait()` before `rc_lut_ensure()`
  returns immediately unless the FIFO holds 255 commands, so the reported race is very likely still
  open. `rc_cut_lut` is also an 80-bit bus read in clk_sdram without a synchroniser, so a reload during
  a corner sequence can be seen half-written.
- `fb_blend_on()`: harmless only because `fb_fence()` is used before `fb_blend_off()`, and that fence
  costs up to 50 ms of mailbox polling.

Fix options, cheapest first: (a) add an "engine idle" status bit (FIFO empty, `!engine_busy`, `A_IDLE`,
no fill in flight) to `R_FB_GO` bit 1 through a 2-flop synchroniser, and add `fb_drain()`; (b) better,
send table and sticky writes **through the command FIFO** as their own opcodes, so they are ordered
with the draws and never need a wait. (b) is what the rewrite in section 6 does.

### 4.3 Latent: bursts that cross a 1,024-word page

The controller stops a read burst by itself before column 1,023 and does not tell the client, so a
source row that crosses a page boundary leaves `A_COPYRD` waiting forever, and because `A_IDLE` is
never reached, scanout stops too (frozen screen). A write burst crossing a page wraps to column 0 of the
same row and silently overwrites it. With stride 512 and `x + w <= 512` no span crosses a page, which is
true of every current caller (sticky base/stride are never changed from 0/512 in firmware). The RTL
accepts any 25-bit base and 10-bit stride, so this is one firmware change away. Either split at page
boundaries in the engine's address generator or refuse such commands; document it in `docs/TALOS.md`
either way.

### 4.4 Design gaps (not bugs yet)

- Double buffering (H2) offsets only non-blit opcodes. `OP_BLIT`/`OP_SBLIT`/`OP_CBLIT` always write to
  their sticky base (default 0 = buffer 0). With H2 on, firmware would have to rewrite `DST_BASE` at
  every flip or blits land in the front buffer. A single "target surface" register applied to every
  opcode is cleaner.
- Width limit 127 (7-bit `char_w`, 128-entry row buffer) is enforced in firmware for COPY only;
  `fb_cblit()`, `fb_blit()` and `fb_sblit()` rely on callers.
- No hardware clip rectangle; a bad coordinate can overwrite the off-screen stash (art, thumbnails,
  Chladni plane, probe cells).
- Stale comments: the glyph buffer "64 entries", "EPX doubles 8x8", "max 64x64" describe the pre-Inter
  engine.

## 5. The three external suggestions, checked

| Claim | Verdict for Tau on the Pocket |
|---|---|
| 49K LE Cyclone V | Correct. Exact figure: 18,480 ALMs (third suggestion right, second's "19,600" is a rough LE/2.5). 308 M10K + MLAB, 66 DSP. |
| Blitter 500-3,000 ALMs, 0-2 DSP, 2-8 M10K | Plausible range for a clean design: Talos is 1,313 ALMs without blend (measured). With blend it is 7,800 because the row buffer fell out of RAM (section 1a), and logic is now the binding constraint (94%). |
| Barrel shifter / sub-word alignment, bitwise ROPs | Not applicable: 1 pixel per 16-bit word. The project already concluded this for B3. |
| Avalon/AXI master, HPS, Nios V | Not applicable: 5CEBA4 is a Cyclone V E (no HPS), no Platform Designer here, the CPU is VexRiscv. |
| 8-bit indexed framebuffer | Reject for this UI. Anti-aliased text blends in colour space, the background is a gradient, covers are photos. The bandwidth it saves is not needed: scanout is about 11 to 13% of the bus. |
| Framebuffer in the 256 KB SRAM | Does not fit: 400 x 360 x 2 = 288,000 bytes > 262,144. |
| Transparency with byte write masks instead of destination reads | Right idea, at word granularity (section 3.4). The best idea in the three. |
| Burst reads and writes, decoupled through buffers; count stalls | Right. Talos bursts its writes and COPY reads but not CBLIT/SBLIT reads, and does not decouple (one buffer). The busy counter (B7) already exists. |
| Put assets and framebuffer in independent memories | Only real parallelism available. PSRAM is independent of SDRAM, but it serves CPU instruction fetch and cold code, so blit reads there would compete with the decoder. Parked. |
| Multiple pixel lanes | No: the bus moves one word per cycle; a 1-pixel-per-cycle pipeline already matches it. |
| Affine rotation, filtering | No consumer in this UI. |
| Tile/sprite compositor at scanout | Would bypass the framebuffer for layers, but the UI is mostly static redraws; not worth the M10K. |

## 6. What a reimplementation ("Talos 2") would look like

Same outside contract: same MMIO registers, same FIFO CDC, same "scanout always wins, preempt between
bursts", same opcodes (so firmware and `tools/host/blit_reference.py` stay the golden model).
Different inside:

```
FIFO -> Front end ---------------> Row jobs -> Back end (one pipeline, one writer)
        decode, clip, split at                   SRC:  none | const | burst read | font rows
        page and 128 words,                      OP:   key->mask | CLUT (pipelined M10K) | AA | blend
        rect/bar/rrect/copy all                  DST:  streaming burst write with per-beat mask
        become "row jobs",                       two row buffers (ping-pong, MLAB, 17 bits wide)
        every value registered                   exactly one registered write into each buffer
```

1. **Front end turns every command into row jobs** `{dst, width, src kind, src addr, pixel op, colour}`.
   RECT, BAR, RRECT corners, COPY, BLIT, CBLIT, SBLIT and CHAR all become sequences of these. All the
   arithmetic that has been retimed five times (bar split, scaled extent, glyph base, corner cut and
   address) lives here, one value per register per cycle, off the dispatch path by construction.
2. **Width splitting and page splitting in hardware**: firmware no longer needs `FB_COPY_MAX`, and the
   page hazard in 4.3 disappears. Clipping against a sticky clip rectangle goes here too.
3. **Back end is one pipeline**: source stage fills buffer A while the writer drains buffer B. Every
   pixel operation writes the buffer from one registered stage (address, data, mask), which removes the
   shared write-mux timing problem structurally instead of case by case.
4. **CLUT and scaled blits use burst reads** into the row buffer; the CLUT lookup is a pipeline stage.
   Scaled blits read the source row once and resample from the buffer.
5. **Keying is a mask bit**, written with DQM; no destination pre-read except for blend.
6. **State and tables go through the FIFO** (set field, load CLUT entry, load cut entry, fence), so
   ordering is guaranteed by the queue. A readable idle bit remains for the CPU.
7. **One target-surface register** applies the H2 back-buffer offset to every opcode.
8. Controller changes in parallel: length-based refresh check, fill ends at 400, DQM per beat on
   streaming writes. Bank overlap only if measurements later show it matters.

Budget estimate (not synthesised): about the no-blend figure (1,300 to 1,800 ALMs) even with blend, because every buffer keeps one write port by construction, 0 extra M10K if
the row buffers stay in MLAB (2 x 128 x 17 bits), and likely easier timing because the dispatch
priority chain and the six-writer mux both go away. The one real cost is verification: every opcode
must match the reference renderer again, and it is a new fit.

## 7. Recommended order

**Superseded by `docs/features/TALOS2_REIMPLEMENTATION_PLAN.md`** (phases P0-P4, scheduled after the FLAC LPC work). The table below is kept as the review's original reasoning.

| Step | What | Where | Risk |
|---|---|---|---|
| T2-00 | Row buffer back into MLAB with blend on (section 1a): about 6,500 ALMs saved | `mp3_fb.sv` + fit | low: same behaviour, same tests |
| T2-0 | `fb_bar()` split above 127 lit rows; guard width in `fb_cblit/fb_blit/fb_sblit`; document the page rule | firmware + docs | none, no fit |
| T2-1 | Engine-idle status bit + `fb_drain()`; use it in `fb_clut_load`, `fb_round_rect_on`, blend (replaces the 50 ms fence) | small RTL + firmware | low; old bitstream needs a probe fallback |
| T2-2 | Length-based refresh check (with the mid-burst refresh made safe); fill stops at 400 | `sdram_fb.sv` | medium: touches every SDRAM client; measure CPU-window cost before/after with the existing A-094 probe |
| T2-3 | Burst source reads for CBLIT/SBLIT; DQM keying | `mp3_fb.sv` + controller | medium: new fit, reference renderer diff |
| T2-4 | Row-job front end + single-writer pipeline (section 6) | rewrite | high; only when the next opcode is due |

T2-0 and T2-1 fix real bugs. T2-2 and T2-3 are performance work with no measured user-visible problem
behind them today, so they should be scheduled against the audio and UI roadmap, not ahead of it.
