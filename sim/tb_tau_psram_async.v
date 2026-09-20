// P1 controller test: tau_psram_async against the strict four-die chip model.
// Covers hold-off, walking patterns, address lines, byte lanes, random traffic
// across all dies, guard words, die/chip boundaries, reset mid-operation, and
// the slow-timing dials. Mutation hooks: -Ptb_tau_psram_async.T_ACC=4/6/7 must FAIL (T_ACC=6 was the
// pre-datasheet default and violates tOE/tAADV; 7 is one cycle short).
`timescale 1ns/1ps
`default_nettype none
module tb_tau_psram_async;
    parameter T_ACC = 8;
    localparam INIT_CYC = 40;

    reg clk = 0, rst = 1;
    always #8.333 clk = ~clk;

    reg         req = 0, we = 0, clr = 0;
    reg  [22:0] cw = 0;
    reg  [31:0] wd = 0;
    reg  [3:0]  be = 4'hf, rdx = 0, wrx = 0;
    wire        done, guard, busy, sce, sgh;
    wire [31:0] rdata;

    wire [21:16] a0, a1;
    wire [15:0]  dq0, dq1;
    wire adv0, adv1, ce00, ce01, ce10, ce11, oe0, oe1, we0, we1, ub0, ub1, lb0, lb1;
    wire clk0, clk1, cre0, cre1;

    tau_psram_async #(.INIT_CYC(INIT_CYC), .T_ACC(T_ACC)) dut (
        .clk(clk), .rst(rst), .req(req), .we(we), .cpu_word(cw), .wdata(wd), .be(be),
        .cfg_rd_extra(rdx), .cfg_wr_extra(wrx), .clr_sticky(clr),
        .done(done), .rdata(rdata), .guard(guard), .busy(busy),
        .sticky_ce_conflict(sce), .sticky_guard_hit(sgh),
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
        if (g !== e) begin
            $display("FAIL: %0s got %h expected %h", what, g, e);
            errors = errors + 1;
        end
    endtask

    // ---- always-on pin monitors ---------------------------------------------
    integer cyc_since_rst = 0, first_ce = -1;
    wire any_ce = !ce00 || !ce01 || !ce10 || !ce11;
    always @(posedge clk) begin
        if (rst) begin cyc_since_rst <= 0; first_ce <= -1; end
        else begin
            cyc_since_rst <= cyc_since_rst + 1;
            if (any_ce && first_ce < 0) first_ce <= cyc_since_rst;
        end
        if ((drv0 && dut.dq_oe0) || (drv1 && dut.dq_oe1)) begin
            $display("FAIL: DQ contention at %0t", $time); errors = errors + 1;
        end
        if (!rst && dut.st == 2'd0 && any_ce) begin
            $display("FAIL: CE# active during power-up hold-off"); errors = errors + 1;
        end
    end

    // ---- driver ---------------------------------------------------------------
    reg [31:0] got; reg gg;
    task xfer(input w, input [22:0] a_, input [31:0] d_, input [3:0] b_);
        begin
            we <= w; cw <= a_; wd <= d_; be <= b_; req <= 1;
            @(posedge clk);
            while (!done) @(posedge clk);
            got = rdata; gg = guard; req <= 0;
            @(posedge clk);
        end
    endtask
    task wr(input [22:0] a_, input [31:0] d_); xfer(1, a_, d_, 4'hf); endtask
    task rd(input [22:0] a_, input [31:0] e_, input [8*40-1:0] what);
        begin xfer(0, a_, 32'd0, 4'hf); check(got, e_, what); end
    endtask

    localparam [22:0] D00 = {2'b00, 21'd0}, D01 = {2'b01, 21'd0},
                      D10 = {2'b10, 21'd0}, D11 = {2'b11, 21'd0};
    reg [22:0] bases [0:3];
    integer d, i, k, n;

    // random scoreboard
    reg [22:0] ra [0:63];
    reg [31:0] rv [0:63];
    reg        rw [0:63];
    reg [22:0] cand;
    integer    found, j;

    task suite;
        begin
            for (d = 0; d < 4; d = d + 1) begin
                // walking one / zero
                for (i = 0; i < 32; i = i + 1) begin
                    wr(bases[d] + i, 32'd1 << i);
                    wr(bases[d] + 32 + i, ~(32'd1 << i));
                end
                for (i = 0; i < 32; i = i + 1) begin
                    rd(bases[d] + i, 32'd1 << i, "walking one");
                    rd(bases[d] + 32 + i, ~(32'd1 << i), "walking zero");
                end
                // address lines: one address bit at a time, address-as-data
                for (k = 0; k < 20; k = k + 1) wr(bases[d] + (23'd1 << k), {9'd0, bases[d] + (23'd1 << k)} ^ 32'hA5A50000);
                for (k = 0; k < 20; k = k + 1) rd(bases[d] + (23'd1 << k), {9'd0, bases[d] + (23'd1 << k)} ^ 32'hA5A50000, "address line");
                // byte lanes
                wr(bases[d] + 200, 32'h11223344);
                xfer(1, bases[d] + 200, 32'hAABBCCDD, 4'b0101);
                rd(bases[d] + 200, 32'h11BB33DD, "lanes 0,2");
                xfer(1, bases[d] + 200, 32'h55667788, 4'b1010);
                rd(bases[d] + 200, 32'h55BB77DD, "lanes 1,3");
                xfer(1, bases[d] + 200, 32'hFFFFFFFF, 4'b0000);
                rd(bases[d] + 200, 32'h55BB77DD, "no lanes");
                xfer(1, bases[d] + 200, 32'h000000EE, 4'b0001);
                rd(bases[d] + 200, 32'h55BB77EE, "lane 0 only");
                // read-after-write and alternating
                for (i = 0; i < 8; i = i + 1) begin
                    wr(bases[d] + 300 + i, 32'hC0DE0000 + i);
                    rd(bases[d] + 300 + i, 32'hC0DE0000 + i, "read-after-write");
                end
                // last legal word before the guard, then the guard itself
                wr(bases[d] + 23'h1FFFFE, 32'hDEAD0000 + d);
                rd(bases[d] + 23'h1FFFFE, 32'hDEAD0000 + d, "last legal word");
                xfer(1, bases[d] + 23'h1FFFFF, 32'h13572468, 4'hf);
                if (!gg) begin $display("FAIL: guard write not refused (die %0d)", d); errors = errors + 1; end
                xfer(0, bases[d] + 23'h1FFFFF, 32'd0, 4'hf);
                if (!gg) begin $display("FAIL: guard read not refused (die %0d)", d); errors = errors + 1; end
                check(got, 32'd0, "guard read data");
            end
            // die/chip boundary transitions in one stream
            wr(D00 + 23'h1FFFFE, 32'h00000001); wr(D01, 32'h00000002);
            wr(D01 + 23'h1FFFFE, 32'h00000003); wr(D10, 32'h00000004);
            wr(D10 + 23'h1FFFFE, 32'h00000005); wr(D11, 32'h00000006);
            wr(D11 + 23'h1FFFFE, 32'h00000007);
            rd(D00 + 23'h1FFFFE, 1, "bnd 0"); rd(D01, 2, "bnd 1"); rd(D01 + 23'h1FFFFE, 3, "bnd 2");
            rd(D10, 4, "bnd 3"); rd(D10 + 23'h1FFFFE, 5, "bnd 4"); rd(D11, 6, "bnd 5");
            rd(D11 + 23'h1FFFFE, 7, "bnd 6");
            // random read/write across all dies
            for (n = 0; n < 64; n = n + 1) begin
                cand = $random;
                while (cand[20:0] == 21'h1FFFFF) cand = $random;
                ra[n] = cand; rw[n] = 0;
            end
            for (n = 0; n < 200; n = n + 1) begin
                j = $unsigned($random) % 64;
                if ($random & 1) begin
                    rv[j] = $random; rw[j] = 1; wr(ra[j], rv[j]);
                end else if (rw[j]) rd(ra[j], rv[j], "random readback");
            end
        end
    endtask

    integer sub_errors;
    initial begin
        bases[0] = D00; bases[1] = D01; bases[2] = D10; bases[3] = D11;
        repeat (4) @(posedge clk);
        rst <= 0;
        // request immediately: the hold-off must delay all chip activity
        wr(23'h000123, 32'hCAFEBABE);
        if (first_ce < INIT_CYC) begin
            $display("FAIL: first CE# at cycle %0d, hold-off is %0d", first_ce, INIT_CYC);
            errors = errors + 1;
        end else $display("ok:   hold-off respected (first CE# at cycle %0d)", first_ce);
        rd(23'h000123, 32'hCAFEBABE, "first access");

        // access cost at the default timing (controller only, req -> done)
        begin : cost
            integer c0_, c1_;
            we <= 1; cw <= 23'h000300; wd <= 32'h1; be <= 4'hf; req <= 1;
            c0_ = 0; @(posedge clk); while (!done) begin @(posedge clk); c0_ = c0_ + 1; end
            req <= 0; repeat (3) @(posedge clk);
            we <= 0; req <= 1;
            c1_ = 0; @(posedge clk); while (!done) begin @(posedge clk); c1_ = c1_ + 1; end
            req <= 0; repeat (3) @(posedge clk);
            $display("cost: 32-bit write %0d clocks, 32-bit read %0d clocks (controller, defaults, 60 MHz)", c0_ + 1, c1_ + 1);
        end
        $display("--- default timing ---");
        suite;
        $display("--- slow-timing dials (rd+3, wr+3) ---");
        rdx = 3; wrx = 3;
        suite;
        rdx = 0; wrx = 0;

        // reset in the middle of a read on chip 1 die 1, then again mid-write
        $display("--- reset mid-operation ---");
        for (k = 0; k < 2; k = k + 1) begin
            we <= k; cw <= D11 + 23'd50; wd <= 32'h0BADBEEF; be <= 4'hf; req <= 1;
            repeat (8 + 6 * k) @(posedge clk);
            rst <= 1; req <= 0;
            repeat (2) @(posedge clk);
            if (any_ce || dut.dq_oe0 || dut.dq_oe1 || !oe1 || !we1 || !adv1) begin
                $display("FAIL: pins not idle 2 clocks after reset (k=%0d)", k); errors = errors + 1;
            end
            repeat (2) @(posedge clk);
            rst <= 0;
            repeat (INIT_CYC + 30) begin
                @(posedge clk);
                if (done) begin $display("FAIL: phantom done after reset"); errors = errors + 1; end
            end
        end
        rd(D00 + 23'h1FFFFE, 1, "data survives reset");
        wr(D11 + 23'd50, 32'h12345678); rd(D11 + 23'd50, 32'h12345678, "usable after reset");

        // sticky flags (reset clears them, so provoke a guard hit now)
        if (sgh) begin $display("FAIL: sticky guard flag survived reset"); errors = errors + 1; end
        xfer(0, D10 + 23'h1FFFFF, 32'd0, 4'hf);
        if (!sgh) begin $display("FAIL: sticky guard flag lost"); errors = errors + 1; end
        if (sce)  begin $display("FAIL: unexpected CE conflict"); errors = errors + 1; end
        clr <= 1; @(posedge clk); clr <= 0; @(posedge clk);
        if (sgh) begin $display("FAIL: sticky guard flag not cleared"); errors = errors + 1; end
        if (gt0 || gt1) begin $display("FAIL: chip saw the guard word"); errors = errors + 1; end
        errors = errors + c0.errors + c1.errors;
        $display("model: chip0 %0d writes/%0d reads, chip1 %0d writes/%0d reads, model errors %0d",
                 c0.ops_write, c0.ops_read, c1.ops_write, c1.ops_read, c0.errors + c1.errors);
        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end
    initial begin #400000000; $display("FAIL: timeout"); $display("\nFAILED (timeout)"); $finish; end
endmodule
`default_nettype wire
