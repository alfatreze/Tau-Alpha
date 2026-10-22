// tb_tau_audio_stats.v -- tau_audio_stats against an independent behavioural model. Random and extreme samples at the real sample spacing: every window's
// N, SLL, SRR, SLR (signed, 48 bits) must equal the model's. Then a stress with samples every 4 clocks and latches at random moments: no sample is lost or counted twice
// (the windows add up to the totals). Also: clear, the sequence number, the status signature and the overflow flag.
`timescale 1ns/1ps
module tb_tau_audio_stats;
    parameter BUGP = 0;
    reg clk = 0; always #7.5 clk = ~clk;
    reg rst = 1, tick = 0;
    reg signed [15:0] in_l = 0, in_r = 0;
    reg ctl_we = 0; reg [1:0] ctl_data = 0;
    reg idx_we = 0; reg [2:0] idx_data = 0;
    wire [31:0] rd_data, status;
    tau_audio_stats #(.BUG(BUGP)) dut (.clk(clk), .rst(rst), .tick(tick), .in_l(in_l), .in_r(in_r), .ctl_we(ctl_we), .ctl_data(ctl_data),
        .idx_we(idx_we), .idx_data(idx_data), .rd_data(rd_data), .status(status));

    integer fails = 0, w, i, n_m, sp, lt;
    reg signed [63:0] m_ll, m_rr, m_lr, t_ll, t_rr, t_lr, g_ll, g_rr, g_lr;
    integer t_n, g_n;
    reg [31:0] w1, w2, w3, w4, w5, w6, w7;
    integer seed = 12345;

    task push(input integer l, input integer r, input integer gap);
        begin
            in_l <= l; in_r <= r; tick <= 1; @(posedge clk); tick <= 0;
            repeat (gap) @(posedge clk);
            n_m = n_m + 1; m_ll = m_ll + l * l; m_rr = m_rr + r * r; m_lr = m_lr + l * r;
        end
    endtask
    task latch; begin ctl_data <= 2'b01; ctl_we <= 1; @(posedge clk); ctl_we <= 0; @(posedge clk); end endtask
    task rd(input [2:0] k, output [31:0] v); begin idx_data <= k; idx_we <= 1; @(posedge clk); idx_we <= 0; @(posedge clk); v = rd_data; end endtask
    task getwin;
        begin
            rd(1, w1); rd(2, w2); rd(3, w3); rd(4, w4); rd(5, w5); rd(6, w6); rd(7, w7);
            g_n = w1; g_ll = {w3[15:0], w2}; g_rr = {w5[15:0], w4}; g_lr = {{16{w7[15]}}, w7[15:0], w6};
            g_ll = $signed({w3[15:0], w2}); g_rr = $signed({w5[15:0], w4});
        end
    endtask
    task check(input [255:0] name);
        begin
            getwin;
            if (g_n !== n_m || g_ll !== m_ll || g_rr !== m_rr || g_lr !== m_lr) begin
                fails = fails + 1; $display("FAIL %0s: n %0d/%0d ll %0d/%0d rr %0d/%0d lr %0d/%0d", name, g_n, n_m, g_ll, m_ll, g_rr, m_rr, g_lr, m_lr);
            end
        end
    endtask
    function integer rnd(input integer amp); begin seed = seed * 1103515245 + 12345; rnd = ((seed >>> 8) & 32'hFFFF) % (2 * amp + 1) - amp; end endfunction

    initial begin
        repeat (4) @(posedge clk); rst = 0; repeat (4) @(posedge clk);
        rd(0, w1);
        if (w1[31:16] !== 16'h5354 || w1[0] !== 1'b1 || w1[1] !== 1'b0) begin fails = fails + 1; $display("FAIL status signature %h", w1); end
        // three windows at the real spacing (1388 clocks): random, extreme, opposite channels
        for (w = 0; w < 3; w = w + 1) begin
            n_m = 0; m_ll = 0; m_rr = 0; m_lr = 0;
            for (i = 0; i < 40; i = i + 1) begin
                if (w == 0) push(rnd(30000), rnd(30000), 20);
                else if (w == 1) push((i % 2) ? -32768 : 32767, (i % 3) ? -32768 : 32767, 20);
                else begin sp = rnd(20000); push(sp, -sp, 20); end
            end
            latch; check("window");
        end
        // an empty window reads zero
        n_m = 0; m_ll = 0; m_rr = 0; m_lr = 0; latch; check("empty window");
        // the sequence number counts latches
        rd(0, w1); if (w1[15:8] !== 8'd4) begin fails = fails + 1; $display("FAIL sequence %0d", w1[15:8]); end
        // a second latch reads the NEW window (old sums are gone from the accumulators)
        n_m = 0; m_ll = 0; m_rr = 0; m_lr = 0; push(1000, -2000, 20); latch; check("one sample");
        n_m = 0; m_ll = 0; m_rr = 0; m_lr = 0; push(300, 400, 20); latch; check("window after one sample");
        // clear
        ctl_data <= 2'b10; ctl_we <= 1; @(posedge clk); ctl_we <= 0; @(posedge clk);
        n_m = 0; m_ll = 0; m_rr = 0; m_lr = 0; check("after clear"); rd(0, w1); if (w1[15:8] !== 8'd0) begin fails = fails + 1; $display("FAIL sequence after clear"); end
        // stress: a sample every 4 clocks, latches at random times; the windows must add up to the totals
        t_n = 0; t_ll = 0; t_rr = 0; t_lr = 0; n_m = 0; m_ll = 0; m_rr = 0; m_lr = 0;
        for (i = 0; i < 6000; i = i + 1) begin
            lt = ((rnd(50) % 17) == 0);
            push(rnd(32000), rnd(32000), lt ? (i % 3) : 3);                  // a latch lands 0..2 clocks after a sample: the product may still be in the pipeline
            if (lt) begin
                latch; getwin; t_n = t_n + g_n; t_ll = t_ll + g_ll; t_rr = t_rr + g_rr; t_lr = t_lr + g_lr;
                n_m = n_m - g_n; m_ll = m_ll - g_ll; m_rr = m_rr - g_rr; m_lr = m_lr - g_lr;
            end
        end
        repeat (6) @(posedge clk); latch; getwin; t_n = t_n + g_n; t_ll = t_ll + g_ll; t_rr = t_rr + g_rr; t_lr = t_lr + g_lr;
        n_m = n_m - g_n; m_ll = m_ll - g_ll; m_rr = m_rr - g_rr; m_lr = m_lr - g_lr;
        if (n_m != 0 || m_ll != 0 || m_rr != 0 || m_lr != 0) begin fails = fails + 1; $display("FAIL conservation: left over n %0d ll %0d rr %0d lr %0d", n_m, m_ll, m_rr, m_lr); end
        // overflow flag: a window longer than 131,072 samples sets it, the next latch clears it
        ctl_data <= 2'b10; ctl_we <= 1; @(posedge clk); ctl_we <= 0; @(posedge clk);
        for (i = 0; i < 131080; i = i + 1) begin in_l <= 1; in_r <= 1; tick <= 1; @(posedge clk); tick <= 0; repeat (3) @(posedge clk); end
        latch; rd(0, w1); if (w1[1] !== 1'b1) begin fails = fails + 1; $display("FAIL overflow flag not set"); end
        latch; rd(0, w1); if (w1[1] !== 1'b0) begin fails = fails + 1; $display("FAIL overflow flag not cleared"); end
        if (fails == 0) $display("PASSED (0 failures)"); else $display("FAILED (%0d)", fails);
        $finish;
    end
endmodule
