#!/usr/bin/env bash
# Verilator -Wall lint of the PHY: default build and each optional feature on. MIT licence.
set -u
cd "$(dirname "$0")/../.."
R=src/fpga/core
F="$R/tau_sdram_phy.sv $R/tau_sdram_phy_core.sv $R/tau_sdram_dq_cap.sv $R/tau_sdram_dq_cap_dual.sv $R/tau_sdram_clk_out.sv"
rc=0
for G in "" "-GCALIBRATE=1" "-GBIST=1" "-GPERF=1" "-GCALIBRATE=1 -GBIST=1 -GPERF=1 -GCL=3"; do
    n=$(verilator --lint-only -Wall $G --top-module tau_sdram_phy $F 2>&1 | grep -c '^%')
    echo "lint [$G]: $n warnings/errors"
    [ "$n" -eq 0 ] || { verilator --lint-only -Wall $G --top-module tau_sdram_phy $F 2>&1 | grep '^%' | head -10; rc=1; }
done
exit $rc
