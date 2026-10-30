# Extra constraints for the TAU_SDRAM_SCHED_A build only (added by tools/sched_a_qsf_append.txt through a second SDC_FILE; the
# shipped core_constraints.sdc is NOT edited, D-T08). MIT licence. Sources: docs/features/SDRAM_CONTROLLER_CONTRACT.md (v1.0-rc1
# paragraph), tools/sdram_probe/sdram_probe.sdc (the probe that closed on the VM), src/fpga/core/tau_sdram_sched_a_assignments.txt (section D).
# This file is read AFTER core_constraints.sdc, so a set_output_delay without -add_delay replaces the earlier value for the same ports.
# 1. command and DQM output delays: setup 1.5 + 0.3 ns margin (the old line used the datasheet's 0.5).
set_output_delay -clock sdram_clk_pin -max 1.8  [get_ports {dram_dqm[*] dram_cas_n dram_ras_n dram_we_n}]
set_output_delay -clock sdram_clk_pin -min -1.8 [get_ports {dram_dqm[*] dram_cas_n dram_ras_n dram_we_n}]
# 2. read side: tAC max 8 ns, tOH min 1.8 ns, no board margin (as the probe).
set_input_delay -clock sdram_clk_pin -max 8.0 [get_ports {dram_dq[*]}]
set_input_delay -clock sdram_clk_pin -min 1.8 [get_ports {dram_dq[*]}]
# 3. the capture register is the RISING-edge cap_r two cycles after the launching edge (the chip clock leaves the FPGA about
# 3.6 ns after the capture clock reaches its register, B-915): setup multicycle 2, hold 1. The fitted names carry the generate
# prefix (g_rise.cap_r[*]); a pattern that matches nothing silently drops the constraint (KB-027, B-916), so say so loudly.
set cap_rise_regs [get_registers -nowarn {*u_cap|g_rise.cap_r[*] *u_cap|g_rise.cap_r_r[*] *u_cap|cap_r[*] *u_cap|cap_r_r[*]}]
if {[get_collection_size $cap_rise_regs] == 0} {
    post_message -type critical_warning "core_constraints_sched_a.sdc: no capture register matched: the read-capture multicycle is NOT applied"
} else {
    post_message -type info "core_constraints_sched_a.sdc: read-capture multicycle applied to [get_collection_size $cap_rise_regs] registers"
}
set_multicycle_path -setup 2 -end -from [get_clocks sdram_clk_pin] -to $cap_rise_regs
set_multicycle_path -hold  1 -end -from [get_clocks sdram_clk_pin] -to $cap_rise_regs
