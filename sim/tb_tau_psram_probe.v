// P2 mailbox test: tau_psram_probe driven through the expansion-MMIO port exactly
// as firmware will drive it, against the strict four-die chip model.
// Run with -Ptb_tau_psram_probe.WD=8 to check that TIMEOUT latches (op still
// completes); the default watchdog must NOT trip on any normal operation.
`timescale 1ns/1ps
`default_nettype none
module tb_tau_psram_probe;
    parameter WD = 65535;
    localparam INIT_CYC = 40;
    localparam [7:0] R_ID = 8'h88, R_ADDR = 8'h8C, R_WDATA = 8'h90, R_CTRL = 8'h94,
                     R_STATUS = 8'h98, R_RDATA = 8'h9C, R_CFG = 8'hA0, R_LAST = 8'hA4,
                     R_COUNT = 8'hA8;

    reg clk = 0, rst = 1;
    always #8.333 clk = ~clk;

    reg  [7:0]  xm_reg = 0;
    reg         xm_wr = 0;
    reg  [31:0] xm_wdata = 0;
    wire [31:0] xm_rdata;

    wire [21:16] a0, a1;  wire [15:0] dq0, dq1;
    wire adv0, adv1, ce00, ce01, ce10, ce11, oe0, oe1, we0, we1, ub0, ub1, lb0, lb1,
         clk0, clk1, cre0, cre1;

    // CPU-window client (driven like the bus wrapper: level request until win_done)
    reg         win_req = 0, win_we = 0;
    reg  [22:0] win_word = 0;
    reg  [31:0] win_wdata = 0;
    reg  [3:0]  win_be = 4'hf;
    wire        win_done, win_guard;
    wire [31:0] win_rdata;
    integer     win_done_cnt = 0;
    always @(posedge clk) if (win_done) win_done_cnt <= win_done_cnt + 1;

    tau_psram_probe #(.INIT_CYC(INIT_CYC), .WATCHDOG(WD), .WIN_PRESENT(1)) dut (
        .clk(clk), .rst(rst), .xm_reg(xm_reg), .xm_wr(xm_wr), .xm_wdata(xm_wdata),
        .xm_rdata(xm_rdata),
        .win_req(win_req), .win_we(win_we), .win_word(win_word), .win_wdata(win_wdata), .win_be(win_be),
        .win_done(win_done), .win_rdata(win_rdata), .win_guard(win_guard),
        .cram0_a(a0), .cram0_dq(dq0), .cram0_wait(1'b1), .cram0_clk(clk0), .cram0_adv_n(adv0),
        .cram0_cre(cre0), .cram0_ce0_n(ce00), .cram0_ce1_n(ce01), .cram0_oe_n(oe0),
        .cram0_we_n(we0), .cram0_ub_n(ub0), .cram0_lb_n(lb0),
        .cram1_a(a1), .cram1_dq(dq1), .cram1_wait(1'b1), .cram1_clk(clk1), .cram1_adv_n(adv1),
        .cram1_cre(cre1), .cram1_ce0_n(ce10), .cram1_ce1_n(ce11), .cram1_oe_n(oe1),
        .cram1_we_n(we1), .cram1_ub_n(ub1), .cram1_lb_n(lb1)
    );
    wire drv0, drv1, gt0, gt1;
    psram_chip_model #(.NAME("chip0")) c0 (.a(a0), .dq(dq0), .adv_n(adv0), .ce0_n(ce00), .ce1_n(ce01),
        .oe_n(oe0), .we_n(we0), .ub_n(ub0), .lb_n(lb0), .drv_en(drv0), .guard_touched(gt0));
    psram_chip_model #(.NAME("chip1")) c1 (.a(a1), .dq(dq1), .adv_n(adv1), .ce0_n(ce10), .ce1_n(ce11),
        .oe_n(oe1), .we_n(we1), .ub_n(ub1), .lb_n(lb1), .drv_en(drv1), .guard_touched(gt1));

    integer errors = 0;
    task check(input [31:0] g, input [31:0] e, input [8*40-1:0] what);
        if (g !== e) begin $display("FAIL: %0s got %h expected %h", what, g, e); errors = errors + 1; end
    endtask

    task mw(input [7:0] r, input [31:0] d);
        begin
            xm_reg <= r; xm_wdata <= d; xm_wr <= 1; @(posedge clk);
            xm_wr <= 0; @(posedge clk);
        end
    endtask
    reg [31:0] rv;
    task mr(input [7:0] r);
        begin xm_reg <= r; @(posedge clk); #1; rv = xm_rdata; end
    endtask
    task clr_sticky_pulse; begin mw(R_CTRL, 32'h40); end endtask
    task wait_idle;
        integer n;
        begin
            n = 0; mr(R_STATUS);
            while (rv[0] && n < 5000) begin mr(R_STATUS); n = n + 1; end
            if (rv[0]) begin $display("FAIL: still busy after 5000 polls"); errors = errors + 1; end
        end
    endtask
    task op(input we_, input [22:0] a_, input [31:0] d_, input [3:0] be_);
        begin
            wait_idle;
            mw(R_ADDR, {9'd0, a_}); mw(R_WDATA, d_);
            mw(R_CTRL, {26'd0, 1'b0, be_, we_, 1'b1});
            wait_idle;
        end
    endtask
    task wr(input [22:0] a_, input [31:0] d_); op(1, a_, d_, 4'hf); endtask
    task rd(input [22:0] a_, input [31:0] e_, input [8*40-1:0] what);
        begin op(0, a_, 32'd0, 4'hf); mr(R_RDATA); check(rv, e_, what); end
    endtask

    reg [31:0] wgot; reg wguard;
    task wop(input we_, input [22:0] a_, input [31:0] d_, input [3:0] b_);
        begin
            win_we <= we_; win_word <= a_; win_wdata <= d_; win_be <= b_; win_req <= 1;
            @(posedge clk); while (!win_done) @(posedge clk);
            wgot = win_rdata; wguard = win_guard; win_req <= 0;
            @(posedge clk); @(posedge clk);
        end
    endtask
    task wwr(input [22:0] a_, input [31:0] d_); wop(1, a_, d_, 4'hf); endtask
    task wrd(input [22:0] a_, input [31:0] e_, input [8*40-1:0] what);
        begin wop(0, a_, 32'd0, 4'hf); check(wgot, e_, what); end
    endtask

    integer d, i, cnt0, first_ce = -1, cyc = 0, w0;
    always @(posedge clk) begin
        if (!rst) cyc <= cyc + 1;
        if (!rst && first_ce < 0 && (!ce00 || !ce01 || !ce10 || !ce11)) first_ce <= cyc;
        if ((drv0 && dut.u_ctl.dq_oe0) || (drv1 && dut.u_ctl.dq_oe1)) begin
            $display("FAIL: DQ contention"); errors = errors + 1;
        end
    end

    initial begin
        repeat (4) @(posedge clk); rst <= 0; repeat (2) @(posedge clk);
        mr(R_ID);  check(rv, 32'h50535231, "ID register");
        mr(R_STATUS); if (!rv[0]) begin $display("FAIL: not busy during hold-off"); errors = errors + 1; end
        // a REQ during the hold-off must be ignored (BUSY set), not queued
        mw(R_CTRL, 32'h0000_003D);
        repeat (INIT_CYC + 10) @(posedge clk);
        wait_idle; mr(R_COUNT); check(rv, 0, "no op ran during hold-off");
        if (first_ce >= 0 && first_ce < INIT_CYC) begin $display("FAIL: CE# during hold-off"); errors = errors + 1; end

        for (d = 0; d < 4; d = d + 1) begin
            wr({d[1:0], 21'd0} + 23'h100, 32'hC0DE0000 + d);
            wr({d[1:0], 21'd0} + 23'h1FFFFE, 32'hDEAD0000 + d);
        end
        for (d = 0; d < 4; d = d + 1) begin
            rd({d[1:0], 21'd0} + 23'h100, 32'hC0DE0000 + d, "die word");
            rd({d[1:0], 21'd0} + 23'h1FFFFE, 32'hDEAD0000 + d, "last legal word");
        end
        // byte lanes and LAST register
        wr(23'h000200, 32'h11223344);
        op(1, 23'h000200, 32'hAABBCCDD, 4'b0101);
        rd(23'h000200, 32'h11BB33DD, "byte lanes");
        mr(R_LAST); check(rv, {4'd0, 4'hf, 1'b0, 23'h000200}, "LAST after read");
        // a write to ADDR/WDATA while idle must not disturb RDATA/LAST
        mw(R_ADDR, 32'h7FFFFF); mw(R_WDATA, 32'hFFFFFFFF);
        mr(R_RDATA); check(rv, 32'h11BB33DD, "RDATA held");
        // guard word
        op(0, 23'h1FFFFF, 32'd0, 4'hf);
        mr(R_STATUS);
        if (!rv[3] || !rv[5]) begin $display("FAIL: guard flags %h", rv); errors = errors + 1; end
        mr(R_RDATA); check(rv, 32'd0, "guard RDATA");
        mw(R_CTRL, 32'h40); mr(R_STATUS);
        if (rv[5] || rv[4]) begin $display("FAIL: CLR did not clear sticky flags"); errors = errors + 1; end
        // dials
        mw(R_CFG, 32'h33); mr(R_CFG); check(rv, 32'h10933, "CFG readback incl. T_ACC and window-present");
        wr(23'h300000, 32'h13572468); rd(23'h300000, 32'h13572468, "slow dials round trip");
        mw(R_CFG, 32'h00);
        // counters
        mr(R_COUNT); cnt0 = rv;
        for (i = 0; i < 5; i = i + 1) rd(23'h000200, 32'h11BB33DD, "repeat");
        mr(R_COUNT); check(rv, cnt0 + 5, "COUNT increments per op");
        mr(R_STATUS); check({24'd0, rv[15:8]}, (cnt0 + 5) & 32'hFF, "STATUS op counter");
        // ---- CPU-window client sharing the controller (B-016) ----
        mr(8'hAC); w0 = rv;
        mr(R_CFG); check(rv[16], 1'b1, "CFG[16] reports the window is present");
        for (d = 0; d < 4; d = d + 1) begin
            wwr({d[1:0], 21'd0} + 23'h1234, 32'hA0B0C000 + d);
            mr(R_STATUS); // (status readable between window ops)
            rd({d[1:0], 21'd0} + 23'h1234, 32'hA0B0C000 + d, "window write -> mailbox read");
            wr({d[1:0], 21'd0} + 23'h2345, 32'h0D0E0F00 + d);
            wrd({d[1:0], 21'd0} + 23'h2345, 32'h0D0E0F00 + d, "mailbox write -> window read");
        end
        // byte lanes through the window
        wwr(23'h000300, 32'h11223344); wop(1, 23'h000300, 32'hAABBCCDD, 4'b0101);
        wrd(23'h000300, 32'h11BB33DD, "window byte lanes 0,2");
        // guard word through the window: data 0, guard flag, no chip access
        clr_sticky_pulse;
        wop(0, 23'h1FFFFF, 32'd0, 4'hf);
        check(wgot, 32'd0, "window guard read returns 0"); if (!wguard) begin $display("FAIL: window guard flag"); errors = errors + 1; end
        mr(R_STATUS); if (!rv[5]) begin $display("FAIL: guard sticky not set by window access"); errors = errors + 1; end
        // contention: both clients ask in the same clock; window wins, mailbox runs next
        wwr(23'h000400, 32'hCAFE0001); wr(23'h000404, 32'hCAFE0002);
        mw(R_ADDR, 32'h000404); mw(R_CTRL, 32'h1);            // mailbox READ pending
        win_we <= 0; win_word <= 23'h000400; win_be <= 4'hf; win_req <= 1;   // window READ in the same window of time
        @(posedge clk); while (!win_done) @(posedge clk);
        check(win_rdata, 32'hCAFE0001, "window read under contention"); win_req <= 0;
        mr(R_STATUS); if (!rv[0] && !rv[1]) begin $display("FAIL: mailbox request lost"); errors = errors + 1; end
        wait_idle; mr(R_RDATA); check(rv, 32'hCAFE0002, "mailbox read completes after the window op");
        // back-to-back window beats (registered-ACK style: gap of 2 clocks)
        for (i = 0; i < 6; i = i + 1) wwr(23'h000500 + i, 32'hB0000000 + i);
        for (i = 0; i < 6; i = i + 1) wrd(23'h000500 + i, 32'hB0000000 + i, "window back-to-back");
        mr(8'hAC); if (rv - w0 !== win_done_cnt) begin $display("FAIL: WCOUNT %0d vs %0d window ops", rv - w0, win_done_cnt); errors = errors + 1; end
        // window op shows BUSY in STATUS while it runs
        win_we <= 0; win_word <= 23'h000500; win_req <= 1; @(posedge clk); @(posedge clk);
        mr(R_STATUS); if (!rv[0]) begin $display("FAIL: BUSY not set during a window op"); errors = errors + 1; end
        while (!win_done) @(posedge clk); win_req <= 0; @(posedge clk); @(posedge clk);
        // watchdog: a tiny WATCHDOG must latch TIMEOUT; the default must not
        mr(R_STATUS);
        if (WD < 100) begin
            if (!rv[2]) begin $display("FAIL: TIMEOUT not latched with WD=%0d", WD); errors = errors + 1; end
        end else if (rv[2]) begin $display("FAIL: spurious TIMEOUT"); errors = errors + 1; end
        if (rv[4]) begin $display("FAIL: CE conflict flag"); errors = errors + 1; end
        if (gt0 || gt1) begin $display("FAIL: chip saw the guard word"); errors = errors + 1; end
        errors = errors + c0.errors + c1.errors;
        $display("model: chip0 %0d w/%0d r, chip1 %0d w/%0d r, model errors %0d",
                 c0.ops_write, c0.ops_read, c1.ops_write, c1.ops_read, c0.errors + c1.errors);
        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end
    initial begin #200000000; $display("FAIL: timeout"); $display("\nFAILED (timeout)"); $finish; end
endmodule
`default_nettype wire
