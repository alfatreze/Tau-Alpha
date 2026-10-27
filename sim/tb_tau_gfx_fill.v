// ============================================================================
// tb_tau_gfx_fill.v -- Talos 2 T3b: a scanout fill of 400 words (the picture) must show exactly the picture a fill of the whole 512-word stride shows, in fewer engine
// cycles, and with no late line. Two tau_gfx (FILL_WORDS = 400 and 512) get the same random drawing (a full screen of one-row rectangles, then random rectangles and
// glyphs); once both are idle, a full frame of the video output is compared on every pixel clock, the engine cycles spent in fills are counted, and the late-line counters
// must be 0. MUT 1 drives the first one with 399 words (the last pixel of every line stale): the bench must FAIL.
//   make test-rtl-gfx-fill
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module fl_sdram_stub (
    input  wire        clk,
    input  wire [24:0] p0_addr, input wire [15:0] p0_data, input wire [10:0] p0_wr_len,
    input  wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req,
    output reg  [15:0] p0_q = 0, output reg p0_available = 0, p0_ready = 0, p0_data_available = 0,
    output reg  [10:0] wsrc_addr = 0, input wire [15:0] wsrc_q
);
    reg [15:0] mem [0:(1<<20)-1];
    integer k; initial for (k = 0; k < (1<<20); k = k + 1) mem[k] = (k * 16'h9E37 + (k >> 7)) ^ 16'h5A5A;
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

module tb_tau_gfx_fill;
    parameter MUT = 0;
    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5    clk_sdram = ~clk_sdram;
    always #10   clk_sys   = ~clk_sys;
    always #41.7 clk_vid   = ~clk_vid;
    reg cmd_push = 0; reg [3:0] cmd_op = 0; reg [18:0] cmd_addr = 0; reg [8:0] cmd_w = 0, cmd_h = 0;
    reg [15:0] cmd_fg = 0, cmd_bg = 0; reg [6:0] cmd_glyph = 0; reg [1:0] cmd_sx = 0, cmd_sy = 0;
    wire a_full, b_full, a_idle, b_idle;
    wire [24:0] a_addr, b_addr; wire [15:0] a_data, b_data; wire [10:0] a_len, b_len; wire a_str, b_str, a_wr, b_wr, a_rd, b_rd, a_eb, b_eb;
    wire [15:0] a_q, b_q; wire a_av, b_av, a_rdy, b_rdy, a_da, b_da; wire [10:0] a_wa, b_wa; wire [15:0] a_wq, b_wq;
    wire [23:0] a_rgb, b_rgb; wire a_de, b_de, a_hs, b_hs, a_vs, b_vs; wire [15:0] a_uf, b_uf;

    tau_gfx #(.BLIT_BLEND_ENABLE(0), .FILL_WORDS(MUT == 1 ? 399 : 400)) dut_a (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr), .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy), .cmd_glyph_hi(2'd0), .font_we(1'b0), .font_waddr(13'd0), .font_wdata(32'd0),
        .cmd_full(a_full), .text_light(1'b0),
        .blt_src_base(25'd0), .blt_src_stride(10'd512), .blt_dst_base(25'd0), .blt_dst_stride(10'd512), .blt_key_en(1'b0), .blt_key(16'd0),
        .blt_blend_en(1'b0), .blt_blend_mode(3'd0), .blt_blend_alpha(8'd0), .rc_cut_lut(80'd0), .blt_reindex(8'd0), .clut_wr(1'b0), .clut_waddr(8'd0), .clut_wdata(16'd0),
        .dbuf_cpu_buf(1'b0), .dbuf_flip_req_tgl(1'b0), .dbuf_disp_buf(), .dbuf_flip_pending(),
        .sdram_init_complete(1'b1), .p0_addr(a_addr), .p0_data(a_data), .p0_byte_en(), .p0_wr_len(a_len), .p0_wr_stream(a_str), .p0_q(a_q),
        .p0_wr_req(a_wr), .p0_rd_req(a_rd), .p0_end_burst_req(a_eb), .p0_available(a_av), .p0_ready(a_rdy), .p0_data_available(a_da), .wsrc_addr(a_wa), .wsrc_q(a_wq),
        .video_rgb(a_rgb), .video_de(a_de), .video_hs(a_hs), .video_vs(a_vs), .scan_vc(),
        .gfx_idle(a_idle), .gfx_fence(), .gfx_free(), .gfx_ovf(), .gfx_underflow(a_uf));
    tau_gfx #(.BLIT_BLEND_ENABLE(0), .FILL_WORDS(512)) dut_b (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr), .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy), .cmd_glyph_hi(2'd0), .font_we(1'b0), .font_waddr(13'd0), .font_wdata(32'd0),
        .cmd_full(b_full), .text_light(1'b0),
        .blt_src_base(25'd0), .blt_src_stride(10'd512), .blt_dst_base(25'd0), .blt_dst_stride(10'd512), .blt_key_en(1'b0), .blt_key(16'd0),
        .blt_blend_en(1'b0), .blt_blend_mode(3'd0), .blt_blend_alpha(8'd0), .rc_cut_lut(80'd0), .blt_reindex(8'd0), .clut_wr(1'b0), .clut_waddr(8'd0), .clut_wdata(16'd0),
        .dbuf_cpu_buf(1'b0), .dbuf_flip_req_tgl(1'b0), .dbuf_disp_buf(), .dbuf_flip_pending(),
        .sdram_init_complete(1'b1), .p0_addr(b_addr), .p0_data(b_data), .p0_byte_en(), .p0_wr_len(b_len), .p0_wr_stream(b_str), .p0_q(b_q),
        .p0_wr_req(b_wr), .p0_rd_req(b_rd), .p0_end_burst_req(b_eb), .p0_available(b_av), .p0_ready(b_rdy), .p0_data_available(b_da), .wsrc_addr(b_wa), .wsrc_q(b_wq),
        .video_rgb(b_rgb), .video_de(b_de), .video_hs(b_hs), .video_vs(b_vs), .scan_vc(),
        .gfx_idle(b_idle), .gfx_fence(), .gfx_free(), .gfx_ovf(), .gfx_underflow(b_uf));
    fl_sdram_stub mem_a (.clk(clk_sdram), .p0_addr(a_addr), .p0_data(a_data), .p0_wr_len(a_len), .p0_wr_stream(a_str), .p0_wr_req(a_wr), .p0_rd_req(a_rd),
        .p0_end_burst_req(a_eb), .p0_q(a_q), .p0_available(a_av), .p0_ready(a_rdy), .p0_data_available(a_da), .wsrc_addr(a_wa), .wsrc_q(a_wq));
    fl_sdram_stub mem_b (.clk(clk_sdram), .p0_addr(b_addr), .p0_data(b_data), .p0_wr_len(b_len), .p0_wr_stream(b_str), .p0_wr_req(b_wr), .p0_rd_req(b_rd),
        .p0_end_burst_req(b_eb), .p0_q(b_q), .p0_available(b_av), .p0_ready(b_rdy), .p0_data_available(b_da), .wsrc_addr(b_wa), .wsrc_q(b_wq));

    integer seed = 32'h51c0ffee;
    function integer rnd(input integer n); rnd = $unsigned($random(seed)) % n; endfunction
    task push(input [3:0] op, input [18:0] a, input [8:0] w, input [8:0] h, input [15:0] fg);
        begin
            @(posedge clk_sys); while (a_full || b_full) @(posedge clk_sys);
            cmd_op <= op; cmd_addr <= a; cmd_w <= w; cmd_h <= h; cmd_fg <= fg; cmd_bg <= rnd(65536); cmd_glyph <= 7'h21 + rnd(90); cmd_sx <= 0; cmd_sy <= 0; cmd_push <= 1'b1;
            @(posedge clk_sys); cmd_push <= 1'b0;
        end
    endtask

    // engine cycles spent in scanout fills (A_FILL, state 1), counted per instance while both are in the measured window
    integer fill_a = 0, fill_b = 0; reg measure = 0;
    always @(posedge clk_sdram) if (measure) begin
        if (dut_a.u_talos.astate == 4'd1) fill_a = fill_a + 1;
        if (dut_b.u_talos.astate == 4'd1) fill_b = fill_b + 1;
    end
    integer errors = 0, cmp_px = 0, i, uf_a0, uf_b0;
    reg cmp_on = 0;
    always @(negedge clk_vid) if (cmp_on) begin
        cmp_px = cmp_px + 1;
        if ({a_rgb, a_de, a_hs, a_vs} !== {b_rgb, b_de, b_hs, b_vs}) begin errors = errors + 1; if (errors < 5) $display("DIFF video t=%0t a=%h b=%h", $time, a_rgb, b_rgb); end
    end
    initial begin
        repeat (6) @(posedge clk_sys); reset = 0; repeat (6) @(posedge clk_sys);
        for (i = 0; i < 360; i = i + 1) push(4'd1, i * 512, 9'd400, 9'd1, rnd(65536));     // every visible row a different colour
        for (i = 0; i < 120; i = i + 1) push(4'd1, rnd(350) * 512 + rnd(380), 1 + rnd(20), 1 + rnd(8), rnd(65536));
        for (i = 0; i < 60; i = i + 1) push(4'd2, rnd(344) * 512 + rnd(380), 9'd0, 9'd0, rnd(65536));   // glyphs
        begin : drain integer n; n = 0; repeat (4) @(posedge clk_sys); while (!(a_idle && b_idle) && n < 400000) begin @(posedge clk_sys); n = n + 1; end end
        repeat (2) @(negedge clk_vid);
        // two whole frames of settled picture, then one measured frame
        repeat (2 * 500 * 400) @(posedge clk_vid);
        uf_a0 = a_uf; uf_b0 = b_uf; measure = 1; cmp_on = 1;
        repeat (500 * 400) @(posedge clk_vid);
        measure = 0; cmp_on = 0;
        $display("pixels compared %0d, video differences %0d; engine cycles in fills over one frame: 400-word %0d, 512-word %0d; late lines (400 / 512): %0d / %0d",
                 cmp_px, errors, fill_a, fill_b, a_uf - uf_a0, b_uf - uf_b0);
        if (errors == 0 && cmp_px > 190000 && fill_a * 100 < fill_b * 85 && a_uf == uf_a0 && b_uf == uf_b0) $display("PASSED the 400-word fill shows the same picture, in fewer engine cycles, with no late line");
        else $display("FAILED");
        $finish;
    end
endmodule
