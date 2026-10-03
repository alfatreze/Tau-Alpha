// tb_tau_audio_stats.v -- tau_audio_stats against an independent 64-bit model: per-window power sums, the stereo cross sum and the clip
// counters, over signals chosen to hit every corner (full-scale noise, identical channels, opposite channels, silence, constant rails).
// Prints PASSED or FAILED; the mutation target (make test-rtl-audio-stats-mutation) builds it with BUG=1..5, every one of which must fail.
`timescale 1ns/1ps
module tb_tau_audio_stats;
    parameter BUG = 0;
    localparam WIN = 1024;
    reg clk = 0; always #8.333 clk = ~clk;
    reg rst = 1, tick = 0;
    reg signed [15:0] in_l = 0, in_r = 0;
    reg ctl_we = 0, ctl_data = 0, idx_we = 0; reg [2:0] idx_data = 0;
    wire [31:0] rd_data, status;
    tau_audio_stats #(.WIN_LOG2(10), .BUG(BUG)) dut (.clk(clk), .rst(rst), .tick(tick), .in_l(in_l), .in_r(in_r),
        .ctl_we(ctl_we), .ctl_data(ctl_data), .idx_we(idx_we), .idx_data(idx_data), .rd_data(rd_data), .status(status));

    reg signed [63:0] m_ll, m_rr, m_lr;
    integer m_cl, m_cr, fails, w, i, mode;
    reg [31:0] lo, hi;
    reg signed [63:0] got;
    reg [31:0] seed = 32'h1234ABCD;
    function [31:0] rnd(input [31:0] s); begin rnd = s * 32'd1664525 + 32'd1013904223; end endfunction

    task put(input integer l, input integer r);
        begin
            in_l <= l; in_r <= r; tick <= 1; @(posedge clk); tick <= 0;
            repeat (11) @(posedge clk);
            m_ll = m_ll + l * l; m_rr = m_rr + r * r; m_lr = m_lr + l * r;
            if (l >= 32767 || l <= -32767) m_cl = m_cl + 1;
            if (r >= 32767 || r <= -32767) m_cr = m_cr + 1;
        end
    endtask

    task rd(input [2:0] k, output [31:0] v);
        begin idx_data <= k; idx_we <= 1; @(posedge clk); idx_we <= 0; @(posedge clk); v = rd_data; end
    endtask

    task check(input integer wi);
        begin
            rd(0, lo); rd(1, hi); got = {16'd0, hi, lo};
            if (got !== m_ll) begin fails = fails + 1; $display("FAIL window %0d LL got %0d want %0d", wi, got, m_ll); end
            rd(2, lo); rd(3, hi); got = {16'd0, hi, lo};
            if (got !== m_rr) begin fails = fails + 1; $display("FAIL window %0d RR got %0d want %0d", wi, got, m_rr); end
            rd(4, lo); rd(5, hi); got = $signed({hi, lo});
            if (got !== m_lr) begin fails = fails + 1; $display("FAIL window %0d LR got %0d want %0d", wi, got, m_lr); end
            rd(6, lo);
            if (lo[15:0] !== m_cl[15:0] || lo[31:16] !== m_cr[15:0]) begin fails = fails + 1; $display("FAIL window %0d clips got L%0d R%0d want L%0d R%0d", wi, lo[15:0], lo[31:16], m_cl, m_cr); end
            if (status[31:16] !== wi + 1) begin fails = fails + 1; $display("FAIL window counter got %0d want %0d", status[31:16], wi + 1); end
            if (status[0] !== 1'b1) begin fails = fails + 1; $display("FAIL present bit"); end
        end
    endtask

    integer l, r;
    initial begin
        fails = 0; m_cl = 0; m_cr = 0;
        repeat (4) @(posedge clk); rst <= 0; repeat (4) @(posedge clk);
        for (w = 0; w < 8; w = w + 1) begin
            m_ll = 0; m_rr = 0; m_lr = 0;
            for (i = 0; i < WIN; i = i + 1) begin
                seed = rnd(seed);
                case (w)
                    0: begin l = $signed(seed[31:16]); seed = rnd(seed); r = $signed(seed[31:16]); end          // full-range noise
                    1: begin l = $signed(seed[31:16]); r = l; end                                              // identical channels
                    2: begin l = $signed(seed[31:16]); if (l == -32768) l = -32767; r = -l; end                // opposite channels
                    3: begin l = 0; r = 0; end                                                                 // silence
                    4: begin l = 32767; r = -32768; end                                                        // both rails: clips on both
                    5: begin l = (i % 2) ? 32767 : -32768; r = 5000; end                                       // alternating rails on L only
                    6: begin l = $signed(seed[31:16]) >>> 6; seed = rnd(seed); r = $signed(seed[31:16]) >>> 6; end  // quiet noise
                    default: begin l = 100; r = -3; end
                endcase
                put(l, r);
            end
            repeat (4) @(posedge clk);
            check(w);
            if (w == 5) begin   // clear the clip counters mid-run; the model clears with it
                ctl_data <= 1; ctl_we <= 1; @(posedge clk); ctl_we <= 0; ctl_data <= 0; @(posedge clk);
                m_cl = 0; m_cr = 0;
                rd(6, lo);
                if (lo !== 32'd0) begin fails = fails + 1; $display("FAIL clip clear: %0h", lo); end
            end
        end
        if (fails == 0) $display("PASSED"); else $display("FAILED %0d", fails);
        $finish;
    end
endmodule
