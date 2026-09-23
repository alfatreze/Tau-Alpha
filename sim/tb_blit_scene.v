// ============================================================================
// tb_blit_scene.v -- plays one fixed, multi-command "scene" through the real
// mp3_fb command-push interface (the same cmd_push/cmd_op/... ports firmware
// drives via R_FB_GO) and dumps every word the engine actually wrote to a
// file, for tools/host/blit_reference.py to diff against byte for byte
// (PHASE_F_SPEC.md section 12: "render the same scene through the reference
// and through the RTL simulation and compare buffers exactly").
//
// This is NOT a replacement for sim/tb_mp3_fb.v's per-feature unit tests --
// those hand-verify one behaviour at a time and stay the first line of
// defence. This testbench answers a different question: does a whole
// sequence of commands, writing to a shared destination the way firmware
// actually would, agree with an independent reference model exactly.
//
// The scene's literal addresses/values are hand-picked to avoid any
// unintended overlap between commands and MUST match
// sim/test_blit_reference.py's SCENE list exactly -- see that file's own
// header for the shared layout. Every destination a keyed or blended BLIT
// touches is pre-painted by an earlier RECT in this same scene, since a
// keyed/blended pixel's correct value depends on the destination already
// holding a known, non-X value (A_KEYDST pre-reads real content -- there is
// no "correct" keyed/blended answer against untouched memory).
//
// Harness (DUT + SDRAM stub) is adapted directly from sim/tb_mp3_fb.v --
// same contract, same "word N reads back as N+1" source-read convention.
//
//   iverilog -g2012 -o tb.vvp sim/tb_blit_scene.v src/fpga/core/mp3_fb.sv \
//            src/fpga/core/font_rom.v && vvp tb.vvp +DUMP=/tmp/scene.txt
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module tb_blit_scene;
    // Same mutation hooks as sim/tb_mp3_fb.v, passed through to the DUT --
    // overridden from the command line (-Ptb_blit_scene.BUG_...=1), matching
    // the Makefile's existing tb_mp3_fb convention exactly.
    parameter BUG_IGNORE_BLIT_STRIDE = 0;
    parameter BUG_IGNORE_KEY = 0;
    parameter BUG_SBLIT_NO_SCALE = 0;
    parameter BUG_BLEND_ALWAYS_SRC = 0;
    parameter BUG_CBLIT_NO_LOOKUP = 0;

    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5    clk_sdram = ~clk_sdram;   // 100 MHz
    always #10   clk_sys   = ~clk_sys;     //  50 MHz
    always #41.7 clk_vid   = ~clk_vid;     //  12 MHz

    reg         cmd_push = 0;
    reg  [2:0]  cmd_op = 0;
    reg  [18:0] cmd_addr = 0;
    reg  [8:0]  cmd_w = 0, cmd_h = 0;
    reg  [15:0] cmd_fg = 16'h0000, cmd_bg = 16'h0000;
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

    mp3_fb #(.BUG_IGNORE_BLIT_STRIDE(BUG_IGNORE_BLIT_STRIDE), .BUG_IGNORE_KEY(BUG_IGNORE_KEY),
             .BUG_SBLIT_NO_SCALE(BUG_SBLIT_NO_SCALE), .BLIT_BLEND_ENABLE(1),
             .BUG_BLEND_ALWAYS_SRC(BUG_BLEND_ALWAYS_SRC),
             .BUG_CBLIT_NO_LOOKUP(BUG_CBLIT_NO_LOOKUP)) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr),
        .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy), .cmd_full(cmd_full),
        .blt_src_base(blt_src_base), .blt_src_stride(blt_src_stride),
        .blt_dst_base(blt_dst_base), .blt_dst_stride(blt_dst_stride),
        .blt_key_en(blt_key_en), .blt_key(blt_key),
        .blt_blend_en(blt_blend_en), .blt_blend_mode(blt_blend_mode), .blt_blend_alpha(blt_blend_alpha),
        .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
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
    reg [24:0]  out_addr [0:511];
    reg [15:0]  out_val  [0:511];

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

    task push(input [2:0] op, input [18:0] a, input [8:0] w, input [8:0] h,
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

        // ---- the scene: MUST match sim/test_blit_reference.py's SCENE ----
        // 1. RUN: addr=0, w=8, fg=0x1234
        push(3'd0, 19'd0, 9'd8, 9'd0, 16'h1234, 16'h0000, 7'd0, 2'd0, 2'd0);
        wait (out_n == 8); repeat (10) @(posedge clk_sdram);

        // 2. RECT: addr=1024, w=6, h=3, fg=0x2222
        push(3'd1, 19'd1024, 9'd6, 9'd3, 16'h2222, 16'h0000, 7'd0, 2'd0, 2'd0);
        wait (out_n == 8+18); repeat (10) @(posedge clk_sdram);

        // 3. Pre-paint for the keyed/blended BLITs below: addr=3072, w=4, h=2, fg=0x5555
        push(3'd1, 19'd3072, 9'd4, 9'd2, 16'h5555, 16'h0000, 7'd0, 2'd0, 2'd0);
        wait (out_n == 8+18+8); repeat (10) @(posedge clk_sdram);

        // 4. COPY: dest addr=4608, w=4, h=2, source packed {fg[2:0],bg} = (5<<16)|100
        push(3'd3, 19'd4608, 9'd4, 9'd2, 16'h0005, 16'h0064, 7'd0, 2'd0, 2'd0);
        wait (out_n == 8+18+8+8); repeat (10) @(posedge clk_sdram);

        // 5. BLIT plain (sticky base 0/stride 512, degenerates to COPY-shape):
        //    dest addr=6144, w=4, h=2, source offset=200
        push(3'd4, 19'd6144, 9'd4, 9'd2, 16'h0000, 16'h00C8, 7'd0, 2'd0, 2'd0);
        wait (out_n == 8+18+8+8+8); repeat (10) @(posedge clk_sdram);

        // 6. BLIT keyed: dest addr=3072 (the pre-painted region), w=4, h=1,
        //    source offset=300, key=302 (keys out column 1: src_read(301)=302)
        blt_key_en <= 1'b1; blt_key <= 16'h012E;   // 302
        push(3'd4, 19'd3072, 9'd4, 9'd1, 16'h0000, 16'h012C, 7'd0, 2'd0, 2'd0);
        wait (out_n == 8+18+8+8+8+4); repeat (10) @(posedge clk_sdram);
        blt_key_en <= 1'b0;

        // 7. BLIT blended: dest addr=3584 (the other pre-painted row), w=4, h=1,
        //    source offset=400, DSP mode alpha=128
        blt_blend_en <= 1'b1; blt_blend_mode <= 3'd0; blt_blend_alpha <= 8'd128;
        push(3'd4, 19'd3584, 9'd4, 9'd1, 16'h0000, 16'h0190, 7'd0, 2'd0, 2'd0);
        wait (out_n == 8+18+8+8+8+4+4); repeat (10) @(posedge clk_sdram);
        blt_blend_en <= 1'b0;

        // 8. BAR: addr=7680, w=5, h=4, fg(lit)=0x0F0F, bg(unlit)=0x00F0,
        //    cmd_glyph=3 lit rows (so 1 unlit + 3 lit)
        push(3'd5, 19'd7680, 9'd5, 9'd4, 16'h0F0F, 16'h00F0, 7'd3, 2'd0, 2'd0);
        wait (out_n == 8+18+8+8+8+4+4+20); repeat (10) @(posedge clk_sdram);

        // 9. SBLIT: dest addr=10240, source 2x2 at offset=500, 2x/2x scale -> 4x4 out
        push(3'd6, 19'd10240, 9'd2, 9'd2, 16'h0000, 16'h01F4, 7'd0, 2'd2, 2'd2);
        wait (out_n == 8+18+8+8+8+4+4+20+16); repeat (10) @(posedge clk_sdram);

        // 10. BLIT custom stride: dest addr=16384, w=4, h=2, source offset=600,
        //     dst_stride=96, src_stride=64 -- exercises the sticky-stride path
        //     BUG_IGNORE_BLIT_STRIDE targets (default stride 512 never would).
        blt_dst_stride <= 10'd96; blt_src_stride <= 10'd64;
        push(3'd4, 19'd16384, 9'd4, 9'd2, 16'h0000, 16'h0258, 7'd0, 2'd0, 2'd0);
        wait (out_n == 8+18+8+8+8+4+4+20+16+8); repeat (10) @(posedge clk_sdram);
        blt_dst_stride <= 10'd512; blt_src_stride <= 10'd512;

        // 11. CHAR: 'A' (0x41), scale 1x1, fg=0xFFFF, bg=0x0000, addr=12800
        push(3'd2, 19'd12800, 9'd0, 9'd0, 16'hFFFF, 16'h0000, 7'h41, 2'd0, 2'd0);
        wait (out_n == 8+18+8+8+8+4+4+20+16+8+256); repeat (20) @(posedge clk_sdram);

        // 12. CBLIT (B8): dest addr=20480, w=4, h=2, source offset=255. src_read(255+c)
        //     = 256+c for row 0 (low byte = c, i.e. indices 0..3) and src_read(767+c)
        //     = 769+c for row 1 (indices 1..4, since 769 & 0xFF = 1) -- CLUT entries
        //     0..4 preloaded with distinct values so every index used is checkable.
        clut_load(8'd0, 16'h1001); clut_load(8'd1, 16'h1002); clut_load(8'd2, 16'h1003);
        clut_load(8'd3, 16'h1004); clut_load(8'd4, 16'h1005);
        push(3'd7, 19'd20480, 9'd4, 9'd2, 16'h0000, 16'h00FF, 7'd0, 2'd0, 2'd0);
        wait (out_n == 8+18+8+8+8+4+4+20+16+8+256+8); repeat (10) @(posedge clk_sdram);

        // ---- dump every written word ---------------------------------------
        if (!$value$plusargs("DUMP=%s", dump_path)) dump_path = "/dev/null";
        dumpf = $fopen(dump_path, "w");
        for (k = 0; k < out_n; k = k + 1)
            $fdisplay(dumpf, "%0d %0d", out_addr[k], out_val[k]);
        $fclose(dumpf);
        $display("PASSED (%0d words written)", out_n);
        $finish;
    end
endmodule
