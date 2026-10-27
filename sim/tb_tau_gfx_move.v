// ============================================================================
// tb_tau_gfx_move.v -- how the engine's BLIT behaves when source and destination overlap inside the framebuffer (what a scroll is), on the real RTL.
//   A. a LEFT move (destination to the left of the source in the same rows): every row is read whole into the row buffer before it is written, so the overlap is harmless:
//      one BLIT per column chunk moves the box exactly like a memmove, no temporary copy needed.
//   B. a DOWN move in ONE command (destination d rows below the source, h > d): rows go top to bottom, so a row's destination is read as a source later: the result is NOT a memmove
//      (this is why a downward scroll cannot be one command).
//   C. a DOWN move in chunks of d rows, bottom up (chunk height <= d): exactly a memmove, no temporary copy needed.
// PASSED when A and C equal the memmove and B differs from it (it documents the hazard).
// ============================================================================
`timescale 1ns/1ps
`default_nettype none
module mv_sdram_stub (
    input  wire        clk,
    input  wire [24:0] p0_addr, input wire [15:0] p0_data, input wire [10:0] p0_wr_len,
    input  wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req,
    output reg  [15:0] p0_q = 0, output reg p0_available = 0, p0_ready = 0, p0_data_available = 0,
    output reg  [10:0] wsrc_addr = 0, input wire [15:0] wsrc_q
);
    reg [15:0] mem [0:(1<<20)-1];
    integer k; initial for (k = 0; k < (1<<20); k = k + 1) mem[k] = k[15:0] * 16'h9E37 + 16'h1234;
    localparam IDLE = 0, STREAM = 1, CONST = 2, DONE = 3, READ = 4;
    integer st = IDLE, beats = 0;
    reg [24:0] a; reg [10:0] len; reg [15:0] cdata;
    always @(posedge clk) begin
        p0_ready <= 1'b0; p0_data_available <= 1'b0;
        case (st)
            IDLE: begin
                p0_available <= 1'b1;
                if (p0_rd_req) begin p0_available <= 1'b0; a <= p0_addr; st <= READ; end
                else if (p0_wr_req) begin p0_available <= 1'b0; a <= p0_addr; len <= p0_wr_len; cdata <= p0_data; wsrc_addr <= 0; beats = 0; st <= p0_wr_stream ? STREAM : CONST; end
            end
            STREAM: begin wsrc_addr <= wsrc_addr + 1; if (wsrc_addr > 0) begin mem[(a + beats) & 25'hFFFFF] <= wsrc_q; beats = beats + 1; end if (beats == len) st <= DONE; end
            READ: begin p0_data_available <= 1'b1; p0_q <= mem[a & 25'hFFFFF]; a <= a + 25'd1; if (p0_end_burst_req) st <= IDLE; end
            CONST: begin for (beats = 0; beats < len; beats = beats + 1) mem[(a + beats) & 25'hFFFFF] <= cdata; st <= DONE; end
            DONE: begin p0_ready <= 1'b1; st <= IDLE; end
        endcase
    end
endmodule

module tb_tau_gfx_move;
    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5 clk_sdram = ~clk_sdram; always #10 clk_sys = ~clk_sys; always #41.7 clk_vid = ~clk_vid;
    reg cmd_push = 0; reg [3:0] cmd_op = 0; reg [18:0] cmd_addr = 0; reg [8:0] cmd_w = 0, cmd_h = 0; reg [15:0] cmd_fg = 0, cmd_bg = 0;
    wire cmd_full, idle;
    wire [24:0] p0_addr; wire [15:0] p0_data; wire [10:0] p0_len; wire p0_str, p0_wr, p0_rd, p0_eb;
    wire [15:0] p0_q; wire p0_av, p0_rdy, p0_da; wire [10:0] wsa; wire [15:0] wsq;
    tau_gfx #(.BLIT_BLEND_ENABLE(0)) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr), .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(7'd0), .cmd_sx(2'd0), .cmd_sy(2'd0), .cmd_glyph_hi(2'd0), .font_we(1'b0), .font_waddr(13'd0), .font_wdata(32'd0), .cmd_full(cmd_full), .text_light(1'b0),
        .blt_src_base(25'd0), .blt_src_stride(10'd512), .blt_dst_base(25'd0), .blt_dst_stride(10'd512), .blt_key_en(1'b0), .blt_key(16'd0),
        .blt_blend_en(1'b0), .blt_blend_mode(3'd0), .blt_blend_alpha(8'd0), .rc_cut_lut(80'd0), .blt_reindex(8'd0), .clut_wr(1'b0), .clut_waddr(8'd0), .clut_wdata(16'd0),
        .dbuf_cpu_buf(1'b0), .dbuf_flip_req_tgl(1'b0), .dbuf_disp_buf(), .dbuf_flip_pending(),
        .sdram_init_complete(1'b1), .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(), .p0_wr_len(p0_len), .p0_wr_stream(p0_str), .p0_q(p0_q),
        .p0_wr_req(p0_wr), .p0_rd_req(p0_rd), .p0_end_burst_req(p0_eb), .p0_available(p0_av), .p0_ready(p0_rdy), .p0_data_available(p0_da), .wsrc_addr(wsa), .wsrc_q(wsq),
        .video_rgb(), .video_de(), .video_hs(), .video_vs(), .scan_vc(), .gfx_idle(idle), .gfx_fence(), .gfx_free(), .gfx_ovf(), .gfx_underflow());
    mv_sdram_stub mem (.clk(clk_sdram), .p0_addr(p0_addr), .p0_data(p0_data), .p0_wr_len(p0_len), .p0_wr_stream(p0_str), .p0_wr_req(p0_wr), .p0_rd_req(p0_rd),
        .p0_end_burst_req(p0_eb), .p0_q(p0_q), .p0_available(p0_av), .p0_ready(p0_rdy), .p0_data_available(p0_da), .wsrc_addr(wsa), .wsrc_q(wsq));

    // one BLIT: copy a w x h box from (sx, sy) to (dx, dy) (words, stride 512), src offset rides in {fg[2:0], bg}
    task blit(input integer sx, input integer sy, input integer dx, input integer dy, input integer w, input integer h);
        integer so;
        begin
            so = sy * 512 + sx;
            @(posedge clk_sys); while (cmd_full) @(posedge clk_sys);
            cmd_op <= 4'd4; cmd_addr <= dy * 512 + dx; cmd_w <= w; cmd_h <= h; cmd_fg <= so[18:16]; cmd_bg <= so[15:0]; cmd_push <= 1'b1;
            @(posedge clk_sys); cmd_push <= 1'b0;
        end
    endtask
    task settle; integer n; begin n = 0; repeat (4) @(posedge clk_sys); while (!idle && n < 400000) begin @(posedge clk_sys); n = n + 1; end end endtask
    reg [15:0] ref_mem [0:(1<<20)-1];
    task snapshot; integer k; begin for (k = 0; k < (1<<20); k = k + 1) ref_mem[k] = mem.mem[k]; end endtask
    // the memmove of a box, computed in the bench from the snapshot (reads all old values first)
    task expect_move(input integer sx, input integer sy, input integer dx, input integer dy, input integer w, input integer h);
        integer r, c; reg [15:0] tmp [0:(1<<16)-1];
        begin
            for (r = 0; r < h; r = r + 1) for (c = 0; c < w; c = c + 1) tmp[r * 512 + c] = ref_mem[(sy + r) * 512 + sx + c];
            for (r = 0; r < h; r = r + 1) for (c = 0; c < w; c = c + 1) ref_mem[(dy + r) * 512 + dx + c] = tmp[r * 512 + c];
        end
    endtask
    function integer diffs(input integer x0, input integer y0, input integer w, input integer h);
        integer r, c, n; begin n = 0; for (r = 0; r < h; r = r + 1) for (c = 0; c < w; c = c + 1) if (mem.mem[(y0 + r) * 512 + x0 + c] !== ref_mem[(y0 + r) * 512 + x0 + c]) n = n + 1; diffs = n; end
    endfunction
    integer errors = 0, dA, dB, dC, ch, rem, n;
    initial begin
        repeat (6) @(posedge clk_sys); reset = 0; repeat (6) @(posedge clk_sys);
        // A: left move by 5 columns, 120 wide, 30 rows
        snapshot; expect_move(105, 40, 100, 40, 120, 30); blit(105, 40, 100, 40, 120, 30); settle; dA = diffs(90, 40, 140, 30);
        // B: down move by 3 rows in one command, 60 wide, 30 rows, in a region untouched by A
        snapshot; expect_move(10, 200, 10, 203, 60, 30); blit(10, 200, 10, 203, 60, 30); settle; dB = diffs(0, 195, 80, 45);
        // C: down move by 3 rows in chunks of 3 rows, bottom up
        snapshot; expect_move(10, 300, 10, 303, 60, 30);
        rem = 30; while (rem > 0) begin n = rem < 3 ? rem : 3; blit(10, 300 + rem - n, 10, 303 + rem - n, 60, n); rem = rem - n; end
        settle; dC = diffs(0, 295, 80, 45);
        $display("A left move, one command per chunk: %0d words differ from a memmove", dA);
        $display("B down move, one command: %0d words differ from a memmove (expected: many)", dB);
        $display("C down move, chunks of d rows bottom up: %0d words differ from a memmove", dC);
        if (dA == 0 && dB > 100 && dC == 0) $display("PASSED a left move needs no temporary copy; a down move needs chunks of at most d rows from the bottom");
        else $display("FAILED");
        $finish;
    end
endmodule
