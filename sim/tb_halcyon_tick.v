// B-649: the engines' sample tick must run at 48,000 Hz on the 66.667 MHz clock, not 48,030.7 Hz (an integer divider by 1388 made every non-FLAT setting glitch about 31 times a second:
// the first hardware listening test's "fluttering wings"). Counts the ticks of tau_halcyon and eq_biquad over 0.1 s (6,666,667 clocks): 4,800 expected, within +-1.
// -DHBUG=11 selects the old integer divider in tau_halcyon, which must FAIL (4,803).
`timescale 1ns/1ps
`ifndef HBUG
`define HBUG 0
`endif
module tb_halcyon_tick;
    reg clk = 0, rst = 1;
    always #5 clk = ~clk;
    wire signed [15:0] hl, hr, el, er;
    tau_halcyon #(.CLK_HZ(66_666_667), .RATE_HZ(48_000), .BUG(`HBUG)) hal (.clk(clk), .rst(rst), .in_l(16'sd0), .in_r(16'sd0), .bypass(1'b1), .wr_we(1'b0), .wr_idx(8'd0), .wr_data(24'sd0),
        .nact_in(6'd0), .commit(1'b0), .clr(1'b0), .out_l(hl), .out_r(hr), .busy());
    eq_biquad #(.CLK_HZ(66_666_667), .RATE_HZ(48_000)) eq (.clk(clk), .rst(rst), .in_l(16'sd0), .in_r(16'sd0), .preset(3'd0), .out_l(el), .out_r(er));
    integer nh = 0, ne = 0, n = 0;
    always @(posedge clk) if (!rst) begin
        n <= n + 1;
        if (hal.tick) nh <= nh + 1;
        if (eq.tick)  ne <= ne + 1;
    end
    initial begin
        repeat (4) @(posedge clk); rst = 0;
        repeat (6_666_667) @(posedge clk);
        if (nh >= 4799 && nh <= 4801 && ne >= 4799 && ne <= 4801) $display("PASS tb_halcyon_tick: %0d / %0d ticks in 6,666,667 clocks (4800 expected)", nh, ne);
        else $display("FAIL tb_halcyon_tick: halcyon %0d, eq %0d ticks (4800 expected)", nh, ne);
        $finish;
    end
endmodule
