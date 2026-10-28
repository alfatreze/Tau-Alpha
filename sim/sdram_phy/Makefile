# SDRAM PHY tests. MIT licence.  Run from the repository root:  make -f sim/sdram_phy/Makefile test
# (relative paths on purpose: GNU make cannot handle the spaces in the owner's absolute path)
.PHONY: test mutants long lint

test:
	bash sim/sdram_phy/run_tests.sh

long:
	bash sim/sdram_phy/run_tests.sh long

mutants:
	bash sim/sdram_phy/run_tests.sh mutants

lint:
	verilator --lint-only -Wall --top-module tau_sdram_phy src/fpga/core/tau_sdram_phy.sv src/fpga/core/tau_sdram_phy_core.sv src/fpga/core/tau_sdram_dq_cap.sv src/fpga/core/tau_sdram_clk_out.sv
