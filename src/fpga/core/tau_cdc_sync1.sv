// =============================================================================
// tau_cdc_sync1.sv -- a plain N-stage synchroniser for a single-bit LEVEL signal
// crossing clock domains (Helios/Talos H0, docs/HELIOS_SPEC.md section 9).
//
// NOT for multi-bit values -- tau_cdc_gray_ctr.sv's Gray-code technique exists
// specifically because multiple bits of a COUNTER can change on the same source
// edge, which a plain synchroniser cannot safely resolve (some bits could be
// captured pre-change, others post-change, yielding a value that was never
// actually on the counter). A single bit has no such hazard: there is only ever
// one bit to resolve, and the classic metastability risk that creates is exactly
// what a 2-3 flop synchroniser chain is the standard, textbook fix for.
//
// Matches this project's own existing precedent for exactly this shape of
// problem: mp3_fb.sv's own `painted_vid` (a level that crosses clk_sys -> clk_vid
// through a 3-stage shift register) uses the identical technique in the opposite
// direction. Pulled out into its own reusable module here because H0 needs the
// SAME thing again (clk_vid -> clk_sys this time, for vblank status) and a named
// module is easier to find, test and reuse a third time than another inline
// three-line shift register.
// =============================================================================
module tau_cdc_sync1 #(
    parameter STAGES = 3   // 2 is the textbook minimum; 3 for extra margin,
                            // matching mp3_fb.sv's own painted_vid precedent.
)(
    input  wire clk_dst,
    input  wire d_src,
    output wire q_dst
);
    reg [STAGES-1:0] sync = {STAGES{1'b0}};
    always @(posedge clk_dst) sync <= {sync[STAGES-2:0], d_src};
    assign q_dst = sync[STAGES-1];
endmodule
