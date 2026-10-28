// SDRAM chip clock output: the inverse of clk through a DDIO-style output (datain_h 0, datain_l 1). MIT licence.
// Simulation: the behavioural DDIO below (out = clk ? 0 : 1 = ~clk). Quartus: define TAU_SDRAM_ALTDDIO_OUT to use ALTDDIO_OUT.
`default_nettype none
module tau_sdram_clk_out (
    input  wire clk,
    output wire dram_clk
);
`ifdef TAU_SDRAM_ALTDDIO_OUT
    altddio_out #(.width(1), .oe_reg("UNREGISTERED"), .intended_device_family("Cyclone V")) u_ddio (
        .datain_h(1'b0), .datain_l(1'b1), .outclock(clk), .outclocken(1'b1), .aclr(1'b0), .aset(1'b0),
        .sclr(1'b0), .sset(1'b0), .oe(1'b1), .dataout(dram_clk));
`else
    wire dh = 1'b0;     // value on the rising half
    wire dl = 1'b1;     // value on the falling half
    assign dram_clk = clk ? dh : dl;
`endif
endmodule
`default_nettype wire
