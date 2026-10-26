// ============================================================================
// tb_tau_gfx_cblit.v -- Talos 2 T3b: the burst CBLIT (a source row read as one SDRAM burst, looked up in the CLUT as the words arrive) must draw exactly what the per-pixel
// CBLIT draws, and must be much faster. Two tau_gfx instances (CBLIT_BURST = 1 and 0), each with a storing SDRAM model, get the same random commands (CBLIT mixed with RECT,
// COPY, BLIT, BAR), random CLUT contents, re-index offsets and sticky bases/strides; after every command both are idle and the destination regions are compared; at the end the
// whole memories are compared. The per-pixel instance is itself proven cycle-for-cycle against the pre-split original by tb_tau_gfx_diff.v, so the chain is complete.
//   make test-rtl-gfx-cblit   (mutants: -Ptb_tau_gfx_cblit.MUT=1 the burst ends one pixel short, 2 pixels written one column left)
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module cb_sdram_stub (
    input  wire        clk,
    input  wire [24:0] p0_addr, input wire [15:0] p0_data, input wire [10:0] p0_wr_len,
    input  wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req,
    output reg  [15:0] p0_q = 0, output reg p0_available = 0, p0_ready = 0, p0_data_available = 0,
    output reg  [10:0] wsrc_addr = 0, input wire [15:0] wsrc_q
);
    reg [15:0] mem [0:(1<<22)-1];
    integer k; initial for (k = 0; k < (1<<22); k = k + 1) mem[k] = (k * 16'h9E37 + (k >> 7)) ^ 16'h5A5A;
    localparam IDLE = 0, STREAM = 1, CONST = 2, DONE = 3, READ = 4;
    integer st = IDLE, beats = 0;
    reg [24:0] a; reg [10:0] len; reg [15:0] cdata;
    function [21:0] f(input [24:0] x); f = x[21:0]; endfunction
    always @(posedge clk) begin
        p0_ready <= 1'b0; p0_data_available <= 1'b0;
        case (st)
            IDLE: begin
                p0_available <= 1'b1;
                if (p0_rd_req) begin p0_available <= 1'b0; a <= p0_addr; st <= READ; end
                else if (p0_wr_req) begin
                    p0_available <= 1'b0; a <= p0_addr; len <= p0_wr_len; cdata <= p0_data; wsrc_addr <= 0; beats = 0;
                    st <= p0_wr_stream ? STREAM : CONST;
                end
            end
            STREAM: begin
                wsrc_addr <= wsrc_addr + 1;
                if (wsrc_addr > 0) begin mem[f(a + beats)] <= wsrc_q; beats = beats + 1; end
                if (beats == len) st <= DONE;
            end
            READ: begin p0_data_available <= 1'b1; p0_q <= mem[f(a)]; a <= a + 25'd1; if (p0_end_burst_req) st <= IDLE; end
            CONST: begin for (beats = 0; beats < len; beats = beats + 1) mem[f(a + beats)] <= cdata; st <= DONE; end
            DONE: begin p0_ready <= 1'b1; st <= IDLE; end
        endcase
    end
endmodule

module tb_tau_gfx_cblit;
    parameter MUT  = 0;
    parameter NCMD = 400;
    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5    clk_sdram = ~clk_sdram;
    always #10   clk_sys   = ~clk_sys;
    always #41.7 clk_vid   = ~clk_vid;

    reg cmd_push = 0; reg [3:0] cmd_op = 0; reg [18:0] cmd_addr = 0; reg [8:0] cmd_w = 0, cmd_h = 0;
    reg [15:0] cmd_fg = 0, cmd_bg = 0; reg [6:0] cmd_glyph = 0; reg [1:0] cmd_sx = 0, cmd_sy = 0, cmd_glyph_hi = 0;
    reg [24:0] blt_src_base = 0, blt_dst_base = 0; reg [9:0] blt_src_stride = 512, blt_dst_stride = 512;
    reg [7:0] blt_reindex = 0; reg clut_wr = 0; reg [7:0] clut_waddr = 0; reg [15:0] clut_wdata = 0;
    wire a_full, b_full;
    wire [24:0] a_addr, b_addr; wire [15:0] a_data, b_data; wire [10:0] a_len, b_len; wire a_str, b_str, a_wr, b_wr, a_rd, b_rd, a_eb, b_eb;
    wire [15:0] a_q, b_q; wire a_av, b_av, a_rdy, b_rdy, a_da, b_da; wire [10:0] a_wa, b_wa; wire [15:0] a_wq, b_wq;
    wire a_idle, b_idle;

    tau_gfx #(.BLIT_BLEND_ENABLE(0), .CBLIT_BURST(1), .BUG_CBLIT_EARLY(MUT == 1), .BUG_CBLIT_SHIFT(MUT == 2)) dut_a (   // A: the burst
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr), .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy), .cmd_glyph_hi(cmd_glyph_hi), .font_we(1'b0), .font_waddr(13'd0), .font_wdata(32'd0),
        .cmd_full(a_full), .text_light(1'b0),
        .blt_src_base(blt_src_base), .blt_src_stride(blt_src_stride), .blt_dst_base(blt_dst_base), .blt_dst_stride(blt_dst_stride),
        .blt_key_en(1'b0), .blt_key(16'd0), .blt_blend_en(1'b0), .blt_blend_mode(3'd0), .blt_blend_alpha(8'd0),
        .rc_cut_lut(80'd0), .blt_reindex(blt_reindex), .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
        .dbuf_cpu_buf(1'b0), .dbuf_flip_req_tgl(1'b0), .dbuf_disp_buf(), .dbuf_flip_pending(),
        .sdram_init_complete(1'b1), .p0_addr(a_addr), .p0_data(a_data), .p0_byte_en(), .p0_wr_len(a_len), .p0_wr_stream(a_str), .p0_q(a_q),
        .p0_wr_req(a_wr), .p0_rd_req(a_rd), .p0_end_burst_req(a_eb), .p0_available(a_av), .p0_ready(a_rdy), .p0_data_available(a_da),
        .wsrc_addr(a_wa), .wsrc_q(a_wq), .video_rgb(), .video_de(), .video_hs(), .video_vs(), .scan_vc(),
        .gfx_idle(a_idle), .gfx_fence(), .gfx_free(), .gfx_ovf(), .gfx_underflow());
    tau_gfx #(.BLIT_BLEND_ENABLE(0), .CBLIT_BURST(0)) dut_b (                                                           // B: the per-pixel path
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr), .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy), .cmd_glyph_hi(cmd_glyph_hi), .font_we(1'b0), .font_waddr(13'd0), .font_wdata(32'd0),
        .cmd_full(b_full), .text_light(1'b0),
        .blt_src_base(blt_src_base), .blt_src_stride(blt_src_stride), .blt_dst_base(blt_dst_base), .blt_dst_stride(blt_dst_stride),
        .blt_key_en(1'b0), .blt_key(16'd0), .blt_blend_en(1'b0), .blt_blend_mode(3'd0), .blt_blend_alpha(8'd0),
        .rc_cut_lut(80'd0), .blt_reindex(blt_reindex), .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
        .dbuf_cpu_buf(1'b0), .dbuf_flip_req_tgl(1'b0), .dbuf_disp_buf(), .dbuf_flip_pending(),
        .sdram_init_complete(1'b1), .p0_addr(b_addr), .p0_data(b_data), .p0_byte_en(), .p0_wr_len(b_len), .p0_wr_stream(b_str), .p0_q(b_q),
        .p0_wr_req(b_wr), .p0_rd_req(b_rd), .p0_end_burst_req(b_eb), .p0_available(b_av), .p0_ready(b_rdy), .p0_data_available(b_da),
        .wsrc_addr(b_wa), .wsrc_q(b_wq), .video_rgb(), .video_de(), .video_hs(), .video_vs(), .scan_vc(),
        .gfx_idle(b_idle), .gfx_fence(), .gfx_free(), .gfx_ovf(), .gfx_underflow());
    cb_sdram_stub mem_a (.clk(clk_sdram), .p0_addr(a_addr), .p0_data(a_data), .p0_wr_len(a_len), .p0_wr_stream(a_str), .p0_wr_req(a_wr), .p0_rd_req(a_rd),
        .p0_end_burst_req(a_eb), .p0_q(a_q), .p0_available(a_av), .p0_ready(a_rdy), .p0_data_available(a_da), .wsrc_addr(a_wa), .wsrc_q(a_wq));
    cb_sdram_stub mem_b (.clk(clk_sdram), .p0_addr(b_addr), .p0_data(b_data), .p0_wr_len(b_len), .p0_wr_stream(b_str), .p0_wr_req(b_wr), .p0_rd_req(b_rd),
        .p0_end_burst_req(b_eb), .p0_q(b_q), .p0_available(b_av), .p0_ready(b_rdy), .p0_data_available(b_da), .wsrc_addr(b_wa), .wsrc_q(b_wq));

    integer seed = 32'h13b1f7a3;
    function integer rnd(input integer n); rnd = $unsigned($random(seed)) % n; endfunction
    integer errors = 0, cmds_cblit = 0, cyc_a = 0, cyc_b = 0, ta0, ta1;
    integer sdr_cycles = 0; always @(posedge clk_sdram) sdr_cycles = sdr_cycles + 1;

    task wait_idle2(input integer max);
        integer n; begin n = 0; repeat (3) @(posedge clk_sys); while (!(a_idle && b_idle) && n < max) begin @(posedge clk_sys); n = n + 1; end end
    endtask
    // time each engine separately: record when each goes idle
    integer t_push, t_a, t_b;
    reg a_done, b_done;
    always @(posedge clk_sdram) begin
        if (cmd_push) begin a_done <= 0; b_done <= 0; t_push <= sdr_cycles; end
        if (!a_done && a_idle && sdr_cycles > t_push + 8) begin a_done <= 1; t_a <= sdr_cycles; end
        if (!b_done && b_idle && sdr_cycles > t_push + 8) begin b_done <= 1; t_b <= sdr_cycles; end
    end

    task push;
        begin @(posedge clk_sys); cmd_push <= 1'b1; @(posedge clk_sys); cmd_push <= 1'b0; end
    endtask
    integer i, j, w, h, ds, mism, rowbase;
    reg [18:0] da;
    initial begin
        repeat (6) @(posedge clk_sys); reset = 0; repeat (6) @(posedge clk_sys);
        for (i = 0; i < NCMD; i = i + 1) begin
            // new CLUT entries, re-index and bases (changed only while both are idle)
            if (rnd(2) == 0) begin repeat (rnd(40)) begin @(posedge clk_sys); clut_waddr <= rnd(256); clut_wdata <= rnd(65536); clut_wr <= 1; @(posedge clk_sys); clut_wr <= 0; end end
            blt_reindex <= (rnd(3) == 0) ? rnd(256) : 8'd0;
            blt_src_base <= rnd(1 << 19); blt_dst_base <= rnd(1 << 19);
            blt_src_stride <= (rnd(3) == 0) ? 1 + rnd(1023) : 10'd512; blt_dst_stride <= (rnd(3) == 0) ? 1 + rnd(1023) : 10'd512;
            w = (rnd(5) == 0) ? 1 + rnd(127) : 1 + rnd(40); h = 1 + rnd(8); da = rnd(1 << 18);
            cmd_addr <= da; cmd_w <= w; cmd_h <= h; cmd_fg <= {13'd0, 3'(rnd(8))}; cmd_bg <= rnd(65536);
            case (rnd(8))
                0: cmd_op <= 4'd1; 1: cmd_op <= 4'd3; 2: cmd_op <= 4'd4; 3: cmd_op <= 4'd5;
                default: cmd_op <= 4'd7;
            endcase
            cmd_glyph <= rnd(128); cmd_glyph_hi <= rnd(4);
            push;
            wait_idle2(200000);
            if (cmd_op == 4'd7) begin
                cmds_cblit = cmds_cblit + 1;
                if (w >= 32) begin cyc_a = cyc_a + (t_a - t_push); cyc_b = cyc_b + (t_b - t_push); end
                // the destination region of this command, compared word for word
                mism = 0;
                for (j = 0; j < h; j = j + 1) for (ds = 0; ds < w; ds = ds + 1) begin
                    rowbase = blt_dst_base + da + j * blt_dst_stride + ds;
                    if (mem_a.mem[rowbase[21:0]] !== mem_b.mem[rowbase[21:0]]) mism = mism + 1;
                end
                if (mism != 0) begin errors = errors + 1; if (errors < 6) $display("DIFF cblit %0d: %0d words differ (w=%0d h=%0d dst=%0d reindex=%0d)", i, mism, w, h, da, blt_reindex); end
            end
        end
        begin : full
            integer k, md; md = 0;
            for (k = 0; k < (1 << 22); k = k + 1) if (mem_a.mem[k] !== mem_b.mem[k]) md = md + 1;
            if (md != 0) begin errors = errors + 1; $display("DIFF memory: %0d words differ", md); end
        end
        $display("cblit commands compared: %0d; engine cycles for the wide ones (w>=32): burst %0d, per-pixel %0d (ratio %0d.%0d x faster)", cmds_cblit, cyc_a, cyc_b, cyc_b / (cyc_a == 0 ? 1 : cyc_a), (cyc_b * 10 / (cyc_a == 0 ? 1 : cyc_a)) % 10);
        if (errors == 0 && cmds_cblit > 150 && cyc_b > cyc_a * 3) $display("PASSED the burst CBLIT matches the per-pixel one and is more than 3x faster on wide rows");
        else $display("FAILED errors=%0d cblit=%0d", errors, cmds_cblit);
        $finish;
    end
endmodule
