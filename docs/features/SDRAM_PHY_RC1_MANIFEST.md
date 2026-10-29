# SDRAM PHY + sched_min release candidate 1: manifest (2026-10-29)

Simulation and one Quartus probe of the PHY and the full scheduler only (B-914..B-917). Nothing on a Pocket. `sched_min` has NOT been through Quartus. The tag is `sdram-phy-rc1` (this commit).

| File | SHA-256 |
|---|---|
| `src/fpga/core/tau_sdram_phy.sv` | f037d613de0f04cb08e1d3f298bad88484e4523333d28f8daa8a29b62ba821b7 |
| `src/fpga/core/tau_sdram_phy_core.sv` | 20f892b752908a1f4332b5229d2e9b26c1b093a16cdbf6d55e15899c25d9ffb7 |
| `src/fpga/core/tau_sdram_dq_cap.sv` | 1ee87071dd5828c6828330614fad76f47078aaed5ca090ca45d72ea0d6fa3889 |
| `src/fpga/core/tau_sdram_dq_cap_dual.sv` | 06a245ff89547712b77ab1e831db90267ba48eb68b04dad3fe01840ae2b2d82c |
| `src/fpga/core/tau_sdram_clk_out.sv` | 39d719d8d0134411cfd512bc6757a6b171441caf0c5de66513f764cf57b65021 |
| `src/fpga/core/tau_sdram_phy_assignments.txt` | c05b52b13e0e98fbd572c8dcf2c6600121ddbcd70150241878a1749c6b9d4384 |
| `src/fpga/core/tau_sdram_sched_min.sv` | d12661ee8a2e89a73de55ad9e91e6996c8c1aeeba85beb1cf1da54386a702b0e |
| `tools/sdram_probe/sdram_probe.sdc` | c6c83cfb07186a325c7c7c1c1ffe1c1eb568be59461bec2616ded83cf5ba8ada |
| `sim/sdram_chip_model.sv` | 849ac25efe2fbe8d2492fa5b2269ca03013e951969cb820f81fbc40c8aee9ec9 |
| `docs/features/SDRAM_CONTROLLER_CONTRACT.md` | a8b3de9c1770159a922ca5ab28b0a51b84475553687ae78483207c454ebfa7e4 |
