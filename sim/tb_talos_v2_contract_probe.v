// ============================================================================
// tb_talos_v2_contract_probe.v -- ORACLE-SIDE probe for the Talos v2 behavioural contract
// (docs/features/TALOS_ENGINE_V2_CONTRACT.md, audit B-1178). It runs the CURRENT engine (tau_talos, or
// Tau Gallery's copy with -DGAL) on a functional SDRAM stub and prints facts the contract states:
// engine cycles per command (dispatch to retire), what memory a command writes, and a few edge rules.
// It is NOT a conformance bench and must NOT be given to the clean-room author (it instantiates the old
// engine and reads two of its internal signals to time a command: inflight and retire).
//
//   iverilog -g2012 -Isrc/fpga/core -o build/rtl/probe.vvp sim/tb_talos_v2_contract_probe.v \
//       src/fpga/core/tau_talos.sv src/fpga/core/tau_cdc_gray_ctr.sv src/fpga/core/font_rom.v src/fpga/core/font_ram.v
//   vvp build/rtl/probe.vvp
// Gallery (read-only clone, nothing written into it):
//   iverilog -g2012 -DGAL -o build/rtl/probe_gal.vvp sim/tb_talos_v2_contract_probe.v <clone>/src/fpga/core/tau_talos.sv \
//       src/fpga/core/tau_cdc_gray_ctr.sv src/fpga/core/font_rom.v src/fpga/core/font_ram.v
//
// The stub: reads return one word per clock from the cycle after the request until end-of-burst; a constant
// write takes one clock plus a done clock; a streamed write one clock per word. Cycle counts are therefore
// RELATIVE (the same stub must be used for the old and the new engine); they are not the Pocket's figures.
// ============================================================================
`timescale 1ns/1ps
`default_nettype none
module probe_sdram_stub (
    input  wire        clk,
    input  wire [24:0] p0_addr, input wire [15:0] p0_data, input wire [10:0] p0_wr_len,
    input  wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req,
    output reg  [15:0] p0_q = 0, output reg p0_available = 0, p0_ready = 0, p0_data_available = 0,
    output reg  [10:0] wsrc_addr = 0, input wire [15:0] wsrc_q
);
    reg [15:0] mem [0:(1<<21)-1];
    integer nrd = 0, nwr = 0, nwords = 0;
    integer wlo = 32'h7fffffff, whi = -1;           // lowest / highest word written since the last clear
    integer last_len = 0;
    task note(input integer ad); begin if (ad < wlo) wlo = ad; if (ad > whi) whi = ad; nwords = nwords + 1; end endtask
    localparam IDLE = 0, STREAM = 1, CONST = 2, DONE = 3, READ = 4;
    integer st = IDLE, beats = 0;
    reg [24:0] a; reg [10:0] len; reg [15:0] cdata;
    always @(posedge clk) begin
        p0_ready <= 1'b0; p0_data_available <= 1'b0;
        case (st)
            IDLE: begin
                p0_available <= 1'b1;
                if (p0_rd_req) begin p0_available <= 1'b0; a <= p0_addr; st <= READ; nrd = nrd + 1; end
                else if (p0_wr_req) begin p0_available <= 1'b0; a <= p0_addr; len <= p0_wr_len; last_len = p0_wr_len; cdata <= p0_data; wsrc_addr <= 0; beats = 0; nwr = nwr + 1; st <= p0_wr_stream ? STREAM : CONST; end
            end
            STREAM: begin wsrc_addr <= wsrc_addr + 1; if (wsrc_addr > 0) begin mem[(a + beats) & 25'h1FFFFF] <= wsrc_q; note((a + beats) & 25'h1FFFFF); beats = beats + 1; end if (beats == len) st <= DONE; end
            READ: begin p0_data_available <= 1'b1; p0_q <= mem[a & 25'h1FFFFF]; a <= a + 25'd1; if (p0_end_burst_req) st <= IDLE; end
            CONST: begin for (beats = 0; beats < len; beats = beats + 1) begin mem[(a + beats) & 25'h1FFFFF] <= cdata; note((a + beats) & 25'h1FFFFF); end st <= DONE; end
            DONE: begin p0_ready <= 1'b1; st <= IDLE; end
        endcase
    end
endmodule

module tb_talos_v2_contract_probe;
    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5 clk_sdram = ~clk_sdram; always #7.5 clk_sys = ~clk_sys; always #41.7 clk_vid = ~clk_vid;
    reg cmd_push = 0; reg [3:0] cmd_op = 0; reg [18:0] cmd_addr = 0; reg [8:0] cmd_w = 0, cmd_h = 0; reg [15:0] cmd_fg = 0, cmd_bg = 0;
    reg [6:0] cmd_glyph = 0; reg [1:0] cmd_sx = 0, cmd_sy = 0, cmd_glyph_hi = 0;
    reg [24:0] src_base = 0, dst_base = 0; reg [9:0] src_stride = 512, dst_stride = 512;
    reg key_en = 0; reg [15:0] key = 0; reg blend_en = 0; reg [2:0] blend_mode = 0; reg [7:0] alpha = 0; reg [7:0] reindex = 0;
    reg [79:0] rc_lut = 0; reg clut_wr = 0; reg [7:0] clut_waddr = 0; reg [15:0] clut_wdata = 0;
    reg cpu_buf = 0; reg fill_tgl = 0; reg [9:0] fill_line = 0;
    wire cmd_full, idle; wire [31:0] fence; wire [8:0] free; wire [15:0] ovf;
    wire [24:0] p0_addr; wire [15:0] p0_data; wire [10:0] p0_len; wire p0_str, p0_wr, p0_rd, p0_eb;
    wire [15:0] p0_q; wire p0_av, p0_rdy, p0_da; wire [10:0] wsa; wire [15:0] wsq; wire is_fill; wire [9:0] rd_len;
    wire lb_we; wire [9:0] lb_waddr; wire [15:0] lb_wdata; wire fdi; wire [9:0] fdl; wire fseen;
    tau_talos #(.BLIT_BLEND_ENABLE(1), .DBUF_ENABLE(1), .FONT_RAM_ENABLE(0)) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr), .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy), .cmd_glyph_hi(cmd_glyph_hi),
        .font_we(1'b0), .font_waddr(13'd0), .font_wdata(32'd0), .cmd_full(cmd_full), .text_light(1'b0),
        .blt_src_base(src_base), .blt_src_stride(src_stride), .blt_dst_base(dst_base), .blt_dst_stride(dst_stride),
        .blt_key_en(key_en), .blt_key(key), .blt_blend_en(blend_en), .blt_blend_mode(blend_mode), .blt_blend_alpha(alpha),
        .rc_cut_lut(rc_lut), .blt_reindex(reindex), .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
        .dbuf_cpu_buf(cpu_buf), .dbuf_flip_req_tgl(1'b0), .dbuf_disp_buf(), .dbuf_flip_pending(),
        .sdram_init_complete(1'b1), .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(), .p0_wr_len(p0_len), .p0_wr_stream(p0_str), .p0_q(p0_q),
        .p0_wr_req(p0_wr), .p0_rd_req(p0_rd), .p0_end_burst_req(p0_eb), .p0_available(p0_av), .p0_ready(p0_rdy), .p0_data_available(p0_da),
        .wsrc_addr(wsa), .wsrc_q(wsq), .p0_is_fill(is_fill), .p0_rd_len(rd_len),
        .fill_req_tgl(fill_tgl), .fill_line_req(fill_line), .vblank_tgl(1'b0),
        .lb_we(lb_we), .lb_waddr(lb_waddr), .lb_wdata(lb_wdata), .fill_done_inc(fdi), .fill_done_line(fdl), .fill_seen(fseen),
        .gfx_idle(idle), .gfx_fence(fence), .gfx_free(free), .gfx_ovf(ovf)
`ifdef GAL
        , .view_en(1'b0), .view_base(25'd0), .view_stride(16'd1200), .view_dbl(1'b0), .vlb_we(), .vlb_waddr(), .vlb_wdata(), .view_active()
`endif
    );
    probe_sdram_stub mem (.clk(clk_sdram), .p0_addr(p0_addr), .p0_data(p0_data), .p0_wr_len(p0_len), .p0_wr_stream(p0_str), .p0_wr_req(p0_wr), .p0_rd_req(p0_rd),
        .p0_end_burst_req(p0_eb), .p0_q(p0_q), .p0_available(p0_av), .p0_ready(p0_rdy), .p0_data_available(p0_da), .wsrc_addr(wsa), .wsrc_q(wsq));

    // ---- timing of one command: clk_sdram cycles with the engine's in-flight flag set (dispatch to retire) ----
    integer busy = 0, maxbusy = 0;
    always @(posedge clk_sdram) if (dut.inflight) busy = busy + 1;
    // ---- the longest wait of a scanout fill: request edge seen by the engine to the fill's read request ----
    integer fwait = -1, fwait_max = 0, fills = 0;
    reg [2:0] fs = 0;
    always @(posedge clk_sdram) begin
        fs <= {fs[1:0], fill_tgl};
        if (fs[2] ^ fs[1]) fwait = 0; else if (fwait >= 0) fwait = fwait + 1;
        if (p0_rd && is_fill) begin if (fwait > fwait_max) fwait_max = fwait; fwait = -1; fills = fills + 1; end
    end

    integer errors = 0;
    task check(input cond, input [511:0] what); begin if (!cond) begin errors = errors + 1; $display("FAIL %0s", what); end end endtask

    task push(input [3:0] op, input [18:0] ad, input [8:0] w, input [8:0] h, input [6:0] g, input [1:0] sx, input [1:0] sy, input [1:0] ghi);
        begin
            @(posedge clk_sys); while (cmd_full) @(posedge clk_sys);
            cmd_op <= op; cmd_addr <= ad; cmd_w <= w; cmd_h <= h; cmd_glyph <= g; cmd_sx <= sx; cmd_sy <= sy; cmd_glyph_hi <= ghi; cmd_push <= 1'b1;
            @(posedge clk_sys); cmd_push <= 1'b0;
        end
    endtask
    task wait_idle; begin repeat (8) @(posedge clk_sys); while (!idle) @(posedge clk_sys); repeat (4) @(posedge clk_sdram); end endtask
    task clear; begin wait_idle; busy = 0; mem.nrd = 0; mem.nwr = 0; mem.nwords = 0; mem.wlo = 32'h7fffffff; mem.whi = -1; end endtask
    // run one command alone and print its cost
    task one(input [255:0] name, input [3:0] op, input [18:0] ad, input [8:0] w, input [8:0] h, input [6:0] g, input [1:0] sx, input [1:0] sy, input [1:0] ghi);
        begin
            clear; push(op, ad, w, h, g, sx, sy, ghi); wait_idle;
            $display("COST %-28s busy=%0d reads=%0d writes=%0d words=%0d span=[%0d..%0d]", name, busy, mem.nrd, mem.nwr, mem.nwords, mem.wlo, mem.whi);
        end
    endtask
    function [15:0] rd(input integer ad); rd = mem.mem[ad]; endfunction

    integer i, j, k, d, e, bad; reg [15:0] p, q, r0;
    initial begin
        for (k = 0; k < (1<<21); k = k + 1) mem.mem[k] = k[15:0] * 16'h9E37 + 16'h1234;
        for (k = 0; k < 256; k = k + 1) dut.clut[k] = 16'hC000 | k;     // a known palette (oracle-side shortcut, not a CPU load)
        repeat (10) @(posedge clk_sdram); reset = 0; repeat (20) @(posedge clk_sdram);
        cmd_fg = 16'hF800; cmd_bg = 16'h001F;

        // ---------------- 1. cost per command class (no scanout fills) ----------------
        one("FENCE", 4'd9, 0, 0, 0, 0, 0, 0, 0);
        one("RECT 1x1", 4'd1, 19'd1000, 1, 1, 0, 0, 0, 0);
        one("RECT 16x16", 4'd1, 19'd1000, 16, 16, 0, 0, 0, 0);
        one("RECT 127x16", 4'd1, 19'd1000, 127, 16, 0, 0, 0, 0);
        one("RECT 400x1", 4'd1, 19'd1024, 400, 1, 0, 0, 0, 0);
        one("RECT 511x1", 4'd1, 19'd1024, 511, 1, 0, 0, 0, 0);
        one("RECT 400x360", 4'd1, 19'd0, 400, 360, 0, 0, 0, 0);
        one("RUN 100", 4'd0, 19'd2048, 100, 7, 0, 0, 0, 0);
        one("CHAR 1x 'A'", 4'd2, 19'd4096, 0, 0, 7'h41, 0, 0, 0);
        one("CHAR 1.5x 'A'", 4'd2, 19'd4096, 0, 0, 7'h41, 1, 1, 0);
        one("CHAR 2x 'A'", 4'd2, 19'd4096, 0, 0, 7'h41, 2, 2, 0);
        one("CHAR 3x 'A'", 4'd2, 19'd4096, 0, 0, 7'h41, 3, 3, 0);
        one("BAR 8x100 lit 50", 4'd5, 19'd8192, 8, 100, 7'd50, 0, 0, 0);
        cmd_fg = 16'd0; cmd_bg = 16'd40960;                                   // source offset {fg[2:0], bg} = 40960 (row 80)
        one("COPY 127x16", 4'd3, 19'd60000, 127, 16, 0, 0, 0, 0);
        src_base = 25'd300000; dst_base = 25'd0;
        one("BLIT 127x16", 4'd4, 19'd60000, 127, 16, 0, 0, 0, 0);
        one("BLIT 16x16", 4'd4, 19'd60000, 16, 16, 0, 0, 0, 0);
        blend_en = 1; alpha = 8'd128;
        one("BLIT 127x16 blend", 4'd4, 19'd60000, 127, 16, 0, 0, 0, 0);
        blend_en = 0; key_en = 1; key = 16'h1234;
        one("BLIT 127x16 key", 4'd4, 19'd60000, 127, 16, 0, 0, 0, 0);
        key_en = 0;
        one("SBLIT 40x40 x1", 4'd6, 19'd60000, 40, 40, 0, 0, 0, 0);
        one("SBLIT 40x40 x2", 4'd6, 19'd60000, 40, 40, 0, 2, 2, 0);
        one("SBLIT 40x40 x3", 4'd6, 19'd60000, 40, 40, 0, 3, 3, 0);
        one("CBLIT 127x16", 4'd7, 19'd60000, 127, 16, 0, 0, 0, 0);
        rc_lut = 0; for (k = 1; k <= 8; k = k + 1) rc_lut[k*5 +: 5] = 9 - k;   // cut(dy) = 9 - dy for dy 1..8
        one("RRECT 100x50 r8", 4'd8, 19'd70000, 100, 50, 7'd8, 0, 0, 0);
        one("RECT 100x50 (RRECT base)", 4'd1, 19'd70000, 100, 50, 0, 0, 0, 0);

        // 1b. fence round trip seen by the CPU: clk_sys cycles from the push of a FENCE (engine idle) to R_GFX_FENCE moving,
        //     and from the push of a 1 x 1 RECT to idle reading 1 again
        clear; d = fence; e = 0; push(4'd9, 0, 0, 0, 0, 0, 0, 0); while (fence == d) begin @(posedge clk_sys); e = e + 1; end
        $display("LAT FENCE push to fence counter +1: %0d clk_sys cycles", e);
        clear; e = 0; push(4'd1, 19'd1000, 1, 1, 0, 0, 0, 0); @(posedge clk_sys); check(!idle, "idle reads busy one cycle after a push");
        while (!idle) begin @(posedge clk_sys); e = e + 1; end
        $display("LAT RECT 1x1 push to idle: %0d clk_sys cycles", e + 1);

        // ---------------- 2. edge rules ----------------
        // 2a. unknown opcodes 10..15 on the Tau Alpha engine: what do they write?
`ifndef GAL
        for (k = 10; k <= 15; k = k + 1) begin
            clear; cmd_fg = 16'hABCD; push(k[3:0], 19'd90000, 9'd5, 9'd3, 0, 0, 0, 0); wait_idle;
            $display("EDGE op %0d: writes=%0d words=%0d span=[%0d..%0d] word0=%04x", k, mem.nwr, mem.nwords, mem.wlo, mem.whi, rd(90000));
            check(mem.nwords == 5 && rd(90000) == 16'hABCD && rd(90004) == 16'hABCD, "unknown opcode draws one row of w in fg (as RUN)");
        end
`endif
        // 2b. zero sizes: nothing written, still retires
        clear; push(4'd1, 19'd91000, 0, 5, 0, 0, 0, 0); push(4'd1, 19'd91000, 5, 0, 0, 0, 0, 0); push(4'd4, 19'd91000, 5, 0, 0, 0, 0, 0);
        push(4'd6, 19'd91000, 0, 5, 0, 0, 0, 0); push(4'd7, 19'd91000, 5, 0, 0, 0, 0, 0); push(4'd5, 19'd91000, 0, 5, 7'd2, 0, 0, 0); push(4'd5, 19'd91000, 5, 0, 7'd2, 0, 0, 0);
        push(4'd8, 19'd91000, 0, 5, 7'd2, 0, 0, 0); push(4'd0, 19'd91000, 0, 5, 0, 0, 0, 0); push(4'd3, 19'd91000, 0, 5, 0, 0, 0, 0); wait_idle;
        $display("EDGE zero sizes (10 commands): reads=%0d writes=%0d", mem.nrd, mem.nwr);
        check(mem.nrd == 0 && mem.nwr == 0, "zero width or height: no memory access");
        // 2c. BAR lit above h is clamped to h; lit counts 9 bits
        clear; cmd_fg = 16'h07E0; cmd_bg = 16'h0010; push(4'd5, 19'd92000, 4, 10, 7'd44, 0, 0, 2'd1); wait_idle;   // lit = 128 + 44 = 172 > h = 10
        bad = 0; for (j = 0; j < 10; j = j + 1) if (rd(92000 + j * 512) != 16'h07E0) bad = bad + 1;
        $display("EDGE BAR h=10 lit=172: rows not lit=%0d words=%0d", bad, mem.nwords);
        check(bad == 0 && mem.nwords == 40, "BAR lit > h: all h rows lit, nothing above");
        clear; push(4'd5, 19'd93000, 2, 200, 7'd2, 0, 0, 2'd1); wait_idle;   // lit = 130 of h = 200: 70 unlit rows on top
        check(rd(93000 + 69 * 512) == 16'h0010 && rd(93000 + 70 * 512) == 16'h07E0 && rd(93000 + 199 * 512) == 16'h07E0, "BAR lit 130 of 200: rows 0..69 bg, 70..199 fg");
        // 2d. SBLIT and CBLIT ignore the sticky blend and key; BLIT honours them
        src_base = 25'd300000; dst_base = 25'd0; cmd_fg = 0; cmd_bg = 0;
        clear; push(4'd6, 19'd100000, 30, 4, 0, 2, 2, 0); wait_idle; r0 = rd(100000 + 512 + 7);
        for (i = 0; i < 60; i = i + 1) mem.mem[110000 + i] = rd(100000 + i);
        blend_en = 1; alpha = 8'd64; key_en = 1; key = rd(300000);
        clear; push(4'd6, 19'd100000, 30, 4, 0, 2, 2, 0); wait_idle;
        bad = 0; for (i = 0; i < 60; i = i + 1) if (rd(100000 + i) != mem.mem[110000 + i]) bad = bad + 1;
        $display("EDGE SBLIT with blend+key on: %0d of 60 words differ from the plain run, reads=%0d", bad, mem.nrd);
        check(bad == 0, "SBLIT ignores blend and key");
        blend_en = 0; key_en = 0;
        clear; push(4'd7, 19'd101000, 20, 2, 0, 0, 0, 0); wait_idle; p = rd(101000 + 3);
        blend_en = 1; key_en = 1; key = rd(300003);
        clear; push(4'd7, 19'd101000, 20, 2, 0, 0, 0, 0); wait_idle;
        $display("EDGE CBLIT with blend+key on: word3 %04x vs plain %04x (expect equal), reads=%0d", rd(101000 + 3), p, mem.nrd);
        check(rd(101000 + 3) == p && p == (16'hC000 | (mem.mem[300003] & 16'hFF)), "CBLIT ignores blend and key; out = CLUT[src low byte]");
        blend_en = 0; key_en = 0;
        // 2e. CBLIT re-index wraps mod 256
        reindex = 8'd200; clear; push(4'd7, 19'd102000, 4, 1, 0, 0, 0, 0); wait_idle; reindex = 0;
        check(rd(102000) == (16'hC000 | ((mem.mem[300000] + 200) & 16'hFF)), "CBLIT re-index: CLUT[(index + offset) mod 256]");
        // 2f. blend arithmetic: out = (src*a + dst*(256-a)) >> 8 per 5/6/5 channel, a = 0, 1, 255
        for (k = 0; k < 3; k = k + 1) begin
            alpha = (k == 0) ? 8'd0 : (k == 1) ? 8'd1 : 8'd255; blend_en = 1;
            for (i = 0; i < 8; i = i + 1) begin mem.mem[300000 + i] = 16'hFFFF - i * 16'h1111; mem.mem[103000 + i] = 16'h0841 * i; end
            clear; push(4'd4, 19'd103000, 8, 1, 0, 0, 0, 0); wait_idle;
            bad = 0;
            for (i = 0; i < 8; i = i + 1) begin
                p = 16'hFFFF - i * 16'h1111; q = 16'h0841 * i;
                d = (p[15:11] * alpha + q[15:11] * (256 - alpha)) >> 8; e = (p[10:5] * alpha + q[10:5] * (256 - alpha)) >> 8; j = (p[4:0] * alpha + q[4:0] * (256 - alpha)) >> 8;
                r0 = {d[4:0], e[5:0], j[4:0]};
                if (rd(103000 + i) != r0) bad = bad + 1;
            end
            $display("EDGE blend a=%0d: %0d of 8 words differ from (f*a + b*(256-a)) >> 8", alpha, bad);
            check(bad == 0, "blend formula");
        end
        blend_en = 0;
        // 2g. key: a source word equal to the key leaves the destination word
        for (i = 0; i < 8; i = i + 1) begin mem.mem[300000 + i] = (i == 3) ? 16'h5555 : 16'h1000 + i; mem.mem[104000 + i] = 16'hEEEE; end
        key_en = 1; key = 16'h5555; clear; push(4'd4, 19'd104000, 8, 1, 0, 0, 0, 0); wait_idle; key_en = 0;
        check(rd(104003) == 16'hEEEE && rd(104002) == 16'h1002 && rd(104004) == 16'h1004, "key: keyed word keeps destination");
        // key + blend: keyed word kept, others blended
        for (i = 0; i < 8; i = i + 1) begin mem.mem[300000 + i] = (i == 3) ? 16'h5555 : 16'hFFFF; mem.mem[104100 + i] = 16'h0000; end
        key_en = 1; key = 16'h5555; blend_en = 1; alpha = 8'd128; clear; push(4'd4, 19'd104100, 8, 1, 0, 0, 0, 0); wait_idle; key_en = 0; blend_en = 0;
        $display("EDGE key+blend: keyed %04x (expect 0000), other %04x (expect 7BEF)", rd(104103), rd(104100));
        check(rd(104103) == 16'h0000 && rd(104100) == 16'h7BEF, "key has priority over blend");
        // 2h. blit-class widths above 127 (outside the shipped domain): what is written?
        for (k = 0; k < 3; k = k + 1) begin
            d = (k == 0) ? 128 : (k == 1) ? 200 : 255;
            clear; push(4'd4, 19'd120000, d[8:0], 9'd2, 0, 0, 0, 0); wait_idle;
            $display("EDGE BLIT w=%0d h=2: writes=%0d words=%0d span=[%0d..%0d] last_len=%0d reads=%0d", d, mem.nwr, mem.nwords, mem.wlo, mem.whi, mem.last_len, mem.nrd);
        end
        // 2i. double buffer: RECT across the visible/stash boundary with the CPU on buffer 1; blit-class ignores it
        cpu_buf = 1; repeat (4) @(posedge clk_sdram);
        clear; cmd_fg = 16'h1357; push(4'd1, 19'd358 * 512 + 10, 9'd3, 9'd4, 0, 0, 0, 0); wait_idle;
        $display("EDGE DBUF RECT rows 358..361 on buffer 1: span=[%0d..%0d]", mem.wlo, mem.whi);
        check(rd(1048576 + 358 * 512 + 10) == 16'h1357 && rd(1048576 + 359 * 512 + 12) == 16'h1357 && rd(360 * 512 + 10) == 16'h1357 && rd(361 * 512 + 12) == 16'h1357,
              "DBUF: visible rows shift to buffer 1 per row, stash rows do not");
        clear; push(4'd5, 19'd1000, 9'd2, 9'd2, 7'd1, 0, 0, 0); wait_idle;
        check(mem.wlo >= 1048576, "DBUF: BAR follows the CPU buffer");
        clear; push(4'd4, 19'd1000, 9'd2, 9'd2, 0, 0, 0, 0); wait_idle;
        check(mem.whi < 1048576, "DBUF: BLIT ignores the CPU buffer (sticky bases only)");
        clear; cmd_fg = 0; cmd_bg = 16'd2000; push(4'd3, 19'd5000, 9'd4, 9'd1, 0, 0, 0, 0); wait_idle;
        check(mem.wlo == 1048576 + 5000 && rd(1048576 + 5000) == rd(1048576 + 2000), "DBUF: COPY reads and writes the CPU buffer");
        cpu_buf = 0; repeat (4) @(posedge clk_sdram);
        // 2j. CHAR geometry: cell width and rows written per scale
        for (k = 0; k < 4; k = k + 1) begin
            clear; push(4'd2, 19'd130000, 9'd77, 9'd77, 7'h41, k[1:0], k[1:0], 0); wait_idle;
            $display("EDGE CHAR scale sel %0d: writes(rows)=%0d words=%0d (w,h fields ignored)", k, mem.nwr, mem.nwords);
        end
        // 2k. SBLIT output clamp: 60 x 60 at x3 -> 127 x 127
        clear; push(4'd6, 19'd140000, 9'd60, 9'd60, 0, 2'd3, 2'd3, 0); wait_idle;
        $display("EDGE SBLIT 60x60 x3: rows=%0d words=%0d (clamped to 127 x 127 = 16129)", mem.nwr, mem.nwords);
        check(mem.nwr == 127 && mem.nwords == 127 * 127, "SBLIT clamps output columns AND rows to 127");
        clear; push(4'd6, 19'd140000, 9'd5, 9'd3, 0, 2'd1, 2'd1, 0); wait_idle;
        $display("EDGE SBLIT 5x3 sel 1 (1.5x): rows=%0d words=%0d (expect 4 rows of 7)", mem.nwr, mem.nwords);

        // ---------------- 3. scanout fill wait under a mixed command load ----------------
        fwait_max = 0; fills = 0; src_base = 25'd300000;
        fork
            begin : fillgen
                forever begin repeat (4167) @(posedge clk_sdram); fill_line = (fill_line + 1) % 360; fill_tgl = ~fill_tgl; end
            end
            begin
                for (k = 0; k < 40; k = k + 1) begin
                    push(4'd1, 19'd0, 9'd511, 9'd20, 0, 0, 0, 0);
                    push(4'd4, 19'd200000, 9'd127, 9'd20, 0, 0, 0, 0);
                    push(4'd2, 19'd4096, 0, 0, 7'h57, 3, 3, 0);
                    push(4'd6, 19'd60000, 9'd40, 9'd10, 0, 3, 3, 0);
                    push(4'd7, 19'd60000, 9'd127, 9'd10, 0, 0, 0, 0);
                    blend_en = (k % 2); alpha = 8'd100;
                    push(4'd4, 19'd200000, 9'd127, 9'd4, 0, 0, 0, 0);
                    push(4'd5, 19'd8192, 9'd400, 9'd30, 7'd10, 0, 0, 0);
                end
                wait_idle; disable fillgen;
            end
        join
        $display("FILLWAIT fills=%0d worst wait from request edge to fill read=%0d clk_sdram cycles", fills, fwait_max);

`ifdef GAL
        // ---------------- 4. Gallery: DSBLIT and the sticky blend / key ----------------
        src_base = 25'd300000; dst_base = 25'd0; cmd_fg = 0; cmd_bg = 0; blend_en = 0; key_en = 0;
        one("DSBLIT 96x12 2:3x3:4", 4'd11, 19'd150000, 9'd96, 9'd12, 0, 2'd1, 2'd0, 0);
        one("DSBLIT 120x120 1:2", 4'd11, 19'd150000, 9'd120, 9'd120, 0, 2'd2, 2'd2, 0);
        clear; push(4'd11, 19'd150000, 9'd96, 9'd12, 0, 2'd1, 2'd0, 0); wait_idle;   // the plain run again (the 120 x 120 one overwrote it)
        for (i = 0; i < 64 * 9; i = i + 1) mem.mem[160000 + i] = rd(150000 + (i / 64) * 512 + (i % 64));
        blend_en = 1; alpha = 8'd77; key_en = 1; key = rd(300000);
        clear; push(4'd11, 19'd150000, 9'd96, 9'd12, 0, 2'd1, 2'd0, 0); wait_idle;
        bad = 0; for (i = 0; i < 64 * 9; i = i + 1) if (rd(150000 + (i / 64) * 512 + (i % 64)) != mem.mem[160000 + i]) bad = bad + 1;
        $display("GAL DSBLIT with blend+key on: %0d words differ from the plain run (reads=%0d)", bad, mem.nrd);
        check(bad == 0, "DSBLIT ignores the sticky blend and key");
        blend_en = 0; key_en = 0;
        rc_lut = 0; for (k = 1; k <= 8; k = k + 1) rc_lut[k*5 +: 5] = 9 - k;
        one("CORNERS 100x50 r8", 4'd10, 19'd70000, 100, 50, 7'd8, 0, 0, 0);
        for (k = 12; k <= 15; k = k + 1) begin
            clear; cmd_fg = 16'hABCD; push(k[3:0], 19'd90000, 9'd5, 9'd3, 0, 0, 0, 0); wait_idle;
            $display("GAL op %0d: writes=%0d words=%0d", k, mem.nwr, mem.nwords);
        end
`endif
        $display("%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end
    initial begin #2000000000; $display("FAILED timeout"); $finish; end
endmodule
`default_nettype wire
