#
# user core constraints — Moon Patrol Pocket core
#
# All clock domains are asynchronous to each other.
# ic = core_top instance in apf_top; mp1 = shared PLL instance (clk_sys/clk_vid/clk_sdram) in core_top;
# mp2 = dedicated I2S MCLK PLL (B-457/B-460/B-462), also in core_top.
# Four PLL outputs on mp1: [0] clk_sys 30M  [1] clk_vid 6M  [2] clk_vid_90 6M  [3] clk_snd 3.58M
# One PLL output on mp2: [0] I2S audio MCLK 12.288 MHz. `ic|mclk_r` (the old sound_i2s.v phase-
# accumulator register this used to name) no longer exists -- removed rather than left dangling.

set_clock_groups -asynchronous \
 -group { bridge_spiclk } \
 -group { clk_74a } \
 -group { clk_74b } \
 -group { ic|mp1|mf_pllbase_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk } \
 -group { ic|mp1|mf_pllbase_inst|altera_pll_i|general[1].gpll~PLL_OUTPUT_COUNTER|divclk } \
 -group { ic|mp1|mf_pllbase_inst|altera_pll_i|general[2].gpll~PLL_OUTPUT_COUNTER|divclk } \
 -group { ic|mp1|mf_pllbase_inst|altera_pll_i|general[3].gpll~PLL_OUTPUT_COUNTER|divclk } \
 -group { ic|mp2|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk }

# ---- SDRAM interface (shared PHY + sched_v path, the only SDRAM path since D-T13, 2026-10-31) ---------------------------------------------------------------------
# One file since the old controller was retired (core_constraints_sched_a.sdc was merged in here; the effective constraints are those of fit def-b931). Sources:
# docs/features/SDRAM_CONTROLLER_CONTRACT.md (v1.0-rc1 paragraph), tools/sdram_probe/sdram_probe.sdc (the probe that closed on the VM), docs/features/SDRAM_PART_FACTS.md.
# The chip clock leaves through a DDIO in tau_sdram_clk_out (datain_h 0, datain_l 1 on clk_sdram), i.e. the inverted clk_sdram. The OUTPUT paths (command, address, DQM, CKE, data and its output
# enable) are checked against the chip's setup and hold from the Alliance AS4C32M16MS -6 datasheet plus 0.3 ns of board margin: address and data 1.5/1.0 ns (+0.3),
# command and DQM 1.5 setup / 1.5 hold (+0.3).
create_generated_clock -name sdram_clk_pin -source [get_pins {ic|mp1|mf_pllbase_inst|altera_pll_i|general[3].gpll~PLL_OUTPUT_COUNTER|divclk}] -invert [get_ports {dram_clk}]
set_output_delay -clock sdram_clk_pin -max 1.8 [get_ports {dram_a[*] dram_ba[*] dram_dq[*] dram_cke}]
set_output_delay -clock sdram_clk_pin -min -1.3 [get_ports {dram_a[*] dram_ba[*] dram_dq[*] dram_cke}]
# 1. command and DQM output delays: setup 1.5 + 0.3 ns margin.
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
    post_message -type critical_warning "core_constraints.sdc: no capture register matched: the read-capture multicycle is NOT applied"
} else {
    post_message -type info "core_constraints.sdc: read-capture multicycle applied to [get_collection_size $cap_rise_regs] registers"
}
set_multicycle_path -setup 2 -end -from [get_clocks sdram_clk_pin] -to $cap_rise_regs
set_multicycle_path -hold  1 -end -from [get_clocks sdram_clk_pin] -to $cap_rise_regs
