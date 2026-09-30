# MMIO allocation (mp3_soc, 0x8000_0000 page)

Single table for every memory-mapped register, so a new block never picks an
offset ad hoc (roadmap Phase B4). Source of truth is `src/fpga/core/mp3_soc.v`
(`R_*` localparams) and, for the expansion window, `tau_psram_probe.sv`.

The decoder maps a 4 KiB page (`0x8000_0000..0x8000_0FFF`), but `mp3_soc` decodes
only 8 bits of offset (`mmio_reg`, 64 registers, 4-byte stride); offsets at
`0x100` and above **alias** onto `0x00..0xFC`. Do not widen the page.

| Offset | Name | Dir | Owner / use |
|---|---|---|---|
| 0x00 | CONSOLE | W | console character |
| 0x04 | EXIT | W | sim exit / halt marker |
| 0x08 | AUDIO | W | audio sample push |
| 0x0C | CYCLES | R | free-running clk_sys counter |
| 0x10-0x1C | STAT0-3 | W | status words shown by the video block |
| 0x20-0x30 | TGT_* | W/R | APF target command (id, offset, addr, len, go/status) |
| 0x34 | PCM_ST | R/W | PCM FIFO status (write = flush) |
| 0x38 | PCM_RATE | W | PCM rate increment |
| 0x3C | INPUT | R | menu + controller keys |
| 0x40 | VERSION | R | RTL interlock (`CORE_VERSION`) |
| 0x44 | RELOAD | R | slot reload flags |
| 0x48-0x58 | FB_* | W/R | framebuffer draw engine |
| 0x5C | SLOT_SZ | R | slot size |
| 0x60-0x64 | DT_ADDR/DT_DATA | W/R | datatable |
| 0x68 | EQ | R/W | EQ preset |
| 0x6C-0x70 | SET_IDX/SET_DAT | W/R | persisted settings words (interact.json) |
| 0x74-0x84 | SDR_* | W/R | Phase 1 SDRAM mailbox |
| **0x88-0xAC** | **expansion window** | | **PSRAM diagnostic mailbox (B-004) and window counter (B-016), routed through the `xm_*` port** |
| 0xB0 | IF_N | R | Phase G2: instruction beats served from PSRAM (0 when `PSRAM_IFETCH_ENABLE` is off) |
| 0xB4 | IF_CYC | R | Phase G2: cycles the instruction fetch stage held a PSRAM request |
| 0xB8 | IF_CFG | R/W | Phase G2: bit0 = instruction fetch from PSRAM present; any write clears IF_N and IF_CYC |
| 0xBC | SDR_BUSY | R | Phase F B7: SDRAM port-busy cycles, free-running since reset (0 when `TAU_SDRAM_BUSY` is off). Counts clk_sdram cycles the single SDRAM controller port is occupied by either master (framebuffer or CPU), regardless of which -- the resource any future SDRAM client (the blit engine) would compete for. Crosses from clk_sdram via a Gray-coded CDC (`tau_cdc_gray_ctr.sv`); never cleared by firmware -- sample before/after a measurement window and take the delta, same convention as CYCLES (0x0C). |
| 0xC0 | BLT_IDX | W | Phase F B1/B2/B5/B9 (section 9): selects a sticky blit-state field (0=SRC_BASE, 1=SRC_STRIDE, 2=DST_BASE, 3=DST_STRIDE, 4=KEY: bit16=enable, bits[15:0]=RGB565 colour, 5=BLEND: bit0=enable, bits[3:1]=mode -- 0=DSP 0-255 alpha, 1=PSX B/2+F/2, 2=PSX B+F clamp, 3=PSX B-F clamp, 4=PSX B+F/4 clamp -- bits[15:8]=alpha level, DSP mode only, 6=REINDEX: bits[7:0]=offset added to `OP_CBLIT`'s palette index before the CLUT lookup, 0=no-op). Read only when `TAU_BLIT` is built; the BLEND field is additionally gated on the separate `TAU_BLIT_BLEND` macro (see section 10 -- kept droppable on its own, the documented timing-cliff risk). 0 otherwise. |
| 0xC4 | BLT_DATA | W | Writes the field BLT_IDX selects, then auto-increments BLT_IDX (wraps 6->0) -- a burst of 7 writes loads the whole state after one index write. `FB_GO`'s existing opcode field (now 3 bits, was 2) carries `OP_BLIT`/`OP_BAR`/`OP_SBLIT`/`OP_CBLIT`; no new GO register. |
| 0xC8 | CLUT_IDX | W | Phase F B8 ("B8 detailed design", section 5): selects one of 256 CLUT entries (0-255). Read only when `TAU_BLIT` is built; the CLUT itself is inert (never read) unless `OP_CBLIT` is dispatched. |
| 0xCC | CLUT_DATA | W | Writes the RGB565 value at `CLUT_IDX`, then auto-increments `CLUT_IDX` (wraps 255->0, natural 8-bit rollover) -- a burst of 256 writes loads the whole palette after one index write. `FB_GO`'s opcode field carries the new `OP_CBLIT` (3'd7, the last value the existing 3-bit field has room for); no new GO register. |
| 0xD0 | VBLANK | R | Helios/Talos H0 (B-260: bits 31:16 are now a free-running frame counter from `tau_vs_counter.sv`, because the pulse is ~167 us and cannot be polled) -- (`docs/features/HELIOS_SPEC.md` section 9): bit 0 = vblank status, CDC'd from `mp3_fb.sv`'s own `vid_vs_w` (clk_vid) into clk_sys via a plain single-bit synchroniser (`tau_cdc_sync1.sv`, NOT the Gray-code technique `tau_cdc_gray_ctr.sv` uses for multi-bit counters -- a single level has no multi-bit-hazard to guard against). Reads 0 when `TAU_VBLANK` is off. |
| 0xD4 | RC_IDX | W | B11 (`OP_RRECT`): selects one of 16 corner-cut LUT entries. (Was missing from this table.) |
| 0xD8 | RC_DATA | W | B11: writes the 5-bit cut(dy) at `RC_IDX`, auto-increments. (Was missing from this table.) |
| 0xDC | SPEC_IDX | W | B-263 spectrum filter bank (`tau_spec_bank.sv`, `TAU_SPEC`): band index 0..15 whose window mean `SPEC_DATA` returns. |
| 0xE0 | SPEC_DATA | R | 20-bit mean \|band\| over the last completed 1024-sample window for band `SPEC_IDX` (stage `o` saw `1024>>o` samples, so mean = acc >> (10-o)). 0 when `TAU_SPEC` is off. |
| 0xE4 | SPEC_ST | R | bit 0 = the bank is built in; bits 31:16 = windows completed (increments every 1024 samples). Read it before and after reading the 16 means and retry if it changed. 0 when `TAU_SPEC` is off. |
| 0xE8 | SCAN | R | Helios beam position (B-267, `TAU_BEAM`): bit 9 = present, bits 8:0 = the video line counter `vc` (0..399), carried clk_vid -> clk_sys in Gray code by `tau_cdc_gray_bus.sv`. The row being scanned is `vc - 4` while `4 <= vc < 364`; rows are prefetched one line ahead. May read a wrong value for about one sample around the 399 -> 0 wrap (inside vertical blanking, where every draw is safe). 0 when `TAU_BEAM` is off. |
| 0xEC | WAVE_CTL | W | B-283 level/scope block (`tau_wave_meter.sv`, `TAU_WAVE`): bit 0 clears the peaks, bit 1 arms a scope capture (waits for a rising zero crossing of the mono mix, or gives up after 2048 samples and captures anyway), bits 11:8 = samples per column - 1. |
| 0xF0 | WAVE_IDX | W | scope column 0..255 presented at `WAVE_DATA` (read the data at least 3 clocks later; the CPU always is). |
| 0xF4 | WAVE_DATA | R | `{min[31:16], max[15:0]}`, signed 16-bit each, of the selected column of the last completed capture (the min/max envelope of the mono mix over the column's samples). 0 when `TAU_WAVE` is off. |
| 0xF8 | WAVE_PK | R | `{max\|R\| [31:16], max\|L\| [15:0]}` since the last clear; free-running. 0 when off. |
| 0xFC | WAVE_ST | R | bit 0 = the block is built in, bit 1 = a capture is running, bit 2 = the last capture's trigger timed out, bits 15:8 = capture count. 0 when off. |
| 0x100 | POLY_CTL | W | B-292 MP3 window unit (`tau_mp3_poly.sv`, `TAU_POLY`): bit 0 = clear the history (writes zeros over the 1,024-word array, busy for 1,024 clocks), bit 1 = go (compute the pending slot, both channels). |
| 0x104 | POLY_PUSH | W | one FDCT32 output word in push order (P0 = sample 0, P1..16 = samples 16..31, P17..31 = samples 15..1); 64 per slot: channel 0's 32, then channel 1's. Ignored while busy. |
| 0x108 | POLY_IDX | W | PCM word 0..31 to present at `POLY_OUT`. |
| 0x10C | POLY_OUT | R | `{R sample [31:16], L sample [15:0]}` of the last computed slot (Helix's interleave). 0 when off. |
| 0x110 | POLY_ST | R | bit 0 = built in, bit 1 = busy, bits 31:16 = slots computed. 0 when off. |
| 0x114 | TEXT_MODE | R/W | Theme/gamma: write bit 0 = 1 selects the light-polarity text weight table (`cov_weight_light`, dark text on a light ramp); read bit 31 = the bitstream has it (0 on older ones), bit 0 = current value. Synchronised into clk_sdram (`tau_cdc_sync1`). |
| 0x118 | DBUF_CPU | R/W | Helios H2 (B-340, `tau_fb.v`/mp3_soc.v `TAU_DBUF`): bit 0 = which physical copy of the VISIBLE framebuffer (row < 360, word address < 184,320) ordinary RECT/CHAR/COPY/BAR/RRECT (non-independently-addressed) commands read and write; buffer 1 sits 1,048,576 words above buffer 0. **Corrected 2026-09-29 (B-414):** only BLIT/SBLIT/CBLIT genuinely use independent addressing through the sticky `blt_*_base` fields and are unaffected by this bit -- BAR and RRECT both address through the same `R_FB_ADDR`/`cmd_addr` path as RECT/CHAR (confirmed from `fb_bar()`/`fb_rrect()`'s own source, both write `REG(R_FB_ADDR)` directly), so they ARE covered by this bit; the previous wording grouping them with the true blit-mode opcodes was wrong and caused a real hardware bug to go undiagnosed longer than necessary (Chladni, which genuinely does use `fb_sblit()`/`fb_blit()`, B-414). Any address >= 184,320 (the off-screen stash region) is unaffected regardless of this bit. Read: bit 31 present (0 on a bitstream without it), bit 0 echo. |
| 0x11C | DBUF_DISP | R/W | Write: bit 0 = 1 requests a flip, applied only at the next vertical blanking (never mid-frame). Read: bit 31 present, bit 1 = a requested flip is still pending, bit 0 = which buffer is currently displayed (what the scanout prefetch reads). |
| 0x120 | LPC_CFG | W | B-368 FLAC LPC unit (`tau_flac_lpc.sv`, `TAU_LPC`, `docs/research/FLAC_LPC_KERNEL_DESIGN.md` section 5): bits[5:0] = predictor order (1-32), bits[11:6] = right-shift amount (0-31). Written once per subframe; resets the coefficient/warm-up index pointers to 0. |
| 0x124 | LPC_COEF_IDX | W | selects one of 32 coefficient slots (0 = the tap paired with the MOST RECENT sample, `fw/flac.c`'s own `coef[0]` convention). |
| 0x128 | LPC_COEF_DATA | W | writes the signed 16-bit coefficient at `LPC_COEF_IDX`, then auto-increments the index -- a burst of `order` writes loads the whole coefficient set after one index write (same convention as `R_CLUT_IDX`/`DATA`, `R_RC_IDX`/`DATA`). |
| 0x12C | LPC_WARM_IDX | W | selects one of 32 warm-up/history slots, same index convention as `LPC_COEF_IDX`. |
| 0x130 | LPC_WARM_DATA | W | writes the signed 32-bit warm-up sample at `LPC_WARM_IDX`, then auto-increments the index. |
| 0x134 | LPC_RESIDUAL | W | writes the next signed 32-bit residual and starts one reconstruction (MAC over `order` taps, one multiply-add per clock, then a registered shift and add -- never chained combinationally, this project's own timing rule). |
| 0x138 | LPC_SAMPLE | R | the last reconstructed sample. **Reading this register is itself the acknowledgement** that lets the unit accept the next residual (wired straight to the bus's one-cycle read-request pulse, not a separate write-to-ack step) -- poll `LPC_STATUS` for done first. 0 when off. |
| 0x13C | LPC_STATUS | R | bit 0 = built in, bit 1 = busy, bit 2 = done (the sample at `LPC_SAMPLE` is ready and unread). 0 when off. |
| 0x140 | I2S_DIAG_MINMAX | R | B-467 (Cymo 44.1 kHz investigation): bits[15:0] = min interval ever (clk_sys cycles, saturates at 0xFFFF), bits[31:16] = max interval ever (saturates), between successive real changes of the I2S DAC-domain sample word, measured after crossing `sound_i2s.v`'s clk_audio->clk_mclk CDC and back into `clk` via `tau_cdc_sync1`. Free-running since reset, never cleared. 0 when `I2S_DIAG_ENABLE` is 0. |
| 0x144 | I2S_DIAG_CNT | R | count of update events since reset. 0 when off. |
| 0x148 | I2S_DIAG_SUM | R | sum of all measured intervals since reset (for an average via delta / delta-count). 0 when off. |
| 0x14C | I2S_DIAG_ST | R | bit 0 = built in (`I2S_DIAG_ENABLE`). |
| 0x150-0x1FC | free | | B-287 widened the decode (0x100 upward is open); 0x00-0xFF is full. |

## Expansion window 0x88-0xAC (`TAU_PSRAM_PROBE`)

| Offset | Name | Dir | Fields |
|---|---|---|---|
| 0x88 | PS_ID | R | 0x50535231 ("PSR1"); reads 0 when no probe is built in |
| 0x8C | PS_ADDR | RW | [22:0] CPU word offset ([22] chip, [21] die) |
| 0x90 | PS_WDATA | RW | write data |
| 0x94 | PS_CTRL | W | [0] REQ, [1] WE, [5:2] byte enables, [6] CLR |
| 0x98 | PS_STATUS | R | [0] BUSY [1] DONE [2] TIMEOUT [3] GUARD [4] CE_CONFLICT [5] GUARD_HIT [6] WAIT_LO [7] WAIT_HI [15:8] op counter |
| 0x9C | PS_RDATA | R | response, loaded at DONE |
| 0xA0 | PS_CFG | RW | [3:0] extra read clocks, [7:4] extra write clocks; [15:8] read-only build read-sample index `T_ACC` (B-012); [16] read-only: CPU window present (B-016) |
| 0xA4 | PS_LAST | R | last accepted request (word, WE, byte enables) |
| 0xA8 | PS_COUNT | R | completed mailbox ops |
| 0xAC | PS_WCOUNT | R | completed CPU-window ops (B-016) |

Rules: firmware waits for `BUSY = 0` (it stays set during the 200 us power-up
hold-off), polls with a timeout, never uses fixed delays. Without the macro the
window reads as zero and writes are ignored.

`CORE_VERSION` (0x40) was **not** bumped for this change: the window is additive
and inert without the macro, so every existing ROM/RBF pairing stays valid. The
PSRAM ROM checks `PS_ID` itself. Bump `CORE_VERSION` only if an existing register's
meaning changes.

## PSRAM CPU window (`TAU_PSRAM_WINDOW`, B-016) - not MMIO, a data window

`0xA400_0000..0xA5FF_FFFF` (32 MiB, uncached because address bit 31 is set; CPU-word `0x29000000..0x297FFFFF`), directly after the SDRAM window
(`0xA010_0000..0xA3FF_FFFF`). CPU word offset within the window: `[22]` chip, `[21]` die, `[20:0]` word (die = 8 MiB). Classic Wishbone only (no bursts); loads,
stores and sub-word stores work (byte enables reach the chip's LB#/UB#). The last CPU word of each 8 MiB die (`0x7FFFFC` + die base) is the guard: reads return 0,
stores are ignored, the mailbox STATUS `GUARD_HIT` sticky bit sets, and the access ACKs (no bus error, because the firmware has no exception handler). There is
no cached alias: `0x2400_0000..` and every other unmapped range still terminate as a bus error. Needs the SDRAM Phase 2 decode (`TAU_PHASE2_WINDOW`) and,
for now, the probe module that owns the controller (`TAU_PSRAM_PROBE`). The mailbox and the window share the controller (window has priority when both wait).

