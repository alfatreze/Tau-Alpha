# PSRAM timing contract (P0)

**Status:** checked against the AS1C8M16PL-70BIN datasheet (Alliance Memory, Rev 1.0
preliminary, Aug 2018; local copy `docs/vendor/DOC012312972.pdf`, 54 PDF pages).
Page references below are **PDF page numbers** (the printed footer reads one lower).
Rows tagged **[DS]** are datasheet-verified; **[REF]** agg23 reference controller
(no longer relied on for any number below); **[CHOSEN]** a design value picked
here; **[ASSUMED]** an interpretation the datasheet text does not settle, taken in
its safe direction and listed in section 5 for hardware confirmation.

## 1. Device and pins

| Item | Value | Source |
|---|---|---|
| Part | AS1C8M16PL-70BIN: 128 Mb = two stacked 64 Mb (4M x 16) dies, 1.7-1.95 V, x16 multiplexed address/data, async and burst | [DS] p.2 |
| Pocket | 2 chips, each with CE0#/CE1# selecting one die: 4 dies, 32 MiB total | [DS] p.3, Analogue docs |
| Never | CE0# and CE1# low at the same time | [DS] p.3, p.5 note 2 |
| Pins per chip | `a[21:16]`, `dq[15:0]` (A/DQ), `adv_n`, `ce0_n`, `ce1_n`, `oe_n`, `we_n`, `lb_n`, `ub_n`, `cre`, `clk`, `wait` | [DS] p.5, `core_top.v:46-58` |
| Idle (current core) | all controls high, `cre` 0, `clk` 0, `a` 0, `dq` high-Z | `core_top.v:140-150`; `make test-rtl-psram-idle` |

## 2. Modes, defaults and hazards

| Item | Fact | Decision | Source |
|---|---|---|---|
| Power-up init | 150 us after Vcc/VccQ >= 1.7 V; CE# must stay high until then | Hold both chips idle `INIT_CYC` = 12000 clocks (200 us) after reset. The FPGA loads long after power-on, so this is only a safety margin | [DS] p.7 |
| Power-up registers | BCR = 9D1Fh (bit 15 = 1: **asynchronous default**), RCR = 0010h. Diagram caption on p.3 says each device "need BCR, RCR register setting after power on"; the text on p.7 says init loads defaults | Never write registers; async on defaults. P3 must confirm plain reads/writes work with no register access | [DS] pp.3, 7, 21, 26 |
| CRE | Held low; software register access needs no CRE | `cram_cre` = 0 | [DS] p.19 |
| CLK | May be tied static in async use; must be static during async access | `cram_clk` = 0 | [DS] p.5 note 1 |
| WAIT | Ignore in all async operations; driven during async reads, High-Z on write | Observe only, never used for handshaking | [DS] pp.5, 13 |
| Async read sequence | CE#, ADV#, LB#/UB# low, OE#/WE# high, address on A/DQ; ADV# high latches address; then OE# low; data valid after access time | Implemented so | [DS] pp.8-9 |
| Async write sequence | CE#, ADV#, WE#, LB#/UB# low with address on A/DQ (**OE# must be high** while address is driven); ADV# high latches address; then write data is driven. Data latched at the first of CE#/WE#/UB#/LB# rising | Implemented so | [DS] p.8 |
| LB#/UB# | Must be low during reads; both high disables the data bus | Reads use both low; writes use the byte mask | [DS] p.13 |
| Max CE# low | tCEM = 4 us; every op here is under 0.2 us. A refresh opportunity is CE# high longer than 15 ns | Op recovery is 2 clocks (33 ns) | [DS] pp.30-33 |
| **Software-access hazard** | A sequence of **two async reads then two async writes, all at address 3FFFFFh** of a die enters register access; the data on the third operation (0000h RCR / 0001h BCR / 0002h DIDR) selects the register. The contents at 3FFFFFh are not changed by the sequence | The controller refuses any CPU word containing 3FFFFFh (CPU-word offset `0x1FFFFF` in each die): no chip access, sticky `guard_hit`, bus error. A shadow register may replace this later | [DS] p.19 |

## 3. Timing values (60 MHz, period 16.667 ns)

| Parameter | Datasheet | Controller (defaults) | Margin | Source |
|---|---|---|---|---|
| `tVP` ADV# pulse width | min 5 ns | ADV# low 2 clocks = 33 ns | large | [DS] p.30/32 |
| `tAVS` address setup to ADV# high | min 5 ns | within ADV# low time | large | [DS] |
| `tAVH` address hold from ADV# high | min 2 ns | 1 clock = 16.7 ns | 14.7 ns | [DS] |
| `tCVS` CE# low to ADV# high | min 7 ns | CE# and ADV# fall together, ADV# rises 2 clocks later (33 ns) | 26 ns | [DS] |
| `tAA` address access | max 70 ns | sample 7 clocks after address valid (117 ns) | large | [DS] p.30 |
| `tCO` chip-select access | max 70 ns | sample 7 clocks after CE# low | large | [DS] |
| `tAADV` ADV# access | max 70 ns | **[ASSUMED] measured from ADV# rising** (safe reading): sample 5 clocks = 83.3 ns after ADV# rises | 13.3 ns | [DS] p.30 |
| `tOE` OE# low to valid data | max 20 ns | sample 3 clocks = 50 ns after OE# falls | 30 ns | [DS] p.30 |
| `tOLZ` / `tOHZ` | 3 ns / 7 ns | DQ released before OE# falls; OE# rises before CE# | ok | [DS] |
| `tWP` WE# pulse | min 45 ns | 3 clocks = 50 ns | 5 ns | [DS] p.32 |
| `tDW` data setup | min 20 ns | data driven 1 clock before WE# falls and held until it rises (>= 50 ns) | large | [DS] |
| `tDH` data hold | min 0 ns | 1 clock past WE# rise | ok | [DS] |
| `tAW` address valid to end of write | min 70 ns | WE# rises 7 clocks after start (117 ns) | 47 ns | [DS] |
| `tCW` CE# low to end of write | min 70 ns | same | 47 ns | [DS] |
| `tVS` ADV# setup to end of write | min 70 ns | same | 47 ns | [DS] |
| `tBW` LB#/UB# to end of write | min 70 ns | lanes fixed for the whole op | 47 ns | [DS] |
| `tCPH` CE# high between ops | min 5 ns | `T_REC` = 2 clocks (33 ns) | 28 ns | [DS] p.32 |

Note the change from the first draft (A-098): the read sample was 5 clocks after
ADV# **falls**, which is only about 1 clock (16.7 ns) after OE# falls and about
3 clocks (50 ns) after ADV# rises. That violates `tOE` (20 ns) and, if `tAADV` runs
from ADV# rising, `tAADV` as well. The model had not checked `tOE`. Fixed in A-099
(`T_ACC` = 8); `T_ACC` = 6 and 7 are now killed by the mutation tests.

Defaults stay conservative until hardware shows the margin. Run-time dials
(`cfg_rd_extra`, `cfg_wr_extra`) add cycles for a slow-timing pass in P3. The
tightest datasheet-derived margin is `tWP` (5 ns), then `tAADV` (13 ns, only under
the assumption above).

## 4. Interface rules enforced by simulation (P1)

1. At most one of the four CE# outputs low at any time; all high when idle.
2. No DQ contention: the controller drives DQ only in the address and write-data
   phases and releases it before OE# falls.
3. `we_n` and `oe_n` never low together; OE# high while the address is on the bus.
4. Read data is captured only at the sample index from a registered DQ input. The
   model returns X until `tAADV` (from ADV# rising), `tAA`, `tCO` and `tOE` have
   all elapsed, so an early sample fails.
5. The model also checks `tVP`, `tAVS`, `tAVH`, `tCVS`, `tWP`, `tDW`, `tAW`, `tCW`,
   `tCPH`, `tCEM`, stable byte lanes during a write, and any access to 3FFFFFh.
6. Response is a controller register; ACK comes only from the bus wrapper after
   that register loads (KB-008, KB-024).
7. Reset in any phase returns every control high and DQ released within one clock,
   with no phantom `done`.

## 5. What the datasheet does not settle (hardware questions for P3/P4)

1. **`tAADV` origin.** The text lists "ADV# access time 70 ns" without saying from
   which edge (figure labels do not disambiguate). Controller assumes from ADV#
   rising. Measuring with a shorter capture (dial or `T_ACC` build) on hardware can
   show real margin; do not tighten before then.
2. **Defaults-only operation.** Confirm on Pocket that reads/writes work with no
   register writes (the p.3 caption vs the p.7 text above). Failure would mean a
   register-write path is needed, which touches the guarded address and is a new
   decision.
3. **Pocket board effects** not in the datasheet: trace delay, FPGA output skew and
   input setup (I/O constraints in P2), drive strength (default half strength,
   BCR[5:4] = 01b).
4. **Datasheet status.** Rev 1.0 preliminary, marked confidential; re-check the
   values if Alliance issues a newer revision.
