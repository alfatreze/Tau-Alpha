# SDRAM PHY + sched_min release candidate 2: manifest (2026-10-29)

rc2 changes against rc1: `tau_sdram_sched_min.sv` has registered offer outputs and uses `tau_sdram_sv_tm.sv` (new dependency, the scheduler timing model); the PHY and the chip model are UNCHANGED since rc1. Reason: Tau V fit rc1 (setup -0.69 ns slow 85C: sched_min offered combinationally). Simulation only; rc2 has not been through Quartus. Nothing on a Pocket. Tag `sdram-phy-rc2`.

| File | SHA-256 |
|---|---|
| `src/fpga/core/tau_sdram_phy.sv` | f037d613de0f04cb08e1d3f298bad88484e4523333d28f8daa8a29b62ba821b7 |
| `src/fpga/core/tau_sdram_phy_core.sv` | 20f892b752908a1f4332b5229d2e9b26c1b093a16cdbf6d55e15899c25d9ffb7 |
| `src/fpga/core/tau_sdram_dq_cap.sv` | 1ee87071dd5828c6828330614fad76f47078aaed5ca090ca45d72ea0d6fa3889 |
| `src/fpga/core/tau_sdram_dq_cap_dual.sv` | 06a245ff89547712b77ab1e831db90267ba48eb68b04dad3fe01840ae2b2d82c |
| `src/fpga/core/tau_sdram_clk_out.sv` | 39d719d8d0134411cfd512bc6757a6b171441caf0c5de66513f764cf57b65021 |
| `src/fpga/core/tau_sdram_phy_assignments.txt` | c05b52b13e0e98fbd572c8dcf2c6600121ddbcd70150241878a1749c6b9d4384 |
| `src/fpga/core/tau_sdram_sched_min.sv` | 3d8603a187176dd88b53528af6f7797c3987b80b24b25cbcc598349e2a336baf |
| `src/fpga/core/tau_sdram_sv_tm.sv` | 86c66c468ebf72d9b82081031df589cf13ad3b9809678ef454c4346c3b848d1b |
| `tools/sdram_probe/sdram_probe.sdc` | c6c83cfb07186a325c7c7c1c1ffe1c1eb568be59461bec2616ded83cf5ba8ada |
| `sim/sdram_chip_model.sv` | 849ac25efe2fbe8d2492fa5b2269ca03013e951969cb820f81fbc40c8aee9ec9 |
| `docs/features/SDRAM_CONTROLLER_CONTRACT.md` | b234568d86f32aeddab0306444f52c649abfc0e508dfef95736916ff2309ac31 |
