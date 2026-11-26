// ORACLE-SIDE cost bench (audit B-1181): per-command dispatch-to-retire cycles of the SHIPPED engine (tau_gfx = tau_talos
// + tau_scanout, scan-out fills running) on the realistic memory path (tau_sdram_sched_a: PHY + sched_v + the chip model),
// optionally with the CPU hammering the SDRAM through tau_sdram_a_cpu. Reads one internal signal of the old engine
// (u_talos.inflight) to time a command: NEVER give this bench or its output to the clean-room author.
// Each command class of contract table 7.1 runs REPS times at different addresses, one at a time (the engine idle before
// each push); the bench prints "COST <name> n=.. min=.. p50=.. p95=.. max=.." in clk_sdram cycles.
//   sim/sdram_sched_a/run_gfx_cost.sh [CPU_HAMMER]
// MIT licence. Simulation only (iverilog -g2012 -DSIM).
`timescale 1ps/1ps
`default_nettype none
module tb_a_gfx_cost #(parameter integer CPU_HAMMER = 0, parameter integer REPS = 9);
    reg clk = 0, clk_sys = 0, clk_vid = 0;
    always #5000 clk = ~clk;
    always #7500 clk_sys = ~clk_sys;              // 66.667 MHz (the shipped CPU clock)
    always #41700 clk_vid = ~clk_vid;
    reg reset = 1, rst_sys = 1;
    reg cmd_push = 0; reg [3:0] cmd_op = 0; reg [18:0] cmd_addr = 0; reg [8:0] cmd_w = 0, cmd_h = 0;
    reg [15:0] cmd_fg = 16'hF800, cmd_bg = 16'h001F; reg [6:0] cmd_glyph = 0; reg [1:0] cmd_sx = 0, cmd_sy = 0, cmd_glyph_hi = 0;
    reg [24:0] src_base = 25'd300000, dst_base = 0; reg [9:0] src_stride = 512, dst_stride = 512;
    reg key_en = 0; reg [15:0] key = 16'h1234; reg blend_en = 0; reg [7:0] alpha = 8'd128;
    reg clut_wr = 0; reg [7:0] clut_waddr = 0; reg [15:0] clut_wdata = 0; reg [79:0] rc_lut = 0;
    reg gfx_ok = 0;
    wire cmd_full, gfx_idle; wire [31:0] gfx_fence; wire [8:0] gfx_free; wire [15:0] gfx_ovf, gfx_underflow;
    wire [24:0] g_addr; wire [15:0] g_data; wire [1:0] g_be; wire [10:0] g_len; wire g_str, g_wr, g_rd, g_eb, g_fill; wire [9:0] g_rdlen;
    wire [15:0] g_wsq; wire p_av, p_rdy, p_da; wire [15:0] p_q; wire [10:0] p_wsa;
    tau_gfx #(.BLIT_BLEND_ENABLE(1), .DBUF_ENABLE(0), .FONT_RAM_ENABLE(0)) gfx (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr), .cmd_w(cmd_w), .cmd_h(cmd_h),
        .cmd_fg(cmd_fg), .cmd_bg(cmd_bg), .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy),
        .cmd_glyph_hi(cmd_glyph_hi), .font_we(1'b0), .font_waddr(13'd0), .font_wdata(32'd0),
        .cmd_full(cmd_full), .text_light(1'b0),
        .blt_src_base(src_base), .blt_src_stride(src_stride), .blt_dst_base(dst_base),
        .blt_dst_stride(dst_stride), .blt_key_en(key_en), .blt_key(key),
        .blt_blend_en(blend_en), .blt_blend_mode(3'd0), .blt_blend_alpha(alpha),
        .rc_cut_lut(rc_lut), .blt_reindex(8'd0),
        .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
        .dbuf_cpu_buf(1'b0), .dbuf_flip_req_tgl(1'b0), .dbuf_disp_buf(), .dbuf_flip_pending(),
        .sdram_init_complete(gfx_ok), .p0_addr(g_addr), .p0_data(g_data), .p0_byte_en(g_be),
        .p0_wr_len(g_len), .p0_wr_stream(g_str), .p0_q(p_q), .p0_wr_req(g_wr), .p0_rd_req(g_rd),
        .p0_end_burst_req(g_eb), .p0_available(p_av), .p0_ready(p_rdy), .p0_data_available(p_da),
        .wsrc_addr(p_wsa), .wsrc_q(g_wsq), .p0_is_fill(g_fill), .p0_rd_len(g_rdlen),
        .video_rgb(), .video_de(), .video_hs(), .video_vs(), .scan_vc(),
        .gfx_idle(gfx_idle), .gfx_fence(gfx_fence), .gfx_free(gfx_free), .gfx_ovf(gfx_ovf), .gfx_underflow(gfx_underflow));

    reg sys_start = 0, sys_write = 0; reg [24:0] sys_addr = 0; reg [31:0] sys_wdata = 0;
    wire sys_busy, sys_done; wire [31:0] sys_rdata;
    wire init_done, busy, rst_out, phy_fault; wire [15:0] fcount, dbg_pmiss; wire [31:0] refreshes;
    wire c_req_valid, c_req_ready, c_req_we, c_w_next, c_r_valid, c_r_last, c_done; wire [24:0] c_req_addr; wire [9:0] c_req_len, c_w_idx;
    wire [15:0] c_w_data, c_r_data; wire [1:0] c_w_be;
    wire [95:0] cnt_requests, cnt_words, cnt_busy; wire [47:0] cnt_bad, cnt_worst_wait, cnt_urgent_over;
    wire [12:0] c_a; wire [1:0] c_ba, c_dqm; wire c_clk, c_cke, c_ras, c_cas, c_we; wire [15:0] dq;
    tau_sdram_sched_a #(.QDEPTH(1), .CNT_EN(1), .INIT_WAIT_CYCLES(200), .SIM_SHORT_INIT(1)) u_sa (
        .clk(clk), .rst_in(reset), .rst_out(rst_out), .init_done(init_done), .busy(busy),
        .p0_addr(g_addr), .p0_data(g_data), .p0_byte_en(g_be), .p0_wr_len(g_len), .p0_wr_stream(g_str), .p0_wr_req(g_wr),
        .p0_rd_req(g_rd), .p0_end_burst_req(g_eb), .p0_is_fill(g_fill), .p0_rd_len(g_rdlen), .p0_q(p_q), .p0_available(p_av),
        .p0_ready(p_rdy), .p0_data_available(p_da), .wsrc_addr(p_wsa), .wsrc_q(g_wsq),
        .cpu_req_valid(c_req_valid), .cpu_req_ready(c_req_ready), .cpu_req_we(c_req_we), .cpu_req_addr(c_req_addr), .cpu_req_len(c_req_len),
        .cpu_w_next(c_w_next), .cpu_w_idx(c_w_idx), .cpu_w_data(c_w_data), .cpu_w_be(c_w_be), .cpu_r_valid(c_r_valid), .cpu_r_data(c_r_data),
        .cpu_r_last(c_r_last), .cpu_done(c_done),
        .cnt_requests(cnt_requests), .cnt_words(cnt_words), .cnt_busy(cnt_busy), .cnt_bad(cnt_bad), .cnt_worst_wait(cnt_worst_wait),
        .cnt_urgent_over(cnt_urgent_over), .dbg_pmiss(dbg_pmiss), .phy_fault(phy_fault), .phy_fault_count(fcount), .refreshes(refreshes),
        .dram_a(c_a), .dram_ba(c_ba), .dram_dq(dq), .dram_dqm(c_dqm), .dram_clk(c_clk), .dram_cke(c_cke), .dram_ras_n(c_ras),
        .dram_cas_n(c_cas), .dram_we_n(c_we));
    tau_sdram_a_cpu u_cpu (
        .clk_sys(clk_sys), .rst_sys(rst_sys), .sys_start(sys_start), .sys_wb_start(1'b0), .sys_write(sys_write), .sys_addr(sys_addr),
        .sys_wdata(sys_wdata), .sys_byte_en(4'hF), .sys_busy(sys_busy), .sys_done(sys_done), .sys_rdata(sys_rdata),
        .clk_sdram(clk), .rst_sdram(rst_out), .req_valid(c_req_valid), .req_ready(c_req_ready), .req_we(c_req_we), .req_addr(c_req_addr),
        .req_len(c_req_len), .w_next(c_w_next), .w_idx(c_w_idx), .w_data(c_w_data), .w_be(c_w_be), .r_valid(c_r_valid), .r_data(c_r_data),
        .r_last(c_r_last), .done(c_done));
    sdram_chip_model #(.CL_DEFAULT(2), .INIT_NOP_MIN(200), .INIT_WAIT_PS(200 * 10000), .MEM_LOG2(22), .MEM_COMPACT(2), .VERBOSE(0)) u_chip (
        .dram_clk(c_clk), .dram_cke(c_cke), .dram_ras_n(c_ras), .dram_cas_n(c_cas), .dram_we_n(c_we), .dram_dqm(c_dqm),
        .dram_ba(c_ba), .dram_a(c_a), .dram_dq(dq), .fpga_oe(u_sa.u_phy.u_core.dq_oe), .cap_clk(clk));

    // CPU hammer: a write and a read of the mailbox region, back to back, while CPU_HAMMER != 0
    reg cpu_run = 0; integer cseed = 5;
    initial begin : hammer
        wait (cpu_run);
        while (cpu_run) begin
            @(posedge clk_sys); sys_write <= ($random(cseed) & 1); sys_addr <= 25'h78000 + (($random(cseed) & 32'h7FFF) & ~32'd1); sys_wdata <= $random(cseed); sys_start <= 1;
            @(posedge clk_sys); sys_start <= 0;
            begin : w forever begin @(posedge clk_sys); if (sys_done === 1'b1) disable w; end end
            repeat ($unsigned($random(cseed)) % 24) @(posedge clk_sys);
        end
    end

    // timing: cycles with the engine's in-flight flag set (dispatch to retire)
    integer busy_c = 0; always @(posedge clk) if (gfx.u_talos.inflight) busy_c = busy_c + 1;
    integer samp [0:63]; integer ns;
    task push1(input [3:0] op, input [18:0] ad, input [8:0] w, input [8:0] h, input [6:0] g, input [1:0] sx, input [1:0] sy);
        integer n;
        begin
            n = 0; while (!gfx_idle && n < 40000000) begin @(posedge clk_sys); n = n + 1; end
            repeat (37 + ns * 113) @(posedge clk);     // a different phase against the scan-out and refresh each repeat
            busy_c = 0;
            @(posedge clk_sys); cmd_op <= op; cmd_addr <= ad; cmd_w <= w; cmd_h <= h; cmd_glyph <= g; cmd_sx <= sx; cmd_sy <= sy; cmd_push <= 1;
            @(posedge clk_sys); cmd_push <= 0;
            repeat (4) @(posedge clk_sys);
            n = 0; while (!gfx_idle && n < 40000000) begin @(posedge clk_sys); n = n + 1; end
            repeat (4) @(posedge clk);
            samp[ns] = busy_c; ns = ns + 1;
        end
    endtask
    task report(input [255:0] name);
        integer i, j, t;
        begin
            for (i = 0; i < ns; i = i + 1) for (j = i + 1; j < ns; j = j + 1) if (samp[j] < samp[i]) begin t = samp[i]; samp[i] = samp[j]; samp[j] = t; end
            $display("COST %0s n=%0d min=%0d p50=%0d p95=%0d max=%0d", name, ns, samp[0], samp[ns / 2], samp[(ns * 95 + 99) / 100 - 1], samp[ns - 1]);
            ns = 0;
        end
    endtask
    `define ONE(NAME, OP, AD, W, H, G, SX, SY) begin ns = 0; for (r = 0; r < REPS; r = r + 1) push1(OP, (AD) + r * 7 * 512 + r * 3, W, H, G, SX, SY); report(NAME); end
    integer r, k;
    initial begin
        repeat (6) @(posedge clk_sys); reset = 0; rst_sys = 0;
        k = 0; while (!init_done && k < 60000) begin @(posedge clk); k = k + 1; end
        repeat (50) @(posedge clk);
        for (k = 0; k < 256; k = k + 1) begin @(posedge clk_sys); clut_waddr <= k; clut_wdata <= k * 16'h0101; clut_wr <= 1; end
        @(posedge clk_sys); clut_wr <= 0;
        for (k = 1; k <= 8; k = k + 1) rc_lut[k*5 +: 5] = 9 - k;
        gfx_ok = 1; repeat (20) @(posedge clk);
        if (CPU_HAMMER != 0) cpu_run = 1;
        ns = 0;
        `ONE("FENCE", 4'd9, 19'd0, 0, 0, 0, 0, 0)
        `ONE("RECT 1x1", 4'd1, 19'd1000, 1, 1, 0, 0, 0)
        `ONE("RECT 16x16", 4'd1, 19'd1000, 16, 16, 0, 0, 0)
        `ONE("RECT 127x16", 4'd1, 19'd1000, 127, 16, 0, 0, 0)
        `ONE("RECT 400x1", 4'd1, 19'd1024, 400, 1, 0, 0, 0)
        `ONE("RECT 511x1", 4'd1, 19'd1024, 511, 1, 0, 0, 0)
        `ONE("RECT 400x360", 4'd1, 19'd0, 400, 360, 0, 0, 0)
        `ONE("RUN 100", 4'd0, 19'd2048, 100, 7, 0, 0, 0)
        `ONE("CHAR 1x", 4'd2, 19'd4096, 0, 0, 7'h41, 0, 0)
        `ONE("CHAR 1.5x", 4'd2, 19'd4096, 0, 0, 7'h41, 1, 1)
        `ONE("CHAR 2x", 4'd2, 19'd4096, 0, 0, 7'h41, 2, 2)
        `ONE("CHAR 3x", 4'd2, 19'd4096, 0, 0, 7'h41, 3, 3)
        `ONE("BAR 8x100", 4'd5, 19'd8192, 8, 100, 7'd50, 0, 0)
        cmd_fg = 16'd0; cmd_bg = 16'd40960;
        `ONE("COPY 127x16", 4'd3, 19'd60000, 127, 16, 0, 0, 0)
        `ONE("BLIT 127x16", 4'd4, 19'd60000, 127, 16, 0, 0, 0)
        `ONE("BLIT 16x16", 4'd4, 19'd60000, 16, 16, 0, 0, 0)
        blend_en = 1;
        `ONE("BLIT 127x16 blend", 4'd4, 19'd60000, 127, 16, 0, 0, 0)
        blend_en = 0; key_en = 1;
        `ONE("BLIT 127x16 key", 4'd4, 19'd60000, 127, 16, 0, 0, 0)
        key_en = 0;
        `ONE("SBLIT 40x40 1x", 4'd6, 19'd60000, 40, 40, 0, 0, 0)
        `ONE("SBLIT 40x40 2x", 4'd6, 19'd60000, 40, 40, 0, 2, 2)
        `ONE("SBLIT 40x40 3x", 4'd6, 19'd60000, 40, 40, 0, 3, 3)
        `ONE("CBLIT 127x16", 4'd7, 19'd60000, 127, 16, 0, 0, 0)
        `ONE("RRECT 100x50 r8", 4'd8, 19'd70000, 100, 50, 7'd8, 0, 0)
        $display("DONE underflow=%0d bad_req=%h", gfx_underflow, cnt_bad);
        $finish;
    end
    initial begin #(64'd1_000_000 * 400_000); $display("TIMEOUT"); $finish; end
endmodule
`default_nettype wire
