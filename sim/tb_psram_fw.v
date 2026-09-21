// P2 firmware-in-the-loop test: the real VexRiscv CPU (via mp3_soc) runs the
// psram_diag ROM against tau_psram_probe and the strict four-die chip model.
// Two runs: A (default timing) then B (slow dials). The published interact record
// (set_* words) is dumped so tools can verify it independently of the firmware:
//   +ROM=<file>  +OUT=<file>
// Uses the sim copy of mp3_soc made by sim/make_soc_sim.py.
`timescale 1ns/1ps
`default_nettype none
module tb_psram_fw;
    parameter WIN = 1;     // 1: SDRAM Phase 2 decode + PSRAM CPU window (B-016)
    parameter T_ACC = 9;   // read-sample index of the controller (margin experiment)
    parameter [15:0] EARLY = 16'hxxxx;   // DQ value before data is valid (definite in prediction runs)
    parameter real IO_NS = 0.0;   // pad delay added to read data valid (prediction runs)
    parameter FAULT = 0;   // 1: flip one bit of chip1/die0 word 10 (CPU die 2, offset 5)
    reg clk = 0, clk74 = 0, rst = 1;
    always #8.333 clk = ~clk;
    always #6.734 clk74 = ~clk74;

    // ---- ROM loader ---------------------------------------------------------
    reg         ld_wr = 0;
    reg  [31:0] ld_addr = 0;
    reg  [7:0]  ld_data = 0;
    reg  [7:0]  rom [0:65535];
    integer     rom_n, fd, i;
    reg  [1023:0] romfile, outfile;

    // ---- keys: A = bit4, B = bit5 ------------------------------------------
    reg  [31:0] cont_key = 0;

    // ---- settings words (interact) echo memory ------------------------------
    wire [3:0]  set_idx;
    wire        set_wr;
    wire [31:0] set_wdata;
    reg  [31:0] set_mem [0:15];
    wire [31:0] set_rdata = set_mem[set_idx];
    integer     publishes = 0;
    always @(posedge clk) if (set_wr) begin
        set_mem[set_idx] <= set_wdata;
        if (set_idx == 4'd15) publishes <= publishes + 1;
    end

    // ---- expansion MMIO <-> probe ------------------------------------------
    wire [7:0]  xm_reg;  wire xm_wr;  wire [31:0] xm_wdata;  wire [31:0] xm_rdata;

    // PSRAM CPU window: controller-side interface between mp3_soc and the probe
    wire        pw_req, pw_we, pw_done, pw_guard;
    wire [22:0] pw_word;
    wire [31:0] pw_wdata, pw_rdata;
    wire [3:0]  pw_be;

    mp3_soc #(.PHASE2_WINDOW_ENABLE(WIN), .PSRAM_WINDOW_ENABLE(WIN)) u_soc (
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
    wire adv0, adv1, ce00, ce01, ce10, ce11, oe0, oe1, we0, we1, ub0, ub1, lb0, lb1,
         clk0, clk1, cre0, cre1;
    tau_psram_probe #(.INIT_CYC(100), .T_ACC(T_ACC), .WIN_PRESENT(WIN)) u_probe (
        .clk(clk), .rst(rst), .xm_reg(xm_reg), .xm_wr(xm_wr), .xm_wdata(xm_wdata),
        .xm_rdata(xm_rdata),
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
    psram_chip_model #(.NAME("chip0"), .T_IO(IO_NS), .EARLY_FILL(EARLY)) c0 (.a(a0), .dq(dq0), .adv_n(adv0), .ce0_n(ce00), .ce1_n(ce01),
        .oe_n(oe0), .we_n(we0), .ub_n(ub0), .lb_n(lb0), .drv_en(drv0), .guard_touched(gt0));
    psram_chip_model #(.NAME("chip1"), .T_IO(IO_NS), .EARLY_FILL(EARLY), .FAULT_EN(FAULT), .FAULT_ADDR(22'd10), .FAULT_DIE(0), .FAULT_MASK(16'h0001)) c1 (.a(a1), .dq(dq1), .adv_n(adv1), .ce0_n(ce10), .ce1_n(ce11),
        .oe_n(oe1), .we_n(we1), .ub_n(ub1), .lb_n(lb1), .drv_en(drv1), .guard_touched(gt1));

    integer errors = 0;
    always @(posedge clk) if ((drv0 && u_probe.u_ctl.dq_oe0) || (drv1 && u_probe.u_ctl.dq_oe1)) begin
        $display("FAIL: DQ contention at %0t", $time); errors = errors + 1;
    end

    task dump_record(input integer run);
        integer j, f;
        begin
            f = $fopen(outfile, run == 1 ? "w" : "a");
            $fwrite(f, "run%0d", run);
            for (j = 0; j < 16; j = j + 1) $fwrite(f, " %h", set_mem[j]);
            $fwrite(f, "\n");
            $fclose(f);
        end
    endtask

    task press(input integer bit_, input integer n);
        begin
            cont_key[bit_] <= 1; repeat (2000) @(posedge clk); cont_key[bit_] <= 0;
            wait (publishes >= n);
            repeat (400) @(posedge clk);
            dump_record(n);
            $display("run %0d published at %0t", n, $time);
        end
    endtask

    initial begin
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "work/diagnostics/psram-diag-sim/tau.rom";
        if (!$value$plusargs("OUT=%s", outfile)) outfile = "build/rtl/psram_fw_record.txt";
        for (i = 0; i < 16; i = i + 1) set_mem[i] = 32'd0;
        fd = $fopen(romfile, "rb");
        if (fd == 0) begin $display("FAIL: cannot open ROM"); $finish; end
        rom_n = $fread(rom, fd);
        $fclose(fd);
        $display("ROM %0d bytes", rom_n);
        repeat (4) @(posedge clk);
        for (i = 0; i < rom_n; i = i + 1) begin
            ld_addr <= i; ld_data <= rom[i]; ld_wr <= 1; @(posedge clk);
        end
        ld_wr <= 0; repeat (4) @(posedge clk);
        rst <= 0;

        // run A (automatic at boot)
        wait (publishes >= 1);
        repeat (400) @(posedge clk);
        dump_record(1);
        $display("run A published at %0t", $time);
        // runs 2..4: X (read +1, bit 6), Y (read +2, bit 7), B (slow +3/+3, bit 5)
        press(6, 2); press(7, 3); press(5, 4);
        if (WIN) begin
            press(8, 5);                       // L1: window suite
            press(9, 6);                       // R1: soak, first pass
            wait (publishes >= 7);             // soak, second pass
            repeat (400) @(posedge clk);
            dump_record(7);
            $display("run 7 published at %0t", $time);
        end

        if (gt0 || gt1) begin $display("FAIL: chip saw the guard word"); errors = errors + 1; end
        errors = errors + c0.errors + c1.errors;
        $display("model: chip0 %0d w/%0d r, chip1 %0d w/%0d r, model errors %0d",
                 c0.ops_write, c0.ops_read, c1.ops_write, c1.ops_read, c0.errors + c1.errors);
        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end
    initial begin #4000000000; $display("FAIL: timeout"); $display("\nFAILED (timeout)"); $finish; end
endmodule
`default_nettype wire
