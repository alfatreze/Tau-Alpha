// ============================================================================
// tb_font_ram.v -- the writable Latin-1 font memory (docs/features/FONT_SPEC.md, TAU_FONT_RAM) through the
// real mp3_fb command interface: CHAR commands for ASCII and for the 0xA0..0xFF range (glyph bit 7 = cmd_glyph_hi[0]),
// the blank codes (0x7F..0x9F), then the CPU write port (FONT_IDX/FONT_DATA as mp3_soc.v drives it) replacing two
// glyphs, and CHAR again. Every word the engine writes is dumped for sim/test_font_ram.py to diff against
// tools/host/blit_reference.py using the same atlas (font_ram.v's power-up contents plus the patches).
// Harness adapted from sim/tb_blit_scene.v. Mutation hooks: BUG_FONT_NO_HI, BUG_FONT_RANGE, BUG_FONT_NO_WRITE.
//   iverilog -g2012 -o tb.vvp sim/tb_font_ram.v src/fpga/core/tau_{gfx,talos,scanout}.sv src/fpga/core/font_rom.v src/fpga/core/font_ram.v
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module tb_font_ram;
    // Same mutation hooks as sim/tb_mp3_fb.v, passed through to the DUT --
    // overridden from the command line (-Ptb_blit_scene.BUG_...=1), matching
    // the Makefile's existing tb_mp3_fb convention exactly.
    parameter BUG_IGNORE_BLIT_STRIDE = 0;
    parameter BUG_IGNORE_KEY = 0;
    parameter BUG_SBLIT_NO_SCALE = 0;
    parameter BUG_BLEND_ALWAYS_SRC = 0;
    parameter BUG_CBLIT_NO_LOOKUP = 0;
    parameter BUG_IGNORE_REINDEX = 0;
    parameter BUG_IGNORE_TEXT_LIGHT = 0;
    parameter BUG_FONT_NO_HI = 0;
    parameter BUG_FONT_RANGE = 0;
    parameter BUG_FONT_NO_WRITE = 0;

    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5    clk_sdram = ~clk_sdram;   // 100 MHz
    always #10   clk_sys   = ~clk_sys;     //  50 MHz
    always #41.7 clk_vid   = ~clk_vid;     //  12 MHz

    reg         cmd_push = 0;
    reg  [3:0]  cmd_op = 0;
    reg  [18:0] cmd_addr = 0;
    reg  [8:0]  cmd_w = 0, cmd_h = 0;
    reg  [15:0] cmd_fg = 16'h0000, cmd_bg = 16'h0000;
    reg  [6:0]  cmd_glyph = 0;
    reg  [1:0]  cmd_sx = 0, cmd_sy = 0;
    reg  [1:0]  cmd_glyph_hi = 0;   // B12 (B-454): BAR lit-count high bits, left 0 (this scene's BAR command fits in 7 bits)
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
    reg         font_we = 1'b0;
    reg  [12:0] font_waddr = 13'd0;
    reg  [31:0] font_wdata = 32'd0;
    reg         clut_wr = 1'b0;
    reg  [7:0]  clut_waddr = 8'd0;
    reg  [15:0] clut_wdata = 16'd0;
    reg  [79:0] rc_cut_lut = 80'd0;

    wire [24:0] p0_addr;
    wire [15:0] p0_data;
    wire [1:0]  p0_byte_en;
    wire [10:0] p0_wr_len;
    wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req;
    reg  [15:0] p0_q = 0;
    reg         p0_available = 0, p0_ready = 0, p0_data_available = 0;
    reg  [10:0] wsrc_addr = 0;
    wire [15:0] wsrc_q;

    tau_gfx #(.BUG_IGNORE_BLIT_STRIDE(BUG_IGNORE_BLIT_STRIDE), .BUG_IGNORE_KEY(BUG_IGNORE_KEY),
             .BUG_SBLIT_NO_SCALE(BUG_SBLIT_NO_SCALE), .BLIT_BLEND_ENABLE(1),
             .BUG_BLEND_ALWAYS_SRC(BUG_BLEND_ALWAYS_SRC),
             .BUG_CBLIT_NO_LOOKUP(BUG_CBLIT_NO_LOOKUP),
             .BUG_IGNORE_REINDEX(BUG_IGNORE_REINDEX),
             .BUG_IGNORE_TEXT_LIGHT(BUG_IGNORE_TEXT_LIGHT),
             .FONT_RAM_ENABLE(1), .BUG_FONT_NO_HI(BUG_FONT_NO_HI), .BUG_FONT_RANGE(BUG_FONT_RANGE), .BUG_FONT_NO_WRITE(BUG_FONT_NO_WRITE)) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr),
        .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy), .cmd_glyph_hi(cmd_glyph_hi), .cmd_full(cmd_full),
        .font_we(font_we), .font_waddr(font_waddr), .font_wdata(font_wdata),
        .blt_src_base(blt_src_base), .blt_src_stride(blt_src_stride),
        .blt_dst_base(blt_dst_base), .blt_dst_stride(blt_dst_stride),
        .blt_key_en(blt_key_en), .blt_key(blt_key),
        .blt_blend_en(blt_blend_en), .blt_blend_mode(blt_blend_mode), .blt_blend_alpha(blt_blend_alpha),
        .blt_reindex(blt_reindex),
        .text_light(text_light),
        .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
        .rc_cut_lut(rc_cut_lut),
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

    // ---- SDRAM controller stub, same contract as sim/tb_mp3_fb.v ----------
    localparam S_IDLE = 0, S_STREAM = 1, S_CONST = 2, S_DONE = 3, S_READ = 4;
    integer     st = S_IDLE;
    integer     beats;
    reg [15:0]  captured [0:63];
    reg [24:0]  last_addr;
    reg [10:0]  last_len;
    reg         last_stream;
    reg [24:0]  rd_addr;

    // Every written word lands here: sim can't dynamically grow an array, so
    // size for comfortably more than this scene needs (36 rows x <=8 words).
    integer     out_n = 0;
    reg [24:0]  out_addr [0:4095];
    reg [15:0]  out_val  [0:4095];

    // A real persistent memory, unlike sim/tb_mp3_fb.v's stub (which never
    // needs one -- its tests never read back a destination this same run
    // wrote). Keyed/blended BLITs pre-read the real destination (A_KEYDST),
    // so this scene's earlier RECT writes must actually be visible to a
    // later BLIT's read, not just re-derived from the addr+1 formula.
    // Initialised to that same formula so untouched "source" addresses
    // behave exactly as sim/tb_mp3_fb.v's stub already does; only addresses
    // this scene actually writes differ from it afterwards.
    reg [15:0]  real_mem [0:32767];
    integer     init_i;
    initial for (init_i = 0; init_i < 32768; init_i = init_i + 1)
        real_mem[init_i] = init_i[15:0] + 16'd1;

    integer i;
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
                p0_q    <= real_mem[rd_addr[14:0]];   // reflects any earlier write to this scene
                rd_addr <= rd_addr + 25'd1;
                if (p0_end_burst_req) st <= S_IDLE;
            end
            S_CONST: st <= S_DONE;
            S_DONE: begin
                for (i = 0; i < last_len; i = i + 1) begin
                    out_addr[out_n] = last_addr + i;
                    out_val[out_n]  = last_stream ? captured[i] : p0_data;
                    real_mem[(last_addr + i) & 25'h7FFF] = out_val[out_n];
                    out_n = out_n + 1;
                end
                p0_ready <= 1'b1;
                st       <= S_IDLE;
            end
        endcase
    end

    task push(input [3:0] op, input [18:0] a, input [8:0] w, input [8:0] h,
              input [15:0] fg, input [15:0] bg, input [6:0] g,
              input [1:0] sx, input [1:0] sy);
        begin
            @(posedge clk_sys);
            cmd_op <= op; cmd_addr <= a; cmd_w <= w; cmd_h <= h;
            cmd_fg <= fg; cmd_bg <= bg;
            cmd_glyph <= g; cmd_sx <= sx; cmd_sy <= sy; cmd_push <= 1'b1;
            @(posedge clk_sys);
            cmd_push <= 1'b0;
        end
    endtask

    // CHAR with the glyph's bit 7 on cmd_glyph_hi[0] (R_FB_GO bit 15), the way the Latin-1 firmware sends it.
    task push_char(input [18:0] a, input [7:0] code);
        begin
            @(posedge clk_sys);
            cmd_op <= 4'd2; cmd_addr <= a; cmd_w <= 9'd0; cmd_h <= 9'd0;
            cmd_fg <= 16'hFFFF; cmd_bg <= 16'h0000;
            cmd_glyph <= code[6:0]; cmd_glyph_hi <= {1'b0, code[7]}; cmd_sx <= 2'd0; cmd_sy <= 2'd0; cmd_push <= 1'b1;
            @(posedge clk_sys);
            cmd_push <= 1'b0; cmd_glyph_hi <= 2'd0;
        end
    endtask

    // One word into the font memory (clk_sys), as mp3_soc.v's FONT_DATA write does.
    task font_write(input [12:0] idx, input [31:0] data);
        begin
            @(posedge clk_sys);
            font_waddr <= idx; font_wdata <= data; font_we <= 1'b1;
            @(posedge clk_sys);
            font_we <= 1'b0;
        end
    endtask

    // B8: one CLUT write (clk_sys domain), matching mp3_soc.v's own one-write-
    // per-entry contract (clut_wr is a single-cycle pulse).
    task clut_load(input [7:0] idx, input [15:0] data);
        begin
            @(posedge clk_sys);
            clut_waddr <= idx; clut_wdata <= data; clut_wr <= 1'b1;
            @(posedge clk_sys);
            clut_wr <= 1'b0;
        end
    endtask

    integer dumpf;
    reg [1023:0] dump_path;
    integer k;
    initial begin
        repeat (10) @(posedge clk_sdram);
        reset = 0;
        repeat (10) @(posedge clk_sdram);

        // ---- the scene: MUST match sim/test_font_ram.py's reference_scene() ----
        push_char(19'd0,   8'h41); wait (out_n == 256*1); repeat (20) @(posedge clk_sdram);   // 'A' (ASCII range, base 33*32)
        push_char(19'd16,  8'hE9); wait (out_n == 256*2); repeat (20) @(posedge clk_sdram);   // e-acute: bit 7 on glyph_hi, glyph 95+9
        push_char(19'd32,  8'hFF); wait (out_n == 256*3); repeat (20) @(posedge clk_sdram);   // y-diaeresis: the last glyph (190)
        push_char(19'd48,  8'hA0); wait (out_n == 256*4); repeat (20) @(posedge clk_sdram);   // no-break space: glyph 95
        push_char(19'd64,  8'h9F); wait (out_n == 256*5); repeat (20) @(posedge clk_sdram);   // C1 control: the blank glyph
        push_char(19'd80,  8'h7F); wait (out_n == 256*6); repeat (20) @(posedge clk_sdram);   // DEL: the blank glyph
        // the CPU replaces glyph 'A' (index 33: words 1056..1087) and the last glyph (190: words 6080..6111)
        for (k = 0; k < 32; k = k + 1) font_write(13'd1056 + k, 32'h13579BDF ^ (k * 32'h01010101));
        for (k = 0; k < 32; k = k + 1) font_write(13'd6080 + k, 32'hF0E1D2C3 + (k * 32'h00010203));
        repeat (20) @(posedge clk_sdram);
        push_char(19'd96,  8'h41); wait (out_n == 256*7); repeat (20) @(posedge clk_sdram);   // 'A' again: the patched atlas
        push_char(19'd112, 8'hFF); wait (out_n == 256*8); repeat (20) @(posedge clk_sdram);   // the last glyph again: patched
        push_char(19'd128, 8'hE9); wait (out_n == 256*9); repeat (20) @(posedge clk_sdram);   // e-acute: unchanged by the patches

        if (!$value$plusargs("DUMP=%s", dump_path)) dump_path = "/dev/null";
        dumpf = $fopen(dump_path, "w");
        for (k = 0; k < out_n; k = k + 1)
            $fdisplay(dumpf, "%0d %0d", out_addr[k], out_val[k]);
        $fclose(dumpf);
        $display("PASSED (%0d words written)", out_n);
        $finish;
    end
endmodule
