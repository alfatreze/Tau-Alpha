// SDRAM DQ read-capture registers for the CALIBRATE variant: both capture paths exist (falling edge through a rising
// transfer register, and rising edge); the PHY selects one at run time. MIT licence.
// NOTE: each DQ pin now feeds two registers (fanout 2), so the "fanout 1, FAST_INPUT_REGISTER" packing of the default
// PHY does not apply to this variant. It is used only when CALIBRATE = 1.
`default_nettype none
/* verilator lint_off PROCASSINIT */
module tau_sdram_dq_cap_dual (
    input  wire        clk,
    input  wire        sel_rise,
    input  wire [15:0] d,
    output wire [15:0] q
);
    reg [15:0] cap_f_r = 16'd0, xfer_q = 16'd0, cap_r = 16'd0;
    always @(negedge clk) cap_f_r <= d;
    always @(posedge clk) begin xfer_q <= cap_f_r; cap_r <= d; end
    assign q = sel_rise ? cap_r : xfer_q;
endmodule
`default_nettype wire
