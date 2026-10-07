// B-649: two properties of the engines' sample timing, with both engines running on random stereo input at the 66.667 MHz clock:
//  1. the sample tick is 48,000 Hz (4,800 ticks in 6,666,667 clocks, +-1), not 48,030.7 Hz (an integer divider by 1388: 4,803; the "fluttering wings");
//  2. the two output channels change in the SAME clock: the I2S writer (sound_i2s.v) pushes a word whenever either changes, so a channel-at-a-time update hands the serializer a
//     torn pair (this sample's left with the last sample's right) in a few percent of the frames. Torn edges must be 0.
// -DHBUG=11 selects the old integer divider, -DHBUG=12 the per-channel output update in tau_halcyon: each must FAIL.
`timescale 1ns/1ps
`ifndef HBUG
`define HBUG 0
`endif
module tb_halcyon_tick;
    reg clk = 0, rst = 1;
    always #5 clk = ~clk;
    reg signed [15:0] xl = 0, xr = 0;
    reg wr_we = 0, commit = 0; reg [7:0] wr_idx = 0; reg signed [23:0] wr_data = 0; reg [5:0] nact_in = 0;
    wire signed [15:0] hl, hr, el, er;
    tau_halcyon #(.CLK_HZ(66_666_667), .RATE_HZ(48_000), .BUG(`HBUG)) hal (.clk(clk), .rst(rst), .in_l(xl), .in_r(xr), .bypass(1'b0), .wr_we(wr_we), .wr_idx(wr_idx), .wr_data(wr_data),
        .nact_in(nact_in), .commit(commit), .clr(1'b0), .out_l(hl), .out_r(hr), .busy());
    eq_biquad #(.CLK_HZ(66_666_667), .RATE_HZ(48_000)) eq (.clk(clk), .rst(rst), .in_l(xl), .in_r(xr), .preset(3'd3), .out_l(el), .out_r(er));
    integer nh = 0, ne = 0, i;
    reg [31:0] lfsr = 32'hACE1_2468;
    reg signed [15:0] pl = 0, pr = 0, ql = 0, qr = 0;
    integer torn_h = 0, torn_e = 0, runs_h = 0, runs_e = 0, started = 0;
    always @(posedge clk) if (!rst) begin
        if (hal.tick) begin
            if (started) nh <= nh + 1;
            lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
            xl <= $signed(lfsr[15:0]) >>> 1; xr <= $signed(lfsr[31:16]) >>> 1;       // distinct, half scale
        end
        if (eq.tick && started) ne <= ne + 1;
        pl <= hl; pr <= hr; ql <= el; qr <= er;
        if (started) begin
            if ((hl !== pl) != (hr !== pr)) torn_h <= torn_h + 1;
            if (hl !== pl || hr !== pr) runs_h <= runs_h + 1;
            if ((el !== ql) != (er !== qr)) torn_e <= torn_e + 1;
            if (el !== ql || er !== qr) runs_e <= runs_e + 1;
        end
    end
    task wr(input integer idx, input integer v);
        begin @(posedge clk); wr_idx <= idx[7:0]; wr_data <= v[23:0]; wr_we <= 1; @(posedge clk); wr_we <= 0; end
    endtask
    initial begin
        repeat (4) @(posedge clk); rst = 0;
        repeat (400) @(posedge clk);                                   // the reset sweep
        wr(0, 4194304); wr(1, 0); wr(2, 0); wr(3, 0); wr(4, 0); wr(85, 4194304);            // one unity stage, unity preamp
        @(posedge clk); nact_in <= 6'd1; commit <= 1; @(posedge clk); commit <= 0;
        repeat (4000) @(posedge clk); started = 1;
        repeat (6_666_667) @(posedge clk);
        if (nh >= 4799 && nh <= 4801 && ne >= 4799 && ne <= 4801 && torn_h == 0 && torn_e == 0 && runs_h > 4000 && runs_e > 4000)
            $display("PASS tb_halcyon_tick: ticks %0d / %0d (4800 expected), torn pairs %0d / %0d in %0d / %0d output updates", nh, ne, torn_h, torn_e, runs_h, runs_e);
        else
            $display("FAIL tb_halcyon_tick: ticks halcyon %0d eq %0d (4800 expected), torn pairs %0d / %0d, output updates %0d / %0d", nh, ne, torn_h, torn_e, runs_h, runs_e);
        $finish;
    end
endmodule
