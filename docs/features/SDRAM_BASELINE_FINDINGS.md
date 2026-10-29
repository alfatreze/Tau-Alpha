# The OLD SDRAM controller through the new chip model: baseline findings (2026-10-29)

Contract 7.1 asks for this first: `tau_sdram_ctrl.sv` (the RTL the player ships: 100 MHz, CL2, `WRITE_BURST` 1, full-page bursts, no I/O register packing before S0) driven through `sim/sdram_chip_model.sv` (AS4C32M16MS -6 numbers, command setup/hold 1.5/1.5 ns, tAC 8 ns / tOH 1.8 ns at CL2). Bench, scripts and the assumptions behind the delay scenarios: `sim/sdram_baseline/` (README). **Simulation only: no Quartus, no Pocket.** Evidence label: SIMULATION (iverilog, 4-state), delays ASSUMED where marked.

Runs: two controllers, **pre** = the file before commit `cafd7477` (tRFC setting 80 ns) and **s0** = the tree (98 ns; S0's other changes are QSF/SDC, not RTL, so the I/O packing is modelled by the delay scenarios, not by the file); five delay scenarios; 8 ms of the player's traffic each (CPU word reads and writes, scanout fill bursts, engine copies with streamed writes, 512-word clear chunks, idle gaps; every read word checked against a scoreboard); plus a 64 ms mixed run for both controllers and a 110 ms write-heavy run; plus an as-is init run and delay sweeps.

## Headline
1. **tRFC violation: confirmed, certain, fixed by S0.** Every auto refresh of the pre-S0 controller is followed by a command 90 ns later (the `CEIL` macro turns 80 ns into 9 clocks) against 97.5 ns: 2 in the init (REF2 after REF1 at t = 101.27 us, the MRS after REF2) and 6,784 of 6,860 refreshes in 8 ms. The s0 controller (98 ns, 10 clocks) raises none in any run.
2. **EMRS missing: confirmed, certain, NOT fixed by S0.** The controller sends PREA, REF, REF, MRS (0x027, correct) and stops; the model stays in init and flags the 106 commands of a short run (first at 101.59 us, the first ACT after the MRS). The bench has to waive it to look at anything else.
3. **Two more init deviations from the contract, not fixed by S0:** CKE is low for the first 50 us (5,003 edges), so only about 5,100 NOP edges with CKE high precede PREA (contract 3.7: 10,000, CKE high from the first edge).
4. **tREF: raised under write-only traffic, not under the player's mix.** With 110 ms of writes only (512-word clear chunks, streamed copies) the REF spacing is 11.5 us (min 10.55, max 11.61) against the 7.81 us the 8,192 rows need: `SE_TREF` 3,991 times, first at 64.16 ms. In the 64 ms mixed run it is clean, because of a side effect worth knowing: **the controller refreshes before every read request** (its `needs_burst_refresh` is true all the time: counter + 1023 + 16 is never below the 751-clock timer), so the mean REF interval under mixed traffic is 1.1 us, about 7 times the need and about 9 % of the busy time. Not an error; a bandwidth finding.
5. **Timing at the pins is a narrow window, set by delays nobody constrained.** At a chip clock 3.5 ns after the controller clock the rising-edge capture works with 0 mismatches over 3.0 M read words, but the model's margin is 0.3 ns on the hold side (tOH 1.8 ns stacked with tAC 8 ns). The sweeps put the working region at a chip-clock output delay of 3.0 to 4.5 ns and a command/data output delay of at most 7.0 ns; outside it, CAPTURE, SETUP, HOLD or a one-clock command shift appear (section "Sweeps"). S0's I/O packing and SDC change the output side only; the input side is unchanged.
6. **Nothing else fired:** no BUS_CONFLICT (even with a 3..8 ns output-enable envelope), no TRCD, TRP, TRAS, TRC, TRRD, TDPL, TWR, TDAL, ACT_REF, REF_OPEN, COL_CLOSED, A10_COL, BST, TRAS_MAX, TMRD, MRS_VALUE, TCK, TCCD. The ACT/PRE/read/write machinery is legal against the model's rules.

## Table
Counts are for 8 ms of traffic (the init-time errors are included); "pre" = tRFC 80, "s0" = tRFC 98. Delay scenarios: `nominal` (chip clock +3.5 ns, outputs +1.0 ns) unless stated. Confidence: **certain** = follows from the RTL and the datasheet number, independent of delays; **probable** = real behaviour, but whether it is a violation depends on a contract or model reading; **depends** = switches on or off with the modelled delays.

| error class | count (pre / s0) | first occurrence (pre) | rule | confidence | cause | fixed by S0? | what the new PHY does instead |
|---|---|---|---|---|---|---|---|
| `TRFC` | 6,784 / 0 (56,806 / 0 in 64 ms); 2 of them in the init | 101.2735 us, edge 10124: REF2 only 9 clocks after REF1 | tRFC 97.5 ns after REF (AS4C32M16MS -6 AC table) | **certain** | `SETTING_T_RFC` 80 ns, `CEIL` gives 9 clocks = 90 ns, 7.5 ns short | **yes** (98 ns gives 10 clocks) | tRFC is 10 clocks after every REF, in the init too (PHY finding P16), and the interlock refuses any earlier command |
| `INIT_ORDER`: expected EMRS after MRS | 106 (short as-is run) / 106; first 101.5935 us | first command after the MRS | power-on sequence needs the extended mode register set (datasheet power-on text; contract section 1, B5) | **certain** | the init FSM ends at the MRS; there is no EMRS state | **no** | PREA, tRP, REF, REF, MRS, tMRD, EMRS, tMRD, then `init_done` |
| `INIT_ORDER`: fewer NOP edges than INIT_NOP_MIN | 1 / 1; 101.0635 us | at the PREA | contract 3.7: 10,000 NOP edges (at least 100 us) before PREA | **probable** (the datasheet text read allows CKE to rise during the 100 us; the contract does not) | CKE stays low until clock 5,001 after reset, so half the 10,100 wait clocks are not counted | no | CKE = 1 from the first PHY cycle, `INIT_WAIT_CYCLES` 10,000 counted from CKE high |
| `CKE_LOW` | 5,003 / 5,003; 33.5 ns | the first chip edge | contract 3.2 / 3.7: CKE high at every edge | **probable** (same reading as the row above); 3 of the edges fall while the bench is still applying reset | the controller drives CKE low in reset and raises it at clock 5,001 | no | CKE is 1 from the first cycle and never lowered |
| `UNKNOWN` (X on address / bank at the PREA) | 1 / 1 (only in the as-is run) | 101.0635 us | model rule: no X on a command's address or bank | **certain** in 4-state simulation, a **simulation artefact** on hardware (Cyclone V registers power up to 0; the bench sets them to 0 in the normal runs) | `SDRAM_A`, `SDRAM_BA`, `SDRAM_DQM` have no reset value | no | every output has a reset value; DQM resets to 11 |
| `COL_WRAP` | 105 / 105 (82 write, 23 read) | 111.96 us | contract 3.4: a column wrap past 1023 is a violation | **probable**: real controller behaviour, **harmless on the chip** | write: the 512-word chunk at column 512 ends on column 1023 and the full-page burst stays open for tWR + PRECHARGE, so the chip's counter wraps over masked words; read: the page-end stop is 2 to 3 words late, so the chip launches one word past column 1023 that the controller never captures (0 scoreboard mismatches). The model flags masked and unwanted words too (the PHY finding P12) | no | never offers a column command or burst that needs a wrap; the burst is ended before column 1023 |
| `CAPTURE` | 0 / 0 at `nominal` and `*_early`; **451,930 / 447,475 (every read word) at `unp_late` and `pk_late`** | 228.22 us (first read) at `*_late` | tAC 8 ns / tOH 1.8 ns (CL2) against the capture edge | **depends on modelled delays** (it is a property of the design that the window, 3.8 ns wide, is crossed by a fixed rising edge; where the edge falls is set by the chip-clock output delay) | the controller samples `p0_q` (the pin) on the rising edge 3 clocks after the READ; the valid data window (tAC 8 ns to tOH 1.8 ns after the chip edge) is only 3.8 ns wide, so just a chip-clock output delay of about 3.0 to 4.5 ns puts that edge inside it | **no** (S0 did not touch the input side) | falling-edge capture into one input register per bit (`CAP_MODE` FALL), window checked by the model; S0-style I/O registers on every pin |
| `WDATA_X` | 0 / 0 at `nominal`; 143,232 / 141,824 at `*_late` | 232.5 us | unmasked write byte X/Z | **artefact** | consequence of CAPTURE: the bench's engine copy writes back the corrupted (X) words it read | n/a | n/a |
| `SETUP` | 0 in the matrix; 333 at command delay 7.5 and 8.0 ns in the sweep | | command setup 1.5 ns (model; the sheet prints 0.5 / 1.5, see SDRAM_CHIP_MODEL_FINDINGS) | **depends**: appears only if the unpacked fabric-to-pin delay exceeds 7.0 ns at a 3.5 ns clock delay | no I/O register packing and no SDC before S0 | S0 packs the outputs and constrains them | I/O registers on every output, SDC with the chip's setup and hold |
| `HOLD` | 0 in the matrix; 324 at command delay 9.0 ns, 229 at clock delay 5.0 ns and 323 at 6.0 ns in the sweeps | | command hold 1.5 ns | **depends** | same | same | same |
| `TREF` | **0** in 64 ms mixed / **3,991** in 110 ms write-only; first 64.16 ms | | 8,192 rows in 64 ms (+ 8 x 7.8 us slack) | **certain for a write-only stretch** of about 64 ms or more; **depends on traffic** (in the player's mix the controller over-refreshes) | refresh only in IDLE between operations: 751 clocks plus up to one 512-word burst (524 clocks), mean 11.5 us | no | refresh debt every 780 clocks, forced refresh at the maximum debt, preemption at command boundaries |
| `BUS_CONFLICT` and every rule not listed | 0 | | | n/a | | | |

Not an error class but measured: refresh spacing in the 64 ms mixed run is 1.12 us mean (min 0.09 to 0.10 us, max 11.3 us); 56,657 refreshes (s0) in 64 ms against the 8,192 needed.

## Which findings are the controller and which are the bench or the model
* **Controller, certain:** tRFC 90 ns (fixed by S0); no EMRS; refresh only in IDLE (the write-only tREF result) and the always-true `needs_burst_refresh`; the 5,001-clock CKE-low start.
* **Controller, but only a violation under a stricter reading:** CKE low during the first 50 us and the NOP count; the column wrap (the model flags masked and unwanted words; the PHY finding P12 says the same about the new PHY).
* **Depends on delays I assumed** (no source gives the unpacked output delays of the old design; the S0 notes give only the SDC, 0.8/1.8 ns for the commands and 1.8/1.3 ns for the rest): CAPTURE, SETUP, HOLD. The sweeps give the switching points so the conclusion does not rest on the guess. One corner per run: the model takes one delay per signal, so late and early corners are separate runs, and tAC max with tOH min are stacked in every capture check (pessimistic; the loaded tOH is 2.5 ns).
* **Bench artefacts handled:** the init flood after a missing EMRS (waived by writing the model's init state through hierarchical names, `raw` shows it), X on the controller's address registers (set to 0), chip-clock edges before the controller has seen reset (gated), the iverilog `typedef struct` limitation (the `packed` keyword in a build copy).
* **Not a controller finding:** `WDATA_X` (the bench copies what it read).

## Sweeps (S0 controller, 100 us idle plus a short traffic set; one value per signal group)
| command / address / data / OE delay, chip clock +3.5 ns | result |
|---|---|
| 1.0 to 7.0 ns | clean (CKE_LOW, INIT_ORDER, COL_WRAP only) |
| 7.5, 8.0 ns | `SETUP` 333 (the command arrives less than 1.5 ns before the chip edge) |
| 9.0 ns | `HOLD` 324 (the command lands after the chip edge and is sampled one clock late) |

| chip clock delay (outputs +1.0 ns, board 0.2 to 0.6 ns) | result |
|---|---|
| 0 to 2.5 ns | `CAPTURE` on every word (the rising edge is after the data window closes) |
| 3.0 to 4.5 ns | clean; hold margin of the capture 0.3 ns at 3.5 ns |
| 5.0, 6.0 ns | `HOLD` 229, 323 (the next command changes inside the chip's hold time) |

So the old design works at the one clock-delay region 3.0 to 4.5 ns, a 1.5 ns wide target set by the DDIO clock output and the PLL, with no constraint holding it there. That the player works on every Pocket seen so far means the real delay sits inside it; the S0 constraints and the new PHY's falling-edge capture exist to make that a checked fact.

## What the S0 fix removed, and what it did not
S0 (commit `cafd7477`) changes in this controller only the tRFC constant. Between the two controller runs the only difference is `TRFC`: 6,784 to 0 in 8 ms, 2 to 0 in the init, 56,806 to 0 in 64 ms. Everything else (EMRS, the CKE start, the column wraps, the refresh-before-every-read behaviour, the write-only tREF exposure, the unregistered input capture) is untouched by it. S0's QSF I/O packing and SDC are not visible to this bench except through the `pk_*` scenarios (outputs 1.5 to 3.5 ns), which remove nothing from the table because the output side of the old design was not failing at the sampled corners; the CAPTURE rows stay, since the input side is not packed.

## Asks and notes for the model (not changed here)
1. An init option to waive or skip the EMRS (the bench pokes `init_stage` and `init_done`); alternatively a `NO_EMRS_ALLOWED` parameter that prints one finding and continues.
2. `COL_WRAP` for words that are masked or not wanted (P12 again): the old controller's write chunks that end on column 1023 are legal on the chip.
3. A per-signal min/max output delay (today one value per run, so the setup and the hold corner are separate runs).
4. `CKE_LOW` counts the edges before the FPGA drives anything; the bench gates the chip clock to avoid it. A model parameter for "first edge after reset release" would make that explicit.

## Reproduce
`sim/sdram_baseline/run_baseline.sh matrix` (30 s), `raw`, `sweep`, `long` (several minutes), `summary`. Raw outputs: `build/sdram_baseline/*.json`.
