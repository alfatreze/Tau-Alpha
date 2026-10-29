// Whole-path bench: the real tau_gfx (tau_talos draw engine + tau_scanout) and a CPU hammer on one of three memory stacks:
//   STACK 0  the reference: a plain functional memory stand-in speaking the p0 protocol (no chip model)
//   STACK 1  the OLD path: tau_sdram_cpu_bridge + tau_sdram_arbiter + tau_sdram_ctrl through the chip model (baseline glue)
//   STACK 2  the NEW path: tau_sdram_a_cpu + tau_sdram_sched_a (PHY + sched_v) through the chip model
// The same random command stream (RECT / CHAR / COPY / BLIT / SBLIT / CBLIT ... with random sticky state, DBUF off so the
// result does not depend on timing) and the same scan-out run on every stack; at the end the whole memory image is read
// back through the stack's own p0 port and reduced to a checksum. A stack passes when its checksum equals the reference's
// (+REFCRC=<hex>), the CPU hammer's words match their shadow, no scan-out line was late (underflow 0), FENCE counted
// exactly, and no write lands after gfx_idle. It also prints the cycles the stream took (the "replay" measurement).
// MIT licence. Simulation only (iverilog -g2012 -DSIM, the old controller through the packed-typedef build copy).
`timescale 1ps/1ps
`default_nettype none

module tb_a_gfx #(
    parameter integer STACK = 2,
    parameter integer NCMD = 150,
    parameter integer CPU_HAMMER = 1,
    parameter integer SEED = 32'h7a1a5a11,
    parameter integer QD = 1,
    parameter integer MEMW = 1 << 19           // words of the memory image (the stimulus stays below this)
);
    localparam [24:0] CPU_BASE = 25'h70000, CPU_END = 25'h78000;
    reg clk = 0, clk_sys = 0, clk_vid = 0;
    always #5000 clk = ~clk;
    always #8333 clk_sys = ~clk_sys;
    always #41700 clk_vid = ~clk_vid;
    reg reset = 1;
    reg rst_sys = 1;
    integer cyc = 0; always @(posedge clk) cyc <= cyc + 1;

    // ------------------------------------------------------------------ the graphics path
    reg         cmd_push = 0;
    reg  [3:0]  cmd_op = 0;
    reg  [18:0] cmd_addr = 0;
    reg  [8:0]  cmd_w = 0, cmd_h = 0;
    reg  [15:0] cmd_fg = 16'hFFFF, cmd_bg = 16'h0000;
    reg  [6:0]  cmd_glyph = 0;
    reg  [1:0]  cmd_sx = 0, cmd_sy = 0, cmd_glyph_hi = 0;
    reg         text_light = 0;
    reg  [24:0] blt_src_base = 0, blt_dst_base = 0;
    reg  [9:0]  blt_src_stride = 512, blt_dst_stride = 512;
    reg         blt_key_en = 0; reg [15:0] blt_key = 0;
    reg  [7:0]  blt_reindex = 0;
    reg         clut_wr = 0; reg [7:0] clut_waddr = 0; reg [15:0] clut_wdata = 0;
    reg  [79:0] rc_cut_lut = 0;
    reg         gfx_ok = 0;                      // sdram_init_complete as the graphics path sees it (held low while the bench owns the port)
    wire        cmd_full, gfx_idle; wire [31:0] gfx_fence; wire [8:0] gfx_free; wire [15:0] gfx_ovf, gfx_underflow;
    wire [24:0] g_addr; wire [15:0] g_data; wire [1:0] g_be; wire [10:0] g_len; wire g_str, g_wr, g_rd, g_eb, g_fill; wire [9:0] g_rdlen;
    wire [15:0] g_wsq;                           // tau_gfx's glyph-buffer data (its wsrc_q output)
    wire        p_av, p_rdy, p_da; wire [15:0] p_q; wire [10:0] p_wsa;

    tau_gfx #(.BLIT_BLEND_ENABLE(0), .DBUF_ENABLE(0), .FONT_RAM_ENABLE(0)) gfx (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr), .cmd_w(cmd_w), .cmd_h(cmd_h),
        .cmd_fg(cmd_fg), .cmd_bg(cmd_bg), .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy),
        .cmd_glyph_hi(cmd_glyph_hi), .font_we(1'b0), .font_waddr(13'd0), .font_wdata(32'd0),
        .cmd_full(cmd_full), .text_light(text_light),
        .blt_src_base(blt_src_base), .blt_src_stride(blt_src_stride), .blt_dst_base(blt_dst_base),
        .blt_dst_stride(blt_dst_stride), .blt_key_en(blt_key_en), .blt_key(blt_key),
        .blt_blend_en(1'b0), .blt_blend_mode(3'd0), .blt_blend_alpha(8'd0),
        .rc_cut_lut(rc_cut_lut), .blt_reindex(blt_reindex),
        .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
        .dbuf_cpu_buf(1'b0), .dbuf_flip_req_tgl(1'b0), .dbuf_disp_buf(), .dbuf_flip_pending(),
        .sdram_init_complete(gfx_ok), .p0_addr(g_addr), .p0_data(g_data), .p0_byte_en(g_be),
        .p0_wr_len(g_len), .p0_wr_stream(g_str), .p0_q(p_q), .p0_wr_req(g_wr), .p0_rd_req(g_rd),
        .p0_end_burst_req(g_eb), .p0_available(p_av), .p0_ready(p_rdy), .p0_data_available(p_da),
        .wsrc_addr(p_wsa), .wsrc_q(g_wsq), .p0_is_fill(g_fill), .p0_rd_len(g_rdlen),
        .video_rgb(), .video_de(), .video_hs(), .video_vs(), .scan_vc(),
        .gfx_idle(gfx_idle), .gfx_fence(gfx_fence), .gfx_free(gfx_free), .gfx_ovf(gfx_ovf), .gfx_underflow(gfx_underflow));

    // ------------------------------------------------------------------ the bench's own master (image init and read back)
    reg bm = 1;                                   // 1: the bench owns the port
    reg [24:0] t_addr = 0; reg [15:0] t_data = 0; reg [1:0] t_be = 2'b11; reg [10:0] t_len = 1;
    reg t_str = 0, t_wr = 0, t_rd = 0, t_eb = 0;
    reg [15:0] gly [0:2047]; reg [15:0] t_wsq = 0; always @(posedge clk) t_wsq <= gly[p_wsa];
    wire [24:0] s_addr = bm ? t_addr : g_addr;
    wire [15:0] s_data = bm ? t_data : g_data;
    wire [1:0]  s_be   = bm ? t_be   : g_be;
    wire [10:0] s_len  = bm ? t_len  : g_len;
    wire        s_str  = bm ? t_str  : g_str;
    wire        s_wr   = bm ? t_wr   : g_wr;
    wire        s_rd   = bm ? t_rd   : g_rd;
    wire        s_eb   = bm ? t_eb   : g_eb;
    wire        s_fill = bm ? 1'b0   : g_fill;
    reg  [9:0]  t_rdlen = 10'd1;
    wire [9:0]  s_rdlen = bm ? t_rdlen : g_rdlen;
    wire [15:0] s_wsq  = bm ? t_wsq : g_wsq;

    // ------------------------------------------------------------------ the CPU hammer (mailbox)
    reg sys_start = 0, sys_write = 0; reg [24:0] sys_addr = 0; reg [31:0] sys_wdata = 0; reg [3:0] sys_be = 4'hf;
    wire sys_busy, sys_done; wire [31:0] sys_rdata;

    // ------------------------------------------------------------------ the memory stack
    integer wr_after_idle_base = 0;               // write words seen (stacks 1/2) when idle was reached
    integer chip_wr_words = 0, chip_err = 0, phy_fault_n = 0;
    wire stack_init;                               // the stack's init_done
    reg crc_go = 0; reg [31:0] ref_crc_r;          // STACK 0: checksum of the memory array on request
    reg [15:0] mem_init_pat;
    function [15:0] pat(input [24:0] a); reg [31:0] x; begin x = {7'd0, a} * 32'h9E3779B1; x = x ^ (x >> 15); x = x * 32'h2C1B3C6D; pat = x[31:16] ^ x[15:0]; end endfunction

    generate
    if (STACK == 0) begin : g_ref
        // functional stand-in (the diff bench's contract): reads return the stored word
        reg [15:0] mem [0:(1<<19)-1];
        integer k; initial for (k = 0; k < (1<<19); k = k + 1) mem[k] = pat(k);
        localparam IDLE = 0, STREAM = 1, CONST = 2, DONE = 3, READ = 4;
        integer st = IDLE, beats = 0;
        reg [24:0] a; reg [10:0] len; reg stream; reg [15:0] cdata; reg [1:0] cbe; reg av = 0, rdy = 0, da = 0; reg [15:0] q = 0; reg [10:0] wsa = 0;
        always @(posedge clk) begin
            rdy <= 0; da <= 0;
            case (st)
                IDLE: begin
                    av <= 1'b1;
                    if (s_rd) begin av <= 0; a <= s_addr; st <= READ; end
                    else if (s_wr) begin av <= 0; a <= s_addr; len <= s_len; stream <= s_str; cdata <= s_data; cbe <= s_be; wsa <= 0; beats = 0; st <= s_str ? STREAM : CONST; end
                end
                STREAM: begin wsa <= wsa + 1; if (wsa > 0) begin mem[a + beats] <= s_wsq; beats = beats + 1; end if (beats == len) st <= DONE; end
                READ: begin da <= 1; q <= mem[a]; a <= a + 1; if (s_eb) st <= IDLE; end
                CONST: begin for (beats = 0; beats < len; beats = beats + 1) begin if (cbe[0]) mem[a + beats][7:0] <= cdata[7:0]; if (cbe[1]) mem[a + beats][15:8] <= cdata[15:8]; end st <= DONE; end
                DONE: begin rdy <= 1; st <= IDLE; end
            endcase
        end
        assign p_av = av; assign p_rdy = rdy; assign p_da = da; assign p_q = q; assign p_wsa = wsa;
        assign sys_busy = 0; assign sys_done = 0; assign sys_rdata = 0;
        assign stack_init = 1'b1;
        integer kc; reg [31:0] cc;
        always @(posedge crc_go) begin cc = 32'd0; for (kc = 0; kc < MEMW; kc = kc + 1) if (kc < CPU_BASE || kc >= CPU_END) cc = cc * 31 + mem[kc]; ref_crc_r = cc; end
    end else if (STACK == 1) begin : g_old
        wire        init_complete; assign stack_init = init_complete;
        wire [24:0] cpu_addr; wire [15:0] cpu_data, cpu_q; wire [1:0] cpu_be; wire [10:0] cpu_len;
        wire        cpu_wr, cpu_rd, cpu_end, cpu_acc, cpu_ready, cpu_da, cpu_avail;
        wire [24:0] a_addr; wire [15:0] a_data, a_q; wire [1:0] a_be; wire [10:0] a_len, a_wsa; wire a_str, a_wr, a_rd, a_end, a_av, a_rdy, a_da; wire [15:0] a_wsq;
        wire [15:0] dq_ctrl; wire [12:0] c_a; wire [1:0] c_dqm, c_ba; wire c_ncs, c_nwe, c_nras, c_ncas, c_cke, c_clk;
        wire [24:0] f_addr = s_addr;
        tau_sdram_cpu_bridge u_br (
            .clk_sys(clk_sys), .rst_sys(rst_sys), .sys_start(sys_start), .sys_wb_start(1'b0), .sys_write(sys_write), .sys_addr(sys_addr),
            .sys_wdata(sys_wdata), .sys_byte_en(sys_be), .sys_busy(sys_busy), .sys_done(sys_done), .sys_rdata(sys_rdata),
            .debug_wb_write_seen(), .debug_op_write(), .debug_wdata_all_ones(), .debug_be_all_enabled(), .debug_lo_write_accepted(),
            .debug_hi_write_accepted(), .debug_follow_read_seen(), .debug_follow_read_data_zero(), .debug_follow_read_data_all_ones(),
            .clk_sdram(clk), .rst_sdram(reset), .m_addr(cpu_addr), .m_data(cpu_data), .m_byte_en(cpu_be), .m_wr_len(cpu_len),
            .m_wr_req(cpu_wr), .m_rd_req(cpu_rd), .m_end_burst_req(cpu_end), .m_q(cpu_q), .m_accepted(cpu_acc), .m_ready(cpu_ready),
            .m_data_available(cpu_da));
        tau_sdram_arbiter u_arb (
            .clk(clk), .rst(reset),
            .fb_addr(s_addr), .fb_data(s_data), .fb_byte_en(s_be), .fb_wr_len(s_len), .fb_wr_stream(s_str), .fb_wr_req(s_wr), .fb_rd_req(s_rd),
            .fb_end_burst_req(s_eb), .fb_q(p_q), .fb_available(p_av), .fb_ready(p_rdy), .fb_data_available(p_da), .fb_wsrc_q(s_wsq), .fb_wsrc_addr(p_wsa),
            .cpu_addr(cpu_addr), .cpu_data(cpu_data), .cpu_byte_en(cpu_be), .cpu_wr_len(cpu_len), .cpu_wr_req(cpu_wr), .cpu_rd_req(cpu_rd),
            .cpu_end_burst_req(cpu_end), .cpu_q(cpu_q), .cpu_available(cpu_avail), .cpu_ready(cpu_ready), .cpu_data_available(cpu_da),
            .cpu_accepted(cpu_acc), .p0_cpu_selected(),
            .p0_addr(a_addr), .p0_data(a_data), .p0_byte_en(a_be), .p0_wr_len(a_len), .p0_wr_stream(a_str), .p0_q(a_q), .p0_wr_req(a_wr),
            .p0_rd_req(a_rd), .p0_end_burst_req(a_end), .p0_available(a_av), .p0_ready(a_rdy), .p0_data_available(a_da),
            .wsrc_addr(a_wsa), .wsrc_q(a_wsq));
        tau_sdram_ctrl #(.CLOCK_SPEED_MHZ(100), .BURST_TYPE(0), .CAS_LATENCY(2), .WRITE_BURST(1)) u_ctrl (
            .clk(clk), .reset(reset), .init_complete(init_complete),
            .p0_addr(a_addr), .p0_data(a_data), .p0_byte_en(a_be), .p0_wr_len(a_len), .p0_q(a_q),
            .p0_wr_stream(a_str), .wsrc_addr(a_wsa), .wsrc_q(a_wsq),
            .p0_wr_req(a_wr), .p0_rd_req(a_rd), .p0_end_burst_req(a_end),
            .p0_available(a_av), .p0_ready(a_rdy), .p0_data_available(a_da),
            .SDRAM_DQ(dq_ctrl), .SDRAM_A(c_a), .SDRAM_DQM(c_dqm), .SDRAM_BA(c_ba),
            .SDRAM_nCS(c_ncs), .SDRAM_nWE(c_nwe), .SDRAM_nRAS(c_nras), .SDRAM_nCAS(c_ncas), .SDRAM_CKE(c_cke), .SDRAM_CLK(c_clk));
        initial begin u_ctrl.SDRAM_A = 13'd0; u_ctrl.SDRAM_BA = 2'b00; u_ctrl.SDRAM_DQM = 2'b00; end
        // chip-side glue of sim/sdram_baseline (scenario nominal): chip clock 3.5 ns late, commands 1 ns, board 0
        reg chip_clk_en = 1'b0; initial begin repeat (3) @(posedge clk); chip_clk_en = 1'b1; end
        reg dram_clk = 1'b0; wire clk_g = c_clk & chip_clk_en; always @(clk_g) dram_clk <= #(3500) clk_g;
        reg dram_cke, dram_ras_n, dram_cas_n, dram_we_n; reg [1:0] dram_dqm, dram_ba; reg [12:0] dram_a;
        always @(c_cke) dram_cke <= #(1000) c_cke; always @(c_nras) dram_ras_n <= #(1000) c_nras; always @(c_ncas) dram_cas_n <= #(1000) c_ncas;
        always @(c_nwe) dram_we_n <= #(1000) c_nwe; always @(c_dqm) dram_dqm <= #(1000) c_dqm; always @(c_ba) dram_ba <= #(1000) c_ba; always @(c_a) dram_a <= #(1000) c_a;
        reg [15:0] dq_w; reg dq_w_en = 1'b0;
        always @(u_ctrl.sdram_data) dq_w <= #(1000) u_ctrl.sdram_data;
        always @(u_ctrl.dq_output) dq_w_en <= #(1000) u_ctrl.dq_output;
        wire [15:0] dq_pin = dq_w_en ? dq_w : 16'hzzzz;
        reg [15:0] dq_ret; always @(dq_pin) dq_ret <= dq_pin;
        assign dq_ctrl = u_ctrl.dq_output ? 16'hzzzz : dq_ret;
        reg [15:0] fpga_oe = 16'h0000; always @(u_ctrl.dq_output) begin if (u_ctrl.dq_output) fpga_oe <= #(1000) 16'hffff; else fpga_oe <= #(1000) 16'h0000; end
        sdram_chip_model #(.CAP_MODE(0), .MEM_LOG2(20), .MEM_COMPACT(0), .VERBOSE(0)) u_chip (
            .dram_clk(dram_clk), .dram_cke(dram_cke), .dram_ras_n(dram_ras_n), .dram_cas_n(dram_cas_n), .dram_we_n(dram_we_n),
            .dram_dqm(dram_dqm), .dram_ba(dram_ba), .dram_a(dram_a), .dram_dq(dq_pin), .fpga_oe(fpga_oe), .cap_clk(clk));
        integer kk; initial begin wait (u_chip.init_stage == 4); #1; u_chip.init_stage = 5; u_chip.init_done = 1'b1; for (kk = 0; kk < 8192; kk = kk + 1) u_chip.row_ts[kk] = $time; end
        always @* begin chip_wr_words = u_chip.wr_words; chip_err = u_chip.err_count; end
    end else begin : g_new
        wire init_done, busy, rst_out, phy_fault; assign stack_init = init_done; wire [15:0] fcount, dbg_pmiss; wire [31:0] refreshes;
        wire c_req_valid, c_req_ready, c_req_we, c_w_next, c_r_valid, c_r_last, c_done; wire [24:0] c_req_addr; wire [9:0] c_req_len, c_w_idx;
        wire [15:0] c_w_data, c_r_data; wire [1:0] c_w_be;
        wire [95:0] cnt_requests, cnt_words, cnt_busy; wire [47:0] cnt_bad, cnt_worst_wait, cnt_urgent_over;
        wire [12:0] c_a; wire [1:0] c_ba, c_dqm; wire c_clk, c_cke, c_ras, c_cas, c_we; wire [15:0] dq;
        tau_sdram_sched_a #(.QDEPTH(QD), .CNT_EN(1), .INIT_WAIT_CYCLES(200), .SIM_SHORT_INIT(1)) u_sa (
            .clk(clk), .rst_in(reset), .rst_out(rst_out), .init_done(init_done), .busy(busy),
            .p0_addr(s_addr), .p0_data(s_data), .p0_byte_en(s_be), .p0_wr_len(s_len), .p0_wr_stream(s_str), .p0_wr_req(s_wr),
            .p0_rd_req(s_rd), .p0_end_burst_req(s_eb), .p0_is_fill(s_fill), .p0_rd_len(s_rdlen), .p0_q(p_q), .p0_available(p_av),
            .p0_ready(p_rdy), .p0_data_available(p_da), .wsrc_addr(p_wsa), .wsrc_q(s_wsq),
            .cpu_req_valid(c_req_valid), .cpu_req_ready(c_req_ready), .cpu_req_we(c_req_we), .cpu_req_addr(c_req_addr), .cpu_req_len(c_req_len),
            .cpu_w_next(c_w_next), .cpu_w_idx(c_w_idx), .cpu_w_data(c_w_data), .cpu_w_be(c_w_be), .cpu_r_valid(c_r_valid), .cpu_r_data(c_r_data),
            .cpu_r_last(c_r_last), .cpu_done(c_done),
            .cnt_requests(cnt_requests), .cnt_words(cnt_words), .cnt_busy(cnt_busy), .cnt_bad(cnt_bad), .cnt_worst_wait(cnt_worst_wait),
            .cnt_urgent_over(cnt_urgent_over), .dbg_pmiss(dbg_pmiss), .phy_fault(phy_fault), .phy_fault_count(fcount), .refreshes(refreshes),
            .dram_a(c_a), .dram_ba(c_ba), .dram_dq(dq), .dram_dqm(c_dqm), .dram_clk(c_clk), .dram_cke(c_cke), .dram_ras_n(c_ras),
            .dram_cas_n(c_cas), .dram_we_n(c_we));
        tau_sdram_a_cpu u_cpu (
            .clk_sys(clk_sys), .rst_sys(rst_sys), .sys_start(sys_start), .sys_wb_start(1'b0), .sys_write(sys_write), .sys_addr(sys_addr),
            .sys_wdata(sys_wdata), .sys_byte_en(sys_be), .sys_busy(sys_busy), .sys_done(sys_done), .sys_rdata(sys_rdata),
            .clk_sdram(clk), .rst_sdram(rst_out), .req_valid(c_req_valid), .req_ready(c_req_ready), .req_we(c_req_we), .req_addr(c_req_addr),
            .req_len(c_req_len), .w_next(c_w_next), .w_idx(c_w_idx), .w_data(c_w_data), .w_be(c_w_be), .r_valid(c_r_valid), .r_data(c_r_data),
            .r_last(c_r_last), .done(c_done));
        sdram_chip_model #(.CL_DEFAULT(2), .INIT_NOP_MIN(200), .INIT_WAIT_PS(200 * 10000), .MEM_LOG2(22), .MEM_COMPACT(2), .VERBOSE(0)) u_chip (
            .dram_clk(c_clk), .dram_cke(c_cke), .dram_ras_n(c_ras), .dram_cas_n(c_cas), .dram_we_n(c_we), .dram_dqm(c_dqm),
            .dram_ba(c_ba), .dram_a(c_a), .dram_dq(dq), .fpga_oe(u_sa.u_phy.u_core.dq_oe), .cap_clk(clk));
        always @* begin chip_wr_words = u_chip.wr_words; chip_err = u_chip.err_count; phy_fault_n = phy_fault; end
    end
    endgenerate

    // ------------------------------------------------------------------ the stimulus (identical on every stack)
    integer seed = SEED;
    function integer rnd(input integer n); rnd = $unsigned($random(seed)) % n; endfunction
    integer nfence = 0;
    task push_one;
        reg [3:0] op;
        begin
            op = rnd(9);
            @(posedge clk_sys);
            while (cmd_full) @(posedge clk_sys);
            cmd_op <= op;
            cmd_addr <= (rnd(8) == 0) ? rnd(1 << 18) : (rnd(360) * 512 + rnd(380));
            cmd_w <= (rnd(6) == 0) ? rnd(512) : rnd(100);
            cmd_h <= (rnd(6) == 0) ? rnd(40) : rnd(10);
            cmd_fg <= rnd(65536) & 16'hFFFB; cmd_bg <= rnd(65536);      // fg[2] = 0: a COPY/BLIT source offset stays below 2^18
            cmd_glyph <= rnd(128); cmd_glyph_hi <= rnd(4);
            cmd_sx <= rnd(4); cmd_sy <= rnd(4);
            cmd_push <= 1'b1;
            @(posedge clk_sys);
            cmd_push <= 1'b0;
            if (rnd(12) == 0) begin
                cmd_op <= 4'd9; cmd_push <= 1'b1; nfence = nfence + 1;       // FENCE
                @(posedge clk_sys); cmd_push <= 1'b0;
            end
        end
    endtask
    task perturb;
        begin
            case (rnd(9))
                0: begin blt_src_base <= rnd(1 << 16); blt_dst_base <= rnd(1 << 18);
                         blt_src_stride <= 1 + rnd(1023); blt_dst_stride <= 1 + rnd(1023); end
                1: begin blt_key_en <= rnd(2); blt_key <= rnd(65536); end
                2: blt_reindex <= rnd(256);
                3: begin clut_waddr <= rnd(256); clut_wdata <= rnd(65536); clut_wr <= 1; @(posedge clk_sys); clut_wr <= 0; end
                4: rc_cut_lut <= {rnd(65536), rnd(65536), rnd(65536), rnd(65536), rnd(65536)};
                5: text_light <= rnd(2);
                default: ;
            endcase
        end
    endtask

    // ------------------------------------------------------------------ bench master tasks (p0 protocol; only while bm = 1)
    integer got; reg collect = 0; reg [15:0] rbuf [0:1023];
    always @(posedge clk) if (collect && p_da) begin if (got < 1024) rbuf[got] = p_q; got = got + 1; end
    task m_wait_av; begin while (p_av !== 1'b1) @(posedge clk); end endtask
    task m_stream_write(input [24:0] addr, input integer len);
        begin
            m_wait_av; @(posedge clk);
            t_addr <= addr; t_be <= 2'b11; t_len <= len; t_str <= 1; t_wr <= 1;
            @(posedge clk); t_wr <= 0; t_str <= 0;
            begin : w forever begin @(posedge clk); if (p_rdy === 1'b1) disable w; end end
        end endtask
    task m_read(input [24:0] addr, input integer len);
        integer done_w, take; reg bad;
        begin
            done_w = 0;
            while (done_w < len) begin
                m_wait_av; @(posedge clk);
                got = 0; collect = 1; t_addr <= addr + done_w; t_rd <= 1; t_rdlen <= len - done_w;
                @(posedge clk); t_rd <= 0;
                // the old controller may cut a read short (refresh, page end): end the burst when the wanted words have arrived
                begin : wt integer guard; guard = 0; while (got < len - done_w && guard < 4000) begin @(posedge clk); guard = guard + 1; if (STACK == 1 && p_av === 1'b1 && got > 0) disable wt; end end
                collect = 0; t_eb <= 1; @(posedge clk); t_eb <= 0;
                begin : w forever begin @(posedge clk); if (p_rdy === 1'b1 || p_av === 1'b1) disable w; end end
                take = (got < len - done_w) ? got : len - done_w;
                if (take == 0) begin $display("  read returned no data at %h", addr + done_w); take = 1; end
                for (done_w = done_w; take > 0; take = take - 1) begin end
                done_w = done_w + ((got < len - done_w) ? got : len - done_w);
            end
        end endtask

    // ------------------------------------------------------------------ CPU hammer
    reg cpu_run = 0; integer cpu_ops = 0, cpu_bad = 0; realtime cpu_lat_sum = 0, cpu_lat_max = 0, t_c0;
    reg [15:0] cs_d [0:(1<<15)-1]; reg [1:0] cs_v [0:(1<<15)-1];
    integer cseed = 11; integer ci; initial for (ci = 0; ci < (1<<15); ci = ci + 1) cs_v[ci] = 2'b00;
    function integer crnd(input integer n); integer v; begin v = $random(cseed); if (v < 0) v = -v; crnd = v % n; end endfunction
    task cpu_op(input w, input [24:0] a, input [31:0] d, input [3:0] be);
        realtime l; reg [31:0] rd; integer ix; reg [15:0] e0, e1; reg [1:0] v0, v1;
        begin
            @(posedge clk_sys); sys_write <= w; sys_addr <= a; sys_wdata <= d; sys_be <= be; sys_start <= 1; t_c0 = $realtime;
            @(posedge clk_sys); sys_start <= 0;
            begin : w2 forever begin @(posedge clk_sys); if (sys_done === 1'b1) disable w2; end end
            rd = sys_rdata; l = ($realtime - t_c0) / 1000.0; cpu_lat_sum = cpu_lat_sum + l; if (l > cpu_lat_max) cpu_lat_max = l;
            ix = a - CPU_BASE; cpu_ops = cpu_ops + 1;
            if (w) begin
                if (be[0]) cs_d[ix][7:0] = d[7:0]; if (be[1]) cs_d[ix][15:8] = d[15:8]; cs_v[ix] = cs_v[ix] | be[1:0];
                if (be[2]) cs_d[ix+1][7:0] = d[23:16]; if (be[3]) cs_d[ix+1][15:8] = d[31:24]; cs_v[ix+1] = cs_v[ix+1] | be[3:2];
            end else begin
                e0 = cs_d[ix]; e1 = cs_d[ix+1]; v0 = cs_v[ix]; v1 = cs_v[ix+1];
                if ((v0[0] && rd[7:0] !== e0[7:0]) || (v0[1] && rd[15:8] !== e0[15:8]) || (v1[0] && rd[23:16] !== e1[7:0]) || (v1[1] && rd[31:24] !== e1[15:8])) begin
                    cpu_bad = cpu_bad + 1; if (cpu_bad < 6) $display("  CPU read mismatch at %h: %h", a, rd); end
            end
        end endtask
    initial begin : hammer
        integer off; wait (cpu_run);
        while (cpu_run) begin
            off = 2 * crnd((CPU_END - CPU_BASE) / 2 - 1);
            cpu_op(1, CPU_BASE + off, $random(cseed), 4'hF); cpu_op(1, CPU_BASE + off, $random(cseed), crnd(16)); cpu_op(0, CPU_BASE + off, 0, 0);
            repeat (crnd(40)) @(posedge clk_sys);
        end
    end

    // ------------------------------------------------------------------ main
    integer fails = 0, uf_idle = 0; integer blk, k, n, t_start, t_idle, wr_idle0, ref_crc_arg; reg [31:0] crc;
    reg [8*16-1:0] dummy;
    initial begin
        integer ncmd;
        // reset
        repeat (6) @(posedge clk_sys); reset = 0; rst_sys = 0;
        if (STACK != 0) begin
            n = 0; while (!stack_init && n < 60000) begin @(posedge clk); n = n + 1; end
            repeat (50) @(posedge clk);
            // image init through the port: the pattern in blocks of 512 words (streamed)
            for (blk = 0; blk < MEMW / 512; blk = blk + 1) begin
                for (k = 0; k < 512; k = k + 1) gly[k] = pat(blk * 512 + k);
                m_stream_write(blk * 512, 512);
            end
        end
        bm = 0; gfx_ok = 1; repeat (10) @(posedge clk);
        if (CPU_HAMMER != 0 && STACK != 0) cpu_run = 1;
        t_start = cyc;
        for (ncmd = 0; ncmd < NCMD; ncmd = ncmd + 1) begin
            if (rnd(3) == 0) perturb;
            push_one;
            if (rnd(10) == 0) repeat (rnd(400)) @(posedge clk_sys);
        end
        n = 0; while (!gfx_idle && n < 40_000_000) begin @(posedge clk_sys); n = n + 1; end
        t_idle = cyc;
        cpu_run = 0; begin : drain integer d; d = 0; while (sys_busy === 1'b1 && d < 5000) begin @(posedge clk_sys); d = d + 1; end end
        repeat (200) @(posedge clk);
        wr_idle0 = chip_wr_words;
        repeat (3000) @(posedge clk);
        if (STACK != 0 && chip_wr_words != wr_idle0) begin $display("  FAIL: %0d words written after gfx_idle", chip_wr_words - wr_idle0); fails = fails + 1; end
        if (!gfx_idle) begin $display("  FAIL: gfx did not go idle"); fails = fails + 1; end
        if (gfx_fence != nfence) begin $display("  FAIL: fence count %0d, pushed %0d", gfx_fence, nfence); fails = fails + 1; end
        uf_idle = gfx_underflow;
        if (gfx_underflow != 0) begin $display("  FAIL: %0d late scan-out lines", gfx_underflow); fails = fails + 1; end
        // read the whole image back through the port
        gfx_ok = 0; bm = 1; repeat (20) @(posedge clk);
        crc = 32'd0;
        if (STACK == 0) begin
            crc_go = 1; #1; crc = ref_crc_r;
        end else begin
            for (blk = 0; blk < MEMW / 512; blk = blk + 1) begin
                m_read(blk * 512, 512);
                if (blk * 512 < CPU_BASE || blk * 512 >= CPU_END) for (k = 0; k < 512; k = k + 1) crc = crc * 31 + rbuf[k];
                else for (k = 0; k < 512; k = k + 1) begin : cmp
                    integer ix; reg [15:0] e; reg [1:0] v; ix = blk * 512 + k - CPU_BASE; e = cs_d[ix]; v = cs_v[ix];
                    if ((v[0] && rbuf[k][7:0] !== e[7:0]) || (v[1] && rbuf[k][15:8] !== e[15:8])) cpu_bad = cpu_bad + 1;
                end
            end
        end
        if (cpu_bad != 0) begin $display("  FAIL: %0d CPU words differ from the shadow", cpu_bad); fails = fails + 1; end
        if (chip_err != 0) begin $display("  FAIL: %0d chip errors", chip_err); fails = fails + 1; end
        if ($value$plusargs("REFCRC=%h", ref_crc_arg) && ref_crc_arg !== crc) begin $display("  FAIL: image checksum %h differs from the reference %h", crc, ref_crc_arg); fails = fails + 1; end
        $display("RESULT stack=%0d cmds=%0d cycles_to_idle=%0d cpu_ops=%0d cpu_avg_ns=%0.0f cpu_max_ns=%0.0f underflow=%0d fences=%0d crc=%h", STACK, NCMD, t_idle - t_start, cpu_ops,
                 cpu_ops ? cpu_lat_sum / cpu_ops : 0.0, cpu_lat_max, uf_idle, gfx_fence, crc);
        if (fails == 0) $display("TEST gfx PASS"); else $display("TEST gfx FAIL (%0d)", fails);
        $finish;
    end
    initial begin #(64'd1_000_000 * 120_000); $display("TEST gfx FAIL (global time-out)"); $finish; end
endmodule
`default_nettype wire
