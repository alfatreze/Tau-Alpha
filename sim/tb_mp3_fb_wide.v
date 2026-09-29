// ============================================================================
// tb_mp3_fb_wide.v -- 720 phased spec A3 (test/720 branch,
// docs/features/VIDEO_720_PHASED_SPEC.md section 2.1): the 256-word row buffer.
//
// Every opcode that stages a row in glyphbuf is run at the widest row the
// build allows -- W = 200 with GB_WIDE=1 (and a 255-word COPY), W = 127 with
// GB_WIDE=0 (the shipped limit, regression baseline):
//   COPY, BLIT (custom bases/strides), BLIT with colour key (the A_KEYDST
//   pre-read), BLIT with alpha blend (the bl_i0/bl_i1 write-back), SBLIT 2x
//   (the per-pixel path, 100 -> 200) and CBLIT (the CLUT path).
// The SDRAM stub reads word N back as N+1, so every output word is checked
// against the exact source (and destination) address it must come from.
//
// BUG_GB_NARROW=1 (GB_WIDE=1 only) compares the row end on 7 bits, as if the
// buffer were still 128 words: this bench MUST fail with it.
//
//   make test-rtl-fb-wide
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module tb_mp3_fb_wide;
    parameter GB_WIDE = 1;
    parameter BUG_GB_NARROW = 0;

    localparam integer W  = GB_WIDE ? 200 : 127;   // main row width
    localparam integer WM = GB_WIDE ? 255 : 127;   // widest COPY the build allows

    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5    clk_sdram = ~clk_sdram;   // 100 MHz
    always #10   clk_sys   = ~clk_sys;     //  50 MHz
    always #41.7 clk_vid   = ~clk_vid;     //  12 MHz

    reg         cmd_push = 0;
    reg  [3:0]  cmd_op = 0;
    reg  [18:0] cmd_addr = 0;
    reg  [8:0]  cmd_w = 0, cmd_h = 0;
    reg  [15:0] cmd_fg = 16'h0000, cmd_bg = 16'h0000;
    reg  [1:0]  cmd_sx = 0, cmd_sy = 0;
    wire        cmd_full;

    reg  [24:0] blt_src_base = 25'd0, blt_dst_base = 25'd0;
    reg  [9:0]  blt_src_stride = 10'd512, blt_dst_stride = 10'd512;
    reg         blt_key_en = 1'b0;
    reg  [15:0] blt_key = 16'd0;
    reg         blt_blend_en = 1'b0;
    reg  [2:0]  blt_blend_mode = 3'd0;
    reg  [7:0]  blt_blend_alpha = 8'd0;
    reg         clut_wr = 1'b0;
    reg  [7:0]  clut_waddr = 8'd0;
    reg  [15:0] clut_wdata = 16'd0;

    wire [24:0] p0_addr;
    wire [15:0] p0_data;
    wire [1:0]  p0_byte_en;
    wire [10:0] p0_wr_len;
    wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req;
    reg  [15:0] p0_q = 0;
    reg         p0_available = 0, p0_ready = 0, p0_data_available = 0;
    reg  [10:0] wsrc_addr = 0;
    wire [15:0] wsrc_q;

    mp3_fb #(.BLIT_BLEND_ENABLE(1), .GB_WIDE(GB_WIDE), .BUG_GB_NARROW(BUG_GB_NARROW)) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr),
        .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(7'd0), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy), .cmd_full(cmd_full),
        .blt_src_base(blt_src_base), .blt_src_stride(blt_src_stride),
        .blt_dst_base(blt_dst_base), .blt_dst_stride(blt_dst_stride),
        .blt_key_en(blt_key_en), .blt_key(blt_key),
        .blt_blend_en(blt_blend_en), .blt_blend_mode(blt_blend_mode), .blt_blend_alpha(blt_blend_alpha),
        .blt_reindex(8'd0), .text_light(1'b0),
        .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
        .rc_cut_lut(80'd0),
        .sdram_init_complete(1'b1),
        .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(p0_byte_en),
        .p0_wr_len(p0_wr_len), .p0_wr_stream(p0_wr_stream), .p0_q(p0_q),
        .p0_wr_req(p0_wr_req), .p0_rd_req(p0_rd_req),
        .p0_end_burst_req(p0_end_burst_req),
        .p0_available(p0_available), .p0_ready(p0_ready),
        .p0_data_available(p0_data_available),
        .wsrc_addr(wsrc_addr), .wsrc_q(wsrc_q),
        .video_rgb(), .video_de(), .video_hs(), .video_vs(), .scan_vc()
    );

    // ---- SDRAM stub (as tb_mp3_fb.v, rows up to 256 words) ------------------
    localparam S_IDLE = 0, S_STREAM = 1, S_CONST = 2, S_DONE = 3, S_READ = 4;
    integer     st = S_IDLE;
    integer     beats, i;
    reg [15:0]  captured [0:255];
    reg [24:0]  last_addr, rd_addr;
    reg [10:0]  last_len;
    reg         last_stream;
    integer     rows_written = 0;
    reg [24:0]  row_addr [0:7];
    reg [15:0]  row_pix  [0:7][0:255];
    integer     row_len  [0:7];

    always @(posedge clk_sdram) begin
        p0_ready <= 1'b0;
        p0_data_available <= 1'b0;
        case (st)
            S_IDLE: begin
                p0_available <= 1'b1;
                if (p0_rd_req) begin
                    p0_available <= 1'b0; rd_addr <= p0_addr; st <= S_READ;
                end else if (p0_wr_req) begin
                    p0_available <= 1'b0;
                    last_addr <= p0_addr; last_len <= p0_wr_len; last_stream <= p0_wr_stream;
                    wsrc_addr <= 11'd0; beats = 0;
                    st <= p0_wr_stream ? S_STREAM : S_CONST;
                end
            end
            S_STREAM: begin
                wsrc_addr <= wsrc_addr + 11'd1;
                if (wsrc_addr > 0) begin captured[beats] = wsrc_q; beats = beats + 1; end
                if (beats == last_len) st <= S_DONE;
            end
            S_READ: begin
                p0_data_available <= 1'b1;
                p0_q    <= rd_addr[15:0] + 16'd1;
                rd_addr <= rd_addr + 25'd1;
                if (p0_end_burst_req) st <= S_IDLE;
            end
            S_CONST: st <= S_DONE;
            S_DONE: begin
                if (rows_written < 8) begin
                    row_addr[rows_written] = last_addr;
                    row_len[rows_written]  = last_len;
                    for (i = 0; i < 256; i = i + 1)
                        row_pix[rows_written][i] = last_stream ? captured[i] : p0_data;
                end
                rows_written = rows_written + 1;
                p0_ready <= 1'b1;
                st       <= S_IDLE;
            end
        endcase
    end

    // ---- helpers ------------------------------------------------------------
    task push(input [3:0] op, input [18:0] a, input [8:0] w, input [8:0] h,
              input [1:0] sx, input [1:0] sy);
        begin
            @(posedge clk_sys);
            cmd_op <= op; cmd_addr <= a; cmd_w <= w; cmd_h <= h;
            cmd_sx <= sx; cmd_sy <= sy; cmd_push <= 1'b1;
            @(posedge clk_sys);
            cmd_push <= 1'b0;
        end
    endtask

    integer errors = 0;
    task check(input cond, input [255:0] what);
        begin
            if (!cond) begin $display("FAIL: %0s", what); errors = errors + 1; end
            else         $display("ok:   %0s", what);
        end
    endtask

    // Wait for n rows, with a timeout so a wedged engine fails instead of hanging.
    task wait_rows(input integer n);
        integer t;
        begin
            t = 0;
            while (rows_written < n && t < 200000) begin @(posedge clk_sdram); t = t + 1; end
            repeat (40) @(posedge clk_sdram);
        end
    endtask

    // Row r must hold `n` words equal to base+1+c (the stub's readback model).
    integer c, bad;
    task check_row(input integer r, input integer n, input [24:0] dst, input [15:0] base,
                   input [255:0] what);
        begin
            bad = 0;
            if (row_len[r] != n || row_addr[r] != dst) bad = 1;
            for (c = 0; c < n; c = c + 1)
                if (row_pix[r][c] !== base + 16'd1 + c[15:0]) bad = bad + 1;
            if (bad) $display("  row %0d: len %0d addr %h, word0 %h, word%0d %h", r, row_len[r],
                              row_addr[r], row_pix[r][0], n - 1, row_pix[r][n - 1]);
            check(bad == 0, what);
        end
    endtask

    // DSP blend at alpha 128: exact per-channel average (see tb_mp3_fb.v's B5 case).
    function [15:0] avg565(input [15:0] a, input [15:0] b);
        reg [5:0] r, bl; reg [6:0] g;
        begin
            r  = {1'b0, a[15:11]} + {1'b0, b[15:11]};
            g  = {1'b0, a[10:5]}  + {1'b0, b[10:5]};
            bl = {1'b0, a[4:0]}   + {1'b0, b[4:0]};
            avg565 = {r[5:1], g[6:1], bl[5:1]};
        end
    endfunction

    reg [15:0] sv, dv;
    initial begin
        repeat (10) @(posedge clk_sdram);
        reset = 0;
        repeat (10) @(posedge clk_sdram);
        $display("GB_WIDE=%0d BUG_GB_NARROW=%0d: row width W=%0d, widest COPY %0d", GB_WIDE, BUG_GB_NARROW, W, WM);

        // ---- COPY, W x 2 ----------------------------------------------------
        rows_written = 0;
        cmd_fg <= 16'd0; cmd_bg <= 16'h1000;              // src = {fg[2:0], bg}
        push(4'd3, 19'h3000, W[8:0], 9'd2, 2'd0, 2'd0);
        wait_rows(2);
        check(rows_written == 2, "COPY wide: 2 rows");
        check_row(0, W, 25'h3000, 16'h1000, "COPY wide: row 0 whole width");
        check_row(1, W, 25'h3200, 16'h1200, "COPY wide: row 1 whole width");

        // ---- COPY, widest --------------------------------------------------
        rows_written = 0;
        cmd_fg <= 16'd0; cmd_bg <= 16'h2000;
        push(4'd3, 19'h5000, WM[8:0], 9'd1, 2'd0, 2'd0);
        wait_rows(1);
        check_row(0, WM, 25'h5000, 16'h2000, "COPY widest: whole width");

        // ---- BLIT, custom bases/strides, W x 2 ------------------------------
        rows_written = 0;
        blt_src_base = 25'h8000; blt_src_stride = 10'd256;
        blt_dst_base = 25'h9000; blt_dst_stride = 10'd512;
        cmd_fg <= 16'd0; cmd_bg <= 16'h0010;
        push(4'd4, 19'h0020, W[8:0], 9'd2, 2'd0, 2'd0);
        wait_rows(2);
        check(rows_written == 2, "BLIT wide: 2 rows");
        check_row(0, W, 25'h9020, 16'h8010, "BLIT wide: row 0 whole width");
        check_row(1, W, 25'h9220, 16'h8110, "BLIT wide: row 1 src stride 256");
        blt_src_base = 25'd0; blt_src_stride = 10'd512;
        blt_dst_base = 25'd0; blt_dst_stride = 10'd512;

        // ---- BLIT keyed, W x 1: the key matches source word W-10 only ------
        rows_written = 0;
        blt_key_en = 1'b1; blt_key = 16'h6000 + 16'd1 + (W - 10);
        cmd_fg <= 16'd0; cmd_bg <= 16'h6000;
        push(4'd4, 19'h4000, W[8:0], 9'd1, 2'd0, 2'd0);
        wait_rows(1);
        bad = 0;
        for (c = 0; c < W; c = c + 1) begin
            sv = 16'h6001 + c[15:0]; dv = 16'h4001 + c[15:0];
            if (row_pix[0][c] !== ((c == W - 10) ? dv : sv)) bad = bad + 1;
        end
        check(row_len[0] == W && bad == 0, "BLIT keyed wide: dest kept only at the keyed word");
        blt_key_en = 1'b0; blt_key = 16'd0;

        // ---- BLIT blended (DSP, alpha 128), W x 1 ---------------------------
        rows_written = 0;
        blt_blend_en = 1'b1; blt_blend_mode = 3'd0; blt_blend_alpha = 8'd128;
        cmd_fg <= 16'd0; cmd_bg <= 16'h1100;
        push(4'd4, 19'h2300, W[8:0], 9'd1, 2'd0, 2'd0);
        wait_rows(1);
        bad = 0;
        for (c = 0; c < W; c = c + 1)
            if (row_pix[0][c] !== avg565(16'h1101 + c[15:0], 16'h2301 + c[15:0])) bad = bad + 1;
        check(row_len[0] == W && bad == 0, "BLIT blend wide: every word averaged");
        blt_blend_en = 1'b0; blt_blend_alpha = 8'd0;

        // ---- SBLIT 2x: W/2 source words -> W output words, 2 rows ----------
        rows_written = 0;
        cmd_fg <= 16'd0; cmd_bg <= 16'h7000;
        push(4'd6, 19'h6000, (W / 2), 9'd1, 2'd2, 2'd2);
        wait_rows(2);
        bad = 0;
        for (c = 0; c < (W / 2) * 2; c = c + 1)
            if (row_pix[0][c] !== 16'h7001 + (c >> 1)) bad = bad + 1;
        check(row_len[0] == (W / 2) * 2 && bad == 0, "SBLIT 2x wide: every column doubled");

        // ---- CBLIT, W x 1: CLUT[i] = i ^ 16'hA5A5 ---------------------------
        for (c = 0; c < 256; c = c + 1) begin
            @(posedge clk_sys);
            clut_wr <= 1'b1; clut_waddr <= c[7:0]; clut_wdata <= c[15:0] ^ 16'hA5A5;
        end
        @(posedge clk_sys) clut_wr <= 1'b0;
        rows_written = 0;
        cmd_fg <= 16'd0; cmd_bg <= 16'h0B00;
        push(4'd7, 19'h7000, W[8:0], 9'd1, 2'd0, 2'd0);
        wait_rows(1);
        bad = 0;
        for (c = 0; c < W; c = c + 1) begin
            sv = 16'h0B01 + c[15:0];
            if (row_pix[0][c] !== ({8'd0, sv[7:0]} ^ 16'hA5A5)) bad = bad + 1;
        end
        check(row_len[0] == W && bad == 0, "CBLIT wide: every word looked up");

        if (errors == 0) $display("PASSED (0 failures)");
        else             $display("FAILED (%0d failures)", errors);
        $finish;
    end
endmodule
