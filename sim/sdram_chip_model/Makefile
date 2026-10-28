# SDRAM chip model tests. MIT licence.  Run from the repository root:  make -f sim/sdram_chip_model/Makefile test
# (relative paths on purpose: GNU make cannot handle the spaces in the owner's absolute path)
.PHONY: test test-long lint

test:
	bash sim/sdram_chip_model/run_tests.sh

test-long:
	bash sim/sdram_chip_model/run_tests.sh long

lint:
	verilator --lint-only --timing -Wall -Wno-fatal -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Isim/sdram_chip_model --top-module sdram_chip_model sim/sdram_chip_model.sv
