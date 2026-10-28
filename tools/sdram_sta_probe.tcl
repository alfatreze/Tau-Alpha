# Quartus timing probe of the SDRAM interface on a FITTED project (S0, D-T09). Run on the VM in a copy of a finished fit's src/fpga:
#   quartus_sta -t sdram_sta_probe.tcl <multicycle_m> <tAC_max+board> <tOH_min-board>
# Prints, per corner, the worst setup/hold slack of the input and output paths and, for the slow 85C model, the worst output slack per pin group. The output numbers do not depend on m;
# the input numbers do (the multicycle relationship has to be derived from the RTL: not done yet). The SDC it builds is the one of core_constraints.sdc plus the input side.
# usage: quartus_sta -t sta_probe.tcl <mcp_setup> <cl_tac_max> <cl_toh_min>
set m  [lindex $quartus(args) 0]
set tac [lindex $quartus(args) 1]
set toh [lindex $quartus(args) 2]
load_package sta
project_open ap_core
create_timing_netlist
read_sdc
set sdcname /tmp/sdram_probe.sdc
set f [open $sdcname w]
puts $f {create_generated_clock -name sdram_clk_pin -source [get_pins {ic|mp1|mf_pllbase_inst|altera_pll_i|general[3].gpll~PLL_OUTPUT_COUNTER|divclk}] -invert [get_ports {dram_clk}]}
puts $f "set_input_delay -clock sdram_clk_pin -max $tac \[get_ports {dram_dq\[*\]}\]"
puts $f "set_input_delay -clock sdram_clk_pin -min $toh \[get_ports {dram_dq\[*\]}\]"
puts $f {set_output_delay -clock sdram_clk_pin -max 1.8 [get_ports {dram_a[*] dram_ba[*]}]}
puts $f {set_output_delay -clock sdram_clk_pin -min -1.3 [get_ports {dram_a[*] dram_ba[*]}]}
puts $f {set_output_delay -clock sdram_clk_pin -max 0.8 [get_ports {dram_dqm[*] dram_cas_n dram_ras_n dram_we_n}]}
puts $f {set_output_delay -clock sdram_clk_pin -min -1.8 [get_ports {dram_dqm[*] dram_cas_n dram_ras_n dram_we_n}]}
puts $f {set_output_delay -clock sdram_clk_pin -max 1.8 [get_ports {dram_dq[*] dram_cke}]}
puts $f {set_output_delay -clock sdram_clk_pin -min -1.3 [get_ports {dram_dq[*] dram_cke}]}
puts $f "set_multicycle_path -setup -end -from \[get_clocks sdram_clk_pin\] -to \[get_clocks {ic|mp1|mf_pllbase_inst|altera_pll_i|general\[3\].gpll~PLL_OUTPUT_COUNTER|divclk}\] $m"
puts $f "set_multicycle_path -hold -end -from \[get_clocks sdram_clk_pin\] -to \[get_clocks {ic|mp1|mf_pllbase_inst|altera_pll_i|general\[3\].gpll~PLL_OUTPUT_COUNTER|divclk}\] [expr {$m - 1}]"
close $f
read_sdc $sdcname
update_timing_netlist
foreach cond [get_available_operating_conditions] {
  set_operating_conditions $cond
  update_timing_netlist
  foreach kind {setup hold} {
    foreach {label from to} {IN sdram_clk_pin clk_sdram OUT clk_sdram sdram_clk_pin} {
      set tclk {ic|mp1|mf_pllbase_inst|altera_pll_i|general[3].gpll~PLL_OUTPUT_COUNTER|divclk}
      if {$from eq "clk_sdram"} {set fc [get_clocks $tclk]} else {set fc [get_clocks $from]}
      if {$to eq "clk_sdram"} {set tc [get_clocks $tclk]} else {set tc [get_clocks $to]}
      set paths [get_timing_paths -$kind -from_clock $fc -to_clock $tc -npaths 1]
      set s "n/a"
      foreach_in_collection p $paths { set s [get_path_info $p -slack] }
      puts "RESULT m=$m cond=$cond $label $kind slack=$s"
    }
  }
}
set_operating_conditions [lindex [get_available_operating_conditions] 0]
update_timing_netlist
set tclk {ic|mp1|mf_pllbase_inst|altera_pll_i|general[3].gpll~PLL_OUTPUT_COUNTER|divclk}
report_timing -hold -from_clock [get_clocks sdram_clk_pin] -to_clock [get_clocks $tclk] -npaths 1 -detail full_path -file /tmp/sta_in_hold_m$m.txt
foreach {name pat} {addr {dram_a[*]} ba {dram_ba[*]} dqm {dram_dqm[*]} ras dram_ras_n cas dram_cas_n we dram_we_n cke dram_cke dq {dram_dq[*]}} {
  foreach kind {setup hold} {
    set paths [get_timing_paths -$kind -to [get_ports $pat] -npaths 1]
    foreach_in_collection p $paths { puts "GROUP $name $kind slack=[get_path_info $p -slack] from=[get_node_info -name [get_path_info $p -from]]" }
  }
}
# data path (not the output enable) to dram_dq
set paths [get_timing_paths -setup -from [get_registers {*tau_sdram_ctrl:u_sdram|sdram_data[*]}] -to [get_ports {dram_dq[*]}] -npaths 1]
foreach_in_collection p $paths { puts "GROUP dq_data setup slack=[get_path_info $p -slack]" }
delete_timing_netlist
project_close
