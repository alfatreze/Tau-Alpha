// B-639: the Halcyon MMIO glue in mp3_soc.v, driven by the real VexRiscv running sim/fw_halcyon/main.c. The TB presses the keys the firmware waits for and
// checks the audio output after each step (constant 16000 in): 16000 / 8000 (bank committed through the shadow path, engine selected) / 16000 (bypass) / 8000 (bypass off, clear) / 16000 (engine off).
// +ROM=<file>. The engine's own defects are covered by tb_tau_halcyon.v; this proves the register glue.
`timescale 1ns/1ps
`default_nettype none
module tb_halcyon_soc;
    reg clk = 0, clk74 = 0, rst = 1;
    always #8.333 clk = ~clk;
    always #6.734 clk74 = ~clk74;
    reg         ld_wr = 0;
    reg  [31:0] ld_addr = 0;
    reg  [7:0]  ld_data = 0;
    reg  [7:0]  rom [0:65535];
    integer     rom_n, fd, i, errors = 0;
    reg  [1023:0] romfile;
    reg  [31:0] cont_key = 0;
    wire [4:0]  set_idx;  wire set_wr;  wire [31:0] set_wdata;
    wire [15:0] audio_l, audio_r;
    mp3_soc #(.HALCYON_ENABLE(1)) u_soc (
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
        .audio_l(audio_l), .audio_r(audio_r)
    );
    reg [8*80-1:0] line;
    reg [3:0] seen = 0;
    always @(posedge clk) if (u_soc.con_wr) begin
        if (u_soc.con_char == 8'd10) begin
            $display("fw: %0s", line);
            if (line[8*2-1:0] == "ON")      seen[0] = 1;
            if (line[8*2-1:0] == "SS")      seen[1] = 1;
            if (line[8*2-1:0] == "AR")      seen[2] = 1;
            if (line[8*2-1:0] == "FF")      seen[3] = 1;
            line = 0;
        end else line = {line[8*79-1:0], u_soc.con_char};
    end
    task check(input signed [15:0] want, input [255:0] what);
        begin
            repeat (30000) @(posedge clk);
            if (audio_l !== want || audio_r !== want) begin $display("FAIL: %0s: audio %0d,%0d want %0d", what, audio_l, audio_r, want); errors = errors + 1; end
            else $display("ok: %0s: audio %0d", what, audio_l);
        end
    endtask
    task press(input integer b); begin cont_key[b] <= 1; repeat (2000) @(posedge clk); cont_key[b] <= 0; repeat (4000) @(posedge clk); end endtask
    initial begin
        if (!$value$plusargs("ROM=%s", romfile)) romfile = "build/rtl/fw_halcyon.bin";
        fd = $fopen(romfile, "rb");
        if (fd == 0) begin $display("FAIL: cannot open ROM"); $finish; end
        rom_n = $fread(rom, fd);
        $fclose(fd);
        repeat (4) @(posedge clk);
        for (i = 0; i < rom_n; i = i + 1) begin ld_addr <= i; ld_data <= rom[i]; ld_wr <= 1; @(posedge clk); end
        ld_wr <= 0; repeat (4) @(posedge clk);
        rst <= 0;
        // the FIFO primes after half its depth (1024 samples, 21 ms): wait for real audio
        wait (u_soc.pcm_sample_tick); wait (u_soc.fifo_l === 16'sd16000);
        check(16000, "engine not selected");
        press(4); check(8000, "bank committed via shadow, engine on");
        press(5); check(16000, "bypass");
        press(6); check(8000, "bypass off and state cleared");
        press(7); check(16000, "engine off");
        if (u_soc.hal_widx !== 8'd85) begin $display("FAIL: last written index %0d, expected 85", u_soc.hal_widx); errors = errors + 1; end
        if (!(seen == 4'hF)) begin $display("FAIL: firmware did not reach every step (seen %b)", seen); errors = errors + 1; end
        if (errors == 0) $display("PASSED: tb_halcyon_soc"); else $display("FAILED: %0d errors", errors);
        $finish;
    end
    initial begin #400_000_000; $display("FAIL: timeout"); $finish; end
endmodule
