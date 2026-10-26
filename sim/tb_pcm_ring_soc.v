// B-741: the PCM ring reader through the REAL tau_soc, PSRAM arbiter, tau_psram_async controller and strict chip model, driven by the real VexRiscv (sim/fw_pcmring/main.c).
// The stream the pcm_fifo receives must be: the 2500 pairs the firmware put, in order, none twice or lost, the poison written to R_AUDIO ignored; then, after the flush, none of the 10 marked
// pairs that were still unread, and the 5 pairs put after it, in order. The FIFO is never pushed at or above the reader's threshold and never overflows; no DQ contention, no guard access.
`timescale 1ns/1ps
`default_nettype none
module tb_psram_ifetch;
    parameter IFETCH = 1;
    parameter RING = 1;
    parameter T_ACC = 9;
    parameter IFETCH_GAP = 2;
    parameter EXPECT_NOFEATURE = 0;      // 1: build without the instruction path; the firmware must report G2 NOFEATURE
    reg clk = 0, clk74 = 0, rst = 1;
    always #8.333 clk = ~clk;
    always #6.734 clk74 = ~clk74;

    reg         ld_wr = 0;
    reg  [31:0] ld_addr = 0;
    reg  [7:0]  ld_data = 0;
    reg  [7:0]  rom [0:65535];
    integer     rom_n, fd, i;
    reg  [1023:0] romfile;
    reg  [31:0] cont_key = 0;

    wire [4:0]  set_idx;  wire set_wr;  wire [31:0] set_wdata;
    reg  [31:0] set_mem [0:15];
    wire [31:0] set_rdata = set_mem[set_idx];
    always @(posedge clk) if (set_wr) set_mem[set_idx] <= set_wdata;

    wire [7:0]  xm_reg;  wire xm_wr;  wire [31:0] xm_wdata;  wire [31:0] xm_rdata;
    wire        pw_req, pw_we, pw_done, pw_guard;
    wire [22:0] pw_word;
    wire [31:0] pw_wdata, pw_rdata;
    wire [3:0]  pw_be;

    tau_soc #(.PHASE2_WINDOW_ENABLE(1), .PSRAM_WINDOW_ENABLE(1), .PSRAM_IFETCH_ENABLE(IFETCH), .IFETCH_GAP(IFETCH_GAP), .PCM_RING_ENABLE(RING)) u_soc (
        .clk(clk), .rst(rst), .clk_74a(clk74),
        .ld_wr(ld_wr), .ld_addr(ld_addr), .ld_data(ld_data),
        .cont_key(cont_key), .in_menu(1'b0),
        .dataslot_update(1'b0), .dataslot_update_id(16'd0), .dataslot_update_size(32'd0),
        .dataslot_allcomplete(1'b0),
        .tgt_busy(1'b0), .tgt_done(1'b0), .tgt_seq(8'd0), .tgt_err(3'd0),
        .fb_cmd_full(1'b0), .dt_q(32'd0),
        .set_idx(set_idx), .set_wr(set_wr), .set_wdata(set_wdata), .set_rdata(set_rdata),
        .sdram_busy(1'b0), .sdram_done(1'b0), .sdram_rdata(32'd0),
        .sdram_wb_accept(1'b0), .sdram_wb_done(1'b0), .sdram_wb_rdata(32'd0),
        .xm_reg(xm_reg), .xm_wr(xm_wr), .xm_wdata(xm_wdata), .xm_rdata(xm_rdata),
        .psram_req(pw_req), .psram_we(pw_we), .psram_word(pw_word), .psram_wdata(pw_wdata),
        .psram_be(pw_be), .psram_done(pw_done), .psram_rdata(pw_rdata), .psram_guard(pw_guard)
    );

    wire [21:16] a0, a1;  wire [15:0] dq0, dq1;
    wire adv0, adv1, ce00, ce01, ce10, ce11, oe0, oe1, we0, we1, ub0, ub1, lb0, lb1, clk0, clk1, cre0, cre1;
    tau_psram_probe #(.INIT_CYC(100), .T_ACC(T_ACC), .WIN_PRESENT(1)) u_probe (
        .clk(clk), .rst(rst), .xm_reg(xm_reg), .xm_wr(xm_wr), .xm_wdata(xm_wdata), .xm_rdata(xm_rdata),
        .win_req(pw_req), .win_we(pw_we), .win_word(pw_word), .win_wdata(pw_wdata), .win_be(pw_be),
        .win_done(pw_done), .win_rdata(pw_rdata), .win_guard(pw_guard),
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
    always @(posedge clk) if ((drv0 && u_probe.u_ctl.dq_oe0) || (drv1 && u_probe.u_ctl.dq_oe1)) begin
        $display("FAIL: DQ contention at %0t", $time); errors = errors + 1;
    end

    // console
    reg [8*80-1:0] line;
    reg seen_end = 0, seen_nofeature = 0;
    integer ng_on = -1, ng_off = -1, ng = 0;
    always @(posedge clk) if (u_soc.con_wr) begin
        if (u_soc.con_char == 8'd10) begin
            $display("fw: %0s", line);
            if (line[8*8-1:0] == "RING END")     seen_end = 1;
            if (line[8*12-1:0] == "RING HOLD ON")  ng_on = ng;
            if (line[8*13-1:0] == "RING HOLD OFF") ng_off = ng;
            if (line[8*13-1:0] == "RING NOFEATURE") seen_nofeature = 1;
            line = 0;
        end else line = {line[8*79-1:0], u_soc.con_char};
    end
    // the stream the FIFO receives
    reg [31:0] got [0:4095];
    integer overflow = 0, over_thr = 0, max_level = 0;
    always @(posedge clk) if (!rst) begin
        if (u_soc.u_pcm.push) begin
            if (ng < 4096) got[ng] = u_soc.u_pcm.pdata;
            ng = ng + 1;
            if (u_soc.pcm_level >= 2048) overflow = overflow + 1;
            if (u_soc.pcm_level > 1536) over_thr = over_thr + 1;
        end
        if (u_soc.pcm_level > max_level) max_level = u_soc.pcm_level;
    end
    function [31:0] word(input integer n); word = (n * 32'h9E3779B1) ^ 32'h5A5A1234; endfunction
    integer k, bad = 0, markers = 0;
    initial begin
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "build/rtl/fw_pcmring.bin";
        fd = $fopen(romfile, "rb");
        if (fd == 0) begin $display("FAIL: cannot open ROM"); $finish; end
        rom_n = $fread(rom, fd);
        $fclose(fd);
        repeat (4) @(posedge clk);
        for (i = 0; i < rom_n; i = i + 1) begin ld_addr <= i; ld_data <= rom[i]; ld_wr <= 1; @(posedge clk); end
        ld_wr <= 0; repeat (4) @(posedge clk);
        rst <= 0;
        wait (seen_end || seen_nofeature);
        repeat (400) @(posedge clk);
        if (gt0 || gt1) begin $display("FAIL: chip saw the guard word"); errors = errors + 1; end
        errors = errors + c0.errors + c1.errors;
        if (RING == 0) begin
            if (!seen_nofeature) begin $display("FAIL: expected RING NOFEATURE"); errors = errors + 1; end
        end else begin
            // expected: 2500 pairs, 20 marked pairs delivered only after HOLD is released, the first 1536 of the next 1540 (the rest dropped by the flush), 5 after the flush
            if (ng != 4061) begin $display("FAIL: %0d pairs reached the FIFO, expected 4061", ng); errors = errors + 1; end
            for (k = 0; k < 2500 && k < ng; k = k + 1) if (got[k] !== word(k)) begin bad = bad + 1; if (bad < 4) $display("MISMATCH at %0d: got %h want %h", k, got[k], word(k)); end
            for (k = 0; k < 20 && 2500 + k < ng; k = k + 1) if (got[2500 + k] !== (32'hA0170000 | k)) begin bad = bad + 1; $display("held pair %0d: got %h", k, got[2500 + k]); end
            for (k = 0; k < 1536 && 2520 + k < ng; k = k + 1) if (got[2520 + k] !== (32'hB0000000 | k)) begin bad = bad + 1; if (bad < 8) $display("block pair %0d: got %h", k, got[2520 + k]); end
            for (k = 0; k < 5 && 4056 + k < ng; k = k + 1) if (got[4056 + k] !== (32'hC0DE0000 | k)) begin bad = bad + 1; $display("post-flush pair %0d: got %h", k, got[4056 + k]); end
            if (ng_on < 0 || ng_off < 0 || ng_off != ng_on) begin errors = errors + 1; $display("FAIL: pairs reached the FIFO while HOLD was on (%0d at hold on, %0d at hold off)", ng_on, ng_off); end
            for (k = 0; k < ng && k < 4096; k = k + 1) if (got[k][31:16] === 16'hF1A5 || got[k] === 32'hDEADBEEF) markers = markers + 1;
            if (bad != 0) begin $display("FAIL: %0d wrong pairs", bad); errors = errors + 1; end
            if (markers != 0) begin $display("FAIL: %0d poison/flushed pairs reached the FIFO", markers); errors = errors + 1; end
            if (overflow != 0 || over_thr != 0) begin $display("FAIL: FIFO overflow %0d, above threshold %0d", overflow, over_thr); errors = errors + 1; end
            if (max_level < 1530) begin $display("FAIL: the FIFO never filled to the threshold (max %0d): the level gate was not exercised", max_level); errors = errors + 1; end
        end
        $display("model: chip0 %0d w/%0d r, chip1 %0d w/%0d r, model errors %0d; FIFO max level %0d, %0d pairs", c0.ops_write, c0.ops_read, c1.ops_write, c1.ops_read, c0.errors + c1.errors, max_level, ng);
        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end
    parameter TMAX = 1800000000;
    initial begin #TMAX; $display("FAIL: timeout"); $display("\nFAILED (timeout)"); $finish; end
endmodule
`default_nettype wire
