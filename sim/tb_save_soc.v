// tb_save_soc.v -- the save RAM's CPU side in tau_soc.v with the real CPU (firmware sim/fw_save/main.c), and the bridge side played by the bench like APF does (a LOAD while the CPU is in reset, a FLUSH after Reset Enter).
//   MODE 0: a pattern file is loaded; the firmware reads it back to back, writes through; the bench holds the CPU in reset, reads every word at the shell's timing and compares with the model of the file.
//   MODE 1 (the probe is shortened to 128 words, the RAM is the real 2,048): the self-test (fw2/cores/save_probe_core.h) boots on a ZERO file, the bench flushes, scrambles the RAM (a fresh bitstream), reloads the flushed words as the next load, restarts the CPU: the second boot must report
//           boots 2, CRC ok, pattern ok and the value the first boot wrote; a third boot after a flush whose last words were never written back (a torn Quit: the bench zeroes the tail of the file) must report CRC BAD.
// OFF = 1 is the mutant "the block is left out of the SoC" (SAVE_ENABLE 0): the bench must FAIL. BUG is passed to tau_save_ram (the same mutants as tb_tau_save_ram). +ROM=<file>.
`timescale 1ns/1ps
`default_nettype none
module tb_save_soc;
    parameter MODE = 0;
    parameter OFF = 0;
    parameter BUG = 0;
    parameter WORDS = 2048;
    reg clk = 0, clk74 = 0, clk100 = 0, rst = 1;
    always #7.5 clk = ~clk;                       // 66.667 MHz, the shipped clk_sys
    always #6.734 clk74 = ~clk74;
    always #5 clk100 = ~clk100;
    reg         ld_wr = 0; reg [31:0] ld_addr = 0; reg [7:0] ld_data = 0;
    reg  [7:0]  rom [0:131071];
    integer     rom_n, fd, i, errors = 0, fails = 0;
    reg  [1023:0] romfile;
    wire [4:0]  set_idx; wire set_wr; wire [31:0] set_wdata; wire [15:0] audio_l, audio_r;
    // ---- the bridge as the shell drives it ----
    reg  [31:0] bridge_addr = 0, bridge_wr_data = 0; reg bridge_wr = 0;
    wire        bridge_hit; wire [31:0] save_rd;
    wire        s_idx_we, s_data_we, s_data_req; wire [31:0] s_idx_wd, s_data_wd, s_idx_rd, s_data_rd, s_status;
    tau_save_ram #(.AW(11), .BUG(BUG)) u_save (.clk_74a(clk74), .bridge_addr(bridge_addr), .bridge_wr(bridge_wr), .bridge_wr_data(bridge_wr_data), .bridge_hit(bridge_hit), .bridge_rdata(save_rd),
        .clk_sys(clk), .rst_sys(rst), .idx_we(s_idx_we), .idx_wd(s_idx_wd), .data_we(s_data_we), .data_wd(s_data_wd), .data_req(s_data_req), .idx_rd(s_idx_rd), .data_rd(s_data_rd), .status(s_status));
    tau_soc #(.SAVE_ENABLE(OFF == 0)) u_soc (
        .clk(clk), .rst(rst), .clk_74a(clk74),
        .ld_wr(ld_wr), .ld_addr(ld_addr), .ld_data(ld_data),
        .cont_key(32'd0), .in_menu(1'b0),
        .dataslot_update(1'b0), .dataslot_update_id(16'd0), .dataslot_update_size(32'd0), .dataslot_allcomplete(1'b0),
        .tgt_busy(1'b0), .tgt_done(1'b0), .tgt_seq(8'd0), .tgt_err(3'd0),
        .fb_cmd_full(1'b0), .fb_gfx_idle(1'b0), .fb_gfx_fence(32'd0), .fb_gfx_free(9'd0), .fb_gfx_ovf(16'd0), .fb_gfx_underflow(16'd0), .dt_q(32'd0),
        .set_idx(set_idx), .set_wr(set_wr), .set_wdata(set_wdata), .set_rdata(32'd0),
        .sdram_busy(1'b0), .sdram_done(1'b0), .sdram_rdata(32'd0), .sdram_wb_accept(1'b0), .sdram_wb_done(1'b0), .sdram_wb_rdata(32'd0),
        .xm_rdata(32'd0), .psram_done(1'b0), .psram_rdata(32'd0), .psram_guard(1'b0),
        .rtc_valid(1'b0), .rtc_epoch_seconds(32'd0), .rtc_date_bcd(32'd0), .rtc_time_bcd(32'd0),
        .sda_status(32'd0), .sda_val(32'd0), .i2s_diag_toggle(1'b0),
        .save_idx_we(s_idx_we), .save_idx_wd(s_idx_wd), .save_data_we(s_data_we), .save_data_wd(s_data_wd), .save_data_req(s_data_req), .save_idx_rd(s_idx_rd), .save_data_rd(s_data_rd), .save_status(s_status),
        .audio_l(audio_l), .audio_r(audio_r)
    );
    // ---- the file model, as bus (big-endian) words ----
    reg [31:0] bus [0:WORDS-1];                   // what the RAM holds, from the bridge's side
    reg [31:0] got [0:WORDS-1];                   // what a flush read
    function [31:0] bswap(input [31:0] v); bswap = {v[7:0], v[15:8], v[23:16], v[31:24]}; endfunction
    task b_write(input [31:0] a, input [31:0] d); begin @(posedge clk74); bridge_addr <= a; bridge_wr_data <= d; repeat (2) @(posedge clk74); bridge_wr <= 1; @(posedge clk74); bridge_wr <= 0; repeat (3) @(posedge clk74); end endtask
    reg [31:0] b_got;
    task b_read(input [31:0] a); begin @(posedge clk74); bridge_addr <= a; repeat (4) @(posedge clk74); b_got = save_rd; end endtask
    task load_all; integer k; begin for (k = 0; k < WORDS; k = k + 1) b_write(32'h3000_0000 + 4*k, bus[k]); end endtask
    task flush_all; integer k; begin for (k = 0; k < WORDS; k = k + 1) begin b_read(32'h3000_0000 + 4*k); got[k] = b_got; end end endtask
    // ---- the firmware's console ----
    reg [8*80-1:0] line; reg [31:0] last4 = 0; reg done = 0; integer ndone = 0;
    always @(posedge clk) if (u_soc.con_wr) begin
        last4 = {last4[23:0], u_soc.con_char};
        if (last4 == "FAIL") begin fails = fails + 1; errors = errors + 1; end
        if (u_soc.con_char == 8'd10) begin
            $display("fw: %0s", line);
            if (line[8*4-1:0] == "DONE") begin done = 1; ndone = ndone + 1; end
            line = 0;
        end else line = {line[8*79-1:0], u_soc.con_char};
    end
    task fw_rom_load; begin
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "build/rtl/fw_save.bin";
        fd = $fopen(romfile, "rb"); if (fd == 0) begin $display("FAIL: cannot open ROM"); $finish; end
        rom_n = $fread(rom, fd); $fclose(fd);
        for (i = 0; i < rom_n; i = i + 1) begin ld_addr <= i; ld_data <= rom[i]; ld_wr <= 1; @(posedge clk); end
        ld_wr <= 0; repeat (4) @(posedge clk);
    end endtask
    integer k;
    initial begin
        repeat (4) @(posedge clk);
        fw_rom_load;
        if (MODE == 0) begin
            for (k = 0; k < WORDS; k = k + 1) bus[k] = bswap(32'hA000_0000 ^ (k * 32'h0101_0101));     // the file's LE word -> the bus word
            load_all;
            rst <= 0; wait (done); repeat (50) @(posedge clk);
            rst <= 1; repeat (8) @(posedge clk);                                                       // Reset Enter, then the slot is read out
            flush_all;
            for (k = 0; k < WORDS; k = k + 1) begin
                reg [31:0] want;
                want = bswap(32'hA000_0000 ^ (k * 32'h0101_0101));
                if (k < 16) want = bswap(32'hC0DE_0000 + k);
                if (k == 100) want = bswap(32'h1234_5678);
                if (k == 2047) want = bswap(32'hFEED_F00D);
                if (got[k] !== want) begin errors = errors + 1; if (errors < 8) $display("FAIL flush word %0d: got %08x want %08x", k, got[k], want); end
            end
        end else begin
            for (k = 0; k < WORDS; k = k + 1) bus[k] = 32'h0;                                          // the installer's zero file
            load_all;
            rst <= 0; wait (done); repeat (50) @(posedge clk);
            rst <= 1; repeat (8) @(posedge clk);
            flush_all;                                                                                 // boot 1 -> Quit
            for (k = 0; k < WORDS; k = k + 1) bus[k] = got[k];
            for (k = 0; k < WORDS; k = k + 1) b_write(32'h3000_0000 + 4*k, 32'hDEAD_BEEF);             // a fresh bitstream: the RAM holds junk until the load
            load_all;                                                                                  // boot 2 loads what the Quit wrote
            done = 0; repeat (4) @(posedge clk); rst <= 0; wait (done); repeat (50) @(posedge clk);
            rst <= 1; repeat (8) @(posedge clk);
            flush_all;                                                                                 // boot 2 -> Quit
            for (k = 0; k < WORDS; k = k + 1) bus[k] = got[k];
            for (k = 100; k < WORDS; k = k + 1) bus[k] = 32'h0;                                        // boot 3 loads a file whose tail never made it back (the firmware's probe is 128 words here)
            load_all;
            done = 0; repeat (4) @(posedge clk); rst <= 0; wait (done); repeat (50) @(posedge clk);
        end
        if (errors == 0) $display("PASSED: tb_save_soc (MODE %0d)", MODE); else $display("FAILED: %0d errors", errors);
        $finish;
    end
    always @(posedge clk) if (fails != 0) begin repeat (20) @(posedge clk); $display("FAILED: %0d firmware checks failed", fails); $finish; end
    initial begin #400_000_000; $display("FAIL: timeout"); $finish; end
endmodule
