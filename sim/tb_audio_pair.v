// B-653: the stereo pair invariant, through the REAL mp3_soc audio path and the REAL sound_i2s writer.
// sound_i2s pushes a word into its output FIFO whenever EITHER channel's input changes, so a channel that updates a clock or more before the other hands the serialiser a TORN pair (this
// sample's left with the last sample's right): the old Halcyon/EQ engines did exactly that (B-650). Nothing checked the other sources, they were only believed to be atomic. This bench
// streams a signal whose left and right differ and change on every sample (sim/fw_audiopair/main.c) through six configurations of the real SoC (plain FIFO, + hardware gain stage, + Halcyon
// engine, + Cymo resampler, engine off again, gain off) and in each one, after the path has settled, requires:
//   1. no clock in which exactly one of audio_l / audio_r changes (torn = 0), and a healthy number of changes (the signal is really flowing);
//   2. every word the serialiser is about to send equals a {right, left} pair the SoC presented in the last 128 changes (the 15-bit mapping sound_i2s applies with its 16-bit slot off is
//      applied to the presented pairs first); no word made of two different samples.
// -DTORN=1 delays the right channel by one clock on the way into the checks and the writer (a deliberate tear): both checks must then FAIL (the bench's own mutant).
`timescale 1ns/1ps
`default_nettype none
`include "dcfifo_model.v"
module tb_audio_pair;
    parameter TORN = 0;
    reg clk = 0, clk74 = 0, clk_mclk = 0, rst = 1;
    always #7.5 clk = ~clk;                       // 66.667 MHz
    always #6.734 clk74 = ~clk74;
    always #40.690 clk_mclk = ~clk_mclk;          // 12.288 MHz
    reg         ld_wr = 0;
    reg  [31:0] ld_addr = 0;
    reg  [7:0]  ld_data = 0;
    reg  [7:0]  rom [0:65535];
    integer     rom_n, fd, i, errors = 0;
    reg  [1023:0] romfile;
    reg  [31:0] cont_key = 0;
    wire [4:0]  set_idx;  wire set_wr;  wire [31:0] set_wdata;
    wire [15:0] soc_l, soc_r;
    mp3_soc #(.HALCYON_ENABLE(1), .CYMO_RESAMP_ENABLE(1), .GAIN_ENABLE(1), .WIDE_EQ_ENABLE(0)) u_soc (
        .clk(clk), .rst(rst), .clk_74a(clk74),
        .ld_wr(ld_wr), .ld_addr(ld_addr), .ld_data(ld_data),
        .cont_key(cont_key), .in_menu(1'b0),
        .dataslot_update(1'b0), .dataslot_update_id(16'd0), .dataslot_update_size(32'd0),
        .dataslot_allcomplete(1'b0),
        .tgt_busy(1'b0), .tgt_done(1'b0), .tgt_seq(8'd0), .tgt_err(3'd0),
        .fb_cmd_full(1'b0), .dt_q(32'd0),
        .set_idx(set_idx), .set_wr(set_wr), .set_wdata(set_wdata), .set_rdata(32'd0),
        .sdram_busy(1'b0), .sdram_done(1'b0), .sdram_rdata(32'd0),
        .sdram_wb_accept(1'b0), .sdram_wb_done(1'b0), .sdram_wb_rdata(32'd0),
        .xm_rdata(32'd0),
        .psram_done(1'b0), .psram_rdata(32'd0), .psram_guard(1'b0),
        .audio_l(soc_l), .audio_r(soc_r)
    );
    // what the checks and the writer see; TORN delays the right channel by one clock
    reg [15:0] r_d1 = 0;
    always @(posedge clk) r_d1 <= soc_r;
    wire [15:0] audio_l = soc_l;
    wire [15:0] audio_r = TORN ? r_d1 : soc_r;
    wire mclk, lrck, dac;
    sound_i2s #(.CHANNEL_WIDTH(16), .SIGNED_INPUT(1)) u_i2s (.clk_mclk(clk_mclk), .clk_audio(clk),
        .audio_l(audio_l), .audio_r(audio_r), .full16(1'b0), .audio_mclk(mclk), .audio_lrck(lrck), .audio_dac(dac));
    initial begin u_i2s.audio_lrck = 0; u_i2s.audio_dac = 0; end

    // ---- watchers -------------------------------------------------------------------------------------------------------------------------------------------------------------
    reg [15:0] pl = 0, pr = 0;
    reg observe = 0;
    integer torn = 0, changes = 0, words = 0, badwords = 0;
    reg [31:0] ring [0:127];
    integer rp = 0, k, hit;
    initial for (k = 0; k < 128; k = k + 1) ring[k] = 32'd0;
    always @(posedge clk) begin
        pl <= audio_l; pr <= audio_r;
        if ((audio_l !== pl) || (audio_r !== pr)) begin
            ring[rp] <= {audio_r[15], audio_r[15:1], audio_l[15], audio_l[15:1]};   // what sound_i2s sends with full16 off: {sign, the top 15 bits} per channel
            rp <= (rp + 1) % 128;
            if (observe) begin
                changes = changes + 1;
                if ((audio_l !== pl) != (audio_r !== pr)) torn = torn + 1;
            end
        end
    end
    always @(posedge clk_mclk) begin
        if (u_i2s.sclk_div == 2'd3 && u_i2s.audio_lrck_cnt == 31 && ~u_i2s.audio_lrck && observe) begin
            hit = 0;
            for (k = 0; k < 128; k = k + 1) if (ring[k] === {u_i2s.audgen_sampdata_s[31:16], u_i2s.audgen_sampdata_s[15:0]}) hit = 1;
            words = words + 1;
            if (!hit) badwords = badwords + 1;
        end
    end

    reg [8*80-1:0] line;
    reg [7:0] seen = 0;
    always @(posedge clk) if (u_soc.con_wr) begin
        if (u_soc.con_char == 8'd10) begin $display("fw: %0s", line); line = 0; end
        else line = {line[8*79-1:0], u_soc.con_char};
    end
    task press(input integer b); begin cont_key[b] <= 1; repeat (2000) @(posedge clk); cont_key[b] <= 0; repeat (4000) @(posedge clk); end endtask
    // let the path settle after a configuration change, then observe a window
    task window(input [255:0] what);
        begin
            observe = 0; torn = 0; changes = 0; words = 0; badwords = 0;
            repeat (60000) @(posedge clk);                      // settle (the resampler's pipeline, the engine's first samples)
            observe = 1; repeat (240000) @(posedge clk); observe = 0;       // about 170 output samples
            if (torn != 0 || badwords != 0 || changes < 100 || words < 100) begin
                $display("FAIL: %0s: torn clocks %0d, serialised words not presented by the SoC %0d of %0d, output changes %0d", what, torn, badwords, words, changes); errors = errors + 1;
            end else $display("ok: %0s: %0d output changes, 0 torn, %0d serialised words all matched", what, changes, words);
        end
    endtask
    initial begin
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "build/rtl/fw_audiopair.bin";
        fd = $fopen(romfile, "rb");
        if (fd == 0) begin $display("FAIL: cannot open ROM"); $finish; end
        rom_n = $fread(rom, fd);
        $fclose(fd);
        repeat (4) @(posedge clk);
        for (i = 0; i < rom_n; i = i + 1) begin ld_addr <= i; ld_data <= rom[i]; ld_wr <= 1; @(posedge clk); end
        ld_wr <= 0; repeat (4) @(posedge clk);
        rst <= 0;
        wait (u_soc.pcm_sample_tick); repeat (80000) @(posedge clk);
        window("plain FIFO output");
        press(4); window("hardware gain stage on");
        press(5); window("Halcyon engine on (unity)");
        press(6); window("Cymo resampler live, engine and gain stage on");
        press(7); window("engine off, resampler live, gain stage on");
        press(4); window("gain stage off, resampler live, engine off");
        if (errors == 0) $display("PASSED: tb_audio_pair (TORN=%0d)", TORN); else $display("FAILED: %0d configurations (TORN=%0d)", errors, TORN);
        $finish;
    end
    initial begin #900_000_000; $display("FAIL: timeout"); $finish; end
endmodule
