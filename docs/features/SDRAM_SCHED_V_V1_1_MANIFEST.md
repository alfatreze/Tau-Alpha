# sdram-sched-v-v1.1: manifest (2026-10-30)

`sched_v` v1.1 = v1 + the throughput pass + the timing recovery. **Drop-in for v1: the interface is unchanged** (two optional parameters `FAST_CONT`, `FAST_LOCK`, default 1). The PHY files are the `sdram-phy-v1` files, unchanged. Evidence: simulation (sim/sdram_sched_v 76 tests and mutants; sched_a 17); the full Tau V replay (6 files, flat out, 48 shards PASS, 0 mismatches, 0 `phy_fault`, 0 chip errors, 0 late pictures, scan-out underflow 0, urgent wait max 79 against the 155 bound; busy p50 / max: clip 23.9 / 27.7 %, allbi 76.8 / 77.9 %, static 53.1 / 53.2, scenechange 58.2 / 58.3, pan 58.0 / 58.0, random 56.0 / 57.0); and two Quartus probes (seed 1: core setup +0.517 / +0.649 ns slow 85C / 0C, Fmax 105.5 / 107.0 MHz; seed 2: +0.739 / +0.697 ns, Fmax 108.0 / 107.5 MHz; 3,578 / 3,585 ALMs, 0 DSP, capture +0.65 to +0.71 ns). NOT on a Pocket.

| File | SHA-256 |
|---|---|
| `src/fpga/core/tau_sdram_sched_v.sv` | 32c7a82c9e7ef8469ab76a2626fb88b13725e41d5b7b6bb855fdc5a97b031817 |
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
