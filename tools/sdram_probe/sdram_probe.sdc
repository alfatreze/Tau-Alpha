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

# ---- INPUT side (read path, CL2): tAC max 8 ns, tOH min 1.8 ns (unloaded floor), referenced to the chip clock ----
# The chip drives DQ after ITS rising edge (= the falling edge of clk). The FALL capture register (ALTDDIO_IN dataout_l)
# samples on the falling edge of clk, i.e. on the NEXT rising edge of sdram_clk_pin, one period later: Quartus's default
# edge relationship is exactly that (setup 10 ns, hold 0), so NO multicycle is needed in CAP_MODE FALL; the setup slack
# is 10 - tAC - (pin to register, skew), about 2 ns minus the I/O delays, which is the window of the contract. In
# CAP_MODE RISE (not probed) a multicycle relation would have to be added.
# No board margin on the input side (the task and the contract state tAC 8 and tOH 1.8 as they are); to see it, set in_board > 0.
set tac_max 8.0
set toh_min 1.8
set in_board 0.0
set_input_delay -clock sdram_clk_pin -max [expr {$tac_max + $in_board}] [get_ports {dram_dq[*]}]
set_input_delay -clock sdram_clk_pin -min [expr {$toh_min - $in_board}] [get_ports {dram_dq[*]}]

# ---- the client side of the wrapper is virtual: not a timing path ----
set_false_path -from [get_ports {v_*}]
set_false_path -to   [get_ports {v_*}]
