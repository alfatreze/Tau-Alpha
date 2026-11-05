# sdram-sched-v-v1.2-rc1: manifest (2026-11-05)

`sched_v` v1.2-rc1 = v1.1 + ONE change to ONE file (`tau_sdram_sched_v.sv`): the open-row table entry `o_row` is written one cycle after the ACT (from a registered copy of the ACT's bank and row, `oa_v/oa_b/oa_r`), so the `sv_tm` output no longer feeds `o_row` in the same cycle; the quiet window after an ACT is 5 cycles (was 4) so no stage reads a stale row. **Drop-in for v1.1: the interface is unchanged**, and `tau_sdram_sv_pq.sv`, `_arb`, `_tm` and the PHY files are byte-identical to v1.1 (SHA-256 below equal the v1.1 manifest). Built at Tau V's request (their suggested step 2; the `sv_tm` to `o_row` path was tight in their fits at seeds 6 and 8).

**Not included, on purpose:** step 1 (the `hi_c` adder out of the request path in `sv_pq`). It was built and is host-proven, but FAILS the flat-out replay (scan-out underflow about 19,000 words, urgent wait 5,129 cycles against the bound 155; v1.1 and this candidate pass). Cause by inference, not measured: the entry holds the conservative bound `1fff` for one cycle after each push and the scan-out reads are held back by it under continuous writes. The queue itself matches v1.1 (200,000 random cycles). Not shipped; a retry would need a tighter bound for writes.

**Evidence (NOT fitted in a whole core, NOT on a Pocket):**
* Simulation: `sim/sdram_sched_v` 78 tests and mutants pass (new test `stale_row`, new mutant `MUT_OROW_NOWAIT` killed). `MUT_TQ4` (quiet window 4 kept) SURVIVES every test, including random stimulus: the window of 5 is a conservative choice that no test needs.
* Full Tau V replay, 6 files flat out (`+norelease`), all PASS, 0 mismatches, 0 `phy_fault`, 0 chip errors, scan-out underflow 0, urgent wait worst 65 to 79 cycles against the bound 155. Busy p50 / max: clip 23.91 / 27.74 %, allbi 76.87 / 77.95 %, static 53.13 / 53.18 %, scenechange 58.21 / 58.34 %, pan 58.02 / 58.04 %, random 55.98 / 56.93 % (v1.1: 23.9 / 27.7, 76.8 / 77.9, 53.1 / 53.2, 58.2 / 58.3, 58.0 / 58.0, 56.0 / 57.0).
* Two Quartus probes (base variant, standalone PHY + scheduler, not the whole core): seed 1 core setup +0.189 / +0.579 ns (slow 85C / 0C), Fmax 102.0 / 106.2 MHz; seed 2 +0.897 / +0.990 ns, Fmax 109.9 / 111.0 MHz; scheduler 3,577 to 3,602 ALMs, 0 DSP; core hold +0.141 / +0.155 ns worst (fast corner); output and input paths close. v1.1 probes were +0.517 / +0.649 and +0.739 / +0.697 ns: the difference is inside the seed spread (about 0.5 to 0.7 ns), so **the probe shows no gain and no promise of margin is made**. The path this change targets (`sv_tm` to `o_row`) only shows in a whole-core fit: Tau V's re-fit at seeds 6 and 8 decides.

| File | SHA-256 |
|---|---|
| `src/fpga/core/tau_sdram_sched_v.sv` | 8c8a6a0714ed1b5c3d09deb1e7d6fa649c23783b4e363ae3cbac7c03782205ae |
| `src/fpga/core/tau_sdram_sv_pq.sv` | fb37ff2e21ada6f6bc73b30c73c9fe7c5b071fe9291afb7e56ee920b781fcc91 |
| `src/fpga/core/tau_sdram_sv_arb.sv` | 5a0561400b69b05aea94c52cba947c7e16a34714bfc5600acef6a6c0df12cae1 |
| `src/fpga/core/tau_sdram_sv_tm.sv` | 86c66c468ebf72d9b82081031df589cf13ad3b9809678ef454c4346c3b848d1b |
| `src/fpga/core/tau_sdram_phy.sv` | f037d613de0f04cb08e1d3f298bad88484e4523333d28f8daa8a29b62ba821b7 |
| `src/fpga/core/tau_sdram_phy_core.sv` | 20f892b752908a1f4332b5229d2e9b26c1b093a16cdbf6d55e15899c25d9ffb7 |
| `src/fpga/core/tau_sdram_dq_cap.sv` | 1ee87071dd5828c6828330614fad76f47078aaed5ca090ca45d72ea0d6fa3889 |
| `src/fpga/core/tau_sdram_dq_cap_dual.sv` | 06a245ff89547712b77ab1e831db90267ba48eb68b04dad3fe01840ae2b2d82c |
| `src/fpga/core/tau_sdram_clk_out.sv` | 39d719d8d0134411cfd512bc6757a6b171441caf0c5de66513f764cf57b65021 |
| `src/fpga/core/tau_sdram_phy_assignments.txt` | c05b52b13e0e98fbd572c8dcf2c6600121ddbcd70150241878a1749c6b9d4384 |
| `tools/sdram_probe/sdram_probe.sdc` | c6c83cfb07186a325c7c7c1c1ffe1c1eb568be59461bec2616ded83cf5ba8ada |
| `sim/sdram_chip_model.sv` | 849ac25efe2fbe8d2492fa5b2269ca03013e951969cb820f81fbc40c8aee9ec9 |
