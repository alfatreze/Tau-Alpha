// ============================================================================
// tb_tau_gfx_diff.v -- Talos 2 T2 differential test (D-T04, D-T07).
//
// The split graphics path (tau_gfx = tau_talos + tau_scanout) must behave exactly like the
// pre-split mp3_fb.sv, which is preserved as sim/oracle/tau_talos1_oracle.sv. The same random
// command/register stimulus drives both; each has its own functional SDRAM model; every output
// (SDRAM master port, streaming-write source, status, video) is compared on every clock edge
// of its domain, and the two SDRAM contents are compared at the end. Any difference fails.
//
//   make test-rtl-gfx-diff        (also runs the mutant: -Ptb_tau_gfx_diff.MUTATE=1 must FAIL)
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

// Functional SDRAM stand-in (same contract tb_mp3_fb.v's stub follows, plus real storage):
// p0_available high when idle, reads return stored words (default: address + 1), a streaming
// write pulls wsrc_q (wsrc_addr leads by one cycle), p0_ready pulses when a burst has retired.
module diff_sdram_stub (
    input  wire        clk,
    input  wire [24:0] p0_addr,
    input  wire [15:0] p0_data,
    input  wire [10:0] p0_wr_len,
    input  wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req,
    output reg  [15:0] p0_q = 0,
    output reg         p0_available = 0, p0_ready = 0, p0_data_available = 0,
    output reg  [10:0] wsrc_addr = 0,
    input  wire [15:0] wsrc_q
);
    reg [15:0] mem [0:(1<<22)-1];       // 4M words; addresses are folded into it
    integer k; initial for (k = 0; k < (1<<22); k = k + 1) mem[k] = k[15:0] ^ 16'h5A5A;
    localparam IDLE = 0, STREAM = 1, CONST = 2, DONE = 3, READ = 4;
    integer st = IDLE, beats = 0;
    reg [24:0] a;
    reg [10:0] len;
    reg        stream;
    reg [15:0] cdata;
    function [21:0] f(input [24:0] x); f = x[21:0] ^ {x[24:22], 19'd0}; endfunction
    always @(posedge clk) begin
        p0_ready <= 1'b0;
        p0_data_available <= 1'b0;
        case (st)
            IDLE: begin
                p0_available <= 1'b1;
                if (p0_rd_req) begin
                    p0_available <= 1'b0; a <= p0_addr; st <= READ;
                end else if (p0_wr_req) begin
                    p0_available <= 1'b0; a <= p0_addr; len <= p0_wr_len;
                    stream <= p0_wr_stream; cdata <= p0_data; wsrc_addr <= 11'd0; beats = 0;
                    st <= p0_wr_stream ? STREAM : CONST;
                end
            end
            STREAM: begin
                wsrc_addr <= wsrc_addr + 11'd1;
                if (wsrc_addr > 0) begin
                    mem[f(a + beats)] <= wsrc_q;
                    beats = beats + 1;
                end
                if (beats == len) st <= DONE;
            end
            READ: begin
                p0_data_available <= 1'b1;
                p0_q <= mem[f(a)] + 16'd1;
                a    <= a + 25'd1;
                if (p0_end_burst_req) st <= IDLE;
            end
            CONST: begin
                for (beats = 0; beats < len; beats = beats + 1) mem[f(a + beats)] <= cdata;
                st <= DONE;
            end
            DONE: begin p0_ready <= 1'b1; st <= IDLE; end
        endcase
    end
endmodule

module tb_tau_gfx_diff;
    parameter MUTATE  = 0;       // 1: tau_gfx's CPU commands are corrupted -> the bench must FAIL
    parameter FEATURES = 1;      // 1: BLIT_BLEND + DBUF + FONT_RAM built; 0: all off
    parameter SIM_US  = 18000;   // simulated microseconds (a frame is 16,667 us; vblank flips need > 15 ms)
    parameter NCMD    = 1500;

    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5    clk_sdram = ~clk_sdram;
    always #10   clk_sys   = ~clk_sys;
    always #41.7 clk_vid   = ~clk_vid;

    reg         cmd_push = 0;
    reg  [3:0]  cmd_op = 0;
    reg  [18:0] cmd_addr = 0;
    reg  [8:0]  cmd_w = 0, cmd_h = 0;
    reg  [15:0] cmd_fg = 16'hFFFF, cmd_bg = 16'h0000;
    reg  [6:0]  cmd_glyph = 0;
    reg  [1:0]  cmd_sx = 0, cmd_sy = 0, cmd_glyph_hi = 0;
    reg         font_we = 0; reg [12:0] font_waddr = 0; reg [31:0] font_wdata = 0;
    reg         text_light = 0;
    reg  [24:0] blt_src_base = 0, blt_dst_base = 0;
    reg  [9:0]  blt_src_stride = 512, blt_dst_stride = 512;
    reg         blt_key_en = 0; reg [15:0] blt_key = 0;
    reg         blt_blend_en = 0; reg [2:0] blt_blend_mode = 0; reg [7:0] blt_blend_alpha = 0;
    reg  [7:0]  blt_reindex = 0;
    reg         clut_wr = 0; reg [7:0] clut_waddr = 0; reg [15:0] clut_wdata = 0;
    reg  [79:0] rc_cut_lut = 0;
    reg         dbuf_cpu_buf = 0, dbuf_flip_req_tgl = 0;

    // DUT A: the split; DUT B: the preserved original
    wire        a_full, b_full, a_dd, b_dd, a_dfp, b_dfp;
    wire [24:0] a_addr, b_addr; wire [15:0] a_data, b_data; wire [1:0] a_be, b_be;
    wire [10:0] a_len, b_len;
    wire        a_str, b_str, a_wr, b_wr, a_rd, b_rd, a_eb, b_eb;
    wire [15:0] a_q, b_q; wire a_av, b_av, a_rdy, b_rdy, a_da, b_da;
    wire [10:0] a_wa, b_wa; wire [15:0] a_wq, b_wq;
    wire [23:0] a_rgb, b_rgb; wire a_de, b_de, a_hs, b_hs, a_vs, b_vs; wire [8:0] a_vc, b_vc;

    // The mutation corrupts only DUT A's view of the command op (a CHAR becomes a RECT), simulating
    // a split that mis-wires one input.
    wire [3:0] a_op = (MUTATE != 0 && cmd_op == 4'd2) ? 4'd1 : cmd_op;

    tau_gfx #(.BLIT_BLEND_ENABLE(FEATURES), .DBUF_ENABLE(FEATURES), .FONT_RAM_ENABLE(FEATURES)) dut_a (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(a_op), .cmd_addr(cmd_addr), .cmd_w(cmd_w), .cmd_h(cmd_h),
        .cmd_fg(cmd_fg), .cmd_bg(cmd_bg), .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy),
        .cmd_glyph_hi(cmd_glyph_hi), .font_we(font_we), .font_waddr(font_waddr), .font_wdata(font_wdata),
        .cmd_full(a_full), .text_light(text_light),
        .blt_src_base(blt_src_base), .blt_src_stride(blt_src_stride), .blt_dst_base(blt_dst_base),
        .blt_dst_stride(blt_dst_stride), .blt_key_en(blt_key_en), .blt_key(blt_key),
        .blt_blend_en(blt_blend_en), .blt_blend_mode(blt_blend_mode), .blt_blend_alpha(blt_blend_alpha),
        .rc_cut_lut(rc_cut_lut), .blt_reindex(blt_reindex),
        .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
        .dbuf_cpu_buf(dbuf_cpu_buf), .dbuf_flip_req_tgl(dbuf_flip_req_tgl),
        .dbuf_disp_buf(a_dd), .dbuf_flip_pending(a_dfp),
        .sdram_init_complete(1'b1), .p0_addr(a_addr), .p0_data(a_data), .p0_byte_en(a_be),
        .p0_wr_len(a_len), .p0_wr_stream(a_str), .p0_q(a_q), .p0_wr_req(a_wr), .p0_rd_req(a_rd),
        .p0_end_burst_req(a_eb), .p0_available(a_av), .p0_ready(a_rdy), .p0_data_available(a_da),
        .wsrc_addr(a_wa), .wsrc_q(a_wq),
        .video_rgb(a_rgb), .video_de(a_de), .video_hs(a_hs), .video_vs(a_vs), .scan_vc(a_vc));

    tau_talos1_oracle #(.BLIT_BLEND_ENABLE(FEATURES), .DBUF_ENABLE(FEATURES), .FONT_RAM_ENABLE(FEATURES)) dut_b (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr), .cmd_w(cmd_w), .cmd_h(cmd_h),
        .cmd_fg(cmd_fg), .cmd_bg(cmd_bg), .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy),
        .cmd_glyph_hi(cmd_glyph_hi), .font_we(font_we), .font_waddr(font_waddr), .font_wdata(font_wdata),
        .cmd_full(b_full), .text_light(text_light),
        .blt_src_base(blt_src_base), .blt_src_stride(blt_src_stride), .blt_dst_base(blt_dst_base),
        .blt_dst_stride(blt_dst_stride), .blt_key_en(blt_key_en), .blt_key(blt_key),
        .blt_blend_en(blt_blend_en), .blt_blend_mode(blt_blend_mode), .blt_blend_alpha(blt_blend_alpha),
        .rc_cut_lut(rc_cut_lut), .blt_reindex(blt_reindex),
        .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
        .dbuf_cpu_buf(dbuf_cpu_buf), .dbuf_flip_req_tgl(dbuf_flip_req_tgl),
        .dbuf_disp_buf(b_dd), .dbuf_flip_pending(b_dfp),
        .sdram_init_complete(1'b1), .p0_addr(b_addr), .p0_data(b_data), .p0_byte_en(b_be),
        .p0_wr_len(b_len), .p0_wr_stream(b_str), .p0_q(b_q), .p0_wr_req(b_wr), .p0_rd_req(b_rd),
        .p0_end_burst_req(b_eb), .p0_available(b_av), .p0_ready(b_rdy), .p0_data_available(b_da),
        .wsrc_addr(b_wa), .wsrc_q(b_wq),
        .video_rgb(b_rgb), .video_de(b_de), .video_hs(b_hs), .video_vs(b_vs), .scan_vc(b_vc));

    diff_sdram_stub mem_a (.clk(clk_sdram), .p0_addr(a_addr), .p0_data(a_data), .p0_wr_len(a_len),
        .p0_wr_stream(a_str), .p0_wr_req(a_wr), .p0_rd_req(a_rd), .p0_end_burst_req(a_eb),
        .p0_q(a_q), .p0_available(a_av), .p0_ready(a_rdy), .p0_data_available(a_da),
        .wsrc_addr(a_wa), .wsrc_q(a_wq));
    diff_sdram_stub mem_b (.clk(clk_sdram), .p0_addr(b_addr), .p0_data(b_data), .p0_wr_len(b_len),
        .p0_wr_stream(b_str), .p0_wr_req(b_wr), .p0_rd_req(b_rd), .p0_end_burst_req(b_eb),
        .p0_q(b_q), .p0_available(b_av), .p0_ready(b_rdy), .p0_data_available(b_da),
        .wsrc_addr(b_wa), .wsrc_q(b_wq));

    // ---- comparison ---------------------------------------------------------------------------
    integer diffs = 0, sdram_cycles = 0, vid_cycles = 0, wr_seen = 0, rd_seen = 0, strm_seen = 0;
    always @(negedge clk_sdram) if (!reset) begin
        sdram_cycles = sdram_cycles + 1;
        if (a_wr) wr_seen = wr_seen + 1;
        if (a_rd) rd_seen = rd_seen + 1;
        if (a_str) strm_seen = strm_seen + 1;
        if ({a_addr, a_data, a_be, a_len, a_str, a_wr, a_rd, a_eb, a_wa, a_wq, a_dd, a_dfp} !==
            {b_addr, b_data, b_be, b_len, b_str, b_wr, b_rd, b_eb, b_wa, b_wq, b_dd, b_dfp}) begin
            if (diffs < 10) $display("DIFF sdram t=%0t a:{%h %h %b %0d s%b w%b r%b e%b wa%0d wq%h d%b f%b} b:{%h %h %b %0d s%b w%b r%b e%b wa%0d wq%h d%b f%b}", $time,
                a_addr, a_data, a_be, a_len, a_str, a_wr, a_rd, a_eb, a_wa, a_wq, a_dd, a_dfp,
                b_addr, b_data, b_be, b_len, b_str, b_wr, b_rd, b_eb, b_wa, b_wq, b_dd, b_dfp);
            diffs = diffs + 1;
        end
    end
    always @(negedge clk_sys) if (!reset && a_full !== b_full) begin
        if (diffs < 10) $display("DIFF cmd_full t=%0t a=%b b=%b", $time, a_full, b_full);
        diffs = diffs + 1;
    end
    integer de_seen = 0, flips_seen = 0;
    reg dd_prev = 0;
    always @(negedge clk_sdram) begin dd_prev <= a_dd; if (a_dd !== dd_prev && !reset) flips_seen = flips_seen + 1; end
    always @(negedge clk_vid) if (!reset) begin
        vid_cycles = vid_cycles + 1;
        if (a_de) de_seen = de_seen + 1;
        if ({a_rgb, a_de, a_hs, a_vs, a_vc} !== {b_rgb, b_de, b_hs, b_vs, b_vc}) begin
            if (diffs < 10) $display("DIFF video t=%0t a:{%h %b %b %b %0d} b:{%h %b %b %b %0d}", $time,
                a_rgb, a_de, a_hs, a_vs, a_vc, b_rgb, b_de, b_hs, b_vs, b_vc);
            diffs = diffs + 1;
        end
    end

    // ---- stimulus (identical for both) ------------------------------------------------------
    integer seed = 32'h7a1a5a11;
    function integer rnd(input integer n); rnd = $unsigned($random(seed)) % n; endfunction

    task push_one;
        reg [3:0] op;
        begin
            op = rnd(9);
            @(posedge clk_sys);
            while (a_full || b_full) @(posedge clk_sys);
            cmd_op <= op;
            cmd_addr <= (rnd(8) == 0) ? rnd(19'h7FFFF) : (rnd(360) * 512 + rnd(380));
            cmd_w <= (rnd(6) == 0) ? rnd(512) : rnd(100);
            cmd_h <= (rnd(6) == 0) ? rnd(40) : rnd(10);
            cmd_fg <= rnd(65536); cmd_bg <= rnd(65536);
            cmd_glyph <= rnd(128); cmd_glyph_hi <= rnd(4);
            cmd_sx <= rnd(4); cmd_sy <= rnd(4);
            cmd_push <= 1'b1;
            @(posedge clk_sys);
            cmd_push <= 1'b0;
        end
    endtask

    task perturb;
        begin
            case (rnd(12))
                0: begin blt_src_base <= rnd(1<<22); blt_dst_base <= rnd(1<<22);
                         blt_src_stride <= 1 + rnd(1023); blt_dst_stride <= 1 + rnd(1023); end
                1: begin blt_key_en <= rnd(2); blt_key <= rnd(65536); end
                2: begin blt_blend_en <= rnd(2); blt_blend_mode <= rnd(5); blt_blend_alpha <= rnd(256); end
                3: blt_reindex <= rnd(256);
                4: begin clut_waddr <= rnd(256); clut_wdata <= rnd(65536); clut_wr <= 1; @(posedge clk_sys); clut_wr <= 0; end
                5: rc_cut_lut <= {rnd(65536), rnd(65536), rnd(65536), rnd(65536), rnd(65536)};
                6: text_light <= rnd(2);
                7: dbuf_cpu_buf <= rnd(2);
                8: dbuf_flip_req_tgl <= ~dbuf_flip_req_tgl;
                9: begin font_waddr <= rnd(6112); font_wdata <= $random(seed); font_we <= 1; @(posedge clk_sys); font_we <= 0; end
                default: ;
            endcase
        end
    endtask

    integer n;
    initial begin
        repeat (8) @(posedge clk_sys);
        reset = 0;
        repeat (4) @(posedge clk_sys);
        for (n = 0; n < NCMD; n = n + 1) begin
            if (rnd(3) == 0) perturb;
            push_one;
            if (rnd(10) == 0) repeat (rnd(400)) @(posedge clk_sys);   // idle gaps let FILL and flips interleave
        end
        #(SIM_US * 1000.0);
        // contents: every written word must agree (sampled through each model's own memory)
        begin : cmp
            integer w, md;
            md = 0;
            for (w = 0; w < (1<<22); w = w + 1) if (mem_a.mem[w] !== mem_b.mem[w]) md = md + 1;
            if (md != 0) begin $display("DIFF memory: %0d words differ", md); diffs = diffs + 1; end
        end
        $display("coverage: sdram_cycles=%0d vid_cycles=%0d wr_req=%0d rd_req=%0d stream_wr=%0d de=%0d flips=%0d",
                 sdram_cycles, vid_cycles, wr_seen, rd_seen, strm_seen, de_seen, flips_seen);
        if (diffs == 0 && wr_seen > 1000 && rd_seen > 100 && strm_seen > 100 && de_seen > 100000 && (FEATURES == 0 || flips_seen > 0))
            $display("PASSED tau_gfx matches the preserved original (FEATURES=%0d)", FEATURES);
        else
            $display("FAILED diffs=%0d (or too little coverage)", diffs);
        $finish;
    end
endmodule
