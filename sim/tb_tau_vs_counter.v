// tb_tau_vs_counter.v -- tau_vs_counter counts every vsync rising edge exactly once, however short the pulse
// is relative to the destination clock's polling, and keeps bit 0 as the synchronised level.
`timescale 1ns/1ps
module tb_tau_vs_counter;
    reg clk_dst = 0, vs = 0;
    always #8.333 clk_dst = ~clk_dst;          // ~60 MHz destination
    wire [16:0] q;
    tau_vs_counter #(.STAGES(3), .W(16)) dut (.clk_dst(clk_dst), .d_src(vs), .q(q));
    integer fails = 0, i, base;
    task pulse; begin vs = 1; #(4*41666); vs = 0; #(400*41666 - 4*41666); end endtask   // 4 lines high, 400-line frame
    initial begin
        #1000;
        if (q[16:1] !== 16'd0) begin $display("FAIL: counter not zero at start (%0d)", q[16:1]); fails = fails + 1; end
        base = q[16:1];
        for (i = 0; i < 60; i = i + 1) pulse;
        #1000;
        if (q[16:1] !== base + 60) begin $display("FAIL: counted %0d edges, want 60", q[16:1] - base); fails = fails + 1; end
        // a level that stays high must not keep counting
        vs = 1; #5000000;
        if (q[16:1] !== base + 61) begin $display("FAIL: held level counted %0d", q[16:1] - base); fails = fails + 1; end
        if (q[0] !== 1'b1) begin $display("FAIL: level bit not 1 while vs is high"); fails = fails + 1; end
        vs = 0; #10000;
        if (q[0] !== 1'b0) begin $display("FAIL: level bit not 0 after vs falls"); fails = fails + 1; end
        if (fails == 0) $display("PASSED"); else $display("FAILED (%0d)", fails);
        $finish;
    end
endmodule
