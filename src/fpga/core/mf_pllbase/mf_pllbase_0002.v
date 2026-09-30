// mf_pllbase_0002.v - altera_pll for the Tau MP3 player core (comment inherited from the Moon Patrol
// scaffold this started from; the parameters below are the real values, see mf_pllbase.v).
//
// clk_sys, clk_vid (12.000 MHz, exactly 60.000 Hz) and clk_sdram (100 MHz) share one VCO -- the same
// constraint HarpMudd upstream's release/1.5.1 documented on the shared-origin core. The pixel and
// SDRAM outputs pin the VCO at 600 MHz, so clk_sys = 600/N MHz: 60 (N=10, today) or 66.667 (N=9,
// TAU_CLK66, docs/HARPMUDD_UPSTREAM_1.5_REVIEW.md section 1). Only outclk_0's frequency and this
// comment differ between the two; outclk_1-3 (pixel/SDRAM) are untouched by the macro.
//
// outclk_4 = 12.288 MHz, the I2S audio MCLK (B-457, Cymo investigation). Previously synthesised in
// sound_i2s.v itself via a phase-accumulator (DDA) dividing clk_74a directly -- a real, hardware-only
// source of clock jitter on every bit-clock edge downstream (SCLK/LRCK derive from it), invisible to
// RTL simulation (which checks logical values, not real inter-edge timing) and never examined before
// this investigation. altera_pll's own fractional-N synthesis (`fractional_vco_multiplier("true")`,
// already relied on for outclk_1/2's 12.000 MHz) uses noise-shaped sigma-delta modulation instead of a
// naive accumulator -- the same class of mechanism, but purpose-built for low in-band jitter, pushing
// quantization noise to frequencies the audio path doesn't reproduce. Sharing this VCO (rather than a
// second PLL) keeps this addition free of any new PLL resource; verify the actual achieved frequency
// and jitter against 12.288000 MHz in the real Quartus fit report, not assumed from this parameter
// string alone.
`timescale 1ns/10ps
module mf_pllbase_0002 (
    input  wire refclk,
    input  wire rst,
    output wire outclk_0,
    output wire outclk_1,
    output wire outclk_2,
    output wire outclk_3,
    output wire outclk_4,
    output wire locked
);

    altera_pll #(
        .fractional_vco_multiplier("true"),
        .reference_clock_frequency("74.25 MHz"),
        .operation_mode("normal"),
        .number_of_clocks(5),
        `ifdef TAU_CLK66
        .output_clock_frequency0("66.666667 MHz"),   // clk_sys, N=9 of the same 600 MHz VCO
`else
        .output_clock_frequency0("60.000000 MHz"),    // clk_sys, N=10 of the same 600 MHz VCO
`endif
        .phase_shift0("0 ps"),
        .duty_cycle0(50),
        .output_clock_frequency1("12.000000 MHz"),
        .phase_shift1("0 ps"),
        .duty_cycle1(50),
        .output_clock_frequency2("12.000000 MHz"),
        .phase_shift2("20833 ps"),
        .duty_cycle2(50),
        .output_clock_frequency3("100.000000 MHz"),
        .phase_shift3("0 ps"),
        .duty_cycle3(50),
        .output_clock_frequency4("12.288000 MHz"),   // B-457: I2S audio MCLK, PLL-synthesised instead of a DDA
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
        .outclk   ({outclk_4, outclk_3, outclk_2, outclk_1, outclk_0}),
        .locked   (locked),
        .fboutclk (),
        .fbclk    (1'b0),
        .refclk   (refclk)
    );

endmodule
