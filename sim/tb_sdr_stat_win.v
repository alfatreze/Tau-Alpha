// tb_sdr_stat_win.v -- the read window of the per-port SDRAM statistics (tau_sdr_stat_win.sv): counters that run freely in clk_sdram (steps of +1, +burst and jumps, as sched_v's do),
// a CPU-side driver in an unrelated clk_sys. For many random selections (valid ones and out-of-range ones) it checks:
//   * after the write, `pending` (status bit 8) rises and falls within a bounded time, the selection reads back, the present bit is set;
//   * the value is a SNAPSHOT: lo <= value <= hi, where lo is the counter when the select was written and hi the counter when pending dropped (the counters are monotonic), and
//     two reads of R_SDA_VAL a long time apart (the counters ran meanwhile) are equal -- kills BUG=1 (the live counter, unsynchronised);
//   * per selection the values never go backwards from one selection to the next;
//   * an out-of-range selection reads 0;
//   * a select written while one is pending is ignored: the selection stays the first one and the value belongs to it -- kills BUG=2 (the two request toggles can cancel).
// SYSP / PHASE: clk_sys half period and start offset in ps (the two clocks are unrelated). BUG selects the mutant.
`timescale 1ps/1ps
`default_nettype none
module tb_sdr_stat_win;
    parameter BUG = 0;
    parameter SYSP = 8333;
    parameter PHASE = 0;
    parameter N = 400;
    reg clk_sys = 0, clk_sdram = 0, rst = 1;
    initial begin #(PHASE); forever #(SYSP) clk_sys = ~clk_sys; end
    always #5000 clk_sdram = ~clk_sdram;

    // ---- counters in clk_sdram: monotonic, with steps of 1, a burst length, and jumps ----
    reg [31:0] req [0:2], words [0:2], busy [0:2]; reg [15:0] bad [0:2], worst [0:2], uover [0:2];
    integer p;
    initial for (p = 0; p < 3; p = p + 1) begin req[p] = 0; words[p] = 0; busy[p] = 0; bad[p] = 0; worst[p] = 0; uover[p] = 0; end
    always @(posedge clk_sdram) for (p = 0; p < 3; p = p + 1) begin
        if ($urandom % 3 == 0) req[p] <= req[p] + 1;
        if ($urandom % 4 == 0) words[p] <= words[p] + 1 + ($urandom % 8);
        if ($urandom % 2 == 0) busy[p] <= busy[p] + 1;
        if ($urandom % 50 == 0) worst[p] <= worst[p] + 1 + ($urandom % 40);
        if ($urandom % 70 == 0) uover[p] <= uover[p] + 1;
        if ($urandom % 90 == 0) bad[p] <= bad[p] + 1;
    end
    wire [95:0] c_req = {req[2], req[1], req[0]}, c_words = {words[2], words[1], words[0]}, c_busy = {busy[2], busy[1], busy[0]};
    wire [47:0] c_bad = {bad[2], bad[1], bad[0]}, c_worst = {worst[2], worst[1], worst[0]}, c_uover = {uover[2], uover[1], uover[0]};
    function [31:0] model(input [4:0] sel);
        begin
            model = 32'd0;
            if (sel[4:3] != 2'd3) case (sel[2:0])
                3'd0: model = req[sel[4:3]];   3'd1: model = words[sel[4:3]];  3'd2: model = busy[sel[4:3]];
                3'd3: model = {16'd0, worst[sel[4:3]]}; 3'd4: model = {16'd0, uover[sel[4:3]]}; 3'd5: model = {16'd0, bad[sel[4:3]]};
                default: model = 32'd0;
            endcase
        end
    endfunction

    reg we = 0; reg [4:0] wd = 0; wire [31:0] status, val;
    tau_sdr_stat_win #(.BUG(BUG)) dut (.clk_sys(clk_sys), .rst_sys(rst), .sel_we(we), .sel_wd(wd), .status(status), .val(val),
        .clk_sdram(clk_sdram), .cnt_requests(c_req), .cnt_words(c_words), .cnt_busy(c_busy), .cnt_bad(c_bad), .cnt_worst_wait(c_worst), .cnt_urgent_over(c_uover));

    integer errors = 0, n, t, rp, rs;
    reg [31:0] lo, hi, v1, v2; reg [4:0] sel_i; reg [31:0] last [0:31];
    integer k; initial for (k = 0; k < 32; k = k + 1) last[k] = 0;
    task wr(input [4:0] s); begin @(posedge clk_sys); we <= 1; wd <= s; @(posedge clk_sys); we <= 0; end endtask
    task wait_done; begin t = 0; @(posedge clk_sys); while (status[8] && t < 200) begin @(posedge clk_sys); t = t + 1; end if (t >= 200) begin errors = errors + 1; $display("FAIL: pending never dropped"); end end endtask
    integer pend_seen = 0;
    initial begin
        repeat (6) @(posedge clk_sys); rst <= 0; repeat (10) @(posedge clk_sys);
        if (status[31] !== 1'b1 || status[8] !== 1'b0) begin errors = errors + 1; $display("FAIL: idle status %h", status); end
        for (n = 0; n < N; n = n + 1) begin
            rp = $urandom % 3; rs = $urandom % 6;
            if (n % 5 == 0) begin rp = $urandom % 4; rs = 6 + ($urandom % 2); end        // a stat outside 0..5
            else if (n % 7 == 0) rp = 3;                                                // the port outside 0..2
            sel_i = {rp[1:0], rs[2:0]};
            lo = model(sel_i);
            wr(sel_i);
            @(posedge clk_sys); if (status[8]) pend_seen = pend_seen + 1;
            wait_done;
            hi = model(sel_i);
            v1 = val;
            if (status[4:0] !== sel_i || status[31] !== 1'b1) begin errors = errors + 1; $display("FAIL: status %h for select %0d", status, sel_i); end
            if (v1 < lo || v1 > hi) begin errors = errors + 1; $display("FAIL: select %0d value %0d outside [%0d,%0d]", sel_i, v1, lo, hi); end
            if (sel_i[4:3] == 2'd3 || sel_i[2:0] > 3'd5) begin if (v1 !== 32'd0) begin errors = errors + 1; $display("FAIL: out-of-range select %0d reads %0d", sel_i, v1); end end
            else begin
                if (v1 < last[sel_i]) begin errors = errors + 1; $display("FAIL: select %0d went backwards", sel_i); end
                last[sel_i] = v1;
            end
            repeat (50 + ($urandom % 100)) @(posedge clk_sys);              // the counters run meanwhile
            v2 = val;
            if (v2 !== v1) begin errors = errors + 1; $display("FAIL: select %0d value moved without a new select (%0d -> %0d)", sel_i, v1, v2); end
            if (n % 9 == 0) begin                                          // a second select right behind the first: ignored while pending
                lo = model({2'd1, 3'd1});
                wr({2'd1, 3'd1}); we <= 0;
                @(posedge clk_sys); we <= 1; wd <= {2'd2, 3'd2}; @(posedge clk_sys); we <= 0;
                wait_done; hi = model({2'd1, 3'd1});
                if (status[4:0] !== {2'd1, 3'd1}) begin errors = errors + 1; $display("FAIL: a select written while pending was accepted (%0d)", status[4:0]); end
                if (val < lo || val > hi) begin errors = errors + 1; $display("FAIL: value after the double write outside [%0d,%0d]: %0d", lo, hi, val); end
            end
        end
        if (pend_seen < N/2) begin errors = errors + 1; $display("FAIL: pending was rarely seen high (%0d)", pend_seen); end
        if (errors == 0) $display("PASSED: tb_sdr_stat_win (BUG=%0d SYSP=%0d PHASE=%0d)", BUG, SYSP, PHASE); else $display("FAILED: tb_sdr_stat_win %0d errors (BUG=%0d)", errors, BUG);
        $finish;
    end
    initial begin #2_000_000_000; $display("FAIL: timeout"); $finish; end
endmodule
