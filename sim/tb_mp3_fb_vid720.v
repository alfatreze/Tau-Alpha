// ============================================================================
// tb_mp3_fb_vid720.v -- 720 test step T1 (test/720 branch,
// docs/features/VIDEO_720_TEST_PLAN.md): checks mp3_fb's scanout, not its
// draw engine, in both VID720=0 (shipped 400x360 timing, the regression
// baseline) and VID720=1 (800x720 output, framebuffer doubled).
//
// The SDRAM stub answers every read with (address + 1), so each output pixel
// can be checked against the exact framebuffer word it must come from:
//   VID720=0: word (y * 512 + x)            for output pixel (x, y)
//   VID720=1: word ((y >> 1) * 512 + (x >> 1))
// Also checked, per frame: active lines/pixels, frame period (H_TOT * V_TOT),
// fills per frame (must stay 360 -- the SDRAM scanout load is unchanged),
// scan_vc (the Helios beam position must keep the 360-mode numbering), and
// the APF bus rules the analogue-pocket-dev skill lists (references/
// hardware-video-audio-input.md, KB-014): RGB zero while DE is low, HS not
// within 3 clocks after VS, VS never starting inside an HS pulse, and at
// least one clock between HS and DE.
//
//   make test-rtl-fb-vid720
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module tb_mp3_fb_vid720;
    parameter VID720 = 1;
    parameter BUG_NO_DOUBLE = 0;   // mutation hook: checker expects un-doubled columns -> must FAIL

    localparam integer OW = VID720 ? 800 : 400, OH = VID720 ? 720 : 360;
    localparam integer HT = VID720 ? 850 : 500, VT = VID720 ? 735 : 400;

    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5 clk_sdram = ~clk_sdram;                        // 100 MHz
    always #8 clk_sys   = ~clk_sys;                          // ~62 MHz
    generate if (VID720) begin : g_c720
        always #13.333 clk_vid = ~clk_vid;                   // 37.5 MHz
    end else begin : g_c360
        always #41.667 clk_vid = ~clk_vid;                   // 12 MHz
    end endgenerate

    reg         cmd_push = 0;
    wire        cmd_full;
    wire [24:0] p0_addr;
    wire [15:0] p0_data;
    wire [1:0]  p0_byte_en;
    wire [10:0] p0_wr_len;
    wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req;
    reg  [15:0] p0_q = 0;
    reg         p0_available = 0, p0_ready = 0, p0_data_available = 0;
    reg  [10:0] wsrc_addr = 0;
    wire [15:0] wsrc_q;
    wire [23:0] video_rgb;
    wire        video_de, video_hs, video_vs;
    wire [8:0]  scan_vc;

    mp3_fb #(.VID720(VID720)) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(4'd1), .cmd_addr(19'd400),   // one 1x1 RECT off-screen: sets "painted"
        .cmd_w(9'd1), .cmd_h(9'd1), .cmd_fg(16'h0000), .cmd_bg(16'h0000),
        .cmd_glyph(7'd0), .cmd_sx(2'd0), .cmd_sy(2'd0), .cmd_full(cmd_full),
        .blt_src_base(25'd0), .blt_src_stride(10'd512),
        .blt_dst_base(25'd0), .blt_dst_stride(10'd512),
        .blt_key_en(1'b0), .blt_key(16'd0),
        .blt_blend_en(1'b0), .blt_blend_mode(3'd0), .blt_blend_alpha(8'd0),
        .blt_reindex(8'd0), .text_light(1'b0),
        .clut_wr(1'b0), .clut_waddr(8'd0), .clut_wdata(16'd0),
        .rc_cut_lut(80'd0),
        .sdram_init_complete(1'b1),
        .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(p0_byte_en),
        .p0_wr_len(p0_wr_len), .p0_wr_stream(p0_wr_stream), .p0_q(p0_q),
        .p0_wr_req(p0_wr_req), .p0_rd_req(p0_rd_req),
        .p0_end_burst_req(p0_end_burst_req),
        .p0_available(p0_available), .p0_ready(p0_ready),
        .p0_data_available(p0_data_available),
        .wsrc_addr(wsrc_addr), .wsrc_q(wsrc_q),
        .video_rgb(video_rgb), .video_de(video_de), .video_hs(video_hs), .video_vs(video_vs),
        .scan_vc(scan_vc)
    );

    // ---- SDRAM stub: reads return address+1; writes are accepted and dropped -------------
    localparam S_IDLE = 0, S_READ = 1, S_WR = 2, S_DONE = 3;
    integer    st = S_IDLE, wbeats = 0;
    reg [24:0] rd_addr;
    integer    reads = 0;
    always @(posedge clk_sdram) begin
        p0_ready <= 1'b0;
        p0_data_available <= 1'b0;
        case (st)
            S_IDLE: begin
                p0_available <= 1'b1;
                if (p0_rd_req) begin
                    p0_available <= 1'b0; rd_addr <= p0_addr; reads = reads + 1; st <= S_READ;
                end else if (p0_wr_req) begin
                    p0_available <= 1'b0; wbeats = p0_wr_len; st <= S_WR;
                end
            end
            S_READ: begin
                p0_data_available <= 1'b1;
                p0_q    <= rd_addr[15:0] + 16'd1;
                rd_addr <= rd_addr + 25'd1;
                if (p0_end_burst_req) st <= S_IDLE;
            end
            S_WR: begin
                wsrc_addr <= wsrc_addr + 11'd1;
                wbeats = wbeats - 1;
                if (wbeats <= 0) st <= S_DONE;
            end
            S_DONE: begin p0_ready <= 1'b1; wsrc_addr <= 0; st <= S_IDLE; end
        endcase
    end

    // ---- video checker (clk_vid) ---------------------------------------------------------
    integer failures = 0, frames = 0;
    integer x = 0, y = -1, de_lines = 0, vclk = 0, vs_at = -100, hs_at = -100, de_fell_at = -100;
    integer frame_clocks = 0, last_vs_rise = -1, reads_at_vs = 0, first_de_scan = -1, max_scan = 0;
    reg de_d = 0, hs_d = 0, vs_d = 0;
    reg [15:0] v;
    reg [23:0] exp_rgb;
    integer fx, fy;

    task fail(input [255:0] msg);
        begin
            failures = failures + 1;
            if (failures <= 10) $display("FAIL frame %0d y %0d x %0d: %0s", frames, y, x, msg);
        end
    endtask

    always @(posedge clk_vid) if (!reset) begin
        vclk = vclk + 1;
        // Rule: RGB is zero whenever DE is low (this core never sends sideband words).
        if (!video_de && video_rgb != 24'd0 && frames >= 1) fail("RGB non-zero outside DE");
        // Rule: VS must not start inside an HS pulse.
        if (video_vs && !vs_d && video_hs) fail("VS rose during HS");
        if (video_vs && !vs_d) begin
            if (last_vs_rise >= 0) begin
                frame_clocks = vclk - last_vs_rise;
                if (frame_clocks != HT * VT) fail("frame period != H_TOT*V_TOT");
                if (frames >= 1) begin
                    if (de_lines != OH) fail("active line count");
                    if (reads - reads_at_vs != 360) begin
                        $display("  fills this frame: %0d", reads - reads_at_vs);
                        fail("fills per frame != 360");
                    end
                end
            end
            last_vs_rise = vclk; vs_at = vclk; reads_at_vs = reads;
            frames = frames + 1; de_lines = 0; y = -1;
        end
        // Rule: HS not within 3 clocks after VS.
        if (video_hs && !hs_d) begin
            if (vclk - vs_at < 3) fail("HS within 3 clocks of VS");
            if (video_de) fail("HS during DE");
            hs_at = vclk;
        end
        if (video_de && !de_d) begin
            if (vclk - hs_at < 1 || video_hs) fail("DE starts without a gap after HS");
            y = y + 1; x = 0; de_lines = de_lines + 1;
            if (frames >= 1 && y == 0) first_de_scan = scan_vc;
        end
        if (!video_de && de_d) begin
            if (frames >= 2 && x != OW) fail("active pixels per line");
            de_fell_at = vclk;
        end
        if (video_de) begin
            if (frames >= 2) begin
                fx = (VID720 && !BUG_NO_DOUBLE) ? (x >> 1) : x;
                fy = VID720 ? (y >> 1) : y;
                v = fy * 512 + fx + 1;
                exp_rgb = {v[15:11], 3'b0, v[10:5], 2'b0, v[4:0], 3'b0};
                if (video_rgb !== exp_rgb) fail("pixel != framebuffer word");
            end
            x = x + 1;
        end
        if (scan_vc > max_scan) max_scan = scan_vc;
        de_d = video_de; hs_d = video_hs; vs_d = video_vs;
    end

    initial begin
        repeat (20) @(posedge clk_sys);
        reset = 0;
        repeat (20) @(posedge clk_sys);
        @(posedge clk_sys) cmd_push <= 1'b1;
        @(posedge clk_sys) cmd_push <= 1'b0;
        wait (frames == 4);
        $display("VID720=%0d: frame %0d clocks (%0d x %0d), %0d active lines, first-active scan_vc %0d, max scan_vc %0d",
                 VID720, frame_clocks, HT, VT, OH, first_de_scan, max_scan);
        if (first_de_scan != 4) fail("scan_vc at first active line != 4 (fw/helios.inc HELIOS_VOFF)");
        if (max_scan > 399) fail("scan_vc beyond the 360-mode range");
        if (failures == 0) $display("PASSED (0 failures)");
        else               $display("FAILED (%0d failures)", failures);
        $finish;
    end
endmodule
