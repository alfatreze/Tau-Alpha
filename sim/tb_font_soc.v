// The font memory registers in tau_soc.v (FONT_CTRL/IDX/DATA) driven by the real VexRiscv running sim/fw_font/main.c. The testbench checks the firmware's own verdicts
// (FONT ok n / FONT FAIL n on the console) and the write port the registers drive (fb_font_we pulses with the index and the data): word 6110, 6111, then 0, in that order and nothing else.
// Mutation hooks (-Ptb_font_soc.BUG_FONT_NO_INC / BUG_FONT_NO_GEN) must make it fail. +ROM=<file>.
`timescale 1ns/1ps
`default_nettype none
module tb_font_soc;
    parameter BUG_FONT_NO_INC = 0;
    parameter BUG_FONT_NO_GEN = 0;
    reg clk = 0, clk74 = 0, rst = 1;
    always #8.333 clk = ~clk;
    always #6.734 clk74 = ~clk74;
    reg         ld_wr = 0;
    reg  [31:0] ld_addr = 0;
    reg  [7:0]  ld_data = 0;
    reg  [7:0]  rom [0:65535];
    integer     rom_n, fd, i, errors = 0, wn = 0, fails = 0;
    reg  [1023:0] romfile;
    wire [4:0]  set_idx;  wire set_wr;  wire [31:0] set_wdata;
    wire [15:0] audio_l, audio_r;
    wire        font_we; wire [12:0] font_waddr; wire [31:0] font_wdata;
    tau_soc #(.FONT_RAM_ENABLE(1), .BUG_FONT_NO_INC(BUG_FONT_NO_INC), .BUG_FONT_NO_GEN(BUG_FONT_NO_GEN)) u_soc (
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
        .audio_l(audio_l), .audio_r(audio_r),
        .fb_font_we(font_we), .fb_font_waddr(font_waddr), .fb_font_wdata(font_wdata)
    );
    reg [12:0] want_a [0:2]; reg [31:0] want_d [0:2];
    initial begin want_a[0] = 13'd6110; want_d[0] = 32'hAAAA0001; want_a[1] = 13'd6111; want_d[1] = 32'hBBBB0002; want_a[2] = 13'd0; want_d[2] = 32'h12345678; end
    always @(posedge clk) if (font_we) begin
        if (wn < 3 && (font_waddr !== want_a[wn] || font_wdata !== want_d[wn])) begin $display("FAIL: write %0d went to %0d with 0x%08X (want %0d, 0x%08X)", wn, font_waddr, font_wdata, want_a[wn], want_d[wn]); errors = errors + 1; end
        wn = wn + 1;
    end
    reg [8*80-1:0] line; reg [31:0] last4 = 0; reg done = 0;
    always @(posedge clk) if (u_soc.con_wr) begin
        last4 = {last4[23:0], u_soc.con_char};
        if (last4 == "FAIL") begin fails = fails + 1; errors = errors + 1; end        // the firmware's own verdict
        if (u_soc.con_char == 8'd10) begin
            $display("fw: %0s", line);
            if (line[8*4-1:0] == "DONE") done = 1;
            line = 0;
        end else line = {line[8*79-1:0], u_soc.con_char};
    end
    initial begin
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "build/rtl/fw_font.bin";
        fd = $fopen(romfile, "rb");
        if (fd == 0) begin $display("FAIL: cannot open ROM"); $finish; end
        rom_n = $fread(rom, fd);
        $fclose(fd);
        repeat (4) @(posedge clk);
        for (i = 0; i < rom_n; i = i + 1) begin ld_addr <= i; ld_data <= rom[i]; ld_wr <= 1; @(posedge clk); end
        ld_wr <= 0; repeat (4) @(posedge clk);
        rst <= 0;
        wait (done); repeat (50) @(posedge clk);
        if (wn !== 3) begin $display("FAIL: %0d font writes, expected 3", wn); errors = errors + 1; end
        if (errors == 0) $display("PASSED: tb_font_soc"); else $display("FAILED: %0d errors", errors);
        $finish;
    end
    initial begin #300_000_000; $display("FAIL: timeout"); $finish; end
endmodule
