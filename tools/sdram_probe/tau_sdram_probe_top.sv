// Synthesis + fit probe of the new SDRAM PHY (tau_sdram_phy) and the Tau V scheduler (tau_sdram_sched_v), contract 7.5.
// MIT licence. NOT part of any core: it only exists to be compiled by Quartus (tools/sdram_probe.py) so that the area
// and the Fmax at 100 MHz are measured with the real dram_* pins, I/O standards, drive and SDC.
//
// What is real:  the PHY and the scheduler with their default parameters (NPORTS 6, 2D on port 1, the bank hash
//                with the region table BANK_HASH = 3); the dram_* pins at the pins of the Pocket (probe QSF); the
//                chip clock through ALTDDIO_OUT; the read capture through ALTDDIO_IN (FALL mode); the 100 MHz clock
//                from the same PLL as the core (mf_pllbase outclk_3, 74.25 MHz from the clk_74a pin).
// What is a stand-in: the client side. Every client signal that crosses the wrapper is one register away from a
//                VIRTUAL_PIN (named v_*), so the scheduler and the PHY are placed against free-floating registers, not
//                against constants (nothing can be optimised away) and not against I/O cells. The slow statistics and
//                diagnostic outputs (counters, perf snapshot, fault, calibration, BIST) go straight to virtual pins
//                (no register, no fold: a register or an XOR tree for ~1,500 bits would be the largest thing in the
//                wrapper and would pollute the area); their timing is not constrained.
// Variants (project macros set by `sdram_probe.py launch --variant`): PROBE_CALIBRATE, PROBE_BIST, PROBE_PERF each
// turn the PHY parameter of that name on. With none of them the PHY has every optional feature off.
// Area: read the rows u_phy and u_sched of the fitter's "Resource Utilization by Entity" (the tool prints them); the
// total includes about 750 wrapper registers (input and output registers of the client side).
`default_nettype none
/* verilator lint_off PROCASSINIT */
module tau_sdram_probe_top (
    input  wire         clk_74a,                // PIN_V15, 3.3-V LVCMOS, as in the core; 74.25 MHz
    // ---- virtual pins: client side ----
    input  wire         v_rst,
    input  wire [5:0]   v_req_valid,
    output wire [5:0]   v_req_ready,
    input  wire [5:0]   v_req_we,
    input  wire [149:0] v_req_addr,
    input  wire [59:0]  v_req_len,
    input  wire [47:0]  v_req_tag,
    input  wire [5:0]   v_req_2d,
    input  wire [35:0]  v_req_w,
    input  wire [35:0]  v_req_h,
    input  wire [65:0]  v_req_stride,
    input  wire [5:0]   v_urgent,
    input  wire [5:0]   v_urgent_early,
    input  wire         v_sk_we,
    input  wire [2:0]   v_sk_idx,
    input  wire [2:0]   v_sk_base,
    input  wire [1:0]   v_sk_val,
    output wire [5:0]   v_w_next,
    output wire [59:0]  v_w_idx,
    input  wire [95:0]  v_w_data,
    input  wire [11:0]  v_w_be,
    output wire [5:0]   v_r_valid,
    output wire [95:0]  v_r_data,
    output wire [5:0]   v_r_last,
    output wire [5:0]   v_done,
    output wire [47:0]  v_done_tag,
    // PHY optional-feature controls (registered inputs)
    input  wire         v_idle_ok,
    input  wire         v_bist_start,
    input  wire         v_perf_snap,
    input  wire         v_perf_clear,
    // slow monitor outputs (direct, unregistered)
    output wire [191:0] v_cnt_requests,
    output wire [191:0] v_cnt_words,
    output wire [191:0] v_cnt_busy,
    output wire [95:0]  v_cnt_bad,
    output wire [95:0]  v_cnt_worst_wait,
    output wire [95:0]  v_cnt_urgent_over,
    output wire [3:0]   v_dbg_err,
    output wire [15:0]  v_dbg_pmiss,
    output wire         v_init_done,
    output wire         v_phy_fault,
    output wire [15:0]  v_fault_count,
    output wire [3:0]   v_fault_cause,
    output wire [31:0]  v_refreshes,
    output wire [15:0]  v_dq_conflicts,
    output wire [4:0]   v_rd_lat,
    output wire [7:0]   v_cal_window,
    output wire [3:0]   v_cal_best,
    output wire         v_cal_done,
    output wire         v_cal_fault,
    output wire         v_bist_busy,
    output wire         v_bist_done,
    output wire [31:0]  v_bist_errors,
    output wire [24:0]  v_bist_first_addr,
    output wire [511:0] v_perf_snapshot,
    // ---- the real SDRAM pins (names and pins of core_top.v) ----
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
    localparam integer NP = 6;

`ifdef PROBE_CALIBRATE
    localparam integer P_CAL = 1;
`else
    localparam integer P_CAL = 0;
`endif
`ifdef PROBE_BIST
    localparam integer P_BIST = 1;
`else
    localparam integer P_BIST = 0;
`endif
`ifdef PROBE_PERF
    localparam integer P_PERF = 1;
`else
    localparam integer P_PERF = 0;
`endif

    // ---- clock: the core's PLL, 74.25 MHz in, outclk_3 = 100 MHz (SDRAM clock) ----
    wire clk_unused0, clk_unused1, clk_unused2, clk, pll_locked;
    mf_pllbase u_pll (.refclk(clk_74a), .rst(1'b0), .outclk_0(clk_unused0), .outclk_1(clk_unused1),
                      .outclk_2(clk_unused2), .outclk_3(clk), .locked(pll_locked));

    reg [2:0] rst_q = 3'b111;
    always @(posedge clk) rst_q <= {rst_q[1:0], ~pll_locked | v_rst};
    wire rst = rst_q[2];

    // ---- client inputs: one register each ----
    reg [NP-1:0]    req_valid_q, req_we_q, req_2d_q, urgent_q, urgent_early_q;
    reg [NP*25-1:0] req_addr_q;
    reg [NP*10-1:0] req_len_q;
    reg [NP*8-1:0]  req_tag_q;
    reg [NP*6-1:0]  req_w_q, req_h_q;
    reg [NP*11-1:0] req_stride_q;
    reg             sk_we_q;
    reg [2:0]       sk_idx_q, sk_base_q;
    reg [1:0]       sk_val_q;
    reg [NP*16-1:0] w_data_q;
    reg [NP*2-1:0]  w_be_q;
    reg             idle_ok_q, bist_start_q, perf_snap_q, perf_clear_q;
    always @(posedge clk) begin
        req_valid_q <= v_req_valid; req_we_q <= v_req_we; req_addr_q <= v_req_addr; req_len_q <= v_req_len;
        req_tag_q <= v_req_tag; req_2d_q <= v_req_2d; req_w_q <= v_req_w; req_h_q <= v_req_h;
        req_stride_q <= v_req_stride; urgent_q <= v_urgent; urgent_early_q <= v_urgent_early;
        sk_we_q <= v_sk_we; sk_idx_q <= v_sk_idx; sk_base_q <= v_sk_base; sk_val_q <= v_sk_val;
        w_data_q <= v_w_data; w_be_q <= v_w_be;
        idle_ok_q <= v_idle_ok; bist_start_q <= v_bist_start; perf_snap_q <= v_perf_snap; perf_clear_q <= v_perf_clear;
    end

    // ---- scheduler <-> PHY ----
    wire [NP-1:0]    req_ready, w_next, r_valid, r_last, done;
    wire [NP*10-1:0] w_idx;
    wire [NP*16-1:0] r_data;
    wire [NP*8-1:0]  done_tag;
    wire [2:0]  cmd;
    wire [1:0]  cmd_ba;
    wire [12:0] cmd_a;
    wire        wvalid;
    wire [15:0] wdata;
    wire [1:0]  be;
    wire [3:0]  ok_act, ok_rd, ok_wr, ok_pre, row_aged, ref_debt;
    wire        ok_prea, ok_ref, ok_bst, init_done, ref_due, preempt, preempt_ack, banks_closed;
    wire        rvalid;
    wire [15:0] rdata;

    tau_sdram_sched_v #(
        .NPORTS(NP), .BANK_HASH(3), .ENABLE_2D(8'h02), .SKEW_REGIONS(8)
    ) u_sched (
        .clk(clk), .rst(rst),
        .req_valid(req_valid_q), .req_ready(req_ready), .req_we(req_we_q), .req_addr(req_addr_q),
        .req_len(req_len_q), .req_tag(req_tag_q), .req_2d(req_2d_q), .req_w(req_w_q), .req_h(req_h_q),
        .req_stride(req_stride_q), .urgent(urgent_q), .urgent_early(urgent_early_q),
        .sk_we(sk_we_q), .sk_idx(sk_idx_q), .sk_base(sk_base_q), .sk_val(sk_val_q),
        .w_next(w_next), .w_idx(w_idx), .w_data(w_data_q), .w_be(w_be_q),
        .r_valid(r_valid), .r_data(r_data), .r_last(r_last), .done(done), .done_tag(done_tag),
        .cnt_requests(v_cnt_requests), .cnt_words(v_cnt_words), .cnt_busy(v_cnt_busy), .cnt_bad(v_cnt_bad),
        .cnt_worst_wait(v_cnt_worst_wait), .cnt_urgent_over(v_cnt_urgent_over), .dbg_err(v_dbg_err), .dbg_pmiss(v_dbg_pmiss),
        .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .wvalid(wvalid), .wdata(wdata), .be(be),
        .ok_act(ok_act), .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_bst(ok_bst), .ok_prea(ok_prea),
        .ok_ref(ok_ref), .init_done(init_done), .ref_due(ref_due), .ref_debt(ref_debt), .preempt_ack(preempt_ack),
        .rvalid(rvalid), .rdata(rdata), .row_aged(row_aged), .preempt(preempt), .banks_closed(banks_closed)
    );

    tau_sdram_phy #(
        .CALIBRATE(P_CAL), .BIST(P_BIST), .PERF(P_PERF)
    ) u_phy (
        .clk(clk), .rst(rst),
        .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .wvalid(wvalid), .wdata(wdata), .be(be),
        .ok_act(ok_act), .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_prea(ok_prea), .ok_ref(ok_ref),
        .ok_bst(ok_bst), .init_done(init_done), .rvalid(rvalid), .rdata(rdata), .phy_fault(v_phy_fault),
        .fault_count(v_fault_count), .fault_cause(v_fault_cause), .row_aged(row_aged), .preempt(preempt),
        .preempt_ack(preempt_ack), .banks_closed(banks_closed), .ref_due(ref_due), .ref_debt(ref_debt),
        .refreshes(v_refreshes), .dq_conflicts(v_dq_conflicts), .rd_lat(v_rd_lat),
        .idle_ok(idle_ok_q), .cal_window(v_cal_window), .cal_best(v_cal_best), .cal_done(v_cal_done),
        .cal_fault(v_cal_fault), .bist_start(bist_start_q), .bist_busy(v_bist_busy), .bist_done(v_bist_done),
        .bist_errors(v_bist_errors), .bist_first_addr(v_bist_first_addr), .perf_snap(perf_snap_q),
        .perf_clear(perf_clear_q), .perf_snapshot(v_perf_snapshot),
        .dram_a(dram_a), .dram_ba(dram_ba), .dram_dq(dram_dq), .dram_dqm(dram_dqm), .dram_clk(dram_clk),
        .dram_cke(dram_cke), .dram_ras_n(dram_ras_n), .dram_cas_n(dram_cas_n), .dram_we_n(dram_we_n)
    );
    assign v_init_done = init_done;

    // ---- client outputs: one register each ----
    reg [NP-1:0]    req_ready_q, w_next_q, r_valid_q, r_last_q, done_q;
    reg [NP*10-1:0] w_idx_q;
    reg [NP*16-1:0] r_data_q;
    reg [NP*8-1:0]  done_tag_q;
    always @(posedge clk) begin
        req_ready_q <= req_ready; w_next_q <= w_next; w_idx_q <= w_idx; r_valid_q <= r_valid;
        r_data_q <= r_data; r_last_q <= r_last; done_q <= done; done_tag_q <= done_tag;
    end
    assign v_req_ready = req_ready_q;
    assign v_w_next    = w_next_q;
    assign v_w_idx     = w_idx_q;
    assign v_r_valid   = r_valid_q;
    assign v_r_data    = r_data_q;
    assign v_r_last    = r_last_q;
    assign v_done      = done_q;
    assign v_done_tag  = done_tag_q;
endmodule
`default_nettype wire
