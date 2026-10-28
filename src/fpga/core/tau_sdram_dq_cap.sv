// SDRAM DQ read-capture registers (one register per DQ bit, fanout 1). MIT licence. Clean-room (contract v0.3, section 1).
//
// CAP_MODE 1 (FALL): DQ is captured on the falling edge of clk, then moved to the rising domain by one register.
// CAP_MODE 0 (RISE): DQ is captured on the rising edge of clk (one register).
// `q` is the value visible after the rising edge that follows the capture.
// Quartus: the first-stage register must sit in the I/O cell (FAST_INPUT_REGISTER ON); for the falling edge use the
// ALTDDIO_IN macro (define TAU_SDRAM_ALTDDIO_IN to select it). The behavioural code is what simulation runs.
`default_nettype none
module tau_sdram_dq_cap #(
    parameter integer CAP_MODE = 1
) (
    input  wire        clk,
    input  wire [15:0] d,
    output wire [15:0] q
);
    generate
        if (CAP_MODE == 1) begin : g_fall
            wire [15:0] cap_f;
`ifdef TAU_SDRAM_ALTDDIO_IN
            // dataout_l is the value captured on the falling edge of inclock
            wire [15:0] cap_r_unused;
            altddio_in #(.width(16), .invert_input_clocks("OFF"), .intended_device_family("Cyclone V")) u_ddio (
                .datain(d), .inclock(clk), .inclocken(1'b1), .aclr(1'b0), .aset(1'b0), .sclr(1'b0), .sset(1'b0),
                .dataout_h(cap_r_unused), .dataout_l(cap_f));
`else
            reg [15:0] cap_f_r = 16'd0;
            always @(negedge clk) cap_f_r <= d;
            assign cap_f = cap_f_r;
`endif
            reg [15:0] xfer_q = 16'd0;
            always @(posedge clk) xfer_q <= cap_f;
            assign q = xfer_q;
        end else begin : g_rise
            reg [15:0] cap_r = 16'd0;
            always @(posedge clk) cap_r <= d;
            assign q = cap_r;
        end
    endgenerate
endmodule
`default_nettype wire
