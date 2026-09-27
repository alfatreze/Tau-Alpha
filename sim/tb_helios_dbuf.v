// ============================================================================
// tb_helios_dbuf.v -- Helios H2 double buffering (B-340): the buffer-select mux
// on both the CPU write-address path and the scanout read-address path, and
// the vblank-gated flip. Same SDRAM-stub/push() pattern as tb_mp3_fb.v.
//
//   iverilog -g2012 -o tb.vvp sim/tb_helios_dbuf.v src/fpga/core/mp3_fb.sv \
//            src/fpga/core/font_rom.v && vvp tb.vvp
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module tb_helios_dbuf;
    parameter DBUF_ENABLE = 1;   // 0 must reproduce today's behaviour byte-for-byte (checked below)

    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5    clk_sdram = ~clk_sdram;   // 100 MHz
    always #10   clk_sys   = ~clk_sys;     //  50 MHz
    always #41.7 clk_vid   = ~clk_vid;     //  12 MHz

    reg         cmd_push = 0;
    reg  [3:0]  cmd_op = 0;
    reg  [18:0] cmd_addr = 0;
    reg  [8:0]  cmd_w = 0, cmd_h = 0;
    reg  [15:0] cmd_fg = 16'hFFFF, cmd_bg = 16'h0000;
    reg  [6:0]  cmd_glyph = 0;
    reg  [1:0]  cmd_sx = 0, cmd_sy = 0;
    wire        cmd_full;

    reg  [24:0] blt_src_base = 25'd0, blt_dst_base = 25'd0;
    reg  [9:0]  blt_src_stride = 10'd512, blt_dst_stride = 10'd512;
    reg         blt_key_en = 1'b0;
    reg  [15:0] blt_key = 16'd0;
    reg         blt_blend_en = 1'b0;
    reg  [2:0]  blt_blend_mode = 3'd0;
    reg  [7:0]  blt_blend_alpha = 8'd0;
    reg  [7:0]  blt_reindex = 8'd0;
    reg         text_light = 1'b0;
    reg         clut_wr = 1'b0;
    reg  [7:0]  clut_waddr = 8'd0;
    reg  [15:0] clut_wdata = 16'd0;
    reg  [79:0] rc_cut_lut = 80'd0;

    // The signals under test. dbuf_cpu_buf/dbuf_flip_req_tgl are driven directly here (this is a
    // unit test of mp3_fb.sv itself; the informal clk_sys->clk_sdram sync for cpu_buf and the formal
    // one for text_light both live in core_game.vh, one level up, matching every other sticky-field
    // test in this file that also drives its inputs directly rather than modelling the caller).
    reg  dbuf_cpu_buf = 1'b0;
    reg  dbuf_flip_req_tgl = 1'b0;
    wire dbuf_disp_buf, dbuf_flip_pending;

    wire [24:0] p0_addr;
    wire [15:0] p0_data;
    wire [1:0]  p0_byte_en;
    wire [10:0] p0_wr_len;
    wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req;
    reg  [15:0] p0_q = 0;
    reg         p0_available = 0, p0_ready = 0, p0_data_available = 0;
    reg  [10:0] wsrc_addr = 0;
    wire [15:0] wsrc_q;

    mp3_fb #(.BLIT_BLEND_ENABLE(1), .DBUF_ENABLE(DBUF_ENABLE)) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr),
        .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy), .cmd_full(cmd_full),
        .blt_src_base(blt_src_base), .blt_src_stride(blt_src_stride),
        .blt_dst_base(blt_dst_base), .blt_dst_stride(blt_dst_stride),
        .blt_key_en(blt_key_en), .blt_key(blt_key),
        .blt_blend_en(blt_blend_en), .blt_blend_mode(blt_blend_mode), .blt_blend_alpha(blt_blend_alpha),
        .blt_reindex(blt_reindex),
        .text_light(text_light),
        .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
        .rc_cut_lut(rc_cut_lut),
        .dbuf_cpu_buf(dbuf_cpu_buf), .dbuf_flip_req_tgl(dbuf_flip_req_tgl),
        .dbuf_disp_buf(dbuf_disp_buf), .dbuf_flip_pending(dbuf_flip_pending),
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

    // ---- SDRAM controller stub (identical shape to tb_mp3_fb.v, plus the S_READ state tb_mp3_fb.v
    // also has -- needed here because scanout's prefetch is a READ (p0_rd_req), not a write) -------
    localparam S_IDLE = 0, S_STREAM = 1, S_CONST = 2, S_DONE = 3, S_READ = 4;
    integer     st = S_IDLE;
    integer     beats, i;
    reg [15:0]  captured [0:63];
    integer     rows_written = 0;
    reg [24:0]  row_addr [0:255];
    integer     row_len  [0:255];
    reg [24:0]  last_addr;
    reg [10:0]  last_len;
    reg         last_stream;
    reg [24:0]  rd_addr;
    integer     reads_seen = 0;
    reg [24:0]  read_addr [0:255];

    always @(posedge clk_sdram) begin
        p0_ready <= 1'b0;
        p0_data_available <= 1'b0;
        case (st)
            S_IDLE: begin
                p0_available <= 1'b1;
                if (p0_rd_req) begin
                    p0_available <= 1'b0;
                    rd_addr <= p0_addr;
                    read_addr[reads_seen] = p0_addr;
                    reads_seen = reads_seen + 1;
                    st <= S_READ;
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
            S_READ: begin
                p0_data_available <= 1'b1;
                p0_q    <= rd_addr[15:0] + 16'd1;
                rd_addr <= rd_addr + 25'd1;
                if (p0_end_burst_req) st <= S_IDLE;
            end
            S_STREAM: begin
                wsrc_addr <= wsrc_addr + 11'd1;
                if (wsrc_addr > 0) begin captured[beats] = wsrc_q; beats = beats + 1; end
                if (beats == last_len) st <= S_DONE;
            end
            S_CONST: st <= S_DONE;
            S_DONE: begin
                row_addr[rows_written] = last_addr;
                row_len[rows_written]  = last_len;
                rows_written = rows_written + 1;
                p0_ready <= 1'b1;
                st       <= S_IDLE;
            end
        endcase
    end

    task push(input [3:0] op, input [18:0] a, input [8:0] w, input [8:0] h);
        begin
            @(posedge clk_sys);
            cmd_op <= op; cmd_addr <= a; cmd_w <= w; cmd_h <= h;
            cmd_glyph <= 0; cmd_sx <= 0; cmd_sy <= 0; cmd_push <= 1'b1;
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

    localparam DBUF_BASE1 = 25'd1048576;
    localparam ONSCREEN_ROW = 19'd100 * 19'd512;      // row 100, well inside V_ACT (360)
    localparam OFFSCREEN_ROW = 19'd984 * 19'd512;     // row 984, a real off-screen stash row this project uses (thumbnail stash)

    initial begin
        repeat (10) @(posedge clk_sdram);
        reset = 0;
        repeat (10) @(posedge clk_sdram);

        // -- write-side mux: on-screen address, cpu_buf 0 then 1 -------------
        dbuf_cpu_buf = 1'b0;
        push(4'd1, ONSCREEN_ROW, 9'd4, 9'd1);   // RECT
        repeat (40) @(posedge clk_sdram);
        check(row_addr[rows_written-1] == {6'd0, ONSCREEN_ROW},
              "on-screen RECT, cpu_buf=0: address unchanged");

        dbuf_cpu_buf = 1'b1;
        push(4'd1, ONSCREEN_ROW, 9'd4, 9'd1);
        repeat (40) @(posedge clk_sdram);
        if (DBUF_ENABLE != 0)
            check(row_addr[rows_written-1] == ({6'd0, ONSCREEN_ROW} + DBUF_BASE1),
                  "on-screen RECT, cpu_buf=1: shifted by DBUF_BASE1 (DBUF_ENABLE=1)");
        else
            check(row_addr[rows_written-1] == {6'd0, ONSCREEN_ROW},
                  "on-screen RECT, cpu_buf=1: UNCHANGED when DBUF_ENABLE=0 (the whole feature is a no-op)");

        // -- write-side mux: off-screen stash row, cpu_buf=1 -----------------
        // The real correctness question this design has to get right: cpu_buf must NEVER touch
        // stash rows (art panel, thumbnails, Chladni, TIM1, probes), or every off-screen consumer
        // that reads them back through a DIFFERENT, non-cpu_buf-relative path would read garbage.
        push(4'd1, OFFSCREEN_ROW, 9'd4, 9'd1);
        repeat (40) @(posedge clk_sdram);
        check(row_addr[rows_written-1] == {6'd0, OFFSCREEN_ROW},
              "off-screen RECT (row 984, a real stash row), cpu_buf=1: NEVER shifted, any DBUF_ENABLE");

        dbuf_cpu_buf = 1'b0;

        // -- flip: request while NOT at a vblank edge -> must NOT apply immediately ------------
        check(dbuf_disp_buf == 1'b0, "disp_buf starts at 0");
        dbuf_flip_req_tgl = ~dbuf_flip_req_tgl;
        repeat (20) @(posedge clk_sdram);
        if (DBUF_ENABLE != 0)
            check(dbuf_flip_pending == 1'b1 && dbuf_disp_buf == 1'b0,
                  "a flip request is latched as pending but NOT applied outside vblank");
        else
            check(dbuf_flip_pending == 1'b0 && dbuf_disp_buf == 1'b0,
                  "DBUF_ENABLE=0: a flip request has no effect at all");

        // -- let a full frame pass (H_TOT*V_TOT clk_vid cycles) so the modelled vblank edge fires,
        // and confirm the flip is applied by then, and the scanout read side (fill_line prefetch)
        // then targets buffer 1.
        repeat (210000) @(posedge clk_vid);
        if (DBUF_ENABLE != 0) begin
            check(dbuf_disp_buf == 1'b1 && dbuf_flip_pending == 1'b0,
                  "the pending flip was applied during vblank, within one frame");
            reads_seen = 0;
            repeat (400000) @(posedge clk_vid);   // let scanout prefetch land a few rows
            check(reads_seen > 0, "scanout prefetch issued at least one read after the flip");
            if (reads_seen > 0)
                check((read_addr[0] & DBUF_BASE1) == DBUF_BASE1,
                      "scanout prefetch reads from buffer 1 after the flip (disp_buf, not cpu_buf)");
        end else begin
            check(dbuf_disp_buf == 1'b0, "DBUF_ENABLE=0: disp_buf never moves, no matter what is requested");
        end

        // -- a second flip with no request pending: nothing happens (not a free-running toggle) --
        repeat (210000) @(posedge clk_vid);
        if (DBUF_ENABLE != 0)
            check(dbuf_disp_buf == 1'b1, "no new flip request: disp_buf stays put across further vblanks");

        if (errors == 0) $display("\nPASSED");
        else              $display("\n%0d FAILURE(S)", errors);
        $finish;
    end
endmodule
