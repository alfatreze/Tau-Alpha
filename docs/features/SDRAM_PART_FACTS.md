# The Pocket's SDRAM: part facts from the datasheet, checked against our controller (2026-10-28)

**Part:** Alliance Memory `AS4C32M16MSA-6BIN`, 512 Mbit (32M x 16) mobile SDR SDRAM, 1.7 to 1.95 V, 54-ball FBGA, industrial -40 to +85 C (Pocket reference: `analogue-pocket-dev` skill, `hardware-video-audio-input.md`). **Source of the numbers below:** the Alliance datasheet *AS4C32M16MS-7BCN / AS4C32M16MS-6BIN, Rev. 1.0, June 2016* (https://www.alliancememory.com/wp-content/uploads/AS4C32M16MS-6BIN_512M-LOW-POWER-SDRAM_AS4C32M16MS-7BCN__AS4C32M16MS-6BIN-rev1.0-June-2016.pdf), the nearest datasheet found for the same die family. **The Pocket's part is the `MSA` variant: its own datasheet (Alliance, preliminary Rev 1.0, Dec 2017, on Mouser) must be read and diffed against this table before any number here is relied on.** The datasheet text itself is not copied into the repository; these are facts, with the page of the PDF (AC characteristics, pages 14 to 16; power-on, page 7 or 8).

| Parameter | -6 grade | Our controller (`tau_sdram_ctrl.sv`) | Verdict |
|---|---|---|---|
| tCK min, CL3 / CL2 | 6 ns (166 MHz) / **9 ns (110 MHz max)** | 100 MHz, CL2 (10 ns) | in spec, 10% above the CL2 minimum; **133 MHz needs CL3** |
| tAC max, CL3 / CL2 | 5 ns / 8 ns | not constrained | the capture window of the I/O at 100 MHz CL2 depends on the 8 ns |
| tOH min | 2.5 ns (loaded), 1.8 ns (unloaded) | | |
| tLZ min / tHZ max | 1 ns / 5 ns (CL3), 8 ns (CL2) | | the output-enable overlap at a read-to-write change |
| tRCD, tRP | 18 ns, 18 ns | 18, 18 (2 cycles each) | ok |
| tRAS | 42 ns min, **100 us max** | 48 ns | ok; the 100 us maximum matters for any open-page policy |
| tRC | 60 ns | 60 ns (the `CEIL` macro makes it 7 cycles) | ok |
| tRFC | **97.5 ns** | **80 ns, `CEIL` gives 9 cycles = 90 ns** | **violated by 7.5 ns** (found by this check); Tau V's requirement also says 80 ns |
| tWR | 15 ns | 15 ns | ok |
| tMRD | 2 cycles | 2 | ok |
| tRRD | 2 cycles | n/a (closed page) | needed for open page |
| tCCD, tCDL, tBDL | 1 cycle | | tWTR does not exist for SDR |
| DQM: read latency (tDQZ) / write latency (tDQD, tDQM) | 2 cycles / 0 cycles | | the PHY must align them |
| tROH (data out high-Z from PRECHARGE) | 3 cycles (CL3), 2 (CL2) | | |
| tRDL, tDPL | 2 cycles | | |
| Setup / hold at the chip | address 1.5 / 1.0 ns; commands and DQM 0.5 (setup) and 1.5 (hold) as printed; data 1.5 / 1.0 | | for the SDC output delays (to be confirmed against the MSA sheet) |
| Refresh | 8,192 cycles per 64 ms; a distributed refresh every 7.8125 us, **or 8,192 in a burst once per 64 ms**, both allowed | timer 7.5 us | ok |
| Power-on | 100 us NOP/inhibit, precharge all, **at least two** auto refreshes, mode register set; the extended mode register (drive strength, partial-array self refresh) is optional, default full strength and full array | 100 us, precharge all, two refreshes, MRS 0x227-style, no extended mode register | **matches** (the "200 us and 8 refreshes" of one review is the deep-power-down exit sequence, not power-on) |
| Mode register | burst length 1/2/4/8/full page, sequential or interleaved, CL 2 or 3 | full page, sequential, CL2 | ok |

**What this changes:** (1) fix tRFC (a 98 ns setting gives 10 cycles at 100 MHz); (2) CL3 is mandatory above 110 MHz (so 133 and 150 MHz are CL3 variants, and CL2 at 100 MHz is allowed with a 10% margin on tCK); (3) tAC(CL2) = 8 ns is large against a 10 ns period, so the input timing at CL2 needs a measured window, not an assumption; (4) the init sequence is correct for this part; (5) the open-page policy must respect tRAS(max) = 100 us and tRRD = 2.

## The community reference (agg23's controller, MIT)
Looked at on 2026-10-28 for ideas only (`github.com/agg23/sdram-controller`, MIT licence; our current controller descends from its `sdram_burst.sv` through the HarpMudd framebuffer controller, which is the provenance D-T01 retires). What it shows: the same weaknesses as ours (the read data is the pin, `p0_q = SDRAM_DQ`, no capture register, no `useioff`; its header says the "fast input register" extra cycle is incompatible with its test model); reads end with PRECHARGE, not burst terminate; writes are single words with auto-precharge; one port; a reset that does not re-run the init; the repository carries Micron's SDR behavioural model and testbenches (Micron copyright 2005, "as is": a vendor model of this kind is what the memory review asked to keep OUTSIDE our repository as a cross-check). A working Pocket core, `agg23/openfpga-SNES`, also has **no** `FAST_INPUT_REGISTER` assignment in its QSF and only multicycle paths between the SDRAM module and the PLL clocks in its SDC (no input or output delays): so an unregistered, loosely constrained SDRAM interface is common practice on the Pocket and works in the field; the reviewer's blocker is a hygiene and margin question for the one unit, not proof of a fault. Nothing was copied (D-P01); this is recorded in `docs/PROVENANCE.md`.
