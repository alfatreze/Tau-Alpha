// The audio statistics registers in tau_soc.v (STATS_CTL/IDX/DATA) with the real CPU driving them (firmware sim/fw_stats/main.c). After the firmware prints READY the bench forces eight known
// samples into the block's tap (the PCM FIFO's sample strobe and outputs); the firmware checks N and the three sums. Also: a build without STATS_ENABLE must read 0 at STATS_DATA
// (ENABLE = 0, the mutant), and the firmware's "FAIL" verdicts fail the bench. +ROM=<file>.
`timescale 1ns/1ps
`default_nettype none
module tb_stats_soc;
    parameter ENABLE = 1;
    reg clk = 0, clk74 = 0, rst = 1;
    always #8.333 clk = ~clk;
    always #6.734 clk74 = ~clk74;
    reg         ld_wr = 0;
    reg  [31:0] ld_addr = 0;
    reg  [7:0]  ld_data = 0;
    reg  [7:0]  rom [0:65535];
    integer     rom_n, fd, i, k, errors = 0, fails = 0;
    reg  [1023:0] romfile;
    wire [4:0]  set_idx;  wire set_wr;  wire [31:0] set_wdata;
    wire [15:0] audio_l, audio_r;
    tau_soc #(.STATS_ENABLE(ENABLE)) u_soc (
        .clk(clk), .rst(rst), .clk_74a(clk74),
        .ld_wr(ld_wr), .ld_addr(ld_addr), .ld_data(ld_data),
        .cont_key(32'd0), .in_menu(1'b0),
        .dataslot_update(1'b0), .dataslot_update_id(16'd0), .dataslot_update_size(32'd0),
        .dataslot_allcomplete(1'b0),
        .tgt_busy(1'b0), .tgt_done(1'b0), .tgt_seq(8'd0), .tgt_err(3'd0),
        .fb_cmd_full(1'b0), .dt_q(32'd0),
        .set_idx(set_idx), .set_wr(set_wr), .set_wdata(set_wdata), .set_rdata(32'd0),
        .sdram_busy(1'b0), .sdram_done(1'b0), .sdram_rdata(32'd0),
        .sdram_wb_accept(1'b0), .sdram_wb_done(1'b0), .sdram_wb_rdata(32'd0),
        .xm_rdata(32'd0),
        .psram_done(1'b0), .psram_rdata(32'd0), .psram_guard(1'b0),
        .audio_l(audio_l), .audio_r(audio_r)
    );
    reg [8*80-1:0] line; reg [31:0] last4 = 0; reg done = 0, ready = 0;
    always @(posedge clk) if (u_soc.con_wr) begin
        last4 = {last4[23:0], u_soc.con_char};
        if (last4 == "FAIL") begin fails = fails + 1; errors = errors + 1; end
        if (u_soc.con_char == 8'd10) begin
            $display("fw: %0s", line);
            if (line[8*5-1:0] == "READY") ready = 1;
            if (line[8*4-1:0] == "DONE") done = 1;
            line = 0;
        end else line = {line[8*79-1:0], u_soc.con_char};
    end
    initial begin
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "build/rtl/fw_stats.bin";
        fd = $fopen(romfile, "rb");
        if (fd == 0) begin $display("FAIL: cannot open ROM"); $finish; end
        rom_n = $fread(rom, fd);
        $fclose(fd);
        repeat (4) @(posedge clk);
        for (i = 0; i < rom_n; i = i + 1) begin ld_addr <= i; ld_data <= rom[i]; ld_wr <= 1; @(posedge clk); end
        ld_wr <= 0; repeat (4) @(posedge clk);
        rst <= 0;
        wait (ready || done);
        if (ENABLE != 0) begin
            for (k = 0; k < 8; k = k + 1) begin
                @(negedge clk); force u_soc.fifo_l = (k + 1) * 1000; force u_soc.fifo_r = ((k % 2) ? -1 : 1) * (k + 1) * 700;
                @(negedge clk); force u_soc.pcm_sample_tick = 1'b1; @(negedge clk); force u_soc.pcm_sample_tick = 1'b0;     // changes on the falling edge: no race with the rising one
                repeat (60) @(negedge clk);
            end
            // the tap stays quiet (the FIFO's own strobe would add real samples to the window the firmware is about to read)
        end
        wait (done); repeat (50) @(posedge clk);
        if (errors == 0) $display("PASSED: tb_stats_soc"); else $display("FAILED: %0d errors", errors);
        $finish;
    end
    initial begin #300_000_000; $display("FAIL: timeout"); $finish; end
endmodule
