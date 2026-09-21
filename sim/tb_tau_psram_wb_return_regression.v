// P1 bus regression (KB-024 / A-093 shape): Wishbone master with an
// mp3_soc-style registered ACK (dACK <= wb_ack) that either keeps STB asserted
// into the next beat (gap 0) or idles (gap > 0), driving tau_psram_bus ->
// tau_psram_async -> strict chip model. Asserts data, that every beat produces
// exactly one controller request, and that a guard beat returns ERR, not data.
// Mutation hooks (each must FAIL):
//   -Ptb_tau_psram_wb_return_regression.REL_CYC=1        (A-092 duplicate request)
//   -Ptb_tau_psram_wb_return_regression.MUT_EARLY_ACK=1  (ACK before data loaded)
`timescale 1ns/1ps
`default_nettype none
module tb_tau_psram_wb_return_regression;
    parameter REL_CYC = 2;
    parameter MUT_EARLY_ACK = 0;
    parameter GUARD_ERR = 1;      // 0: the guard beat must ACK with data 0 (CPU window mode)

    reg clk = 0, rst = 1;
    always #8.333 clk = ~clk;

    reg         cyc = 0, stb = 0, we = 0;
    reg  [22:0] adr = 0;
    reg  [31:0] dat = 0;
    reg  [3:0]  sel = 4'hf;
    wire [31:0] rdata;
    wire        ack_pulse, err_pulse, unsupported;
    reg         dACK = 0, dERR = 0;
    always @(posedge clk) begin
        dACK <= rst ? 1'b0 : ack_pulse;
        dERR <= rst ? 1'b0 : err_pulse;
    end

    wire        c_req, c_we, c_done, c_guard;
    wire [22:0] c_word;
    wire [31:0] c_wdata, c_rdata;
    wire [3:0]  c_be;
    tau_psram_bus #(.REL_CYC(REL_CYC), .MUT_EARLY_ACK(MUT_EARLY_ACK), .GUARD_ERR(GUARD_ERR)) bus (
        .clk(clk), .rst(rst), .wb_cyc(cyc), .wb_stb(stb), .wb_we(we), .wb_cti(3'b000),
        .wb_adr(adr), .wb_dat_i(dat), .wb_sel(sel), .wb_dat_o(rdata), .wb_ack(ack_pulse),
        .wb_err(err_pulse), .wb_unsupported(unsupported),
        .ctl_req(c_req), .ctl_we(c_we), .ctl_word(c_word), .ctl_wdata(c_wdata), .ctl_be(c_be),
        .ctl_done(c_done), .ctl_rdata(c_rdata), .ctl_guard(c_guard)
    );

    wire [21:16] a0, a1;  wire [15:0] dq0, dq1;
    wire adv0, adv1, ce00, ce01, ce10, ce11, oe0, oe1, we0, we1, ub0, ub1, lb0, lb1, clk0, clk1, cre0, cre1;
    wire sce, sgh, busy;
    tau_psram_async #(.INIT_CYC(20)) ctl (
        .clk(clk), .rst(rst), .req(c_req), .we(c_we), .cpu_word(c_word), .wdata(c_wdata), .be(c_be),
        .cfg_rd_extra(4'd0), .cfg_wr_extra(4'd0), .clr_sticky(1'b0),
        .done(c_done), .rdata(c_rdata), .guard(c_guard), .busy(busy),
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

    // one controller request per bus beat
    integer beats = 0, starts = 0, errors = 0;
    reg c_req_q = 0;
    always @(posedge clk) begin
        c_req_q <= c_req;
        if (c_req && !c_req_q) starts <= starts + 1;
    end

    // ACK must come from the loaded response register: at the ACK cycle the bus
    // response must already equal the controller's held response.
    always @(posedge clk) if (!rst && ack_pulse && (rdata !== c_rdata)) begin
        $display("FAIL: ACK before response register loaded (bus %h, controller %h)", rdata, c_rdata);
        errors = errors + 1;
    end

    task check(input [31:0] g, input [31:0] e, input [8*40-1:0] what);
        if (g !== e) begin
            $display("FAIL: %0s got %h expected %h", what, g, e);
            errors = errors + 1;
        end else $display("ok:   %0s", what);
    endtask

    reg [31:0] got; reg goterr;
    task beat(input w, input [22:0] a_, input [31:0] d_, input integer gap);
        begin
            cyc <= 1; stb <= 1; we <= w; adr <= a_; dat <= d_; sel <= 4'hf;
            beats = beats + 1;
            @(posedge clk);
            while (!(dACK || dERR)) @(posedge clk);
            got = rdata; goterr = dERR;
            if (gap > 0) begin cyc <= 0; stb <= 0; repeat (gap) @(posedge clk); end
        end
    endtask

    integer gapv;
    localparam [22:0] DIE1 = 23'h200000, CHIP1 = 23'h400000;
    initial begin
        repeat (4) @(posedge clk); rst <= 0;
        repeat (30) @(posedge clk);
        // seed contents (gap 3 so each beat is isolated)
        beat(1, 23'h000200, 32'h12345678, 3);
        beat(1, 23'h000202, 32'h0BADF00D, 3);
        beat(1, 23'h000204, 32'h0DEFACED, 3);
        for (gapv = 0; gapv <= 3; gapv = gapv + 3) begin
            $display("--- gap %0d ---", gapv);
            beat(0, 23'h000200, 32'd0, gapv); check(got, 32'h12345678, "load A");
            beat(0, 23'h000202, 32'd0, gapv); check(got, 32'h0BADF00D, "load B (back-to-back)");
            beat(0, 23'h000204, 32'd0, gapv); check(got, 32'h0DEFACED, "load C (back-to-back)");
            beat(1, DIE1 + 23'h10, 32'hFFFFFFFF, gapv);
            beat(0, DIE1 + 23'h10, 32'd0, gapv);  check(got, 32'hFFFFFFFF, "load after all-ones store");
            beat(1, DIE1 + 23'h10, 32'h00000000, gapv);
            beat(0, DIE1 + 23'h10, 32'd0, gapv);  check(got, 32'h00000000, "load after zero store");
            beat(1, CHIP1 + 23'h20, 32'hA5C33C5A, gapv);
            beat(0, CHIP1 + 23'h20, 32'd0, gapv); check(got, 32'hA5C33C5A, "load after pattern store (chip 1)");
            beat(1, CHIP1 + DIE1 + 23'h30, 32'h5A3CC3A5, gapv);
            beat(0, CHIP1 + DIE1 + 23'h30, 32'd0, gapv); check(got, 32'h5A3CC3A5, "load after store (chip 1 die 1)");
            // guard word: ERR pulse, no data, no chip access
            beat(0, DIE1 + 23'h1FFFFF, 32'd0, gapv);
            if (GUARD_ERR != 0) begin
                if (!goterr) begin $display("FAIL: guard beat did not return ERR"); errors = errors + 1; end
                else $display("ok:   guard beat returns ERR");
            end else begin
                if (goterr) begin $display("FAIL: guard beat returned ERR in ACK mode"); errors = errors + 1; end
                else check(got, 32'd0, "guard beat ACKs with data 0");
            end
            beat(0, 23'h000200, 32'd0, gapv); check(got, 32'h12345678, "load after guard beat");
        end
        cyc <= 0; stb <= 0;
        repeat (400) @(posedge clk);
        if (starts != beats) begin
            $display("FAIL: %0d Wishbone beats produced %0d controller requests", beats, starts);
            errors = errors + 1;
        end else $display("ok:   one controller request per beat (%0d)", beats);
        if (gt0 || gt1) begin $display("FAIL: chip saw the guard word"); errors = errors + 1; end
        errors = errors + c0.errors + c1.errors;
        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end
    initial begin #40000000; $display("FAIL: regression timeout"); $display("\nFAILED (timeout)"); $finish; end
endmodule
`default_nettype wire
