# Tau Alpha's SDRAM scheduler path (`tau_sdram_sched_a`): design notes, verification, first Pocket build

Date 2026-10-29. Contract section 6 (`SDRAM_CONTROLLER_CONTRACT.md`). **Simulation only: nothing in this note was run in Quartus or on a Pocket. Areas are reasoning, not measurements; the probe variant that measures them is prepared (`tools/sdram_probe.py launch NAME --variant sched_a`) and was not launched.** Branch `worktree-agent-a1d10dee1c08e234d`, merged from `main` 7ee757b4.

## 1. What it is

A build-time alternative to `tau_sdram_arbiter` + `tau_sdram_ctrl` + the SDRAM half of `tau_sdram_cpu_bridge`. Selected by the macro `TAU_SDRAM_SCHED_A` in `core_game.vh`; **absent = the old controller, unchanged (D-T08)**.

```
tau_gfx (tau_talos: scan-out FILL + draw engine, ONE master, p0 protocol)
   p0_* + sideband (p0_is_fill, p0_rd_len) --> tau_sdram_a_gfx_if --+-- port 0  scan-out fill (urgent)
                                                                    +-- port 1  draw engine
CPU: wb adapter -> bridge mux -> tau_sdram_a_cpu (mailbox, clk_sys | clk_sdram) -- port 2 (scheduler protocol)
                          tau_sdram_sched_v  NPORTS 3  -->  tau_sdram_phy  -->  dram_* pins (same pins as the old controller)
```
`tau_sdram_sched_a.sv` holds the PHY, `sched_v`, the gfx adapter, the reset synchroniser and the busy tracker; `tau_sdram_a_cpu.sv` is the CPU mailbox (the CPU port is exported as a plain section-4 port).

### The `sched_v` configuration (3 ports; sched_v and the PHY are unchanged, `git diff main` on them is empty)
| parameter | value | why |
|---|---|---|
| `NPORTS` | 3 | fill, engine, CPU |
| `ENABLE_2D` | 0 | no 2D requests: the 2D fields and the address adders of the pieces are removed |
| `BANK_HASH` | 2 | the contract's bank function (no region table, `sk_*` tied off) |
| `URGENT_CAPABLE` | port 0 only; `urgent` tied 1 | every fill is urgent; `URGENT_EARLY_CAPABLE` 0, no bandwidth caps (`BW_WINDOW_CYCLES` 0) |
| `BATCH_PORTS` | 0 | the draw engine has one request in flight; a batch would only add the 1 us defer (`WR_DEFER_NS`) to every row |
| `QDEPTH` | 1 (parameter) | every port has at most ONE request outstanding (Talos is serial, the CPU mailbox is one-outstanding, the fill is one request), so a second queue entry can never be used. The ablation's "only look-ahead earns throughput" applies unchanged: look-ahead, the open-row table and refresh are kept; depth 2 also passes every test (`engine_qd2`) and is a one-parameter change if a future client keeps two requests in flight |
| `CNT_EN` | 0 in the core, 1 in the benches | per-port statistics are about 140 flops per port; off, `bad_req` stays |
| `W_LEAD` | 2 | the adapters are built for 2 |
| weights, `MAXG_*` | 1/1/1, 128 | equal rotation among non-urgent ports; a grant is at most 128 words (the urgent bound's `L_MAX`) |

### Client adapters (thin; the only protocol translation)
* **Graphics master (`tau_sdram_a_gfx_if`).** Talos keeps one request in flight and ends a read with `p0_end_burst_req`; a scheduler needs the length up front. Two **sideband outputs were added to `tau_talos`/`tau_gfx`** (additive, combinational, open and removed by synthesis on the old path): `p0_is_fill` (the read is a scan-out FILL) and `p0_rd_len` (words the burst will consume: `FILL_WORDS`, 1 for SBLIT/CBLIT_RD, `char_w` for COPY/KEYDST/CBLIT_BURST, 256 when `char_w[6:0]` is 0 as the engine's 8-bit counter does). Fill -> port 0, everything else -> port 1. The request fields go to the port in the very cycle of the pulse (no register), and are latched and held only if the port is not ready. `p0_end_burst_req` is ignored. `p0_available` = initialised and nothing pending/outstanding (falls the cycle after the pulse, returns the cycle after `done`); `p0_ready` = `done`; `p0_data_available`/`p0_q` = `r_valid`/`r_data`. A streamed write: `w_idx` -> `wsrc_addr`, `wsrc_q` registered once more so `w_data` is `W_LEAD` = 2 cycles after `w_next` (`glyph_q` already has one cycle). A constant write holds `p0_data`; the byte enables are the request's. A write over 512 words is turned into a rejected request (counted in `bad_req`, completes at once); the engine's longest row is 511.
* **CPU (`tau_sdram_a_cpu`).** The clk_sys half is the old bridge's mailbox (toggle, 3-flop synchronisers, payload held stable). A 32-bit access is ONE 2-word request: a write with the four byte enables as two 2-bit enables (`w_be` selected by the delayed `w_idx[0]`), a read assembled from the two `r_valid` words and returned on `done`. The request is presented in the cycle the synchronised toggle is seen, from the stable mailbox registers (no `op_*` copy: one cycle and 56 flops less than the old bridge). The old bridge's two 16-bit operations existed to give the scan-out an arbitration point between them; the urgent port does that now. The old bridge's Phase 2 probe taps do not exist on this path (tied 0 in `core_game.vh`).
* **`busy`** (every port: request presented or accepted and not done) replaces `arb_p0_available` for the existing `R_SDR_BUSY` counter, so the busy-cycle register keeps working unchanged.

### One consequence worth knowing
Talos is one serial master: a pending scan-out fill still waits for an engine burst that is already in flight (it is dispatched at Talos's `A_IDLE`), exactly as with the old controller. The urgent port therefore buys the fill priority over the CPU, over refresh timing and a faster, look-ahead service, **not** pre-emption of the engine. Concurrent fill and engine requests need a second request FSM in Talos (the scheduler would take both unchanged); that is a follow-up, not in this task.

## 2. Area and timing estimate (reasoning only, to be replaced by the probe)
Measured earlier in Quartus (probe, not the core): PHY 367 ALMs; `sched_v` with 6 ports, 2D, depth 2 and the region table 3,701 ALMs, core Fmax 91 MHz (one 2D adder path left).
| block | estimate |
|---|---|
| PHY | 367 ALMs (measured) |
| `sched_v`, 3 ports, no 2D, depth 1, no caps, `CNT_EN` 0 | 1,400 - 2,000 ALMs: the timing model (about 100 flops), the read descriptor FIFO, the write FIFO, the decision pipeline and the arbiter are port-count independent; the per-port queue, wait timers, RAW ranges and the 6:1 muxes roughly halve; 2D adders and wide entries are gone |
| `tau_sdram_a_gfx_if` | about 90 flops, 100 - 150 ALMs |
| `tau_sdram_a_cpu` SDRAM half | about 40 flops (the 130-flop mailbox exists today in `tau_sdram_cpu_bridge`) |
| reset sync, busy tracker | < 20 ALMs |
| **total** | **about 2,000 - 2,700 ALMs, 0 DSP, 0 - 2 M10K** |
The old path (`tau_sdram_ctrl` + arbiter + bridge) is not in the budget table; the net cost is the difference, expected +1,000 to +1,800 ALMs. Timing: the PHY and `sched_v` registered offers were designed for 100 MHz; the 3-port `sched_v` removes the one 91 MHz path, but **no fit of this configuration exists**: the probe variant `sched_a` is the first thing to run (about 15-30 min on the VM, owner's approval, rule 3). `CNT_EN` 1 adds about 420 flops.

## 3. What is verified in simulation (iverilog, chip model with the measured I/O delays, every word checked)
Run: `sim/sdram_sched_a/run_tests.sh` (unit/system, PHY + `sched_v` + chip model + the real `tau_sdram_a_cpu` and the Wishbone adapter/mux) and `sim/sdram_sched_a/run_gfx.sh [NCMD]` (the real `tau_gfx` on three memory stacks). All finish with `phy_fault` 0, zero chip errors, `dbg_pmiss` 0.

| test | what it covers | result |
|---|---|---|
| `cpu` | every one of the 16 byte-enable combinations, write-after-write, read-after-write, 300 random words in 3 regions | PASS |
| `engine`, `engine_qd2` | constant writes of 1..512 words (page-crossing offsets), partial byte enables, streamed writes through the `wsrc` pull, reads of 1..512, 60 copies (read then streamed write); depth 1 and 2 | PASS |
| `readsizes` | reads of every length 1..130, 256, 400; the fill counted on port 0 only | PASS |
| `mixed`, `mixed_refresh_half` | master + CPU + fills at once on disjoint regions; refresh interval halved | PASS |
| `urgent`, `urgent_refresh_half` | 60 fills of 400 words against a CPU that never stops and engine rows of 480 words, refresh interval halved: first word after 30 cycles at most (bound 155), every fill under 10 us, the fills counted on the urgent port | PASS |
| `refresh_quarter` | refresh interval a quarter of normal under sustained load: 510 refreshes, no PHY fault, data intact | PASS |
| `idle` | `p0_available` low from the cycle after a request, back one cycle after `p0_ready`; `busy` follows; memory complete at `p0_ready` | PASS |
| `wishbone` | the CPU's Wishbone beats through `tau_sdram_wb_adapter` + `tau_sdram_bridge_mux` + `tau_sdram_a_cpu`, back-to-back with STB held (gap 0) and with gaps, a load after an all-ones store, exactly one bridge request per beat (the A-093 regression), while the master streams | PASS |
| `latency` | idle-system latencies (below) | PASS |
| mutants | `MUT_A_WLEAD` (write data one cycle early), `MUT_A_NOFILLPORT`, `MUT_A_CPUBE` (byte enables swapped), `MUT_A_RDLEN` (read length off by one), `MUT_A_AVAIL` (`p0_available` ignores the outstanding request) | all killed |
| `tb_a_gfx` | see section 4 | see section 4 |

FENCE/idle semantics: the Talos block is unchanged apart from the two sideband outputs; `p0_ready` arrives when the last write beat is at the pins (`done`), so a retire (back to `A_IDLE`) still means "written". `tb_a_gfx` counts FENCEs against the reference and checks that **no word is written to the chip after `gfx_idle`**.

Existing suites (RTL unchanged, run again on this branch): see section 4.

## 4. Measurements and the player's traffic, old path vs new path
MEASUREMENTS_PLACEHOLDER

## 5. What the firmware / MMIO needs
* **Nothing to start.** The register map is unchanged; `R_SDR_BUSY` (0x800000BC, the busy-cycle counter, `TAU_SDRAM_BUSY` bitstreams) counts `busy` of `tau_sdram_sched_a` ("any port has a request pending or outstanding"), the same meaning as the old "controller not idle". Its numbers are not comparable one-for-one (the new path is idle between a request's `done` and the next, the old one was busy through the whole bridge handshake).
* **Optional, a later task (do not change firmware now):** the per-port statistics of `sched_v` (`CNT_EN` 1: requests, words, busy cycles, worst wait, urgent waits over the bound, bad requests, per port 0/1/2) can replace the single counter. They live in `clk_sdram`; a read window needs a CDC: propose two new registers, `R_SDA_SEL` (write: `{port[1:0], stat[2:0]}`) and `R_SDA_VAL` (read: a gray-coded snapshot of the selected 32-bit counter, `tau_cdc_gray_bus`), 8 bytes of the MMIO page and about 420 flops. `GFX SCAN LATE` (the scan-out underflow, `gfx_underflow`) and the soak need no change.
* The meter/statistics code that divides `R_SDR_BUSY` by a period keeps working; only its meaning of "busy" shifts as above.

## 6. Risks
1. **Timing at 100 MHz is unknown** (section 2); the first step is the probe. The PHY side was proven on a Pocket in Tau V's memtest (rc2), the 3-port `sched_v` was not.
2. **The read-capture constraint** (RISE register + `set_multicycle_path -setup 2 / -hold 1`) is new to the player's SDC; a pattern that matches no register silently drops it (B-916). Check the collection size in the report.
3. **Talos sideband.** Two outputs added to `tau_talos`/`tau_gfx`; the old path leaves them open. `p0_rd_len` reproduces the engine's own end-of-burst count (including `char_w[6:0] = 0` -> 256, which the engine's 8-bit counter gives); a future change to a burst length in Talos must keep `p0_rd_len` in step (the `readsizes` test and the gfx bench catch a mismatch as a hang or a wrong image).
4. **A write over 512 words is rejected** (the engine's rows are at most 511). A scheduler rejection does not hang but writes nothing.
5. **No fill pre-emption of an in-flight engine burst** (section 1): the scan-out margin is the same as today, not better, for that case. The urgent port helps against the CPU and refresh.
6. **CPU latency is dominated by the mailbox**, not the scheduler: the two CDC crossings (3 destination clocks each) cost about 200 ns of the 250 ns in the idle case. A faster CPU path means a different bridge (a clock-domain-crossing FIFO or running the CPU port in `clk_sys`), not a scheduler change.
7. The old controller's Phase 2 probe overlay shows zeros on this path.
8. `TAU_SDRAM_SCHED_A` in `core_game.vh` was **not elaborated by a tool** (the core needs the APF IP); it was reviewed against the port lists of the new modules, which the benches instantiate with the same connections. The first Quartus run is its first elaboration.
9. Reset: the old path used `~pll_locked` raw; `sched_a` synchronises it (3 clocks) inside. `tau_gfx` waits for `sdram_init_complete` (the PHY's `init_done`, about 100 us after reset), so no request can precede the reset's release.

## 7. First Pocket build (do NOT do it from this note without the owner)
1. **Probe first** (rule 3, the owner approves the VM): `python3 tools/sdram_probe.py launch SA1 --variant sched_a` (expect 15-30 min; the tool prints the local finish time), `status SA1`. Read the rows `u_sa|u_phy`, `u_sa|u_sched`, `u_sa|u_gfx`, the Fmax and the slack per corner. Go/no-go: `sched_v` row under about 2,500 ALMs, 0 DSP, slack positive at slow 85C for the core and capture paths (capture setup about +0.65 ns as in Tau V's rc2 fit).
2. Apply the QSF/SDC changes of `src/fpga/core/tau_sdram_sched_a_assignments.txt` (sources, macros `TAU_SDRAM_SCHED_A`, `TAU_SDRAM_ALTDDIO_OUT`, FAST_* register assignments, read-capture multicycle). The owner reviews them: they touch `ap_core.qsf` and `core_constraints.sdc`.
3. Fit with `tools/vm_fit.py launch` (about 1h40-1h45 from the shipped family; say the finish time, rule 3). Check in the fit report: the capture multicycle matched 16 registers; the output slacks at slow 85C and fast 0C; the dram_* pin registers sit in the I/O cells; ALMs against the estimate above; heap floor unaffected (no firmware change; `tools/check_heap_gap.py` as usual).
4. Package it as a test core named `TAU DEV NN` (next number, remove the superseded cores on the card, memory note) with `tools/install_dev_core.py` (dry run, then `--yes` after the owner's approval, `--card /Volumes/Pock`). The firmware is the shipped one (no change needed).
5. On the Pocket, in this order: the boot and the splash (init); the Check page (the SDRAM map and the engine tests); `GFX SCAN LATE` 0 while playing and scrolling (Waterfall, Settings); the CPU-heavy pages (Library browse); then the soak (`SOAK FAILS` 0 over 30 minutes warm) and the owner's warm soak. Log each in `docs/AUDIT_TRAIL.md` with its evidence label. If anything fails, the old controller is the default build: rebuild without the macro.
