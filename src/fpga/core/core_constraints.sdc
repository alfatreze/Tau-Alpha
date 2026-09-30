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
