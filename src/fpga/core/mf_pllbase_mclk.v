// mf_pllbase_mclk.v - dedicated PLL for the I2S audio MCLK (B-457/B-460/B-462, Cymo 44.1 kHz
// investigation).
//
// The MCLK generator used to be a phase accumulator (DDA) inside sound_i2s.v, dividing clk_74a
// (74.25 MHz) toward ~12.288 MHz with a ratio (4096/12375) that does not reduce to a power of two -- a
// real, hardware-only source of jitter on every downstream SCLK/LRCK/DAC bit-clock edge, invisible to
// RTL simulation.
//
// The first fix attempt added 12.288 MHz as a 5th output on the SHARED PLL that also produces clk_sys,
// clk_vid and clk_sdram (mf_pllbase_0002.v) -- Quartus rejected it as an illegal output frequency. Root
// cause: 12.000 MHz (clk_vid), 100.000 MHz (clk_sdram) and 12.288 MHz have no common VCO with legal
// integer output counters below 38.4 GHz (their LCM in Hz), far past a Cyclone V fPLL's real VCO range.
// clk_sys was never the limiting constraint -- the video and SDRAM clocks are, and neither is
// adjustable (12 MHz is fixed by the panel's exact 500x400 = 60.000 Hz timing; 100 MHz is the SDRAM
// controller's own requirement).
//
// Fix: a second, dedicated altera_pll instance, same fractional-N (noise-shaped sigma-delta) synthesis
// technique already relied on for the shared PLL's 12.000 MHz outputs, but with its own VCO -- no
// shared-frequency conflict, because it produces nothing else. Verify the actual achieved frequency and
// jitter against 12.288000 MHz in the real Quartus fit report, not assumed from this parameter string.
`timescale 1ns/10ps
module mf_pllbase_mclk (
    input  wire refclk,
    input  wire rst,
    output wire outclk_0,  // 12.288 MHz - I2S audio MCLK
    output wire locked
);

    altera_pll #(
        .fractional_vco_multiplier("true"),
        .reference_clock_frequency("74.25 MHz"),
        .operation_mode("normal"),
        .number_of_clocks(1),
        .output_clock_frequency0("12.288000 MHz"),
        .phase_shift0("0 ps"),
        .duty_cycle0(50),
        .output_clock_frequency1("0 MHz"),
        .phase_shift1("0 ps"),
        .duty_cycle1(50),
        .output_clock_frequency2("0 MHz"),
        .phase_shift2("0 ps"),
        .duty_cycle2(50),
        .output_clock_frequency3("0 MHz"),
        .phase_shift3("0 ps"),
        .duty_cycle3(50),
        .output_clock_frequency4("0 MHz"),
        .phase_shift4("0 ps"),
        .duty_cycle4(50),
        .output_clock_frequency5("0 MHz"),
        .phase_shift5("0 ps"),
        .duty_cycle5(50),
        .output_clock_frequency6("0 MHz"),
        .phase_shift6("0 ps"),
        .duty_cycle6(50),
        .output_clock_frequency7("0 MHz"),
        .phase_shift7("0 ps"),
        .duty_cycle7(50),
        .output_clock_frequency8("0 MHz"),
        .phase_shift8("0 ps"),
        .duty_cycle8(50),
        .output_clock_frequency9("0 MHz"),
        .phase_shift9("0 ps"),
        .duty_cycle9(50),
        .output_clock_frequency10("0 MHz"),
        .phase_shift10("0 ps"),
        .duty_cycle10(50),
        .output_clock_frequency11("0 MHz"),
        .phase_shift11("0 ps"),
        .duty_cycle11(50),
        .output_clock_frequency12("0 MHz"),
        .phase_shift12("0 ps"),
        .duty_cycle12(50),
        .output_clock_frequency13("0 MHz"),
        .phase_shift13("0 ps"),
        .duty_cycle13(50),
        .output_clock_frequency14("0 MHz"),
        .phase_shift14("0 ps"),
        .duty_cycle14(50),
        .output_clock_frequency15("0 MHz"),
        .phase_shift15("0 ps"),
        .duty_cycle15(50),
        .output_clock_frequency16("0 MHz"),
        .phase_shift16("0 ps"),
        .duty_cycle16(50),
        .output_clock_frequency17("0 MHz"),
        .phase_shift17("0 ps"),
        .duty_cycle17(50),
        .pll_type("General"),
        .pll_subtype("General")
    ) altera_pll_i (
        .rst      (rst),
        .outclk   (outclk_0),
        .locked   (locked),
        .fboutclk (),
        .fbclk    (1'b0),
        .refclk   (refclk)
    );

endmodule
