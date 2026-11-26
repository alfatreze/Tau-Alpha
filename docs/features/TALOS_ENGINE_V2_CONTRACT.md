# Talos engine v2: behavioural contract (v0.1, draft for review X1)

**Status:** draft v0.1 for review, 2026-11-26. Documents only: no RTL, no firmware, no fit. Audit entry B-1178.
**Decisions honoured:** D-T18 (every command is kept), D-T19 and D-T22 (Gallery's features become build-parameter features, named sets, escape opcode, viewer registers above `0x200`, capability bits, `CORE_VERSION` namespace), D-T20 (no inherited text in what ships), D-T21 (regression definition R1 to R10, SBLIT 127-row clamp is the contract, derived dark text table, improvements behind capability bits), D-T12 (no invented performance need), D-T07 and D-P01 (clean room, behaviour only, no copyleft).
**Who reads it:** the clean-room author (with the vectors and the interface package, see `TALOS_ENGINE_V2_CONFORMANCE_PLAN.md` section 9), reviewer X1 (who has read neither the old engine nor the scoping documents), the owner. The author must not read the files listed in `TALOS_ENGINE_V2_CLEANROOM_PROCESS.md` section 3 and 8 (the old engine, the oracle, the scoping report and its two companions, Gallery's engine). This file is the behaviour; where it is silent the author asks, it does not guess.
**Location note:** the process document names `docs/research/` for this file; the brief placed it in `docs/features/` next to the scoping documents. Same content either way.

**How the rules are tagged.** Every rule has an identifier (C-...) and one tag:
* **PINNED [artefact]**: a public artefact or a shipped output depends on it; the new engine must reproduce it exactly. The artefact is named.
* **MATCH**: nothing public depends on it, but the shipped engine does it and v0.1 asks for the same result so that the classified differential stays in its IDENTICAL class. A different result needs an owner-approved class (`docs/DECISIONS.md`) before it is accepted.
* **FREE**: an implementation artefact of the old engine, free to differ. The tests check only the stated property (for example "retires, writes nothing outside the rectangle"), never the old result.
* **CHANGE [class, decision]**: the new engine must differ from the shipped engine, by an approved decision; the differential classifies the difference.
* **UNCONFIRMED**: what the shipped engine does is not established; the cheapest experiment that settles it is given. Where a short experiment could be run, it was run (`sim/tb_talos_v2_contract_probe.v`, results in B-1178 and quoted as "probe").

Units: a **word** is 16 bits, one RGB565 pixel. A **word address** is 25 bits (the 64 MiB SDRAM as 32 Mi words). Channel values: red `R = word[15:11]` (0..31), green `G = word[10:5]` (0..63), blue `B = word[4:0]` (0..31). `floor` is integer division rounding down; all arithmetic in this file is exact integer arithmetic unless a width is stated.

---

## 1. Scope, parameters, named sets, capability bits

### 1.1 Role
The engine takes drawing commands from the CPU through a queue, executes them one after another against the SDRAM framebuffer memory through one memory master port, serves the scan-out's line fills ahead of any drawing, and reports idle, fence, queue and error counters to the CPU. It holds the colour palette (CLUT), reads a glyph memory (font) and a 16-entry corner table, and follows the double-buffer selection.

### 1.2 In and out of scope
* **In:** the module that replaces `tau_talos` (same module name, selected at build time; the old file stays as the oracle until a Pocket run, D-T01), its port list (section 2.1), the meaning of every CPU register that reaches it (section 2.2, the registers themselves live in the SoC, which is rewritten separately under D-T20 item 3), the commands (section 3), status (section 4), reset (section 5), the engine half of the viewer (section 6), performance (section 7).
* **Out:** the scan-out (`tau_scanout`, its own contract `docs/research/TALOS_SCANOUT_CONTRACT.md`), the viewer scan-out and mode sequencer (second step, `TALOS_SCANOUT_VIEWER_CONTRACT.md`, to be written, imported by hash per D-T22), the SDRAM scheduler and PHY (`docs/features/SDRAM_CONTROLLER_CONTRACT.md`), the font RAM and font ROM modules (black boxes, `font_ram.v`, `font_rom.v`, port lists only), the Gray counter module `tau_cdc_gray_ctr` (reused as is).

### 1.3 Build parameters
All are elaboration-time constants. A feature that is off must leave no logic, no register and no memory behind (proved at map level, conformance plan section 6). Illegal combinations stop elaboration with an error.

| Parameter | Default | Meaning | Rule |
|---|---|---|---|
| `BLIT_BLEND_ENABLE` | 0 | the blend of BLIT (section 3.6) | name kept: `tau_gfx` passes it (shipped: 1) |
| `DBUF_ENABLE` | 0 | double buffering (section 3.14) | shipped: 1 |
| `FONT_RAM_ENABLE` | 0 | glyphs from the CPU-written font RAM (Latin-1); 0 = the fixed font ROM (ASCII) | shipped: 1 |
| `FILL_WORDS` | 400 | words per scan-out fill | 400 shipped; 512 is accepted for the old A/B only |
| `CBLIT_BURST` | 1 | accepted and ignored (**C-PAR-1 FREE**: how a CBLIT row is read is not contractual) | kept only so `tau_gfx` elaborates unchanged |
| `FEAT_CORNERS` | 0 | opcode 10 | |
| `FEAT_DSBLIT` | 0 | opcode 11 | |
| `FEAT_ESC` | 0 | opcode 15 with its sub-opcode field (section 3.13) | |
| `VIEW_ENABLE` | 0 | viewer fill (section 6) | needs the 40 MHz video build (`TAU_VID40`) in the SoC; error otherwise |
| `VIEW_DBL_EN` | 0 | pixel doubler line select (section 6.3) | requires `VIEW_ENABLE` |
| `VIEW_LINE_WORDS` | 1200 | words per viewer plane line | a multiple of `FILL_WORDS`; 1200 is Gallery's RGB888 plane |
| `TEXT_W_DARK` | the table of section 3.4 | 16 weights, 5 bits each | data, section 3.4 |
| `TEXT_W_LIGHT` | the table of section 3.4 | 16 weights, 5 bits each | data, section 3.4 |

Mutation hooks of the old engine (`BUG_*`) are not part of the interface; the new engine brings its own (conformance plan section 5).

### 1.4 Named feature sets (only these are fitted and supported, D-T22)
| Set | CORNERS | DSBLIT | ESC | VIEW | VIEW_DBL | Users |
|---|---|---|---|---|---|---|
| TAU | 0 | 0 | 0 | 0 | 0 | Tau Alpha Player 2 |
| V | 0 | 0 | 0 | 0 | 0 | Tau V (first import) |
| GALLERY | 1 | 1 | 0 | 1 | 1 | Tau Gallery 1.0 |
The other combinations elaborate (the simulation matrix uses eight, conformance plan section 6) but are not fitted.

### 1.5 Capability bits (`R_GFX_STATUS[30:17]`, read only, constants from the parameters)
| Bit | Meaning |
|---|---|
| 17 | `FEAT_CORNERS` built |
| 18 | `FEAT_DSBLIT` built |
| 19 | `FEAT_ESC` built |
| 20 | `VIEW_ENABLE` built (the viewer register block of section 2.2.3 exists) |
| 21 | `VIEW_DBL_EN` built |
| 22..30 | reserved, read 0 (proposed later: wide blit-class rows, overlap-safe copy; open question Q4) |
The engine exposes them on a new output `gfx_caps[13:0]` (bit 0 = status bit 17). In the TAU and V sets all are 0, so `R_GFX_STATUS` reads exactly as today. **Collision check:** the shipped SoC returns 0 in bits 30:17; the only readers of `R_GFX_STATUS` found (Tau Alpha `fw2/gfx/gfx.c`, Gallery `fw2/gfx/gfx.c` at `26d9b1c`) test bit 31 only; nothing in `fw2`, `sdk`, `tools` or `sim` reads bits 30:17 (static search, 2026-11-26). Tau V, Tau Μ and Tau Reader have no T3 status block.

---

## 2. Interfaces

### 2.1 Module ports (drop-in for `tau_talos`; names, widths and domains fixed)
| Port | Dir | Width | Domain | Meaning |
|---|---|---|---|---|
| `reset` | in | 1 | treated as synchronous in each domain | section 5 |
| `clk_sys` | in | 1 | CPU clock (66.667 MHz shipped) | queue write side, status outputs, CLUT and font write ports |
| `clk_sdram` | in | 1 | 100 MHz | everything that executes |
| `clk_vid` | in | 1 | video clock | may be unused inside the engine |
| `cmd_push` | in | 1 | clk_sys | one cycle: append the command on the `cmd_*` inputs |
| `cmd_op` | in | 4 | clk_sys | opcode |
| `cmd_addr` | in | 19 | clk_sys | destination word offset |
| `cmd_w`, `cmd_h` | in | 9, 9 | clk_sys | width, height |
| `cmd_fg`, `cmd_bg` | in | 16, 16 | clk_sys | colours, or source offset (blit class) |
| `cmd_glyph` | in | 7 | clk_sys | glyph code low bits / lit count low bits / radius |
| `cmd_sx`, `cmd_sy` | in | 2, 2 | clk_sys | scale or ratio selectors |
| `cmd_glyph_hi` | in | 2 | clk_sys | glyph code bit 7 (bit 0) / lit count bits 8:7 |
| `cmd_subop` (new, `FEAT_ESC`) | in | 6 | clk_sys | escape sub-opcode, ignored when `FEAT_ESC` = 0 |
| `cmd_full` | out | 1 | clk_sys | the queue cannot accept a push (section 2.3) |
| `font_we`, `font_waddr[12:0]`, `font_wdata[31:0]` | in | | clk_sys | font RAM write port (ignored when `FONT_RAM_ENABLE` = 0) |
| `text_light` | in | 1 | **already in clk_sdram** | weight table select, quasi-static |
| `blt_src_base[24:0]`, `blt_src_stride[9:0]`, `blt_dst_base[24:0]`, `blt_dst_stride[9:0]` | in | | clk_sys level, quasi-static | sticky blit addressing (section 3.2) |
| `blt_key_en`, `blt_key[15:0]` | in | | quasi-static | colour key |
| `blt_blend_en`, `blt_blend_mode[2:0]`, `blt_blend_alpha[7:0]` | in | | quasi-static | blend |
| `blt_reindex[7:0]` | in | | quasi-static | CLUT index offset |
| `rc_cut_lut[79:0]` | in | | quasi-static | corner table, entry `i` = bits `[5i+4:5i]` |
| `clut_wr`, `clut_waddr[7:0]`, `clut_wdata[15:0]` | in | | clk_sys | CLUT write port |
| `dbuf_cpu_buf` | in | 1 | **already in clk_sdram** | CPU drawing buffer, quasi-static |
| `dbuf_flip_req_tgl` | in | 1 | clk_sys toggle | each change requests one flip |
| `dbuf_disp_buf`, `dbuf_flip_pending` | out | 1, 1 | clk_sdram level (read informally by the SoC) | displayed buffer, flip pending |
| `sdram_init_complete` | in | 1 | clk_sdram | the memory may be used |
| `p0_*`, `wsrc_addr`, `wsrc_q`, `p0_is_fill`, `p0_rd_len` | | | clk_sdram | memory master port, section 2.4 |
| `fill_req_tgl`, `fill_line_req[9:0]`, `vblank_tgl` | in | | clk_vid toggles, line stable while the toggle is seen | scan-out handshake, section 2.6 |
| `lb_we`, `lb_waddr[9:0]`, `lb_wdata[15:0]` | out | | clk_sdram | line-buffer write port |
| `fill_done_inc`, `fill_done_line[9:0]`, `fill_seen` | out | | clk_sdram | fill completion (scan-out contract section 2) |
| `gfx_idle`, `gfx_fence[31:0]`, `gfx_free[8:0]`, `gfx_ovf[15:0]` | out | | clk_sys | status, section 4 |
| `gfx_caps[13:0]` (new) | out | 14 | constant | section 1.5 |
| `view_en`, `view_base[24:0]`, `view_stride[15:0]`, `view_dbl`, `vlb_we`, `vlb_waddr[11:0]`, `vlb_wdata[15:0]`, `view_active` (only with `VIEW_ENABLE`) | | | section 6 | viewer fill |

### 2.2 CPU registers that reach the engine (byte offsets in the MMIO page at `0x8000_0000`)
The shipped SoC decodes 9 bits of offset (`0x000..0x1FC`); an offset at `0x200` or above aliases onto `offset & 0x1FC`. Gallery's SoC decodes 10 bits (1 KiB page). Write fields not listed are ignored; registers marked W read 0.

#### 2.2.1 Command registers
| Offset | Name | Access | Fields and effect | Reset |
|---|---|---|---|---|
| 0x048 | `FB_ADDR` | W | `[18:0]` destination word offset; held until rewritten | 0 at system reset |
| 0x04C | `FB_SIZE` | W | `[8:0]` w, `[17:9]` h; held | 0 |
| 0x050 | `FB_COLOR` | W | `[15:0]` fg, `[31:16]` bg; held | 0 |
| 0x054 | `FB_GO` | W | **pushes one command** made of the three held registers and this write: `op = {[14], [2:0]}`, `glyph = [9:3]`, `sx = [11:10]`, `sy = [13:12]`, `glyph_hi = [16:15]`, `subop = [22:17]` (only with `FEAT_ESC`); the push happens on the clock after the write | |
| 0x054 | `FB_GO` | R | `[0]` queue full (`cmd_full`), `[1]` idle (`gfx_idle`), other bits 0 | |
| 0x058 | `FB_STALL` | R | clk_sys cycles during which the queue was full, 32 bits, wraps | 0 |
The held registers make a string of glyphs cost two writes per glyph (`FB_ADDR`, `FB_GO`). **C-IF-1 PINNED [`fw2/gfx/gfx.c`, Gallery `fw2/gfx/gfx.c`]:** this packing; the opcode values `R_FB_GO = 0x4001` (FENCE), `0x4002` (CORNERS), `0x4003 | rx << 10 | ry << 12` (DSBLIT), `0x4007` (ESC).

#### 2.2.2 Sticky state registers
| Offset | Name | Access | Fields | Power-up value |
|---|---|---|---|---|
| 0x0C0 | `BLT_IDX` | W | `[2:0]` field selector | 0 |
| 0x0C4 | `BLT_DATA` | W | writes the selected field, then the selector steps (after field 6 it returns to 0). Field 0 `SRC_BASE[24:0]`; 1 `SRC_STRIDE[9:0]`; 2 `DST_BASE[24:0]`; 3 `DST_STRIDE[9:0]`; 4 `KEY`: `[16]` enable, `[15:0]` colour; 5 `BLEND`: `[0]` enable, `[3:1]` mode, `[15:8]` alpha; 6 `REINDEX[7:0]`. A selector of 7 writes REINDEX (**C-REG-1 FREE**) | bases 0, strides 512, key off and 0, blend off, mode 0, alpha 0, reindex 0 |
| 0x0C8 | `CLUT_IDX` | W | `[7:0]` next CLUT entry | |
| 0x0CC | `CLUT_DATA` | W | `[15:0]` written at the entry, then the entry steps (255 wraps to 0) | contents not initialised |
| 0x0D4 | `RC_IDX` | W | `[3:0]` next corner-table entry | 0 |
| 0x0D8 | `RC_DATA` | W | `[4:0]` written at the entry, then the entry steps (15 wraps to 0) | all 16 entries 0 |
| 0x114 | `TEXT_MODE` | R/W | write `[0]` = 1 selects the light-text table; read `[31]` = 1 present, `[0]` value | 0 |
| 0x118 | `DBUF_CPU` | R/W | write `[0]` CPU drawing buffer; read `[31]` present (`DBUF_ENABLE`), `[0]` value; reads 0 without `DBUF_ENABLE` | 0 |
| 0x11C | `DBUF_DISP` | R/W | write `[0]` = 1 requests a flip at the next vertical blank; read `[31]` present, `[1]` flip pending, `[0]` displayed buffer | 0 |
| 0x1A0 | `FONT_CTRL` | R/W | read `[31]` present, `[27:16]` completed loads, `[15:0]` 6,112 (words); write `[0]` restarts the write index | |
| 0x1A4 | `FONT_IDX` | R/W | write index 0..6111 (larger goes to 0) | |
| 0x1A8 | `FONT_DATA` | W | 32-bit glyph word at the index, then the index steps; after word 6111 it returns to 0 and the load count steps | |
"Power-up value" (C-RST-3): in the shipped SoC these registers take their value at configuration and are **not** changed by the system reset; the font, CLUT and corner-table memories are not cleared by any reset. Their write timing against drawing is the software's job: **C-ORD-4 PINNED [`fw2/gfx/gfx.c`: fences before base, palette, blend-off and font changes]:** software fences before changing a sticky field, the CLUT, the corner table or the font, so a value written while the engine is idle applies to every command pushed after the write. What a command does when a sticky field changes while it executes is FREE (**C-ORD-5 FREE**).

#### 2.2.3 Status registers and the proposed viewer block
| Offset | Name | Access | Fields |
|---|---|---|---|
| 0x1B8 | `GFX_FENCE` | R | FENCE commands retired, 32 bits, wraps |
| 0x1BC | `GFX_STATUS` | R | `[31]` 1 (present), `[30:17]` capability bits (section 1.5, **new**), `[16]` idle, `[8:0]` free queue slots |
| 0x1C0 | `GFX_COUNTS` | R | `[31:16]` late scan-out lines (from the scan-out), `[15:0]` pushes dropped while full (saturating) |
| 0x1D4/0x1D8 | `SDA_SEL`/`SDA_VAL` | R/W | scheduler per-port counters (not the engine's; port 1 "bad requests" must stay 0, section 2.4) |
| **0x280** | `VID_MODE` (proposed, `VIEW_ENABLE`) | R/W | Gallery's `0x1EC` fields unchanged: write `[0]` requested mode; read `[31:16]` completed switches, `[2]` requested, `[1]` pending, `[0]` on the bus |
| **0x284** | `VIEW_BASE` | R/W | `[24:0]` word address of plane line 0 |
| **0x288** | `VIEW_STRIDE` | R/W | `[15:0]` words per plane line, reset 1,200 |
| **0x28C** | `VIEW_CAPS` | R | `[31]` present, `[3]` doubler, `[2]` pixel effects (0 unless the importing project builds them), `[1]` 800 x 720 viewer, `[0]` mode sequencer |
| **0x290** | `VIEW_CTRL` | R/W | `[0]` doubler (moved out of Gallery's `R_PIX_CTRL` bit 3) |
All five read 0 and ignore writes when `VIEW_ENABLE` = 0 (in a 10-bit-decode SoC). **C-IF-5 PROPOSAL (owner decision Q1).** Collision table:

| Range | Tau Alpha main (9-bit decode) | Gallery 1.0.0 (10-bit decode) | Tau Reader (draft) |
|---|---|---|---|
| Gallery's old `0x1EC..0x1F8` | **taken**: `SAVE_ST`, `SAVE_IDX`, `SAVE_DATA` at `0x1EC..0x1F4` (the collision that forces the move) | its viewer block today | its video MMIO test configuration sits at `0x1DC..0x1FF` (its own README calls it a test address) |
| proposed `0x280..0x290` | not decodable: aliases onto `0x080..0x090` (`SDR_*` mailbox and the expansion window start at `0x088`); **a VIEW build therefore needs the 10-bit decode**, and firmware must test capability bit 20 before touching the block | free: Gallery uses `0x200..0x214` (landing, interrupts), `0x220..0x24C` (TGY decoder), `0x250..0x254` (pixel effects), `0x260..0x278` (histogram) | free (none found) |
`CORE_VERSION`: Tau Alpha main is `4D50331B` counting up; Gallery used `4D50331C..4D50331F`. The engine needs nothing from it: firmware detects engine features by the capability bits. The per-project namespace (D-T22) is an SoC change, outside this contract.

### 2.3 The command queue
* **C-Q-1 MATCH:** commands execute in push order, one at a time; a command's effects are complete in memory before the next command's first memory write (section 4.3 states what software can observe).
* **C-Q-2 PINNED [`fw2/gfx/gfx.c` polls bit 0 before every push; T3 spec]:** the queue accepts at least **255** commands; `cmd_full` is 1 when 255 are held (not yet dispatched). A depth above 255 is allowed (not a regression, scoping section 1) but `gfx_free` then saturates at 255 and `cmd_full` must still rise before an entry can be lost.
* **C-Q-3 PINNED [T3 spec; `tb_tau_gfx_t3` 300 pushes into a stalled queue: 255 kept, 45 counted]:** a push while `cmd_full` is 1 is dropped (not stored, not counted as accepted) and increments `gfx_ovf`, which saturates at 65,535.
* **C-Q-4 MATCH:** `gfx_free` = 255 minus the occupancy seen from `clk_sys` (0 when full), registered; it may under-report free space for a few clocks after a dispatch (the read side crosses domains), never over-report.
* **C-Q-5 FREE:** the stored word width and layout. Resource rule: the queue fits in 3 M10K blocks for every named set (an ESC build adds 6 bits per entry, 94 of the 120 bits available in three blocks).
* Flow control for software: poll `FB_GO[0]` (or `GFX_STATUS[8:0]`) before a push; the driver never relies on the overflow count except as an alarm.

### 2.4 The memory master port (the `p0` protocol)
The port connects through `tau_sdram_a_gfx_if` to the scheduler (`SDRAM_CONTROLLER_CONTRACT.md` sections 4 and 6). The engine sees the following protocol (all signals in `clk_sdram`).
* **C-MEM-1 PINNED [interface: `SDRAM_CONTROLLER_CONTRACT.md` section 4, `tau_sdram_a_gfx_if`] Request:** one cycle of `p0_rd_req` or `p0_wr_req` with `p0_addr[24:0]` and, for a read, `p0_rd_len[9:0]` and `p0_is_fill` valid in that cycle; for a write `p0_wr_len[10:0]`, `p0_wr_stream`, `p0_byte_en` (always `2'b11`: the engine writes whole words), and for a constant write `p0_data[15:0]` held unchanged until the write completes.
* **C-MEM-2 PINNED [interface] One at a time:** a request is issued only while `p0_available` and `sdram_init_complete` are 1; after a request the engine issues nothing until the request has finished: a read when its last word has been taken, a write when `p0_ready` has pulsed. Then it waits for `p0_available` again.
* **C-MEM-3 PINNED [interface] Read:** words arrive in address order, one per cycle in which `p0_data_available` is 1 (gaps allowed); the engine takes exactly `p0_rd_len` words and raises `p0_end_burst_req` in the cycle it takes the last one (the scheduler path ignores it; keep it).
* **C-MEM-4 PINNED [interface] Streamed write:** the memory side presents `wsrc_addr` (0, 1, 2, ... within the request) and expects the word for that index on `wsrc_q` **one cycle later** (a registered read).
* **C-MEM-5 PINNED [scheduler contract: `bad_req`] Lengths:** every read and write length is 1..512. A request of length 0 or above 512 is rejected by the scheduler and counted in its `bad_req` counter (`SDA` port 1); the new engine must never issue one. (The shipped engine does, outside the legal domain: a blit-class width that is a multiple of 128 produces a zero-length write, probe, FREE.)
* **C-MEM-6 FREE Pages:** the scheduler splits a request that crosses a 1,024-word page; the engine may issue page-crossing requests. (The old controller, now removed, wrapped writes and hung on reads at a page edge; that is history, not a rule.)
* **C-MEM-7 PINNED [interface] Fill marking:** a scan-out fill read has `p0_is_fill` = 1 (the scheduler's urgent port); no other request has it.
* **C-MEM-8 PINNED [interface; Chladni `mb_read` after `fence`] Ordering between ports:** the scheduler does not order the engine's port against the CPU's. A write is in memory when `p0_ready` has pulsed (the scheduler's `done`, one cycle after the last beat reaches the pins); a later CPU read returns the new data. Retirement (section 4.1) waits for this.

### 2.5 Memories the CPU writes
* **CLUT:** 256 entries of 16 bits, written from `clk_sys` (port of section 2.1), read by the engine. One M10K (resource rule).
* **Font RAM** (`FONT_RAM_ENABLE` = 1): the black-box `font_ram` (6,112 words of 32 bits, write in `clk_sys`, registered read in `clk_sdram`). **C-FONT-1 PINNED [`docs/features/FONT_SPEC.md`, `fw2/gfx/font.c`, the font generator]:** glyph `g` (0..190) occupies words `32g .. 32g+31`; row `r` (0..15) of the glyph is words `32g + 2r` (pixels 0..7) and `32g + 2r + 1` (pixels 8..15); pixel `p` of a word is the 4-bit coverage in bits `[4p+3:4p]`.
* **Font ROM** (`FONT_RAM_ENABLE` = 0): the black-box `font_rom` (3,040 words, the same layout for glyphs 0..94).
* **Corner table:** 16 entries of 5 bits on `rc_cut_lut` (entry 0 is never used by a command).

### 2.6 Scan-out interplay (what the engine guarantees)
The scan-out (`TALOS_SCANOUT_CONTRACT.md`) requests line `n` (0..359) one scanline ahead by toggling `fill_req_tgl` with `fill_line_req` = `n`; it reads the line buffer half `n mod 2` while the engine fills the other half; it counts a line as late when the last finished fill is neither that line nor the next one.
* **C-FILL-1 PINNED [scan-out contract; every picture]:** on each request the engine reads `FILL_WORDS` words starting at word `512n` (plus 1,048,576 when the displayed buffer is 1, section 3.14) and writes word `i` (0..`FILL_WORDS`-1) to the line buffer at address `{n[0], i[8:0]}` with `lb_we`. When the last word is written it sets `fill_done_line` = `n`, pulses `fill_done_inc` and sets `fill_seen` (sticky).
* **C-FILL-2 PINNED [B-904, B-929: `GFX SCAN LATE` 0 on a Pocket]:** a fill is never delayed by more than **one** memory request of the engine that was already issued plus at most **256** clk_sdram cycles of engine work that does not use the memory port. No engine request may be longer than 512 words. Measured on the probe's functional stub under a mixed load (511-wide rectangles, blended blits, 3x glyphs, 3x SBLIT, CBLIT, bars), the shipped engine's worst wait from the request being seen to the fill's read request was **131 clk_sdram cycles** (478 fills). Requirement for the new engine on the same stub and stream: **at most 600 cycles**; on the chip-model bench (`sim/sdram_sched_a/run_gfx.sh`) and on a Pocket: no late line.
* **C-FILL-3 FREE (improvement allowed):** a request that arrives while the previous fill is still running. The shipped engine can lose a request that arrives in the last cycle of a fill, and overwrites the line number of a fill in progress; the new engine must serve every request whose line differs from the one being filled, in order.
* **C-FILL-4 PINNED [scan-out contract]:** the request toggle and the vertical-blank toggle are sampled with at least two flip-flops in `clk_sdram`; `fill_line_req` is stable for the whole scanline and is read when the toggle change is seen.
* **C-FILL-5 MATCH:** the displayed buffer used by a fill is the value at the moment the fill's read is issued; flips happen only at a vertical-blank edge (section 3.14), and no fill is requested between that edge and the end of vertical blanking, so a frame never mixes buffers.

### 2.7 Clock-domain crossings visible at the interface
Exactly these, no more (R9): the queue (`clk_sys` to `clk_sdram`, Gray-coded pointers or an equivalent proven asynchronous FIFO); the retirement count and the fence count (`tau_cdc_gray_ctr` into `clk_sys`); the flip request toggle (`clk_sys` to `clk_sdram`, toggle and sample); the fill and vertical-blank toggles (`clk_vid` to `clk_sdram`); the CLUT and font RAMs (dual-clock memories); the quasi-static inputs (sticky fields, corner table, reindex, viewer base and stride: sampled without a synchroniser, safe because software fences before changing them, C-ORD-4); `dbuf_disp_buf` and `dbuf_flip_pending` read informally by the SoC (change at most once per frame). `text_light` and `dbuf_cpu_buf` arrive already synchronised.

---

## 3. Commands

### 3.0 Opcode map and common rules
| op | Command | Built | v0.1 rule |
|---|---|---|---|
| 0 | RUN | always | 3.1 |
| 1 | RECT | always | 3.1 |
| 2 | CHAR | always | 3.4 |
| 3 | COPY | always | 3.5 |
| 4 | BLIT | always | 3.6 |
| 5 | BAR | always | 3.2 |
| 6 | SBLIT | always | 3.7 |
| 7 | CBLIT | always | 3.8 |
| 8 | RRECT | always | 3.9 |
| 9 | FENCE | always | 3.10 |
| 10 | CORNERS | `FEAT_CORNERS` | 3.11; with the feature off: no-op |
| 11 | DSBLIT | `FEAT_DSBLIT` | 3.12; off: no-op |
| 12, 13, 14 | reserved | | no-op |
| 15 | ESC | `FEAT_ESC` | 3.13; off: no-op |
* **C-OP-1 CHANGE [IMP-RESERVED-NOOP; D-T19/D-T22 opcode map, scoping 7.2 approved by D-T21]:** a reserved opcode, or a feature opcode whose feature is off, retires without any memory access and counts for idle like any command. The shipped Tau Alpha engine draws one row as RUN for opcodes 10..15 (probe: 5 words in fg at the address); Gallery's draws a row for 12..15 (probe). No shipped firmware sends them.
* **C-GEN-1 MATCH:** a command whose drawn width or height is 0 makes no memory access and retires (probe: ten zero-size commands of eight opcodes, 0 reads, 0 writes). For CHAR, `w` and `h` are ignored.
* **C-GEN-2 FREE (safety rule binding):** outside the legal domain stated per command, the result is not compared, but the engine must retire, must not hang, must not issue an illegal memory request (C-MEM-5) and must not write outside the destination rectangle of the command (for RRECT and CORNERS: outside the `w` x `h` rectangle at the address).
* **Address classes.** "Frame offset" commands (RUN, RECT, BAR, CHAR, COPY, RRECT, CORNERS) address with a 19-bit word offset: pixel `(x, y)` of a command at offset `A` is word `A + 512y + x` computed modulo 2^19, then the double-buffer rule (3.14) adds 1,048,576 or not, per row. "Blit-class" commands (BLIT, SBLIT, CBLIT, DSBLIT) address with the sticky 25-bit bases and 10-bit strides, modulo 2^25, and are not touched by the double-buffer rule. **C-MAP-1 PINNED [`sdk/docs/HOST_TABLE.md` (`mb_write`, `blit_abs`, `rect_stash`), `canvas.py` `PITCH 512, ROWS 1024`]:** pitch 512 words per row for frame-offset commands; visible frame rows 0..359, columns 0..399; the stash (rows 360..1023, and columns 400..511 of every row) is ordinary memory.
* **Source offset of blit-class commands and COPY:** `S = {fg[2:0], bg[15:0]}` (19 bits). `fg[15:3]` is ignored.

### 3.1 RUN and RECT
* **C-RECT-1 PINNED [`canvas.py` rect/rect_stash, `tau_fb.js`, HOST_TABLE `rect`]:** RECT writes `fg` to every pixel `(x, y)`, `0 <= x < w`, `0 <= y < h`, at the address of 3.0. Legal domain: `w`, `h` 1..511, `A + 512(h-1) + w - 1 < 2^19`.
* **C-RUN-1 MATCH:** RUN is RECT with `h` replaced by 1 (`h` ignored).

### 3.2 BAR
* **C-BAR-1 PINNED [`canvas.py` bar, `tau_fb.js` bar, HOST_TABLE `bar`, packs Winamp Bars and Sound Map]:** `lit = min(L, h)` with the 9-bit count `L = {glyph_hi[1:0], glyph[6:0]}`. Rows `y < h - lit` get `bg`, rows `y >= h - lit` get `fg`, columns `0 <= x < w`. Probe: `L` = 172 with `h` = 10 lights all 10 rows and nothing above; `L` = 130 of `h` = 200 gives rows 0..69 `bg`, 70..199 `fg`. **Model gap:** `canvas.py` and `tau_fb.js` do not clamp `lit` to `h` (for `lit > h` they paint above the bar); the hardware clamp is the contract (the same reasoning as D-T21's SBLIT ruling); the firmware wrapper clamps before the call, so no pack output differs. To be fixed in the models with the SBLIT row clamp (conformance plan section 2.3).

### 3.3 (reserved number, not used)

### 3.4 CHAR (glyph)
* **Glyph index.** With the font RAM: code `c = {glyph_hi[0], glyph[6:0]}` (8 bits); index `g = c - 0x20` for `0x20 <= c <= 0x7E`, `g = c - 0xA0 + 95` for `c >= 0xA0`, otherwise `g = 0`. With the font ROM: `c = glyph[6:0]`, `g = c - 0x20` for `0x20 <= c <= 0x7E`, otherwise 0. **C-CHAR-1 PINNED [`docs/MMIO_ALLOCATION.md` OP_CHAR note, `FONT_SPEC.md`, `fw2/gfx/gfx.c` Latin-1 text]:** this mapping. (Index 0 is the space glyph in every shipped font, so an unmapped code draws the space cell; the engine draws glyph 0, whatever the CPU loaded there.)
* **Cell size and scale.** Per axis a selector `s` (`sx` across, `sy` down) gives a ratio `(num, den)` and an extent: `s` = 0: (1, 1), 16; 1: (2, 3), 24; 2: (1, 2), 32; 3: (1, 3), 48. Output column `i` (0..extent-1) uses source column `floor(i * num / den)`; output row `j` uses source row `floor(j * num_y / den_y)`. **C-CHAR-2 PINNED [every UI screen, 4 packs]** for 1x (`sx = sy = 0`); **C-CHAR-6 MATCH** for the other scales (D-T18 keeps them; no shipped caller; Tau V's old-player firmware has CHAR scale callers). Probe: 16, 24, 32, 48 rows written for selectors 0..3, `w`, `h` ignored.
* **Pixel.** With coverage `c` (0..15) of the source pixel and weight `wt = T[c]` (`T = TEXT_W_LIGHT` when `text_light` = 1, else `TEXT_W_DARK`), each channel is `out = floor((fg_ch * wt + bg_ch * (16 - wt)) / 16)` for R (5 bits), G (6 bits), B (5 bits) separately. Every pixel of the cell is written (the cell is opaque: `bg` where the coverage is 0). **C-CHAR-3 PINNED [all shipped screens, Gallery text, the screenshot gate R2]** for the formula.
* **Weight tables (data, C-CHAR-4).** Derived by `tools/derive_text_weights.py` (MIT, new, D-T20): for each coverage `c`, the integer `wt` in 0..16 that minimises the squared CIE L* error between the physically correct pixel (linear-light mix at `a = c/15`, then encoded) and the RGB565 result of the formula above, over the 36 weighted text-on-ground pairs of `themes/tau.json` and `themes/ocean.json`; ends fixed (`T[0] = 0`, `T[15] = 16`), never decreasing (`docs/features/TEXT_WEIGHTS_REDO_2026-11-25.md`).

| c | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `TEXT_W_DARK` (light text on a dark ground; derived, **v0.1 default**) | 0 | 4 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 12 | 13 | 14 | 14 | 15 | 15 | 16 |
| `TEXT_W_LIGHT` (dark text on a light ground; **v0.1 default = the values in use**) | 0 | 1 | 1 | 2 | 3 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 12 | 13 | 16 |
| `TEXT_W_LIGHT` derived by the same method (alternative, Q2) | 0 | 1 | 1 | 2 | 3 | 3 | 4 | 5 | 5 | 6 | 7 | 8 | 9 | 11 | 13 | 16 |

**C-CHAR-4 CHANGE [IMP-TEXT-DARK, D-T21]:** the dark table differs from the shipped one at `c` = 5, 9, 12, 14 (one step lower each); D-T21 applies it to Tau Alpha and Gallery together in one release, Gallery's text goldens recaptured together. The light table: open (Q2).
* **C-CHAR-5 MATCH:** the cell's rows are written top to bottom at `A + 512j`, each row subject to the double-buffer rule.

### 3.5 COPY
* **C-COPY-1 MATCH (no shipped caller; Tau V and Tau Μ old-player firmware: 10 call sites):** copies `w` x `h` words from source offset `S + 512y + x` to destination offset `A + 512y + x`, both frame-offset addresses (both subject to the double-buffer rule, probe), rows in the order of 3.15. No key, no blend. Legal domain: `w` 1..127.

### 3.6 BLIT (with key and blend)
* **Addresses.** Source pixel `(x, y)` at `SRC_BASE + S + y * SRC_STRIDE + x`, destination at `DST_BASE + A + y * DST_STRIDE + x` (modulo 2^25). **C-BLIT-1 PINNED [`canvas.py` blit/blit_abs/scroll_*, `tau_fb.js`, HOST_TABLE; Gallery `tb_talos_tilecopy` for 25-bit bases and stride use]** for `w` 1..127, `h` 1..511, strides 0..1023 (stride 0 repeats one source row: the lab's `Fb.blend` uses it, MATCH).
* **Plain:** destination word = source word.
* **Key** (`KEY` enable = 1): a source word equal to `KEY` (all 16 bits) leaves the destination word unchanged. **C-BLIT-2 MATCH** (no shipped caller).
* **Blend** (`BLIT_BLEND_ENABLE` = 1 and `BLEND` enable = 1), destination channel `b`, source channel `f`, per channel (R, G, B with their own maxima `m` = 31, 63, 31):
  * mode 0: `floor((f * a + b * (256 - a)) / 256)` with `a` = alpha 0..255. **C-BLIT-3 PINNED [`canvas.py` blend_blit, `tau_fb.js` blendBlit, HOST_TABLE `blend_on` "a/256", Winamp Scope pack, crossfade]**; probe: exact for `a` = 0, 1, 255.
  * mode 1: `floor((b + f) / 2)`; mode 2: `min(m, b + f)`; mode 3: `b - f` if `f <= b`, else 0; mode 4 and modes 5..7: `min(m, b + floor(f / 4))`. **C-BLIT-4 MATCH [lab-only `tau_fb.js` `Fb.blend`]** (no shipped hardware caller).
* **Key and blend together:** a keyed word leaves the destination; the others are blended (probe). **C-BLIT-5 MATCH.**
* With `BLIT_BLEND_ENABLE` = 0 the blend enable is ignored (plain copy).
* Legal domain: `w` 1..127 (**C-BLIT-6 PINNED [HOST_TABLE `cblit w <= 127`; `fw2/gfx/gfx.c` splits at 127, 120, 112; `tau_fb.js` "rows of at most 127"]**). Wider is out of domain in v0.1 (FREE with C-GEN-2; the shipped engine uses `w mod 128`, and a multiple of 128 issues a zero-length write, probe). Accepting 1..511 is the later improvement IMP-WIDE behind a capability bit (D-T21).

### 3.7 SBLIT (nearest-neighbour scale up)
* Selector per axis (`sx`, `sy`): 0 = 1x, 1 = 1.5x, 2 = 2x, 3 = 3x, ratios `(num, den)` = (1,1), (2,3), (1,2), (1,3). Output extent per axis `E = min(127, floor(s * den / num))` for source extent `s` (that is `s`, `floor(3s/2)`, `2s`, `3s`, each clamped). Output pixel `(i, j)` = source pixel `(floor(i * num / den), floor(j * num_y / den_y))` at the blit-class source address; destination as BLIT. No key, no blend (probe: identical output with both enabled). Zero output: no access.
* **C-SBLIT-1 PINNED [`canvas.py` sblit `min(127, w*sc)` and `i // sc`, `tau_fb.js`, Chladni pack]:** the 1x/2x/3x map and the 127-column clamp.
* **C-SBLIT-2 PINNED [shipped hardware, D-T21]:** the output height is also clamped to 127 (probe: 60 x 60 at 3x writes 127 rows of 127). `canvas.py` (`oh = h * sc`) and `tau_fb.js` are to be corrected to this (D-T21); until then the conformance replay skips SBLIT calls taller than 127 output rows (none is issued by a shipped pack: Chladni's are below, conformance plan 2.3).
* **C-SBLIT-3 MATCH:** selector 1 (1.5x): extent `floor(3s/2)`, column `floor(2i/3)` (probe: 5 x 3 gives 7 x 4).
* **C-SBLIT-4 MATCH:** no key, no blend.

### 3.8 CBLIT (palette)
* Output pixel = `CLUT[(src_word[7:0] + REINDEX) mod 256]`; `src_word[15:8]` ignored; addresses as BLIT. No key, no blend (probe). **C-CBLIT-1 PINNED [`canvas.py` cblit, `tau_fb.js`, HOST_TABLE `w <= 127`, Waterfall pack]** for `REINDEX` = 0; **C-CBLIT-2 MATCH** for the offset (probe: wraps modulo 256). Legal domain `w` 1..127 (as C-BLIT-6).
* **C-CBLIT-3 MATCH:** the hardware does not clip to the visible frame (the models clip; legal pack use stays inside the meter box).

### 3.9 RRECT (rounded rectangle)
* Radius `r = min(glyph[6:0], 15)` (the 7-bit field clamped, not its low 4 bits). First the whole `w` x `h` rectangle in `fg` (as RECT). Then, for each corner row `k = 0 .. r-1` with cut `c = TABLE[r - k]` greater than 0, the words `[0, c)` and `[w - c, w)` of rows `k` and `h - 1 - k` of the rectangle are written in `bg`. `r = 0`: the fill only. **C-RRECT-1 MATCH** (no shipped Player 2 caller; old-player firmware of Tau V/Tau Μ: 7 call sites; the Helios spec names it). Legal domain: `r <= h`, every used cut `<= w`. Out of domain FREE with C-GEN-2 (the shipped engine wraps the mirrored row and can write far outside the rectangle when `h < r`; the new engine must not).
* **C-RRECT-2 FREE:** the order in which corner words are written (they all get the same colour).

### 3.10 FENCE
* **C-FENCE-1 PINNED [`fw2/gfx/gfx.c` hardware fence, HOST_TABLE `fence`, T3 spec, `tb_tau_gfx_t3`]:** draws nothing; retires only after every earlier command has retired (section 4.1); its retirement increments the fence count by exactly 1; no other command changes it.

### 3.11 CORNERS (opcode 10, `FEAT_CORNERS`)
* The corner part of RRECT alone, in `bg`, with the same radius rule and table; no fill; the words inside the corners are not touched. `w = 0`, `h = 0` or `r = 0`: nothing written. Double-buffer rule as a frame-offset command. **C-CORN-1 PINNED [Gallery 1.0.0 thumbnails; Gallery `sim/tb_mp3_fb.v` CORNERS cases]:** r = 2 with `TABLE[2] = 1`, `TABLE[1] = 0` on a 10 x 6 rectangle writes exactly words 0 and 9 of rows 0 and 5; r = 3 with `TABLE[3] = 3, TABLE[2] = 1, TABLE[1] = 0` on 20 x 9 writes `[0,3)`, `[17,20)` of rows 0 and 8 and `[0,1)`, `[19,20)` of rows 1 and 7; a command queued after CORNERS executes after all its writes.
* **Finding:** Gallery's scoping note says only `r[3:0]` is used; Gallery's engine clamps the 7-bit field to 15 like RRECT. The clamp is the contract.

### 3.12 DSBLIT (opcode 11, `FEAT_DSBLIT`)
* Ratio selector per axis (`sx`, `sy`): 0 = 3:4, 1 = 2:3, 2 = 1:2, 3 = 1:3 (output : source), `(num, den)` = (3,4), (2,3), (1,2), (1,3). Output extent `E = min(127, floor(s * num / den))` per axis. Output pixel `(i, j)` uses `X = floor(i * den / num)`, `Y = floor(j * den_y / num_y)` and the four source words at `(X, Y)`, `(X+1, Y)`, `(X, Y+1)`, `(X+1, Y+1)` (source address as BLIT: the row below is `+ SRC_STRIDE`); per channel `out = floor((p0 + p1 + p2 + p3 + 2) / 4)`. Destination as BLIT. Zero output: no access.
* **C-DSB-1 PINNED [Gallery 1.0.0 grid sizes; Gallery `tb_mp3_fb.v` seven cases, mutants `BUG_DSB` 3..7]:** the extents, the position map, the four taps and the rounding.
* **C-DSB-2 MATCH (settled by probe on Gallery's engine at `26d9b1c`):** the sticky key and blend do **not** apply (96 x 12 at 2:3 by 3:4: identical output with both on).
* The taps never leave the source rectangle for these ratios (output extents are floors). **C-DSB-3 FREE:** the read pattern (one word at a time or gathered).

### 3.13 ESC (opcode 15, `FEAT_ESC`)
* The 6-bit sub-opcode selects the command; the other fields are its operands with their usual positions. Sub-opcode 0 = NOP. **v0.1 defines no other sub-opcode**: every sub-opcode retires as a no-op and counts for idle. A future sub-opcode is added to this contract with its own parameter, capability bit and vectors; one that needs more operands brings its own sticky registers. **C-ESC-1 CHANGE [new, D-T22].** With `FEAT_ESC` = 0, `cmd_subop` is ignored and opcode 15 is a no-op (C-OP-1).

### 3.14 Double buffering (`DBUF_ENABLE`)
* **C-DBUF-1 PINNED [`fw2/gfx/gfx.c` frame begin/end, compose, crossfade; `tb_helios_dbuf`]:** for frame-offset commands (RUN, RECT, BAR, CHAR, COPY source and destination, RRECT, CORNERS) a computed word address below 184,320 (row < 360) gets 1,048,576 added when the CPU buffer (`dbuf_cpu_buf`) is 1; the decision is made per row (a rectangle that straddles row 360 splits: probe). Blit-class commands are never changed (their bases select the buffer).
* **C-DBUF-2 PINNED [`fw2/gfx/gfx.c` waits for flip pending to clear]:** a change of `dbuf_flip_req_tgl` sets "flip pending"; at the next vertical-blank edge with a flip pending the displayed buffer inverts and pending clears; several requests before one edge give one flip. The fill (C-FILL-1) reads the displayed buffer.
* With `DBUF_ENABLE` = 0 the CPU buffer and flip inputs are ignored, the displayed buffer stays 0 and `DBUF_*` read 0.
* **C-DBUF-3 MATCH (UNCONFIRMED consequence):** the displayed buffer and flip pending are not changed by `reset` in the shipped engine (they keep their value; power-up 0). Settle: a 20-line bench that flips, resets and reads `dbuf_disp_buf`; whether firmware depends on it: read `fw2` boot code (`gfx_init`).

### 3.15 Row order and overlap
* **C-ORD-1 PINNED [`fw2/gfx/scroll.c` one-pass scroll, `tb_tau_gfx_move` cases A and C]:** within one row of a BLIT, COPY or CBLIT (and the destination pre-read of a keyed or blended BLIT), every source word of the row is read before any destination word of that row is written; rows are processed top to bottom; row `y` reads memory as left by rows `0..y-1`.
* **C-ORD-2 MATCH:** consequence for a downward overlapping copy in one command (destination below the source, overlapping): not a memmove (case B: 1,620 words differ). An overlap-safe order is the later improvement IMP-OVERLAP behind a capability bit (D-T21).

---

## 4. Fence, status, idle, retirement

### 4.1 Retirement and idle
* **C-RET-1 PINNED [T3 spec; `fw2` wd recovery waits for idle]:** each accepted push retires exactly once, in push order, including no-ops and FENCE. A command retires when all its memory writes have completed (C-MEM-8).
* **C-IDLE-1 PINNED [T3 spec, `tb_tau_gfx_t3`]:** `gfx_idle` = (accepted pushes == retired commands), counted modulo 2^16 at least; a push followed by an idle read one `clk_sys` cycle later reads busy (probe: checked).
* **C-IDLE-2 REQUIREMENT (latency):** an idle engine shows a pushed FENCE in `gfx_fence` within **12** `clk_sys` cycles and shows idle again after a 1 x 1 RECT within **20** cycles, on the probe's stub at 66.667/100 MHz (shipped: 6 and 10, probe).
* `R_FB_GO[1]` and `R_GFX_STATUS[16]` are the same signal.

### 4.2 Counters
`gfx_fence` (32 bits, wraps), `gfx_ovf` (16 bits, saturates), `gfx_free` (0..255), the late-line count (scan-out). All reset to 0 (free to 255) by `reset`.

### 4.3 What software may rely on (the ordering guarantees)
1. After `gfx_fence` reaches the count of FENCE pushes issued, every command pushed before that FENCE has retired and its writes are readable by the CPU (Chladni's `mb_read` after `fence`).
2. After idle reads 1, every accepted push has retired.
3. A sticky field, CLUT entry, corner-table entry or font word written while idle reads 1 applies to all later pushes.
4. Commands never overtake each other; a command's writes never land after a later FENCE retires.

---

## 5. Reset and error behaviour
* **C-RST-1 MATCH:** `reset` empties the queue (pending commands are discarded and never retire), abandons the command in progress, clears the push, retire, fence and overflow counts, sets free to 255, and the engine resumes accepting pushes when `reset` falls. A memory request in flight is dropped by the scheduler at its own reset (no `done`).
* **C-RST-2 FREE:** what part of an abandoned command reached memory.
* **C-RST-3 MATCH:** see 2.2.2 (sticky values and the CLUT, font and corner memories survive reset).
* **Errors:** the engine has no error register. The only error signals are the overflow count (C-Q-3), the scheduler's `bad_req` per port (must stay 0, C-MEM-5) and the late-line count. No input sequence may hang the engine (C-GEN-2, R10).

---

## 6. The engine half of the viewer (`VIEW_ENABLE`, `VIEW_DBL_EN`); second step per D-T22
**Belongs here:** what the engine fetches and writes. **Belongs to the viewer scan-out contract (second step):** the 800 x 720 timing at 40 MHz, the mode sequencer, the 1,666 x 400 UI timing at 40 MHz, how the scan-out repeats pixels for the doubler, the `VID_MODE` handshake, the scaler slot word, `video.json`.

### 6.1 Latching
* **C-VIEW-1 PINNED [Gallery 1.0.0 viewer frames; Gallery `tb_tau_gfx_view`]:** at each vertical-blank edge seen by the engine it takes `view_en`, `view_base`, `view_stride` and `view_dbl` as the frame's mode, base, stride and doubler; they never change within a frame. `view_active` shows the latched mode. `reset` clears the latched mode to 0 (UI).

### 6.2 The viewer fill
* **C-VIEW-2 PINNED [same]:** with the latched mode 1, a fill request for screen line `n` (0..719) reads `VIEW_LINE_WORDS` words from `view_base + p * view_stride` where `p = n` (or `floor(n/2)` with the doubler, 6.3), as `VIEW_LINE_WORDS / FILL_WORDS` consecutive requests of `FILL_WORDS` words at consecutive addresses, and writes word `i` to the viewer line buffer at `{n[0], i}` (12-bit address) with `vlb_we`; after the last word it reports `n` as the finished line (C-FILL-1 outputs). The UI line buffer is not written in viewer mode.
* **C-VIEW-3 MATCH:** no queued command runs between the chunks of one viewer line; queued commands run in the time left (each chunk boundary is a point where a waiting fill wins, C-FILL-2 applies per chunk).
* **C-VIEW-4 FREE:** the shipped engine waits three cycles after a line change for its address product; any latency is allowed if the line-deadline requirement holds (a viewer line is 900 clocks at 40 MHz, 2,250 clk_sdram cycles; Gallery's `tb_tau_gfx_view` frame of 576,000 pixels with no late line is the acceptance).
* **C-VIEW-5 FREE (Gallery limitation, not pinned):** a mode change while a fill is in flight may glitch that one line.

### 6.3 Doubler (`VIEW_DBL_EN`)
* **C-DBL-1 PINNED [Gallery 2x and 4x zoom; `tb_tau_gfx_view` DBL=1]:** with the latched doubler bit, the plane line for screen line `n` is `floor(n/2)`; the line-buffer half is still `n[0]`. Fetching only the 600 words a doubled line uses, or once per two lines, is a later improvement (Gallery scoping 1.5), FREE only if the frame pixels stay identical.
* The single latch point for the scan-out and the engine (one register, one moment) is recommended for the second-step contract.

---

## 7. Performance requirements (implementation-independent)
"No regressions, improvements welcome" (owner); D-T21 approved R1 to R10. Figures marked *stub* come from the probe's functional memory stub (reads one word per cycle after the request, constant writes in one cycle, streamed writes one word per cycle): they are valid only for comparing two engines on the same stub. *Pocket* figures are hardware-observed moving averages.

### 7.1 Per command (R4)
Requirement: on the corpus of the conformance plan (section 4) and on the realistic memory model (the `sched_a` + chip model path), the new engine's dispatch-to-retire cycles per command class are within **+5 %** of the old engine's at p50 and p95, and no class is slower than **+10 %**. Reference values of the old engine on the stub (probe, B-1178), single commands, no fills:

| Command | stub cycles | Command | stub cycles |
|---|---|---|---|
| FENCE (idle engine) | 1 | BLIT 127 x 16 plain | 4,225 |
| RECT 1 x 1 / 400 x 1 / 511 x 1 | 6 | BLIT 16 x 16 | 673 |
| RECT 16 x 16 / 127 x 16 | 81 | BLIT 127 x 16 blend / key | 6,353 / 6,337 |
| RECT 400 x 360 | 1,801 | COPY 127 x 16 | 4,225 |
| RUN 100 | 6 | SBLIT 40 x 40 at 1x / 2x / 3x | 11,401 / 45,201 / 101,401 |
| CHAR 1x / 1.5x / 2x / 3x | 928 / 1,968 / 3,392 / 7,392 | CBLIT 127 x 16 | 4,225 |
| BAR 8 x 100 | 501 | RRECT 100 x 50 r 8 (RECT alone 251) | 443 |
| DSBLIT 96 x 12 at 2:3 by 3:4 (Gallery) | 14,446 | DSBLIT 120 x 120 at 1:2 (Gallery) | 90,301 |
| CORNERS 100 x 50 r 8 (Gallery) | 193 | | |
Realistic-model reference (B-1178): `sim/sdram_sched_a/run_gfx.sh 150` (150 random commands through `sched_a`, the PHY and the chip model, with CPU traffic): **591,783 clk_sdram cycles to idle**, 13 fences, 0 late lines, 0 overflow, memory CRC `865a7a4f` (the reference stack without the PHY: 297,103). This stream is NOT all legal-domain (it has text in both polarities, so IMP-TEXT-DARK changes pixels, and blit-class widths up to 511): the new engine's CRC is compared only on a legal-domain variant with the old dark table built in (`TEXT_W_DARK` = the shipped values for that run); the cycle count is the +5 % reference. The bench's `FIXOP` parameter gives per-class streams; their oracle figures are a phase-2 task of the cost harness (not run here). Two older bench figures that the corpus must reproduce: wide CBLIT rows 32,718 cycles against 103,318 per-pixel (B-897, `tb_tau_gfx_cblit`); scan-out fills 144,720 cycles per frame at 400 words (B-898, `tb_tau_gfx_fill`).

### 7.2 Per meter tick on a Pocket (R4)
Requirement: CPU cycles and fence cycles per tick no worse than +5 % (moving averages: within noise is a pass) against:

| Meter (window / fullscreen) | cycles per tick | in fences | source |
|---|---|---|---|
| Winamp Bars | 16.3 k / 17.0 k | 0 | B-900 (DEV 196) |
| Winamp Scope | 9.4 k / 8.1 k | 0.9 k / 0.3 k | B-900 |
| Chladni | 234 k / 251 k | 90 k / 127 k | B-900 |
| Layered Wave | 29.8 k / 87.6 k (worst tick 5.25 M, firmware-bound) | 0 | B-900, B-904 |
| Master VU | 12.4 k / 16.3 k | 0 | B-900 |
| Waterfall horizontal | 89.9 k / 226.8 k (225 k on DEV 198) | 69.3 k / 203.0 k | B-902, B-904 |
| Waterfall vertical | 125.1 k / 450.4 k (443 k on DEV 198) | 68.8 k / 282.0 k | B-902, B-904 |
| Sound Map | 177 k (fullscreen) | 0 | B-900 |
Command issue is at most 9 % of any tick and the queue never filled (B-896): the issue path is not a requirement on the engine.

### 7.3 Scan-out and memory (R6, R7)
`GFX SCAN LATE` 0 and `GFX Q OVF` 0 over the Pocket checks and a blit storm; C-FILL-2; no request longer than 512 words; `bad_req` of the engine port 0; the engine's share of memory bandwidth not above the old engine's on the same stream (the same words read and written per command; a fill reads `FILL_WORDS`, a blit row reads its source once).

### 7.4 Fence latency
C-IDLE-2. A fence adds no work of its own beyond waiting for retirement: FENCE on an idle engine retires within 4 clk_sdram cycles of dispatch (shipped: 1, stub).

### 7.5 Fit and timing (R8), with map-level context
Requirements: two seeds, all corners positive; worst setup not below **+0.655 ns** at 100 MHz and hold not below **+0.078 ns** (fast 0C), no engine-internal path below the current overall worst (today the SDRAM read capture); ALM not above the replaced code +5 %, M10K and DSP not above it; LAB use not above today's; every memory inferred as a RAM block or MLAB (no register-file fallback, map-report check). Context (B-1177, `quartus_map` only, top = the engine):

| Configuration | ALMs (est.) | ALUTs | registers | block bits | DSP |
|---|---|---|---|---|---|
| Tau Alpha engine as shipped (TAU set) | 1,489 | 2,120 | 1,219 | 221,696 | 6 |
| Gallery engine, GALLERY set | 1,902 | 2,732 | 1,433 | 221,696 | 11 |
| per feature: CORNERS / DSBLIT / viewer fill / doubler | +142 / +130 / +136 / +9 | +136 / +234 / +187 / +15 | 0 / +41 / +173 / +1 | 0 | 0 / +4 / +1 / 0 |
Fitted reference for the whole graphics entity (T0, `stats-b821`): 1,532 ALMs, 38 M10K, 6 DSP. Whole device as shipped: 15,484 to 15,507 ALMs (84 %), 274 to 281 of 308 M10K, 23 of 66 DSP; LABs 93 % at T0. Requirements per set: the TAU set's map figures not above the first row (+5 % ALM); the GALLERY set's not above the second; the all-off build equal to a build without the feature code (map level). **Target (not a requirement):** DSBLIT without DSP blocks (its 4 DSP come from constant multiplies that shifts and adds replace; ALMs and LABs, not DSP, are Gallery's tight resources).

---

## 8. PINNED rules (one table)
| Rule | What | Artefact that pins it | Test |
|---|---|---|---|
| C-IF-1 | MMIO command packing and opcode values | `fw2/gfx/gfx.c` (Tau Alpha and Gallery) | SoC bench `tb_gfx_status_soc`-style + firmware harness |
| C-Q-2, C-Q-3 | 255 slots, full flag, dropped push counted | T3 spec, `fw2` poll, `tb_tau_gfx_t3` | queue-pressure vectors |
| C-MEM-1..5, -7, -8 | memory protocol, lengths 1..512, fill marking, write completion | `SDRAM_CONTROLLER_CONTRACT.md` section 4, `tau_sdram_a_gfx_if`, scheduler `bad_req` | protocol monitor on every run; `SDA` port-1 bad requests 0 |
| C-FONT-1 | font word layout | `FONT_SPEC.md`, `fw2/gfx/font.c` | font vectors (every code 0..255) |
| C-FILL-1, -2, -4 | fill address, line-buffer address, done report, wait bound | scan-out contract; B-904, B-929 | fill vectors, fill-wait monitor, chip-model run, Pocket `GFX SCAN LATE` |
| C-MAP-1 | pitch 512, stash layout | HOST_TABLE, `canvas.py` | address vectors |
| C-RECT-1 | rectangle | `canvas.py`, `tau_fb.js`, HOST_TABLE | directed + random |
| C-BAR-1 | bar with `lit` clamped | `canvas.py`, `tau_fb.js`, HOST_TABLE, packs | lit 0, 1, h-1, h, h+1, 127, 128, 511 |
| C-CHAR-1, -2 (1x), -3 | glyph mapping, 1x cell, mix formula | `FONT_SPEC.md`, `fw2` text, shipped screens | font vectors; screenshot gate |
| C-BLIT-1, -3, -6 | blit addressing, DSP blend, width domain | `canvas.py`, `tau_fb.js`, HOST_TABLE, Scope pack, `tb_talos_tilecopy` | blit vectors, alpha 0, 1, 127, 128, 255 |
| C-SBLIT-1, -2 | nearest map, 127 clamp on both axes | `canvas.py`/`tau_fb.js` (columns), shipped hardware + D-T21 (rows) | sblit vectors incl. clamp |
| C-CBLIT-1 | palette lookup | `canvas.py`, `tau_fb.js`, HOST_TABLE | cblit vectors |
| C-FENCE-1 | fence semantics | `gfx.c`, HOST_TABLE, `tb_tau_gfx_t3` | fence storms |
| C-CORN-1 | CORNERS pixels | Gallery 1.0.0, Gallery bench | Gallery vectors, thumbnail golden |
| C-DSB-1 | DSBLIT pixels | Gallery 1.0.0, Gallery bench | Gallery vectors, thumbnail golden |
| C-DBUF-1, -2 | double-buffer rule, flip at vblank | `gfx.c`, `tb_helios_dbuf` | dbuf vectors |
| C-ORD-1 | row read before row write, top-down | `fw2/gfx/scroll.c`, `tb_tau_gfx_move` | overlap vectors |
| C-ORD-4 | sticky changes after a fence apply | `gfx.c` | fence-then-change vectors |
| C-RET-1, C-IDLE-1 | retire once, idle definition | T3 spec, `tb_tau_gfx_t3`, `fw2` | status vectors |
| C-VIEW-1, -2, C-DBL-1 | viewer latch, fetch, doubler | Gallery 1.0.0, `tb_tau_gfx_view` | ported frame bench |

**Count (v0.1, counted by identifier in sections 1 to 6):** 38 PINNED, 22 MATCH, 12 FREE, 3 CHANGE (C-OP-1, C-CHAR-4, C-ESC-1), 1 PROPOSAL (C-IF-5), 1 latency REQUIREMENT (C-IDLE-2); 77 identifiers. UNCONFIRMED: 1 (the consequence of C-DBUF-3); every other rule that was uncertain was settled by the probe or by an existing bench (B-1178).

---

## 9. Open questions for the owner
| # | Question | Facts | Options |
|---|---|---|---|
| Q1 | Viewer block address | `0x1EC..0x1F8` is Tau Alpha's save RAM; `0x280..0x290` is free in Gallery and Reader; Tau Alpha's 9-bit decode aliases it onto `0x080` | (a) `0x280..0x290` as proposed, 10-bit decode only in VIEW builds (recommended); (b) another free block above `0x27C` |
| Q2 | Light text table | in use: the B-316 fit made with an inherited tool; derived alternative differs one step at `c` = 8..13 (`TEXT_WEIGHTS_REDO`); D-T21 made the redo optional | (a) keep the values in use (pixel-identical, but 16 numbers of inherited-tool lineage); (b) take the derived table in the same release as the dark one (one more pixel change, provenance clean) |
| Q3 | Reserved opcodes as no-ops | approved in principle (scoping 7.2 under D-T21); Tau V/Tau Μ old firmware never sends 10..15 | confirm IMP-RESERVED-NOOP as an approved class |
| Q4 | Capability bits for later improvements | D-T21: wide blit rows and overlap-safe copy are later opt-ins | reserve bit 22 (WIDE) and 23 (OVERLAP) now, or allocate when built |
| Q5 | `CBLIT_BURST` | parameter of the old engine, no pixel effect | keep as an ignored parameter (proposed) or drop it from `tau_gfx` |
| Q6 | BAR clamp in the models | `canvas.py`/`tau_fb.js` do not clamp `lit` to `h` | correct the models together with the SBLIT row clamp (proposed) |
| Q7 | Displayed buffer across reset (C-DBUF-3) | the shipped engine keeps it | keep (MATCH) or clear at reset (FREE) after reading `fw2` boot |

---

## 10. Glossary and trace table
**Glossary.** *Accepted push*: a push made while the queue was not full. *Retire*: the moment a command's writes are complete and the retirement count steps. *Frame-offset address*: the 19-bit `A + 512y + x` form. *Blit-class*: BLIT, SBLIT, CBLIT, DSBLIT. *Sticky*: held in SoC registers between commands. *Fill*: the engine's read of one display line into the line buffer for the scan-out. *Unit of engine work*: one memory request or one stretch of internal work between two requests. *Stub*: the functional memory model of the probe (section 7). *Class*: a named kind of allowed difference between old and new engine in the classified differential.

**Trace (requirement to conformance test class, see the conformance plan).** D = directed vectors; R = constrained-random differential; P = performance bench; S = structural/map check; H = Pocket.

| Requirement | Classes |
|---|---|
| C-IF-1, C-IF-5 | D (SoC bench), S (decode, caps) |
| C-Q-1..5 | D, R (back-pressure), S (3 M10K) |
| C-MEM-1..8 | D (protocol monitor on every run), R, P |
| C-FONT-1, C-CHAR-1..5 | D (all 256 codes, both tables, four scales), R, H (screenshot gate) |
| C-FILL-1..5 | D, R (fill-wait monitor), P, H (`GFX SCAN LATE`) |
| C-OP-1, C-GEN-1, -2 | D (reserved and zero vectors), R (out-of-domain fuzz with safety monitor) |
| C-MAP-1, C-RECT-1, C-RUN-1, C-BAR-1 | D, R |
| C-COPY-1, C-BLIT-1..6 | D, R, H (Scope, crossfade) |
| C-SBLIT-1..4, C-CBLIT-1..2 | D, R, H (Chladni, Waterfall) |
| C-RRECT-1, C-CORN-1, C-DSB-1..2 | D (Gallery vectors, thumbnail goldens), R |
| C-FENCE-1, C-RET-1, C-IDLE-1..2 | D, R (fence storms), P |
| C-ESC-1 | D |
| C-DBUF-1..3 | D, R, H |
| C-ORD-1..4 | D (overlap), R (mid-stream changes after fences) |
| C-RST-1..3 | D (reset mid-run) |
| C-VIEW-1..5, C-DBL-1 | D (ported frame bench), P |
| 7.1 to 7.5 | P, S (map probes, two-seed fit), H |
