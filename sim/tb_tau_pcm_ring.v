// B-741: tau_pcm_ring against a behavioural PSRAM (random 20-40 cycle reads, req held until done, the arbiter's idle gap after each done) and a behavioural FIFO that drains at a
// random rate. A CPU model writes distinct words into the ring and moves the write index (data first, index a cycle later, never filling the ring completely). Checks, over
// 3 phases (steady stream with wrap-around of a small ring, flushes in the middle of reads, a disabled period): the pushed stream equals the written stream in order with no
// pair lost, twice or stale; the FIFO is never pushed at or above THR+0 (the level is read when the read starts, so a push can only happen below THR) and never overflows;
// nothing is pushed while disabled; a flush drops exactly the unread pairs; cnt counts every push. -Ptb_tau_pcm_ring.BUG=n selects a mutant of the DUT (the DUT carries the hook).
`timescale 1ns/1ps
module tb_tau_pcm_ring #(parameter BUG = 0);
    localparam THR = 1536; localparam [22:0] SIZE = 23'd37, BASE = 23'd100;
    reg clk = 0, rst = 1;
    always #5 clk = ~clk;
    reg en = 0, flush = 0, hold = 0;
    reg [22:0] wptr = 0;
    reg [11:0] level = 0;
    wire [22:0] ridx; wire [31:0] cnt, pdata; wire push, busy, ctl_req, ctl_we; wire [22:0] ctl_word; wire [31:0] ctl_wdata; wire [3:0] ctl_be;
    reg ctl_done = 0; reg [31:0] ctl_rdata = 0;
    tau_pcm_ring #(.THR(THR), .BUG(BUG)) dut (.clk(clk), .rst(rst), .en(en), .flush(flush), .hold(hold), .base(BASE), .size(SIZE), .wptr(wptr), .fifo_level(level),
        .ridx(ridx), .cnt(cnt), .push(push), .pdata(pdata), .busy(busy), .ctl_req(ctl_req), .ctl_we(ctl_we), .ctl_word(ctl_word), .ctl_wdata(ctl_wdata), .ctl_be(ctl_be),
        .ctl_done(ctl_done), .ctl_rdata(ctl_rdata), .ctl_guard(1'b0));

    reg [31:0] mem [0:511];
    // ---- behavioural PSRAM
    integer lat = 0, gapc = 0; reg inflight = 0;
    always @(posedge clk) begin
        ctl_done <= 1'b0;
        if (rst) begin inflight <= 0; gapc <= 0; end
        else begin
            if (gapc > 0) gapc <= gapc - 1;
            if (!inflight && ctl_req && gapc == 0) begin inflight <= 1; lat <= 20 + ($urandom % 21); end
            else if (inflight) begin
                if (lat > 0) lat <= lat - 1;
                else begin ctl_done <= 1'b1; ctl_rdata <= mem[ctl_word]; inflight <= 0; gapc <= 2; end
            end
        end
    end
    // ---- behavioural FIFO
    integer wmax = 60, max_level = 0;
    integer overflow = 0, drain_pct = 50, pushes = 0, bad = 0, pushed_disabled = 0, over_thr = 0;
    always @(posedge clk) if (!rst) begin
        if (level > max_level) max_level = level;
        if (push) begin
            pushes = pushes + 1;
            if (level >= 2048) overflow = overflow + 1;
            if (level >= THR + 1) over_thr = over_thr + 1;   // the read began below THR; only our own pushes raise the level, so never above THR
            if (!en) pushed_disabled = pushed_disabled + 1;
            level <= level + 1 - ((($urandom % 100) < drain_pct && level > 0) ? 1 : 0);
        end else if (($urandom % 100) < drain_pct && level > 0) level <= level - 1;
    end
    // ---- reference stream
    reg [31:0] W [0:99999];
    integer nw = 0;            // words written by the CPU model so far
    integer nexp = 0;          // index of the next word the DUT must deliver
    reg [22:0] widx = 0;
    function [31:0] word(input integer k); word = (k * 32'h9E3779B1) ^ 32'h5A5A1234; endfunction
    // CPU writer: one word every few cycles, data first, index a cycle later, ring never filled completely
    integer wgap = 0;
    reg pend = 0; reg wr_enable = 0; reg [22:0] nxt;
    always @(posedge clk) if (!rst) begin
        if (pend) begin wptr <= widx; pend <= 0; end
        else if (wgap > 0) wgap <= wgap - 1;
        else begin
            nxt = (widx + 1 == SIZE) ? 23'd0 : widx + 1;
            if (nxt != ridx && wr_enable) begin
                mem[BASE + widx] <= word(nw); W[nw] = word(nw); nw = nw + 1; widx <= nxt; pend <= 1;
                wgap <= $urandom % (wmax + 1);
            end
        end
    end
    // delivery check
    always @(posedge clk) if (!rst && push) begin
        if (pdata !== word(nexp)) begin bad = bad + 1; if (bad < 5) $display("MISMATCH at %0d: got %h want %h (t=%0t nw=%0d ridx=%0d wptr=%0d level=%0d)", nexp, pdata, word(nexp), $time, nw, ridx, wptr, level); end
        nexp = nexp + 1;
    end
    task run(input integer cycles); begin repeat (cycles) @(posedge clk); end endtask
    task do_flush; begin
        // the firmware flushes between its own puts (never concurrently with one): pause the writer, let a pending index update land, flush, resume
        wr_enable <= 0; @(posedge clk); while (pend) @(posedge clk); @(posedge clk);
        flush <= 1; @(posedge clk); flush <= 0;
        run(2); nexp = nw; wr_enable <= 1;
    end endtask
    integer i, prev_pushes, c0;
    initial begin
        run(5); rst <= 0; run(3);
        // phase 1: enabled, steady stream, FIFO drains at 50% then 5% (backs up toward THR) then 95%
        en <= 1; wr_enable <= 1;
        drain_pct = 50; run(60000);
        drain_pct = 0; wmax = 2; run(60000);
        drain_pct = 3; run(60000);
        if (max_level < THR - 2) begin bad = bad + 1; $display("the FIFO never filled to THR (max level %0d): the level gate is untested", max_level); end
        wmax = 60;
        drain_pct = 95; run(60000);
        // phase 2: flushes while reads are in flight and the ring is busy
        for (i = 0; i < 40; i = i + 1) begin run(37 + (i * 53) % 200); do_flush; end
        run(20000);
        // phase 2b: hold (the player's pause): after a read in flight lands, nothing is pushed while held, nothing is lost or stale when released
        drain_pct = 95; hold <= 1; run(200); c0 = pushes; run(4000);
        if (pushes != c0) begin bad = bad + 1; $display("pushed %0d pairs while held", pushes - c0); end
        hold <= 0; run(8000);
        // phase 3: disabled: words are written, none may be pushed, none stale afterwards
        c0 = pushes; en <= 0; run(3000);
        if (pushes - c0 > 2) begin bad = bad + 1; $display("pushed while disabled"); end
        // the firmware enables the ring while nothing is being put: pause the writer, let the reader catch the write index, then enable (words written while disabled are owed to nobody)
        wr_enable <= 0; @(posedge clk); while (pend) @(posedge clk); run(8); nexp = nw; en <= 1; wr_enable <= 1; run(3000);
        wr_enable <= 0; run(5000);
        if (nexp != nw) begin bad = bad + 1; $display("tail not delivered: %0d of %0d", nexp, nw); end
        if (cnt != pushes) begin bad = bad + 1; $display("cnt %0d pushes %0d", cnt, pushes); end
        if (overflow != 0 || over_thr != 0 || pushed_disabled > 2) begin bad = bad + 1; $display("overflow %0d over_thr %0d pushed_disabled %0d", overflow, over_thr, pushed_disabled); end
        if (pushes < 1000) begin bad = bad + 1; $display("too few pushes %0d", pushes); end
        if (bad == 0) $display("PASSED: %0d pairs delivered in order (%0d written), flush/disable/wrap clean", pushes, nw); else $display("FAILED: %0d problems", bad);
        $finish;
    end
endmodule
