// Tau Alpha's SDRAM scheduler path ("sched_a"): PHY + tau_sdram_sched_v (3 ports) + the graphics-master adapter. MIT licence.
// Contract: docs/features/SDRAM_CONTROLLER_CONTRACT.md section 6; design notes: docs/features/SDRAM_SCHED_A_NOTES.md.
//
//   port 0  scan-out FILL (urgent)  \   both from the graphics master (tau_gfx), one request at a time,
//   port 1  draw engine             /   split by the master's p0_is_fill sideband   (tau_sdram_a_gfx_if)
//   port 2  CPU bridge              scheduler protocol port, driven by tau_sdram_a_cpu (clk_sdram side)
//
// sched_v configuration for 3 ports: no 2D, no region table (BANK_HASH 2), no bandwidth caps, no early-urgent, no batching
// (the engine has one request in flight, a batch would only add its 1 us defer), queue depth 1 (one request per port is
// ever outstanding; see the ablation), CNT_EN as a parameter (per-port statistics for the MMIO block, off by default).
// This module is a drop-in alternative to tau_sdram_arbiter + tau_sdram_ctrl on the graphics side: the p0 pins of tau_gfx
// connect here unchanged (plus the two sideband outputs), and the DRAM pins are the same as the old controller's.
`default_nettype none
module tau_sdram_sched_a #(
    parameter integer CLK_HZ     = 100_000_000,
    parameter integer CL         = 2,
    parameter integer QDEPTH     = 1,
    parameter integer BANK_HASH  = 2,
    parameter integer CNT_EN     = 0,
    parameter [3*4-1:0]  WEIGHT  = {4'd1, 4'd1, 4'd1},
    parameter [3*10-1:0] MAXG_RD = {10'd128, 10'd128, 10'd128},
    parameter [3*10-1:0] MAXG_WR = {10'd128, 10'd128, 10'd128},
    // simulation hooks (the PHY's parameters of the same name)
    parameter integer INIT_WAIT_CYCLES    = 10_000,
    parameter integer SIM_SHORT_INIT      = 0,
    parameter integer REFRESH_INTERVAL_PS = 7_800_000
) (
    input  wire         clk,                 // the SDRAM clock (100 MHz)
    input  wire         rst_in,              // asynchronous, active high (~pll_locked); synchronised inside
    output wire         rst_out,             // the synchronised reset, for the CPU bridge's SDRAM half
    output wire         init_done,
    output wire         busy,                // any port has a request pending or outstanding (the busy-cycle counter's input)

    // graphics master: the player's p0 protocol (tau_gfx)
    input  wire [24:0]  p0_addr,
    input  wire [15:0]  p0_data,
    input  wire [1:0]   p0_byte_en,
    input  wire [10:0]  p0_wr_len,
    input  wire         p0_wr_stream,
    input  wire         p0_wr_req,
    input  wire         p0_rd_req,
    input  wire         p0_end_burst_req,
    input  wire         p0_is_fill,
    input  wire [9:0]   p0_rd_len,
    output wire [15:0]  p0_q,
    output wire         p0_available,
    output wire         p0_ready,
    output wire         p0_data_available,
    output wire [10:0]  wsrc_addr,
    input  wire [15:0]  wsrc_q,

    // CPU port (port 2), scheduler protocol, driven by tau_sdram_a_cpu
    input  wire         cpu_req_valid,
    output wire         cpu_req_ready,
    input  wire         cpu_req_we,
    input  wire [24:0]  cpu_req_addr,
    input  wire [9:0]   cpu_req_len,
    output wire         cpu_w_next,
    output wire [9:0]   cpu_w_idx,
    input  wire [15:0]  cpu_w_data,
    input  wire [1:0]   cpu_w_be,
    output wire         cpu_r_valid,
    output wire [15:0]  cpu_r_data,
    output wire         cpu_r_last,
    output wire         cpu_done,

    // statistics (CNT_EN = 1) and diagnostics
    output wire [3*32-1:0] cnt_requests,
    output wire [3*32-1:0] cnt_words,
    output wire [3*32-1:0] cnt_busy,
    output wire [3*16-1:0] cnt_bad,
    output wire [3*16-1:0] cnt_worst_wait,
    output wire [3*16-1:0] cnt_urgent_over,
    output wire [15:0]  dbg_pmiss,
    output wire         phy_fault,
    output wire [15:0]  phy_fault_count,
    output wire [31:0]  refreshes,

    // SDRAM pins (the names and pins of core_top.v)
    output wire [12:0]  dram_a,
    output wire [1:0]   dram_ba,
    inout  wire [15:0]  dram_dq,
    output wire [1:0]   dram_dqm,
    output wire         dram_clk,
    output wire         dram_cke,
    output wire         dram_ras_n,
    output wire         dram_cas_n,
    output wire         dram_we_n
);
    localparam integer NP = 3;

    reg [2:0] rst_q = 3'b111;
    always @(posedge clk or posedge rst_in) begin
        if (rst_in) rst_q <= 3'b111;
        else        rst_q <= {rst_q[1:0], 1'b0};
    end
    wire rst = rst_q[2];
    assign rst_out = rst;

    // ---------------- client side of the scheduler ----------------
    wire [1:0]  g_req_valid, g_req_ready, g_w_next, g_r_valid, g_r_last, g_done;
    wire        g_req_we;
    wire [24:0] g_req_addr;
    wire [9:0]  g_req_len;
    wire [15:0] g_w_data;
    wire [1:0]  g_w_be;
    wire [19:0] g_w_idx;
    wire [31:0] g_r_data;

    wire [NP-1:0]    req_valid = {cpu_req_valid, g_req_valid};
    wire [NP-1:0]    req_ready;
    wire [NP-1:0]    req_we    = {cpu_req_we, g_req_we, g_req_we};
    wire [NP*25-1:0] req_addr  = {cpu_req_addr, g_req_addr, g_req_addr};
    wire [NP*10-1:0] req_len   = {cpu_req_len, g_req_len, g_req_len};
    wire [NP-1:0]    w_next;
    wire [NP*10-1:0] w_idx;
    wire [NP*16-1:0] w_data    = {cpu_w_data, g_w_data, g_w_data};
    wire [NP*2-1:0]  w_be      = {cpu_w_be, g_w_be, g_w_be};
    wire [NP-1:0]    r_valid, r_last, done;
    wire [NP*16-1:0] r_data;
    wire [NP*8-1:0]  done_tag;

    assign g_req_ready = req_ready[1:0];
    assign g_w_next    = w_next[1:0];
    assign g_w_idx     = w_idx[19:0];
    assign g_r_valid   = r_valid[1:0];
    assign g_r_data    = r_data[31:0];
    assign g_r_last    = r_last[1:0];
    assign g_done      = done[1:0];
    assign cpu_req_ready = req_ready[2];
    assign cpu_w_next    = w_next[2];
    assign cpu_w_idx     = w_idx[29:20];
    assign cpu_r_valid   = r_valid[2];
    assign cpu_r_data    = r_data[47:32];
    assign cpu_r_last    = r_last[2];
    assign cpu_done      = done[2];

    tau_sdram_a_gfx_if u_gfx (
        .clk(clk), .rst(rst), .init_done(init_done),
        .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(p0_byte_en), .p0_wr_len(p0_wr_len), .p0_wr_stream(p0_wr_stream),
        .p0_wr_req(p0_wr_req), .p0_rd_req(p0_rd_req), .p0_end_burst_req(p0_end_burst_req),
        .p0_is_fill(p0_is_fill), .p0_rd_len(p0_rd_len),
        .p0_q(p0_q), .p0_available(p0_available), .p0_ready(p0_ready), .p0_data_available(p0_data_available),
        .wsrc_addr(wsrc_addr), .wsrc_q(wsrc_q),
        .req_valid(g_req_valid), .req_ready(g_req_ready), .req_we(g_req_we), .req_addr(g_req_addr), .req_len(g_req_len),
        .w_next(g_w_next), .w_idx(g_w_idx), .w_data(g_w_data), .w_be(g_w_be),
        .r_valid(g_r_valid), .r_data(g_r_data), .r_last(g_r_last), .done(g_done));

    // ---------------- scheduler and PHY ----------------
    wire [2:0]  cmd;
    wire [1:0]  cmd_ba;
    wire [12:0] cmd_a;
    wire        wvalid;
    wire [15:0] wdata;
    wire [1:0]  be;
    wire [3:0]  ok_act, ok_rd, ok_wr, ok_pre, row_aged, ref_debt;
    wire        ok_prea, ok_ref, ok_bst, ref_due, preempt, preempt_ack, banks_closed, rvalid;
    wire [15:0] rdata;
    wire [3:0]  dbg_err;

    tau_sdram_sched_v #(
        .NPORTS(NP), .QDEPTH(QDEPTH), .BANK_HASH(BANK_HASH), .URGENT_CAPABLE(8'h01), .URGENT_EARLY_CAPABLE(8'h00),
        .ENABLE_2D(8'h00), .BATCH_PORTS(8'h00), .CNT_EN(CNT_EN), .CLK_HZ(CLK_HZ), .CL(CL), .RD_LAT(CL + 2),
        .W_LEAD(2), .WEIGHT({20'd0, WEIGHT}), .MAXG_RD({50'd0, MAXG_RD}), .MAXG_WR({50'd0, MAXG_WR})
    ) u_sched (
        .clk(clk), .rst(rst),
        .req_valid(req_valid), .req_ready(req_ready), .req_we(req_we), .req_addr(req_addr), .req_len(req_len),
        .req_tag({NP{8'd0}}), .req_2d({NP{1'b0}}), .req_w({NP{6'd0}}), .req_h({NP{6'd0}}), .req_stride({NP{11'd0}}),
        .urgent(3'b001), .urgent_early(3'b000),
        .sk_we(1'b0), .sk_idx(3'd0), .sk_base(3'd0), .sk_val(2'd0),
        .w_next(w_next), .w_idx(w_idx), .w_data(w_data), .w_be(w_be),
        .r_valid(r_valid), .r_data(r_data), .r_last(r_last), .done(done), .done_tag(done_tag),
        .cnt_requests(cnt_requests), .cnt_words(cnt_words), .cnt_busy(cnt_busy), .cnt_bad(cnt_bad),
        .cnt_worst_wait(cnt_worst_wait), .cnt_urgent_over(cnt_urgent_over), .dbg_err(dbg_err), .dbg_pmiss(dbg_pmiss),
        .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .wvalid(wvalid), .wdata(wdata), .be(be),
        .ok_act(ok_act), .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_bst(ok_bst), .ok_prea(ok_prea), .ok_ref(ok_ref),
        .init_done(init_done), .ref_due(ref_due), .ref_debt(ref_debt), .preempt_ack(preempt_ack),
        .rvalid(rvalid), .rdata(rdata), .row_aged(row_aged), .preempt(preempt), .banks_closed(banks_closed));

    tau_sdram_phy #(
        .CLK_HZ(CLK_HZ), .CL(CL), .INIT_WAIT_CYCLES(INIT_WAIT_CYCLES), .SIM_SHORT_INIT(SIM_SHORT_INIT),
        .REFRESH_INTERVAL_PS(REFRESH_INTERVAL_PS)
    ) u_phy (
        .clk(clk), .rst(rst), .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .wvalid(wvalid), .wdata(wdata), .be(be),
        .ok_act(ok_act), .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_prea(ok_prea), .ok_ref(ok_ref), .ok_bst(ok_bst),
        .init_done(init_done), .rvalid(rvalid), .rdata(rdata), .phy_fault(phy_fault), .fault_count(phy_fault_count),
        .fault_cause(), .row_aged(row_aged), .preempt(preempt), .preempt_ack(preempt_ack), .banks_closed(banks_closed),
        .ref_due(ref_due), .ref_debt(ref_debt), .refreshes(refreshes), .dq_conflicts(), .rd_lat(),
        .idle_ok(1'b0), .cal_window(), .cal_best(), .cal_done(), .cal_fault(), .bist_start(1'b0), .bist_busy(), .bist_done(),
        .bist_errors(), .bist_first_addr(), .perf_snap(1'b0), .perf_clear(1'b0), .perf_snapshot(),
        .dram_a(dram_a), .dram_ba(dram_ba), .dram_dq(dram_dq), .dram_dqm(dram_dqm), .dram_clk(dram_clk),
        .dram_cke(dram_cke), .dram_ras_n(dram_ras_n), .dram_cas_n(dram_cas_n), .dram_we_n(dram_we_n));

    // busy: a request is presented or accepted and not yet completed on any port (the old "controller not idle")
    reg [NP-1:0] outst;
    always @(posedge clk) begin
        if (rst) outst <= {NP{1'b0}};
        else     outst <= (outst | (req_valid & req_ready)) & ~done;
    end
    assign busy = (|outst) | (|req_valid);

    wire _unused = &{1'b0, dbg_err, done_tag, w_next[2:0]};
endmodule
`default_nettype wire
