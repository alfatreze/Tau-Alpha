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
| 0xC0-0xFC | free | | next claimants: EQ extras, accelerators, GPU (see roadmap); allocate here |

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

