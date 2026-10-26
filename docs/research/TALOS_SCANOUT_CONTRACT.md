# Contract for the new `tau_scanout` (Talos 2 T5, step 1; D-T01, D-T07)

This is the written contract the new scanout is built from. It states behaviour only. The author of the new module must work from this file and `docs/features/TALOS2_REIMPLEMENTATION_PLAN.md` (section 4 and the T5 line) and **must not read** `src/fpga/core/tau_scanout.sv`, `sim/oracle/*`, the git history of either, or the scanout part of any older file (`mp3_fb.sv`, `pocket_vector_fb.sv`). The old module stays in the tree only as the **oracle**: the testbench may instantiate it by name (`tau_scanout`) and compare outputs; its text is not a source. Acceptance is a cycle-for-cycle match with the oracle (section 6). The process is recorded in `docs/PROVENANCE.md`.

## 1. Role
The scanout turns the framebuffer rows that the engine copies into a line buffer into a 400 x 360 video stream at a 12 MHz pixel clock (500 x 400 total, 60.000 Hz), asks the engine for the next row one scanline ahead, tells the engine when vertical blanking has begun, keeps the picture black until the CPU has started painting, and counts lines whose fill was late.

## 2. Ports (module `tau_scanout_new`, drop-in for `tau_scanout`; names, widths and clock domains are fixed)
| Port | Dir | Width | Clock | Meaning |
|---|---|---|---|---|
| `reset` | in | 1 | treated as synchronous to `clk_vid` for the video counters | clears the counters |
| `clk_sys` | in | 1 | CPU clock | only the painted level and the output of `underflow_cnt` |
| `clk_sdram` | in | 1 | engine clock | line-buffer write port; source of the fill-done events |
| `clk_vid` | in | 1 | 12 MHz pixel clock | everything else |
| `cmd_push` | in | 1 | `clk_sys` | high for one cycle when the CPU pushes a draw command |
| `lb_we`, `lb_waddr[9:0]`, `lb_wdata[15:0]` | in | 1/10/16 | `clk_sdram` | line-buffer write port: write `lb_wdata` at `lb_waddr` when `lb_we` |
| `fill_done_inc`, `fill_done_line[9:0]`, `fill_seen` | in | 1/10/1 | `clk_sdram` | the engine finished a fill for row `fill_done_line` (`fill_done_line` stays valid until the next one); `fill_seen` is a sticky level, 0 until the first fill has finished (`fill_done_inc` is not needed by the checks below; keep the port) |
| `underflow_cnt[15:0]` | out | 16 | `clk_sys` | number of late lines, wrapping, see section 5 |
| `fill_req_tgl`, `fill_line_req[9:0]`, `vblank_tgl` | out | 1/10/1 | `clk_vid` (registers, power up 0) | request toggle, the row it asks for, the per-frame vertical-blank toggle (the engine samples them in `clk_sdram`) |
| `video_rgb[23:0]`, `video_de`, `video_hs`, `video_vs` | out | 24/1/1/1 | `clk_vid` (registers) | the video stream |
| `scan_vc[8:0]` | out | 9 | `clk_vid` | the low 9 bits of the line counter, combinational from it |

## 3. Geometry and counters
* Pixel counter `hc` counts 0 to 499 and wraps; line counter `vc` counts 0 to 399 and advances when `hc` wraps. Both are 11 bits wide, start at 0 and are held at 0 while `reset` is high (sampled on `clk_vid`).
* The active window is `hc` 8 to 407 inclusive and `vc` 4 to 363 inclusive (400 x 360). Pixel (x, y) of the picture is at `hc = 8 + x`, `vc = 4 + y`.
* Horizontal sync is high for `hc` 420 to 459 inclusive. Vertical sync is high for `vc` 367 to 370 inclusive.
* There is no forced-black band at the top of the picture.

## 4. The fill request and the vertical-blank toggle
* On every cycle where `hc == 0`, if the **next** line (`vc + 1`, with 399 followed by 0) lies in the active line range (4 to 363), the scanout loads `fill_line_req` with that line minus 4 (the framebuffer row 0..359 that will be shown on that line) and inverts `fill_req_tgl`, in the same cycle. Outside that condition neither changes. (So a request is issued one scanline ahead, every scanline whose successor is active, including line 399 for line 0.)
* On the cycle where `hc == 0` and `vc == 367`, `vblank_tgl` inverts (once per frame).
* All three are registers that power up at 0.

## 5. The video outputs, the line buffer and "painted"
* The line buffer holds 1,024 words of 16 bits (RGB565), a parity-split double buffer: the half with index 0 holds the rows whose line number is even, index 1 the odd ones (the engine writes the half it chooses through `lb_waddr[9]`; the scanout reads the half given by the parity of the line counter `vc`, bit 0). A word written with `lb_we` on a `clk_sdram` edge is stored at `lb_waddr`. The array must be inferred as an on-chip block RAM with a `clk_sdram` write port and a `clk_vid` registered read port (`(* ramstyle = "M10K" *)`): the same resource as today.
* For the counter position (`hc`, `vc`) at a `clk_vid` edge the scanout reads the word at address `{vc[0], (hc - 8)[8:0]}` (a registered read: the word is available one edge later).
* **Output timing (cycle exact):** the four video outputs are registers that show, two `clk_vid` edges after a counter position, what that position defines: `video_de` = active, `video_hs`, `video_vs` = the sync windows of section 3, and `video_rgb` = the line-buffer word read for that position expanded to 24 bits as `{r5, 3'b000, g6, 2'b00, b5, 3'b000}` (no bit replication), **or all zero** when the position is not active or when the picture is not yet painted.
* **Painted:** `painted` goes high the first time `cmd_push` is seen high on a `clk_sys` edge and never goes low. It reaches the video side through a three-flop synchroniser on `clk_vid`; the picture is black while the third flop is 0.
* `scan_vc` = the 9 low bits of `vc`.

## 6. The late-line counter (Talos 2 T3)
* `fill_done_line` steps by +1 per fill, so it is carried from `clk_sdram` to `clk_vid` with `tau_cdc_gray_bus` (parameters `W` 10, `STAGES` 3). `fill_seen` is carried with a three-flop synchroniser on `clk_vid`.
* On the cycle where `hc == 8` and `vc` is in 4 to 363 and the synchronised `fill_seen` is 1, let `n` = `vc - 4`. The line is **late** unless the carried `fill_done_line` equals `n`, or the next line also exists (the condition of section 4 holds) and it equals `n + 1`. A late line produces one `clk_vid` pulse (registered, one cycle after the check).
* The pulses are counted in a 16-bit counter that crosses to `clk_sys` with `tau_cdc_gray_ctr` (`WIDTH` 16, `rst_src` and `rst_dst` = `reset`); `underflow_cnt` is its output.

## 7. Acceptance (the author builds this testbench, `sim/tb_tau_scanout_diff.v`, and a Makefile target `test-rtl-scanout-diff`)
1. Instantiate the oracle `tau_scanout` and `tau_scanout_new` on the same three clocks (`clk_sdram` 100 MHz, `clk_sys` 50 MHz, `clk_vid` 12 MHz with a non-integer ratio, as the real design has) and the same random stimulus: `lb_we`/`lb_waddr`/`lb_wdata` bursts, `fill_done_inc`/`fill_done_line` following the requests with random delays (including late and missing ones and two in one line), `fill_seen` rising after a random time, `cmd_push` after a random time, and a reset in the middle of a run.
2. Compare **every output on every edge of its own clock**, over at least three full frames (about 50 ms of simulated time) per run, with several seeds.
3. Coverage counters in the bench must show the run exercised: all four video outputs toggling, `fill_req_tgl` and `vblank_tgl` toggling, at least one late line counted, `painted` rising mid-run, a reset mid-run.
4. At least six mutants of `tau_scanout_new` (for example: active window one pixel off, sync one pixel off, output latency of one cycle, wrong parity bit, `vblank_tgl` on the wrong line, no `painted` gating, late check at the wrong `hc`, request one line early) must each make the bench fail; a Makefile target runs them.
5. `make test-rtl` (with the new target added) and the Verilator lint line for the module must be clean of new errors. The author reports the fit-relevant facts: that the line buffer is a single `ramstyle M10K` array, the register count, and that no latch is inferred.

## 8. What the author must report
Every file it read (paths), the files it wrote, the bench statistics (cycles, frames, coverage), the mutants and which bench check killed each, and anything in this contract it found ambiguous or contradictory (a difference between the oracle and the contract is a finding to report, not something to silently match by reading the oracle's text).
