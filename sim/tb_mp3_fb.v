// ============================================================================
// tb_mp3_fb.v -- validate the rev 7 draw engine BEFORE spending a hardware
// cycle on it. Renders glyphs to the console as ASCII art, so a wrong EPX
// rule, a wrong scale, or an off-by-one in the row loop is visible directly
// rather than inferred from "the Pocket looks wrong".
//
// sdram_fb.sv itself cannot be elaborated here (unpacked SV structs are past
// iverilog), so the controller is stubbed to the contract mp3_fb actually
// depends on, taken from sdram_fb's own comments:
//   * p0_available high only when idle
//   * a streaming write pulls beat N from wsrc_q, with wsrc_addr driven ahead
//     of each beat (BRAM read latency 1)
//   * p0_ready pulses once when the burst has fully retired
//
//   iverilog -g2012 -o tb.vvp sim/tb_mp3_fb.v src/fpga/core/mp3_fb.sv \
//            src/fpga/core/font_rom.v && vvp tb.vvp
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module tb_mp3_fb;
    parameter BUG_IGNORE_BLIT_STRIDE = 0;   // mutation hook: 1 must fail this bench (make test-rtl-fb-mutation)
    parameter BUG_IGNORE_KEY = 0;           // mutation hook: 1 must fail this bench (make test-rtl-fb-mutation)
    parameter BUG_SBLIT_NO_SCALE = 0;       // mutation hook: 1 must fail this bench (make test-rtl-fb-mutation)
    parameter BUG_BLEND_ALWAYS_SRC = 0;     // mutation hook: 1 must fail this bench (make test-rtl-fb-mutation)

    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5    clk_sdram = ~clk_sdram;   // 100 MHz
    always #10   clk_sys   = ~clk_sys;     //  50 MHz
    always #41.7 clk_vid   = ~clk_vid;     //  12 MHz

    reg         cmd_push = 0;
    reg  [2:0]  cmd_op = 0;
    reg  [18:0] cmd_addr = 0;
    reg  [8:0]  cmd_w = 0, cmd_h = 0;
    reg  [15:0] cmd_fg = 16'hFFFF, cmd_bg = 16'h0000;
    reg  [6:0]  cmd_glyph = 0;
    reg  [1:0]  cmd_sx = 0, cmd_sy = 0;
    wire        cmd_full;

    // Phase F B1: sticky blit addressing state. Left at the power-up defaults
    // (base 0, stride 512) makes OP_BLIT degenerate to exactly OP_COPY's own
    // addressing -- the equivalence test below relies on that.
    reg  [24:0] blt_src_base = 25'd0, blt_dst_base = 25'd0;
    reg  [9:0]  blt_src_stride = 10'd512, blt_dst_stride = 10'd512;
    // Phase F B2: colour-key transparency, left disabled by default.
    reg         blt_key_en = 1'b0;
    reg  [15:0] blt_key = 16'd0;
    // Phase F B5: alpha blend, left disabled by default.
    reg         blt_blend_en = 1'b0;
    reg  [2:0]  blt_blend_mode = 3'd0;
    reg  [7:0]  blt_blend_alpha = 8'd0;

    wire [24:0] p0_addr;
    wire [15:0] p0_data;
    wire [1:0]  p0_byte_en;
    wire [10:0] p0_wr_len;
    wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req;
    reg  [15:0] p0_q = 0;
    reg         p0_available = 0, p0_ready = 0, p0_data_available = 0;
    reg  [10:0] wsrc_addr = 0;
    wire [15:0] wsrc_q;

    mp3_fb #(.BUG_IGNORE_BLIT_STRIDE(BUG_IGNORE_BLIT_STRIDE), .BUG_IGNORE_KEY(BUG_IGNORE_KEY),
             .BUG_SBLIT_NO_SCALE(BUG_SBLIT_NO_SCALE), .BLIT_BLEND_ENABLE(1),
             .BUG_BLEND_ALWAYS_SRC(BUG_BLEND_ALWAYS_SRC)) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr),
        .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy), .cmd_full(cmd_full),
        .blt_src_base(blt_src_base), .blt_src_stride(blt_src_stride),
        .blt_dst_base(blt_dst_base), .blt_dst_stride(blt_dst_stride),
        .blt_key_en(blt_key_en), .blt_key(blt_key),
        .blt_blend_en(blt_blend_en), .blt_blend_mode(blt_blend_mode), .blt_blend_alpha(blt_blend_alpha),
        .sdram_init_complete(1'b1),
        .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(p0_byte_en),
        .p0_wr_len(p0_wr_len), .p0_wr_stream(p0_wr_stream), .p0_q(p0_q),
        .p0_wr_req(p0_wr_req), .p0_rd_req(p0_rd_req),
        .p0_end_burst_req(p0_end_burst_req),
        .p0_available(p0_available), .p0_ready(p0_ready),
        .p0_data_available(p0_data_available),
        .wsrc_addr(wsrc_addr), .wsrc_q(wsrc_q),
        .video_rgb(), .video_de(), .video_hs(), .video_vs()
    );

    // ---- SDRAM controller stub ---------------------------------------------
    localparam S_IDLE = 0, S_STREAM = 1, S_CONST = 2, S_DONE = 3;
    integer     st = S_IDLE;
    integer     beats, i;
    reg [15:0]  captured [0:63];      // one streamed row
    reg [24:0]  last_addr;
    reg [10:0]  last_len;
    reg         last_stream;
    integer     rows_written = 0;
    reg [24:0]  row_addr [0:255];
    reg [15:0]  row_pix  [0:255][0:63];
    integer     row_len  [0:255];

    // Simple readable "memory": word N reads back as N+1, so a copy's output
    // is checkable against its source address without modelling real storage.
    localparam S_READ = 4;
    reg [24:0] rd_addr;

    always @(posedge clk_sdram) begin
        p0_ready <= 1'b0;
        p0_data_available <= 1'b0;
        case (st)
            S_IDLE: begin
                p0_available <= 1'b1;
                if (p0_rd_req) begin
                    p0_available <= 1'b0;
                    rd_addr <= p0_addr;
                    st      <= S_READ;
                end else if (p0_wr_req) begin
                    p0_available <= 1'b0;
                    last_addr   <= p0_addr;
                    last_len    <= p0_wr_len;
                    last_stream <= p0_wr_stream;
                    wsrc_addr   <= 11'd0;
                    beats        = 0;
                    st          <= p0_wr_stream ? S_STREAM : S_CONST;
                end
            end
            // Mirrors sdram_fb: wsrc_addr leads by one cycle, so the word
            // valid on wsrc_q now is the one requested last cycle.
            S_STREAM: begin
                wsrc_addr <= wsrc_addr + 11'd1;
                if (wsrc_addr > 0) begin
                    captured[beats] = wsrc_q;
                    beats = beats + 1;
                end
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
                row_addr[rows_written] = last_addr;
                row_len[rows_written]  = last_len;
                for (i = 0; i < 64; i = i + 1)
                    row_pix[rows_written][i] = last_stream ? captured[i] : p0_data;
                rows_written = rows_written + 1;
                p0_ready <= 1'b1;
                st       <= S_IDLE;
            end
        endcase
    end

    // ---- helpers ------------------------------------------------------------
    task push(input [2:0] op, input [18:0] a, input [8:0] w, input [8:0] h,
              input [6:0] g, input [1:0] sx, input [1:0] sy);
        begin
            @(posedge clk_sys);
            cmd_op <= op; cmd_addr <= a; cmd_w <= w; cmd_h <= h;
            cmd_glyph <= g; cmd_sx <= sx; cmd_sy <= sy; cmd_push <= 1'b1;
            @(posedge clk_sys);
            cmd_push <= 1'b0;
        end
    endtask

    reg [15:0] mid_expect;
    task show(input [127:0] label);
        integer r, c;
        reg [15:0] p;
        integer lvl;
        begin
            $display("\n=== %0s : %0d rows ===", label, rows_written);
            for (r = 0; r < rows_written; r = r + 1) begin
                $write("  ");
                for (c = 0; c < row_len[r]; c = c + 1) begin
                    p = row_pix[r][c];
                    // fg=white bg=black, so the green channel tracks coverage.
                    lvl = ((p >> 5) & 6'h3F);
                    if      (lvl < 6)  $write(".");
                    else if (lvl < 16) $write(":");
                    else if (lvl < 26) $write("-");
                    else if (lvl < 36) $write("=");
                    else if (lvl < 46) $write("*");
                    else if (lvl < 56) $write("#");
                    else               $write("@");
                end
                $display("   @%0d", row_addr[r]);
            end
        end
    endtask

    integer errors = 0;
    task check(input cond, input [255:0] what);
        begin
            if (!cond) begin $display("FAIL: %0s", what); errors = errors + 1; end
            else         $display("ok:   %0s", what);
        end
    endtask

    initial begin
        mid_expect = {((cmd_fg[15:11] + cmd_bg[15:11]) >> 1),
                      ((cmd_fg[10:5]  + cmd_bg[10:5])  >> 1),
                      ((cmd_fg[4:0]   + cmd_bg[4:0])   >> 1)};
        repeat (10) @(posedge clk_sdram);
        reset = 0;
        repeat (10) @(posedge clk_sdram);

        // ---- CHAR 'A', scale 1 -> 16x16 -----------------------------------
        rows_written = 0;
        push(2'd2, 19'd0, 9'd0, 9'd0, 7'h41, 2'd0, 2'd0);
        wait (rows_written == 16); repeat (20) @(posedge clk_sdram);
        show("CHAR 'A' scale 1");
        check(rows_written == 16, "16 output rows");
        check(row_len[0] == 16,   "16 pixels wide");
        check(row_addr[1] - row_addr[0] == 512, "rows advance by one stride");

        // ---- CHAR 'A', scale 2 -> 32x32 -----------------------------------
        rows_written = 0;
        push(2'd2, 19'd100, 9'd0, 9'd0, 7'h41, 2'd1, 2'd1);
        wait (rows_written == 24); repeat (20) @(posedge clk_sdram);
        show("CHAR 'A' 1.5x");
        check(rows_written == 24, "24 output rows at 1.5x");
        check(row_len[0] == 24,   "24 pixels wide at 1.5x");
        check(row_addr[0] == 100, "starts at the requested address");

        // ---- RECT ---------------------------------------------------------
        rows_written = 0;
        cmd_fg <= 16'h1234;
        push(2'd1, 19'd2048, 9'd40, 9'd5, 7'd0, 2'd0, 2'd0);
        wait (rows_written == 5); repeat (20) @(posedge clk_sdram);
        check(rows_written == 5,  "RECT emits h rows from one command");
        check(row_len[0] == 40,   "RECT row is w words");
        check(row_pix[0][0] == 16'h1234, "RECT writes the fill colour");
        check(row_addr[4] == 2048 + 4*512, "RECT walks the stride");

        // ---- COPY ---------------------------------------------------------
        // Source 0x1000, dest 0x3000, 8 wide x 3 tall. Row r of the source
        // starts at 0x1000 + r*512, and word N reads back as N+1.
        rows_written = 0;
        cmd_fg <= 16'd0; cmd_bg <= 16'h1000;      /* src = {fg[2:0], bg} */
        push(2'd3, 19'h3000, 9'd8, 9'd3, 7'd0, 2'd0, 2'd0);
        wait (rows_written == 3); repeat (30) @(posedge clk_sdram);
        check(rows_written == 3, "COPY emits h rows from one command");
        check(row_len[0] == 8,   "COPY row is w words");
        check(row_addr[0] == 19'h3000, "COPY writes to the destination");
        check(row_pix[0][0] == 16'h1001, "COPY row 0 takes source word 0");
        check(row_pix[0][7] == 16'h1008, "COPY walks along the source row");
        check(row_pix[1][0] == 16'h1201, "COPY advances source by one stride");
        check(row_addr[1] == 19'h3200,   "COPY advances dest by one stride");

        // ---- BLIT, default sticky state: must equal OP_COPY exactly -------
        // Same source/dest/w/h as the COPY case above, left at power-up
        // defaults (base 0, stride 512) -- OP_BLIT must degenerate to OP_COPY's
        // own addressing byte-for-byte, since that is the whole point of it
        // being a generalisation and not a parallel, divergent code path.
        rows_written = 0;
        cmd_fg <= 16'd0; cmd_bg <= 16'h1000;
        push(3'd4, 19'h3000, 9'd8, 9'd3, 7'd0, 2'd0, 2'd0);
        wait (rows_written == 3); repeat (30) @(posedge clk_sdram);
        check(rows_written == 3, "BLIT default: h rows = COPY");
        check(row_addr[0] == 19'h3000, "BLIT default: dest = COPY dest");
        check(row_pix[0][0] == 16'h1001, "BLIT default: src = COPY src");
        check(row_addr[1] == 19'h3200,   "BLIT default: dest stride 512");
        check(row_pix[1][0] == 16'h1201, "BLIT default: src stride 512");

        // ---- BLIT, non-default sticky state: independent base and stride --
        // src_base=0x8000 stride=64, dst_base=0x9000 stride=96 -- neither
        // matches FB_BASE=0/512, so any leftover hardcoded-512 addressing
        // (the exact bug this feature exists to avoid re-introducing) would
        // show up immediately as a wrong row_addr or wrong row_pix here.
        rows_written = 0;
        blt_src_base = 25'h8000; blt_src_stride = 10'd64;
        blt_dst_base = 25'h9000; blt_dst_stride = 10'd96;
        cmd_fg <= 16'd0; cmd_bg <= 16'h0010;   // src offset 0x10, relative to blt_src_base
        push(3'd4, 19'h0020, 9'd4, 9'd3, 7'd0, 2'd0, 2'd0);   // dest offset 0x20, relative to blt_dst_base
        wait (rows_written == 3); repeat (30) @(posedge clk_sdram);
        check(rows_written == 3, "BLIT (custom) emits h rows");
        check(row_addr[0] == (25'h9000 + 25'h0020), "BLIT custom: dest = DST_BASE+off");
        check(row_pix[0][0] == (25'h8000 + 25'h0010) + 16'd1,
              "BLIT custom: src = SRC_BASE+off");
        check(row_addr[1] == (25'h9000 + 25'h0020) + 25'd96, "BLIT custom: dest stride 96");
        check(row_pix[1][0] == ((25'h8000 + 25'h0010) + 25'd64) + 16'd1,
              "BLIT custom: src stride 64");
        blt_src_base = 25'd0; blt_src_stride = 10'd512;   // restore defaults for anything added after this
        blt_dst_base = 25'd0; blt_dst_stride = 10'd512;

        // ---- BLIT with colour key (B2) -------------------------------------
        // dest 0x5000, src 0x6000 (offset via fg/bg), 4 wide x 2 tall, defaults
        // otherwise. Source row 0 reads back as 0x6001/0x6002/0x6003/0x6004;
        // KEY = 0x6002 keys out word 1 only. Destination row 0 pre-reads as
        // 0x5001/0x5002/0x5003/0x5004 (same readable-memory model, dest
        // address), so a keyed word must show 0x5002 (the destination's own
        // value), not 0x6002 (what a plain BLIT would have written there).
        rows_written = 0;
        blt_key_en = 1'b1; blt_key = 16'h6002;
        cmd_fg <= 16'd0; cmd_bg <= 16'h6000;      // src offset 0x6000
        push(3'd4, 19'h5000, 9'd4, 9'd2, 7'd0, 2'd0, 2'd0);   // dest offset 0x5000
        wait (rows_written == 2); repeat (30) @(posedge clk_sdram);
        check(rows_written == 2, "BLIT keyed: 2 rows written");
        check(row_pix[0][0] == 16'h6001, "BLIT keyed: word 0 = src");
        check(row_pix[0][1] == 16'h5002, "BLIT keyed: word 1 = dest (KEY)");
        check(row_pix[0][2] == 16'h6003, "BLIT keyed: word 2 = src");
        check(row_pix[0][3] == 16'h6004, "BLIT keyed: word 3 = src");
        check(row_pix[1][0] == 16'h6201 && row_pix[1][1] == 16'h6202
              && row_pix[1][2] == 16'h6203 && row_pix[1][3] == 16'h6204,
              "BLIT keyed: row1 no match=src");
        blt_key_en = 1'b0; blt_key = 16'd0;

        // ---- Same BLIT, key disabled: must behave like plain BLIT ---------
        rows_written = 0;
        cmd_fg <= 16'd0; cmd_bg <= 16'h6000;
        push(3'd4, 19'h5000, 9'd4, 9'd2, 7'd0, 2'd0, 2'd0);
        wait (rows_written == 2); repeat (30) @(posedge clk_sdram);
        check(row_pix[0][1] == 16'h6002, "BLIT unkeyed: word 1 = src");

        // ---- SBLIT (B4), 1x: must read/write pixel-for-pixel like a plain --
        // copy, confirming the per-pixel read path agrees with the row-burst
        // path for the trivial 1:1 case. Source 0x8000 (3 wide x 2 tall),
        // dest 0x9000. Row 0 source words read back as 0x8001/0x8002/0x8003;
        // row 1's source steps by the default 512 stride (1x steps every row).
        rows_written = 0;
        cmd_fg <= 16'd0; cmd_bg <= 16'h8000;      // src offset 0x8000
        push(3'd6, 19'h9000, 9'd3, 9'd2, 7'd0, 2'd0, 2'd0);   // src w=3 h=2, scale 1x
        wait (rows_written == 2); repeat (40) @(posedge clk_sdram);
        check(rows_written == 2, "SBLIT 1x: 2 rows (no scaling)");
        check(row_addr[0] == 19'h9000, "SBLIT 1x: dest at offset");
        check(row_pix[0][0] == 16'h8001 && row_pix[0][1] == 16'h8002
              && row_pix[0][2] == 16'h8003, "SBLIT 1x: row 0 pixel-for-pixel");
        check(row_addr[1] == 19'h9000 + 512, "SBLIT 1x: dest steps by stride");
        check(row_pix[1][0] == 16'h8201, "SBLIT 1x: src steps stride");

        // ---- SBLIT, 2x: a 2x1 source doubles to 4x2 (nearest, Bresenham) --
        // Source 0x7000 (2 wide x 1 tall) -- v0=0x7001, v1=0x7002 (readback
        // model). At 2x, every source pixel repeats twice per axis: row 0 and
        // row 1 both read [v0,v0,v1,v1], since the single source row has
        // nothing further to advance to.
        rows_written = 0;
        cmd_fg <= 16'd0; cmd_bg <= 16'h7000;
        push(3'd6, 19'h4000, 9'd2, 9'd1, 7'd0, 2'd2, 2'd2);   // src w=2 h=1, scale 2x/2x
        wait (rows_written == 2); repeat (40) @(posedge clk_sdram);
        check(rows_written == 2, "SBLIT 2x: 2 rows from 1 src");
        check(row_len[0] == 4, "SBLIT 2x: 4 cols from 2 src");
        check(row_pix[0][0] == 16'h7001 && row_pix[0][1] == 16'h7001,
              "SBLIT 2x: col0 doubles");
        check(row_pix[0][2] == 16'h7002 && row_pix[0][3] == 16'h7002,
              "SBLIT 2x: col1 doubles");
        check(row_pix[1][0] == 16'h7001 && row_pix[1][3] == 16'h7002,
              "SBLIT 2x: row1 = row0 (vert)");

        // ---- BLIT with alpha blend (B5), DSP mode -------------------------
        // dest 0x3FFF+1=0x4000 (R=8,G=0,B=0, readback model); src offset
        // 0x000F+1=0x0010 (R=0,G=0,B=16). alpha=128 (~50%) makes the DSP
        // formula (f*a + b*(256-a))>>8 reduce to an exact per-channel average
        // (128/256 = 0.5 with no rounding surprise): R=(8+0)/2=4, G=0,
        // B=(0+16)/2=8 -> packed 0x2008.
        rows_written = 0;
        blt_blend_en = 1'b1; blt_blend_mode = 3'd0; blt_blend_alpha = 8'd128;
        cmd_fg <= 16'd0; cmd_bg <= 16'h000F;      // src offset 0x000F
        push(3'd4, 19'h3FFF, 9'd1, 9'd1, 7'd0, 2'd0, 2'd0);   // dest offset 0x3FFF
        wait (rows_written == 1); repeat (30) @(posedge clk_sdram);
        check(rows_written == 1, "BLEND dsp: 1 row written");
        check(row_pix[0][0] == 16'h2008, "BLEND dsp: alpha=128 averages R/G/B exactly");

        // ---- BLIT with alpha blend (B5), PSX mode 2 (B+F, saturating) -----
        // dest and src both read back as R=20 (0xA000 pattern, G=B=0). 20+20=40
        // overflows the 5-bit R channel (max 31) -- must clamp, not wrap.
        rows_written = 0;
        blt_blend_mode = 3'd2;   // alpha field irrelevant to PSX modes
        cmd_fg <= 16'd0; cmd_bg <= 16'h9FFF;      // src offset 0x9FFF -> reads 0xA000
        push(3'd4, 19'h9FFF, 9'd1, 9'd1, 7'd0, 2'd0, 2'd0);   // dest offset 0x9FFF -> reads 0xA000
        wait (rows_written == 1); repeat (30) @(posedge clk_sdram);
        check(row_pix[0][0] == 16'hF800, "BLEND psx B+F: R channel clamps, not wraps");
        blt_blend_en = 1'b0; blt_blend_mode = 3'd0; blt_blend_alpha = 8'd0;

        // ---- BAR (B6): split bar, 3 unlit rows on top of 2 lit rows -------
        rows_written = 0;
        cmd_fg <= 16'hABCD; cmd_bg <= 16'h1234;   // lit / unlit colours
        push(3'd5, 19'd1000, 9'd4, 9'd5, 7'd2, 2'd0, 2'd0);   // h=5, lit=2 -> unlit=3
        wait (rows_written == 5); repeat (30) @(posedge clk_sdram);
        check(rows_written == 5, "BAR split: 5 rows total");
        check(row_addr[0] == 19'd1000, "BAR split: starts at top-left");
        check(row_addr[2] == 19'd1000 + 2*512, "BAR split: unlit row stride");
        check(row_pix[0][0] == 16'h1234 && row_pix[2][0] == 16'h1234, "BAR split: unlit = bg colour");
        check(row_addr[3] == 19'd1000 + 3*512, "BAR split: lit after unlit");
        check(row_addr[4] == 19'd1000 + 4*512, "BAR split: lit row stride");
        check(row_pix[3][0] == 16'hABCD && row_pix[4][0] == 16'hABCD, "BAR split: lit = fg colour");

        // ---- BAR, fully lit (lit clamps to height, no unlit segment) ------
        rows_written = 0;
        cmd_fg <= 16'h5555; cmd_bg <= 16'h6666;
        push(3'd5, 19'd2000, 9'd3, 9'd3, 7'd9, 2'd0, 2'd0);   // lit=9 clamps to h=3
        wait (rows_written == 3); repeat (30) @(posedge clk_sdram);
        check(rows_written == 3, "BAR full lit: 3 rows");
        check(row_addr[0] == 19'd2000, "BAR full lit: no unlit phase");
        check(row_pix[0][0] == 16'h5555 && row_pix[2][0] == 16'h5555, "BAR full lit: all fg colour");

        // ---- BAR, fully unlit (lit=0, no phase 2 queued) -------------------
        rows_written = 0;
        cmd_fg <= 16'h7777; cmd_bg <= 16'h8888;
        push(3'd5, 19'd3000, 9'd2, 9'd4, 7'd0, 2'd0, 2'd0);   // lit=0
        wait (rows_written == 4); repeat (30) @(posedge clk_sdram);
        check(rows_written == 4, "BAR full unlit: 4 rows");
        check(row_pix[0][0] == 16'h8888 && row_pix[3][0] == 16'h8888, "BAR full unlit: all bg colour");

        // ---- RUN ----------------------------------------------------------
        rows_written = 0;
        push(2'd0, 19'd4096, 9'd7, 9'd0, 7'd0, 2'd0, 2'd0);
        repeat (60) @(posedge clk_sdram);
        check(rows_written == 1, "RUN is a single row");
        check(row_len[0] == 7,   "RUN length honoured");

        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end

    initial begin
        #500000;
        $display("TIMEOUT -- engine stalled");
        $finish;
    end

endmodule

`default_nettype wire
