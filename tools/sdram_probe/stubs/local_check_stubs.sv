// Local-check stand-ins for the Quartus primitives (altddio_in, altddio_out) and the core's PLL wrapper. MIT licence.
// Used ONLY by tools/sdram_probe/check_local.sh (lint and iverilog elaboration with the probe's macros); never staged
// to the VM, never compiled by Quartus (which has the real ones).
`default_nettype none
module altddio_out #(parameter integer width = 1, parameter oe_reg = "UNREGISTERED", parameter intended_device_family = "Cyclone V") (
    input  wire [width-1:0] datain_h, datain_l,
    input  wire             outclock, outclocken, aclr, aset, sclr, sset, oe,
    output wire [width-1:0] dataout
);
    assign dataout = outclock ? datain_h : datain_l;
    wire unused = &{1'b0, outclocken, aclr, aset, sclr, sset, oe};
endmodule

module altddio_in #(parameter integer width = 1, parameter invert_input_clocks = "OFF", parameter intended_device_family = "Cyclone V") (
    input  wire [width-1:0] datain,
    input  wire             inclock, inclocken, aclr, aset, sclr, sset,
    output reg  [width-1:0] dataout_h, dataout_l
);
    always @(posedge inclock) dataout_h <= datain;
    always @(negedge inclock) dataout_l <= datain;
    wire unused = &{1'b0, inclocken, aclr, aset, sclr, sset};
endmodule

module mf_pllbase (
    input  wire refclk, rst,
    output wire outclk_0, outclk_1, outclk_2, outclk_3, locked
);
    reg c = 1'b0;
    always #5 c = ~c;
    assign outclk_0 = c;
    assign outclk_1 = c;
    assign outclk_2 = c;
    assign outclk_3 = c;
    assign locked = 1'b1;
    wire unused = &{1'b0, refclk, rst};
endmodule
`default_nettype wire
