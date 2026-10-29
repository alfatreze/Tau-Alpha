# SDC of the SDRAM PHY + scheduler probe (contract 7.5, section 1). MIT licence.
# Same structure as the SDRAM block of src/fpga/core/core_constraints.sdc, with the contract's numbers and the READ
# side added. The PLL sits at u_pll (the core has it at ic|mp1), so the instance prefix differs.
# NOT VERIFIED in Quartus: the pin name below follows the core's (general[3] = outclk_3, the 100 MHz output).

# ---- clocks ----
create_clock -name clk_74a -period 13.468 [get_ports clk_74a]
derive_pll_clocks
derive_clock_uncertainty

# ---- chip clock: dram_clk is the INVERSE of the 100 MHz controller clock (ALTDDIO_OUT, datain_h 0 / datain_l 1) ----
create_generated_clock -name sdram_clk_pin -source [get_pins {u_pll|mf_pllbase_inst|altera_pll_i|general[3].gpll~PLL_OUTPUT_COUNTER|divclk}] -invert [get_ports {dram_clk}]

# ---- OUTPUT side: the chip's setup/hold (SDRAM_PART_FACTS.md; contract section 1) plus board margin ----
# set_output_delay -max = tSU + board margin, -min = -(tH + board margin). Board margin 0.3 ns.
#   address, BA          tSU 1.5 / tH 1.0   -> max 1.8, min -1.3
#   command, DQM         tSU 1.5 / tH 1.5   -> max 1.8, min -1.8   (the sheet's "0.5/1.5" for commands is a probable transposition)
#   data, output enable  tSU 1.5 / tH 1.0   -> max 1.8, min -1.3
#   CKE                  not in the contract's list: treated like a command (the stricter hold), the core's SDC treats it like an address
set board_margin 0.3
set out_a_su   [expr {1.5 + $board_margin}]
set out_a_h    [expr {1.0 + $board_margin}]
set out_c_su   [expr {1.5 + $board_margin}]
set out_c_h    [expr {1.5 + $board_margin}]
set out_d_su   [expr {1.5 + $board_margin}]
set out_d_h    [expr {1.0 + $board_margin}]
set_output_delay -clock sdram_clk_pin -max $out_a_su  [get_ports {dram_a[*] dram_ba[*]}]
set_output_delay -clock sdram_clk_pin -min [expr {-$out_a_h}] [get_ports {dram_a[*] dram_ba[*]}]
set_output_delay -clock sdram_clk_pin -max $out_c_su  [get_ports {dram_dqm[*] dram_cas_n dram_ras_n dram_we_n dram_cke}]
set_output_delay -clock sdram_clk_pin -min [expr {-$out_c_h}] [get_ports {dram_dqm[*] dram_cas_n dram_ras_n dram_we_n dram_cke}]
set_output_delay -clock sdram_clk_pin -max $out_d_su  [get_ports {dram_dq[*]}]
set_output_delay -clock sdram_clk_pin -min [expr {-$out_d_h}] [get_ports {dram_dq[*]}]

# ---- INPUT side (read path, CL2), RISING-edge capture (the PHY default since B-915) ----
# Facts (B-915, STA of the probe fit, slow 85C; not a Pocket measurement): the chip clock leaves the FPGA through the
# DDIO output cell and the pad (clock path about 6.6 ns) while the capture register's clock arrives after about 3.0 ns,
# so the chip's clock edge is about 3.6 ns LATE with respect to the capture clock; the DQ input pad + input cell adds
# about 2.7 ns. With tAC 8 ns the data is valid from launch + 3.6 + 8 + 2.7 = 14.3 ns after the chip clock edge's
# nominal time, i.e. 19.3 ns after the controller edge E that precedes the chip edge (E + 5 ns), and until about
# E + 22.3 ns: only the rising edge E + 20 ns (two cycles after E) is inside. The falling edge E + 15 ns misses by
# about 4.4 ns (that is the -4.463 ns of the old FALL register).
# set_input_delay is referenced to sdram_clk_pin, whose rising edge is the falling edge of clk (E + 5 ns). Quartus's
# default setup relation to the next rising edge of clk (E + 10 ns) is therefore 5 ns: one capture period too early.
# The data must be captured one period later (E + 20 ns, relation 15 ns): setup multicycle 2 on the capture register,
# hold multicycle 1 (the standard N / N-1 pair, which keeps the hold check against the previous word at the original
# edge). VERIFY in the first report: the setup relationship of this path must read 15 ns (if it reads 10 / 20 the
# relation is counted from another edge and the pair must be 3 / 2, as B-915 suggested).
# No board margin on the input side (the task and the contract state tAC 8 and tOH 1.8 as they are).
set tac_max 8.0
set toh_min 1.8
set in_board 0.0
set_input_delay -clock sdram_clk_pin -max [expr {$tac_max + $in_board}] [get_ports {dram_dq[*]}]
set_input_delay -clock sdram_clk_pin -min [expr {$toh_min - $in_board}] [get_ports {dram_dq[*]}]
# The register that captures DQ in CAP_MODE RISE is cap_r (tau_sdram_dq_cap / tau_sdram_dq_cap_dual), in the I/O cell.
set cap_rise_regs [get_registers -nowarn {*u_cap|cap_r[*] *u_cap|cap_r_r[*]}]
set_multicycle_path -setup 2 -end -from [get_clocks sdram_clk_pin] -to $cap_rise_regs
set_multicycle_path -hold  1 -end -from [get_clocks sdram_clk_pin] -to $cap_rise_regs
# FALL variant (CAP_MODE 1, only for an ideal chip clock/pad; ALTDDIO_IN build): the RISING half of the DDIO input
# register is unused (cap_r_unused) and must not be timed (it produced the artefact of -9.3 ns in B-914):
#   set_false_path -to [get_registers {*u_cap*ddio_ina*DFFHI*}]
# and the falling half keeps the default relation (and fails with the real I/O path, see B-915).
# In the CALIBRATE variant both capture registers exist; the FALL one (cap_f_r, xfer_q) is then only a candidate and is
# not constrained by the multicycle above: its setup will report negative at 100 MHz, so time it as a false path or
# accept the report (the calibration decides at run time):
#   set_false_path -from [get_ports {dram_dq[*]}] -to [get_registers {*u_cap|cap_f_r[*]}]

# ---- the client side of the wrapper is virtual: not a timing path ----
set_false_path -from [get_ports {v_*}]
set_false_path -to   [get_ports {v_*}]
