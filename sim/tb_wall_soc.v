// tb_wall_soc.v -- the wall-clock registers of tau_soc.v with the real CPU driving them (firmware sim/fw_wall/main.c).
//   MODE 0: the host's 0x0090 payload is applied on clk_74a WHILE THE CPU IS STILL IN RESET (as at boot, firmware is still loading); the firmware must find it latched.
//   MODE 2: the payload arrives after the firmware's READY (the firmware first sees valid 0 and zero words, then valid 1 and the words).
//   both: after READY2 the bench sends a second, different payload and a second rising edge is not produced (rtc_valid stays 1, as core_bridge_cmd does), but the words on the bus change: the latched ones must not move.
// BUG_RTC mutates tau_rtc_cdc (the bench must FAIL). +ROM=<file>.
`timescale 1ns/1ps
`default_nettype none
module tb_wall_soc;
    parameter MODE = 0;
    parameter BUG_RTC = 0;
    parameter OFF = 0;              // the mutant: the block under test left out of the SoC (RTC_ENABLE / SDA_ENABLE 0)
    reg clk = 0, clk74 = 0, clk100 = 0, rst = 1;
    always #8.333 clk = ~clk;
    always #6.734 clk74 = ~clk74;
    always #5 clk100 = ~clk100;
    reg         ld_wr = 0;
    reg  [31:0] ld_addr = 0;
    reg  [7:0]  ld_data = 0;
    reg  [7:0]  rom [0:65535];
    integer     rom_n, fd, i, errors = 0, fails = 0;
    reg  [1023:0] romfile;
    wire [4:0]  set_idx;  wire set_wr;  wire [31:0] set_wdata;
    wire [15:0] audio_l, audio_r;
    // wall clock as the bridge drives it (clk_74a)
    reg         rtc_valid = 0; reg [31:0] rtc_ep = 0, rtc_da = 0, rtc_ti = 0;
    tau_soc #(.RTC_ENABLE((MODE != 1) && (OFF == 0)), .BUG_RTC(BUG_RTC)) u_soc (
        .clk(clk), .rst(rst), .clk_74a(clk74),
        .ld_wr(ld_wr), .ld_addr(ld_addr), .ld_data(ld_data),
        .cont_key(32'd0), .in_menu(1'b0),
        .dataslot_update(1'b0), .dataslot_update_id(16'd0), .dataslot_update_size(32'd0),
        .dataslot_allcomplete(1'b0),
        .tgt_busy(1'b0), .tgt_done(1'b0), .tgt_seq(8'd0), .tgt_err(3'd0),
        .fb_cmd_full(1'b0), .fb_gfx_idle(1'b0), .fb_gfx_fence(32'd0), .fb_gfx_free(9'd0), .fb_gfx_ovf(16'd0), .fb_gfx_underflow(16'd0), .dt_q(32'd0),
        .set_idx(set_idx), .set_wr(set_wr), .set_wdata(set_wdata), .set_rdata(32'd0),
        .sdram_busy(1'b0), .sdram_done(1'b0), .sdram_rdata(32'd0),
        .sdram_wb_accept(1'b0), .sdram_wb_done(1'b0), .sdram_wb_rdata(32'd0),
        .xm_rdata(32'd0),
        .psram_done(1'b0), .psram_rdata(32'd0), .psram_guard(1'b0),
        .rtc_valid(rtc_valid), .rtc_epoch_seconds(rtc_ep), .rtc_date_bcd(rtc_da), .rtc_time_bcd(rtc_ti),
        .audio_l(audio_l), .audio_r(audio_r)
    );
    task host_rtc(input [31:0] ep, input [31:0] da, input [31:0] ti, input raise);   // the bridge's registers load, and rtc_valid rises in the same edge (core_bridge_cmd)
        begin @(posedge clk74); rtc_ep <= ep; rtc_da <= da; rtc_ti <= ti; if (raise) rtc_valid <= 1'b1; end
    endtask
    reg [8*80-1:0] line; reg [31:0] last4 = 0; reg done = 0, ready = 0, ready2 = 0;
    always @(posedge clk) if (u_soc.con_wr) begin
        last4 = {last4[23:0], u_soc.con_char};
        if (last4 == "FAIL") begin fails = fails + 1; errors = errors + 1; end
        if (u_soc.con_char == 8'd10) begin
            $display("fw: %0s", line);
            if (line[8*5-1:0] == "READY") ready = 1;
            if (line[8*6-1:0] == "READY2") ready2 = 1;
            if (line[8*4-1:0] == "DONE") done = 1;
            line = 0;
        end else line = {line[8*79-1:0], u_soc.con_char};
    end
    initial begin
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "build/rtl/fw_wall.bin";
        fd = $fopen(romfile, "rb");
        if (fd == 0) begin $display("FAIL: cannot open ROM"); $finish; end
        rom_n = $fread(rom, fd);
        $fclose(fd);
        repeat (4) @(posedge clk);
        if (MODE == 0) host_rtc(32'h6724B3C5, 32'h20261031, 32'h06123456, 1'b1);   // the host's command comes while the CPU is held in reset
        for (i = 0; i < rom_n; i = i + 1) begin ld_addr <= i; ld_data <= rom[i]; ld_wr <= 1; @(posedge clk); end
        ld_wr <= 0; repeat (4) @(posedge clk);
        rst <= 0;
        if (MODE == 2) begin wait (ready); repeat (200) @(posedge clk); host_rtc(32'h6724B3C5, 32'h20261031, 32'h06123456, 1'b1); end
        if (MODE == 0 || MODE == 2) begin
            wait (ready2);
            host_rtc(32'h6725_0000, 32'h20261101, 32'h01000102, 1'b0);              // a different payload, no new rising edge
            repeat (40) @(posedge clk74);
            host_rtc(32'h6726_0000, 32'h20261102, 32'h02000103, 1'b0);
        end
        wait (done); repeat (50) @(posedge clk);
        if (errors == 0) $display("PASSED: tb_wall_soc (MODE %0d)", MODE); else $display("FAILED: %0d errors", errors);
        $finish;
    end
    always @(posedge clk) if (fails != 0) begin repeat (20) @(posedge clk); $display("FAILED: %0d firmware checks failed", fails); $finish; end   // stop at the first failure (the mutants would otherwise run to the timeout)
    initial begin #100_000_000; $display("FAIL: timeout"); $finish; end
endmodule
