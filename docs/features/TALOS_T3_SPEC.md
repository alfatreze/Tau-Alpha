# Talos 2, T3: the graphics status block (idle, fence, free slots, overflow, scanout underflow)

Status: **RTL built and host-proven (branch `talos-t3`), not fitted, not on a Pocket.** Plan: `docs/features/TALOS2_REIMPLEMENTATION_PLAN.md` (T3), decisions D-T03..D-T05. Measured motive: T0 found the CPU issue path, not the engine, is the limit; the one thing the CPU could not observe was whether the engine had finished (the old fence is a 50 ms mailbox poll through the SDRAM window) and whether scanout ever starved.

## What it adds (all additive: an old firmware sees nothing new, and a new firmware probes the present bit)
| Item | Where | Meaning |
|---|---|---|
| `OP_FENCE` = command op 9 (`R_FB_GO` value `0x4001`) | `tau_talos.sv` | draws nothing; it is queued like any command, so it is dispatched only after every earlier command has been dispatched, and it **retires** only when the engine is at `A_IDLE` with nothing chained or in flight. Retiring bumps the fence counter. Ops 10-15 are still the default RUN: do not send them. |
| `R_GFX_FENCE` `0x1B8` (read) | `tau_soc.v` | the number of FENCE commands retired, a 32-bit Gray-synchronised counter (`tau_cdc_gray_ctr`), wraps at 2^32. Firmware counts the fences it issued and waits for `(int32)(counter - issued) >= 0`. Reads 0 on a bitstream without T3. |
| `R_GFX_STATUS` `0x1BC` (read) | `tau_soc.v` | `[31]` present (1 on a T3 bitstream: the probe), `[16]` idle, `[8:0]` free queue slots (0..255, the queue holds 255). |
| `R_FB_GO` bit 1 (read) | `tau_soc.v` | idle: every accepted command has retired. Bit 0 (queue full) is unchanged. |
| `R_GFX_COUNTS` `0x1C0` (read) | `tau_soc.v` | `[15:0]` pushes dropped because the queue was full (saturating; the firmware's poll of bit 0 normally makes this 0), `[31:16]` scanout lines whose fill had not finished when their display started (a 16-bit Gray counter, wraps). |

## How idle is defined (and why it is robust)
Idle is **not** "queue empty": a pushed command is invisible in the engine's domain for two clocks, and the engine's own flags overlap. Idle is `accepted pushes == retired commands`: the `clk_sys` side counts accepted pushes, the `clk_sdram` side counts retirements (`inflight` set at dispatch, cleared at the first `A_IDLE` cycle with no rect, glyph row, blend drain or chained bar/rounded-rect work left), and the retirement count crosses with `tau_cdc_gray_ctr`. A push followed by an idle read one cycle later therefore reads busy. Dispatch now also waits for the chain to be idle (`chain_idle`); for ops 0-8 this changed no cycle (the old-vs-new differential test, `make test-rtl-gfx-diff`, is unchanged: 4,055,236 cycles, 0 differences).

## Scanout underflow
`tau_talos` reports each finished scanout fill (`fill_done_line`, a `tau_cdc_gray_bus` into `clk_vid`, `fill_seen`). At `hc == HOFF` of every active line `tau_scanout` checks that the last finished fill is that line, or the next one (already done). Anything else counts one underflow. Armed after the first fill. This is the instrument for the claim "scanout always wins": on today's engine it should read 0 for ever; if it does not, the SDRAM controller changes of T5 are judged by it.

## Tests
`make test-rtl-gfx-t3` (`sim/tb_tau_gfx_t3.v`: idle low one cycle after a push; FENCE after RECT, BAR, CHAR and RRECT retires only after every row of every earlier command was written and nothing lands later; six fences monotonic, no skip, whole-run monitor; 300 pushes into a stalled queue: 45 counted, 0 free, drain, idle; underflow 0 over about 120 scanlines, then counted when the SDRAM is unavailable for about 80 scanlines; four mutants: fence bumped by every command, overflow not counted, idle ignoring in-flight work, underflow check disabled) and `make test-rtl-gfx-status-soc` (the real VexRiscv reads the four registers; swapped inputs fail it).

## Not built, on purpose
The in-queue `SET_FIELD` / `CLUT_WR` / `RC_WR` opcodes of the plan (section 5.4). They would let a sticky field change mid-stream without a wait, but T0 measured fences at about 2% of the CPU (B-888), the CLUT RAM's write port is in `clk_sys` (moving it to `clk_sdram` risks the M10K inference that B-576 fixed), and the fence is now cheap. Revisit only if a profile shows fence-bound drawing.

## T3b: the burst CBLIT (built with T3, same fit)
`tau_talos.sv` parameter `CBLIT_BURST` (default 1): a `CBLIT` source row is read as ONE SDRAM burst (`A_CBLIT_BURST`, COPY's shape) and each word is looked up in the CLUT as it arrives (`clut_raddr` was already combinational from `p0_q`; the colour is written into the row buffer the next cycle, `cb_v`/`cb_i`), and the row is released after the last write (the blend pipeline's `bl_drain` hold). `CBLIT_BURST = 0` keeps the one-transaction-per-pixel path, cycle for cycle (it is what `tb_tau_gfx_diff.v` compares with the pre-split original). Why: the Waterfall's fullscreen tick spent 678 k of 712 k cycles waiting for the engine (`docs/research/TALOS_T4_MEASUREMENT_RESULT_2026-10-27.md`). `SBLIT` is NOT changed (no measured demand). `make test-rtl-gfx-cblit`: 185 random CBLIT commands mixed with RECT, COPY, BLIT and BAR, random CLUT contents, re-index and strides; every destination region equal word for word with the per-pixel path, the whole memory equal at the end; the wide rows (32 pixels or more) take 3.1 times fewer engine cycles; two mutants (burst one pixel short, pixels one column left) fail.

## T3b, second part: the 400-word scanout fill (built)
`tau_talos.sv` parameter `FILL_WORDS` (default 400; 512 is the old behaviour and what the pre-split differential test runs with): a scanout fill reads the 400 words of the picture, not the 512-word stride. Engine cycles spent in fills over one frame: 144,720 against 185,040 (22% fewer), same picture on every pixel of a frame, no late line (`make test-rtl-gfx-fill`, `sim/tb_tau_gfx_fill.v`; a 399-word mutant fails it). The line buffer words 400..511 of each half are never read by the video side.

## T3b, third part: DQM keying (NOT built, on purpose)
The plan's streaming-write byte mask (so a keyed blit needs no destination pre-read) is a change to the SDRAM controller (`tau_sdram_ctrl`), which D-T06 and the exhaustive review say must be its own commit with its own testbench and the A-094 probe before and after, never bundled with an engine fit; and the measurements (DEV 195) show no meter that draws a keyed blit as a cost. Revisit with the controller work (T5, last) if a profile asks for it.
