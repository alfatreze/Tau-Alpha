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

# ---- SDRAM interface (S0, D-T09) --------------------------------------------------------------------------------------------------------------------------------
# The chip clock leaves through a DDIO (tau_sdram_ctrl: datain_h 0, datain_l 1 on clk_sdram), i.e. the inverted clk_sdram. The OUTPUT paths (command, address, DQM, CKE, data and its output
# enable) are checked against the chip's setup and hold from the Alliance AS4C32M16MS -6 datasheet (docs/features/SDRAM_PART_FACTS.md; to be diffed against the MSA sheet) plus 0.3 ns of board
# margin: address and data 1.5/1.0 ns (+0.3), command and DQM 0.5 setup / 1.5 hold (+0.3). Not constrained here: the input (read) side, which needs a capture register first (a multicycle
# relationship to be derived from the RTL); the report of tools/sdram_sta_probe.tcl gives its numbers meanwhile.
create_generated_clock -name sdram_clk_pin -source [get_pins {ic|mp1|mf_pllbase_inst|altera_pll_i|general[3].gpll~PLL_OUTPUT_COUNTER|divclk}] -invert [get_ports {dram_clk}]
set_output_delay -clock sdram_clk_pin -max 1.8 [get_ports {dram_a[*] dram_ba[*] dram_dq[*] dram_cke}]
set_output_delay -clock sdram_clk_pin -min -1.3 [get_ports {dram_a[*] dram_ba[*] dram_dq[*] dram_cke}]
set_output_delay -clock sdram_clk_pin -max 0.8 [get_ports {dram_dqm[*] dram_cas_n dram_ras_n dram_we_n}]
set_output_delay -clock sdram_clk_pin -min -1.8 [get_ports {dram_dqm[*] dram_cas_n dram_ras_n dram_we_n}]
