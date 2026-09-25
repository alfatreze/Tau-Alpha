// tb_tau_cdc_gray_bus.v -- a line counter (0..399, +1 per step, wrapping) crossing from a 12 MHz domain into a 60 MHz one:
// the decoded value must always be the true value or one step behind/ahead, and must reach every line. The wrap
// (399 -> 0) is excluded from the +-1 check by design (see the module header).
`timescale 1ns/1ps
module tb_tau_cdc_gray_bus;
    reg clk_src = 0, clk_dst = 0;
    always #41.66 clk_src = ~clk_src;        // ~12 MHz
    always #8.333 clk_dst = ~clk_dst;        // ~60 MHz
    reg [8:0] cnt = 0;
    wire [8:0] q;
    tau_cdc_gray_bus #(.W(9), .STAGES(3)) dut (.clk_src(clk_src), .d_src(cnt), .clk_dst(clk_dst), .q_dst(q));
    // advance the source every 500 source clocks (one 'line' of 500 pixels, like the video timing)
    integer n = 0, fails = 0, samples = 0, seen_lines = 0;
    reg [399:0] seen = 0;
    always @(posedge clk_src) begin
        n <= n + 1;
        if (n == 499) begin n <= 0; cnt <= (cnt == 9'd399) ? 9'd0 : cnt + 9'd1; end
    end
    integer diff;
    always @(posedge clk_dst) begin
        samples = samples + 1;
        if (samples > 50) begin
            diff = q - cnt;
            if (cnt >= 3 && cnt <= 396) begin
                if (diff < -1 || diff > 1) begin
                    if (fails < 5) $display("FAIL: decoded %0d vs true %0d at %0t", q, cnt, $time);
                    fails = fails + 1;
                end
            end
            if (q < 400) seen[q] = 1'b1;
        end
    end
    integer j, missing;
    initial begin
        #(2 * 400 * 500 * 83.32 + 1000);      // two full frames
        missing = 0;
        for (j = 0; j < 400; j = j + 1) if (!seen[j]) missing = missing + 1;
        if (missing != 0) begin $display("FAIL: %0d lines never observed", missing); fails = fails + 1; end
        if (fails == 0) $display("PASSED"); else $display("FAILED (%0d)", fails);
        $finish;
    end
endmodule
