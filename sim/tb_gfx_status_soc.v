// The Talos 2 T3 status registers in tau_soc.v (R_FB_GO bit 1, R_GFX_FENCE/STATUS/COUNTS) with the real CPU reading them (firmware sim/fw_gfxstat/main.c).
// The bench drives the tau_gfx status inputs with known values, changes them after the firmware prints READY, and fails on any firmware "FAIL" verdict.
// SWAP = 1 (the mutant) swaps the fence and underflow inputs: the firmware must see the difference. +ROM=<file>.
`timescale 1ns/1ps
`default_nettype none
module tb_gfx_status_soc;
    parameter SWAP = 0;
    reg clk = 0, clk74 = 0, rst = 1;
    always #8.333 clk = ~clk;
    always #6.734 clk74 = ~clk74;
    reg         ld_wr = 0;
    reg  [31:0] ld_addr = 0;
    reg  [7:0]  ld_data = 0;
    reg  [7:0]  rom [0:65535];
    integer     rom_n, fd, i, errors = 0;
    reg  [1023:0] romfile;
    wire [4:0]  set_idx;  wire set_wr;  wire [31:0] set_wdata;
    wire [15:0] audio_l, audio_r;
    reg         full = 0, idle = 1;
    reg  [31:0] fence = 32'h00C0FFEE;
    reg  [8:0]  free_s = 9'd123;
    reg  [15:0] ovf = 16'd45, uf = 16'd7;
    tau_soc u_soc (
        .clk(clk), .rst(rst), .clk_74a(clk74),
        .ld_wr(ld_wr), .ld_addr(ld_addr), .ld_data(ld_data),
        .cont_key(32'd0), .in_menu(1'b0),
        .dataslot_update(1'b0), .dataslot_update_id(16'd0), .dataslot_update_size(32'd0),
        .dataslot_allcomplete(1'b0),
        .tgt_busy(1'b0), .tgt_done(1'b0), .tgt_seq(8'd0), .tgt_err(3'd0),
        .fb_cmd_full(full), .fb_gfx_idle(idle), .fb_gfx_fence(SWAP ? {16'd0, uf} : fence), .fb_gfx_free(free_s), .fb_gfx_ovf(ovf),
        .fb_gfx_underflow(SWAP ? fence[15:0] : uf), .dt_q(32'd0),
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
        if (last4 == "FAIL") errors = errors + 1;
        if (u_soc.con_char == 8'd10) begin
            $display("fw: %0s", line);
            if (line[8*5-1:0] == "READY") ready = 1;
            if (line[8*4-1:0] == "DONE") done = 1;
            line = 0;
        end else line = {line[8*79-1:0], u_soc.con_char};
    end
    initial begin
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "build/rtl/fw_gfxstat.bin";
        fd = $fopen(romfile, "rb");
        if (fd == 0) begin $display("FAIL: cannot open ROM"); $finish; end
        rom_n = $fread(rom, fd);
        $fclose(fd);
        repeat (4) @(posedge clk);
        for (i = 0; i < rom_n; i = i + 1) begin ld_addr <= i; ld_data <= rom[i]; ld_wr <= 1; @(posedge clk); end
        ld_wr <= 0; repeat (4) @(posedge clk);
        rst <= 0;
        wait (ready);
        @(negedge clk); idle <= 0; full <= 1; fence <= 32'h00C0FFEF; free_s <= 9'd0; ovf <= 16'd46; uf <= 16'd8;
        wait (done); repeat (50) @(posedge clk);
        if (errors == 0) $display("PASSED: tb_gfx_status_soc"); else $display("FAILED: %0d errors", errors);
        $finish;
    end
    initial begin #300_000_000; $display("FAIL: timeout"); $finish; end
endmodule
