# sdram-sched-v-v1: manifest (2026-10-29)

`sched_v` (6 ports, registered offers, own timing model, 2D reads, region skew table) with the PHY it was verified against (the `sdram-phy-v1` files). Evidence: simulation (sim/sdram_sched_v 72 tests and mutants), the full Tau V replay (all six files, flat out, 48 shards PASS, 0 mismatches, 0 `phy_fault`, 0 chip errors, 0 late pictures, 0 above 90 %, scan-out underflow 0, urgent wait max 72 against the 155 bound), and one Quartus probe (`probe-add1`: core Fmax 103 MHz, setup +0.315 ns slow 85C, 0 DSP, 3,721 ALMs, capture +0.653 ns). NOT on a Pocket. The margin is thin (allbi 86.3 % busy against the 90 % limit; +0.3 ns core setup).

| File | SHA-256 |
|---|---|
| `src/fpga/core/tau_sdram_sched_v.sv` | 7605f3293c402289e5d90da91bdd2715432884fef142b72a3f9addd26f32993c |
| `src/fpga/core/tau_sdram_sv_pq.sv` | be2b78e2b9e03a0062e0249f18eb5048e728c501390f76a2a5fc3c454789c462 |
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
