// Tau V SDRAM scheduler (reference implementation v1). MIT licence.
// Clean-room, from docs/features/SDRAM_CONTROLLER_CONTRACT.md v0.6 (sections 0, 3, 4, 5, 7.3, 8).
// CHECKPOINT 4: queues, arbitration, open-row table, ACT/PRE look-ahead, reads and writes (chained column commands,
// BST termination, write data pull with W_LEAD and a skid FIFO), refresh in idle gaps / forced at a debt level,
// preemption, aged-row closing, urgent-wait counters.
//
// URGENT BOUND (documented for the interface document; measured against it by sim/sdram_sched_v, test urgent_bound).
//   An urgent read request (port 0, the scan-out) that is pending at cycle u sees its first word on `r_valid` no later than
//     W = L_MAX + PULL_AHEAD + max(OVH_CONF + TURN, OVH_REF) + RD_LAT + PORT_REG
//   cycles after u, where (default values at 100 MHz, CL2, W_LEAD 2):
//     L_MAX      128  the longest uninterruptible grant: a read piece (max grant 128) or a write batch (BATCH_WORDS 128)
//     PULL_AHEAD   4  = W_LEAD + 2: a write grant is committed this many cycles before the end of the burst in progress
//                     and then runs to its end, so a grant can start after the urgent request arrived
//     OVH_CONF    10  = 1 (BST) + max(tWR, tRAS) 5 + tRP 2 + tRCD 2: close another row and open the urgent one
//     TURN         3  read to write turnaround (BST + 3), relevant only to an urgent write
//     OVH_REF     17  = 1 (BST) + 1 (PREA) + tRP 2 + 1 (REF) + tRFC 10 + tRCD 2: a refresh that was started (or is forced at the
//                     debt level REF_HARD_DEBT, even while an urgent request waits) just before the request arrived
//     RD_LAT       4  PHY read latency (CL2, FALL capture)
//     PORT_REG     2  request queue register and return path
//   = 128 + 4 + 17 + 4 + 2 = 155 cycles = 1.55 us (the contract's estimate was L_MAX + 27 = 155). Every term is a parameter.
//   A write grant is never interrupted; the urgent port waits for it (included in L_MAX).
//
// Structure (one command per clock):
//   * every cycle the arbiter proposes a target T = the next piece (a read piece, or the first piece of a write
//     batch). ACT/PRE for T are issued while the current burst streams (look-ahead);
//   * a burst is `cur` (bank, words left). At the boundary cycle r+n the scheduler offers, in this order, the chained
//     column command for T (any open row, no idle cycle), or BST. A read burst of n words is ended exactly at r+n;
//   * a write piece is committed (locked) W_LEAD+2 cycles before it can start (or at once when idle): its words are
//     pulled with `w_next` into a small FIFO; the WR is offered only when at least min(n, W_LEAD+1) words are pulled,
//     which guarantees that no beat of the burst can be missing. A write grant (batch) is up to BATCH_REQS requests
//     and BATCH_WORDS words from one port and is not interrupted (even by an urgent port);
//   * reads return in command order through a descriptor FIFO (port, words, last, tag).
`default_nettype none
module tau_sdram_sched_v #(
    parameter integer NPORTS = 6,
    parameter integer QDEPTH = 4,
    parameter integer BANK_HASH = 2,
    parameter [7:0]   URGENT_CAPABLE = 8'h01,
    parameter [7:0]   ENABLE_2D = 8'h02,
    parameter [7:0]   BATCH_PORTS = 8'h04,           // write ports whose writes are collected (decoder write)
    parameter [8*10-1:0] MAXG_RD = {10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd64, 10'd128},
    parameter [8*10-1:0] MAXG_WR = {10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd32, 10'd128, 10'd128},
    parameter [8*4-1:0]  WEIGHT  = {4'd1, 4'd1, 4'd1, 4'd1, 4'd2, 4'd4, 4'd4, 4'd4},
    parameter integer CLK_HZ = 100_000_000,
    parameter integer SAME_ROW_BREAK_NS = 60000,
    parameter integer W_LEAD = 2,
    parameter integer WF_DEPTH = 8,
    parameter integer RDQ_DEPTH = 16,
    parameter integer BATCH_REQS = 4,
    parameter integer BATCH_WORDS = 128,
    parameter integer WR_HI_REQ = 2,                 // dagger: a batch port becomes eligible at this many queued requests
    parameter integer WR_DEFER_NS = 1000,            // dagger: ... or when its head has waited this long (write done <= 2 us)
    parameter integer REF_SOFT_DEBT = 4,             // dagger: refresh between grants at this debt when no urgent request waits
    parameter integer REF_HARD_DEBT = 7,             // refresh at the next burst boundary at this debt, whatever waits (PHY max 8)
    parameter integer L_MAX = 128,                   // documented urgent bound terms (see the header)
    parameter integer TWR_C = 2, parameter integer TRAS_C = 5, parameter integer TRP_C = 2, parameter integer TRCD_C = 2,
    parameter integer TRFC_C = 10, parameter integer TURN_C = 3, parameter integer RD_LAT = 4, parameter integer PORT_REG = 2
) (
    input  wire                 clk,
    input  wire                 rst,
    // client ports (contract section 4); port p uses slice [p*width +: width]
    input  wire [NPORTS-1:0]    req_valid,
    output wire [NPORTS-1:0]    req_ready,
    input  wire [NPORTS-1:0]    req_we,
    input  wire [NPORTS*25-1:0] req_addr,
    input  wire [NPORTS*10-1:0] req_len,
    input  wire [NPORTS*8-1:0]  req_tag,
    input  wire [NPORTS-1:0]    req_2d,
    input  wire [NPORTS*6-1:0]  req_w,
    input  wire [NPORTS*6-1:0]  req_h,
    input  wire [NPORTS*11-1:0] req_stride,
    input  wire [NPORTS*2-1:0]  req_skew,
    input  wire [NPORTS-1:0]    urgent,
    output wire [NPORTS-1:0]    w_next,
    output wire [NPORTS*10-1:0] w_idx,
    input  wire [NPORTS*16-1:0] w_data,
    input  wire [NPORTS*2-1:0]  w_be,
    output wire [NPORTS-1:0]    r_valid,
    output wire [NPORTS*16-1:0] r_data,
    output wire [NPORTS-1:0]    r_last,
    output wire [NPORTS-1:0]    done,
    output wire [NPORTS*8-1:0]  done_tag,
    // per-port counters
    output wire [NPORTS*32-1:0] cnt_requests,
    output wire [NPORTS*32-1:0] cnt_words,
    output wire [NPORTS*32-1:0] cnt_busy,
    output wire [NPORTS*16-1:0] cnt_bad,
    output wire [NPORTS*16-1:0] cnt_worst_wait,
    output wire [NPORTS*16-1:0] cnt_urgent_over,
    output wire [3:0]           dbg_err,
    // PHY command interface (contract section 3.3)
    output reg  [2:0]           cmd,
    output reg  [1:0]           cmd_ba,
    output reg  [12:0]          cmd_a,
    output wire                 wvalid,
    output wire [15:0]          wdata,
    output wire [1:0]           be,
    input  wire [3:0]           ok_act,
    input  wire [3:0]           ok_rd,
    input  wire [3:0]           ok_wr,
    input  wire [3:0]           ok_pre,
    input  wire                 ok_bst,
    input  wire                 ok_prea,
    input  wire                 ok_ref,
    input  wire                 init_done,
    input  wire                 ref_due,
    input  wire [3:0]           ref_debt,
    output wire                 preempt_ack,
    input  wire                 rvalid,
    input  wire [15:0]          rdata,
    input  wire [3:0]           row_aged,
    input  wire                 preempt,
    input  wire                 banks_closed
);
    localparam [2:0] K_NOP = 3'd0, K_ACT = 3'd1, K_RD = 3'd2, K_WR = 3'd3, K_PRE = 3'd4, K_PREA = 3'd5, K_REF = 3'd6, K_BST = 3'd7;
    localparam [1:0] RF_NONE = 2'd0, RF_PREA = 2'd1, RF_REF = 2'd2;
    localparam integer OVH_CONF = 1 + ((TWR_C > TRAS_C) ? TWR_C : TRAS_C) + TRP_C + TRCD_C;
    localparam integer OVH_REF  = 1 + 1 + TRP_C + 1 + TRFC_C + TRCD_C;
    localparam integer OVH_MAX  = ((OVH_CONF + TURN_C) > OVH_REF) ? (OVH_CONF + TURN_C) : OVH_REF;
    localparam integer URGENT_BOUND_CLK = L_MAX + (W_LEAD + 2) + OVH_MAX + RD_LAT + PORT_REG;      // first r_valid, from the urgent request
    localparam integer URGENT_ISSUE_BOUND = URGENT_BOUND_CLK - RD_LAT - PORT_REG;                   // the column command
    localparam integer STARVE_CLK   = (CLK_HZ / 1000) * (SAME_ROW_BREAK_NS / 1000) / 1000;
    localparam integer WR_DEFER_CLK = (CLK_HZ / 1000) * (WR_DEFER_NS / 1000) / 1000;
    localparam integer PULL_AHEAD   = W_LEAD + 2;
    localparam integer WFW = $clog2(WF_DEPTH + 1);
    localparam integer RQW = $clog2(RDQ_DEPTH + 1);

    // ---------------------------------------------------------------------------------------------------
    // per-port queues
    // ---------------------------------------------------------------------------------------------------
    wire [NPORTS-1:0]    hv, hbad, hwe, hlast, h2d, acc, acc_bad;
    wire [NPORTS*2-1:0]  hbank;
    wire [NPORTS*13-1:0] hrow;
    wire [NPORTS*10-1:0] hcol, hplen;
    wire [NPORTS*8-1:0]  htag;
    wire [NPORTS*4-1:0]  hcnt;
    wire [NPORTS-1:0]    adv;
    wire [NPORTS-1:0]    retire;
    reg  [5:0]           inflight [0:NPORTS-1];
    reg  [NPORTS-1:0]    done_b_q, done_w_q;
    reg  [7:0]           done_b_tag [0:NPORTS-1];
    reg  [7:0]           done_w_tag [0:NPORTS-1];
    reg  [15:0]          bad_q [0:NPORTS-1];
    reg  [31:0]          req_cnt_q [0:NPORTS-1];
    reg  [31:0]          word_cnt_q [0:NPORTS-1];
    reg  [31:0]          busy_cnt_q [0:NPORTS-1];
    reg  [15:0]          worst_q [0:NPORTS-1];
    reg  [15:0]          uover_q [0:NPORTS-1];
    reg  [15:0]          uwc_q [0:NPORTS-1];
    genvar p;
    generate
        for (p = 0; p < NPORTS; p = p + 1) begin : g_p
            tau_sdram_sv_pq #(.DEPTH(QDEPTH), .EN_2D(ENABLE_2D[p]), .BANK_HASH(BANK_HASH),
                .MAXG_RD(MAXG_RD[p*10 +: 10]), .MAXG_WR(MAXG_WR[p*10 +: 10])) u_pq (
                .clk(clk), .rst(rst), .req_valid(req_valid[p]), .req_ready(req_ready[p]), .req_we(req_we[p]),
                .req_addr(req_addr[p*25 +: 25]), .req_len(req_len[p*10 +: 10]), .req_tag(req_tag[p*8 +: 8]),
                .req_2d(req_2d[p]), .req_w(req_w[p*6 +: 6]), .req_h(req_h[p*6 +: 6]), .req_stride(req_stride[p*11 +: 11]),
                .req_skew(req_skew[p*2 +: 2]), .count(hcnt[p*4 +: 4]), .hd_v(hv[p]), .hd_bad(hbad[p]), .hd_we(hwe[p]),
                .hd_bank(hbank[p*2 +: 2]), .hd_row(hrow[p*13 +: 13]), .hd_col(hcol[p*10 +: 10]),
                .hd_plen(hplen[p*10 +: 10]), .hd_last(hlast[p]), .hd_tag(htag[p*8 +: 8]), .hd_2d(h2d[p]),
                .adv(adv[p]), .pop_bad(retire[p]), .accepted(acc[p]), .accepted_bad(acc_bad[p]));
            // a rejected request completes in order: only when nothing of this port is still in flight
            assign retire[p] = hbad[p] & (inflight[p] == 6'd0) & ~done_b_q[p];
            assign cnt_bad[p*16 +: 16]       = bad_q[p];
            assign cnt_requests[p*32 +: 32]  = req_cnt_q[p];
            assign cnt_words[p*32 +: 32]     = word_cnt_q[p];
            assign cnt_busy[p*32 +: 32]      = busy_cnt_q[p];
            assign cnt_worst_wait[p*16 +: 16]   = worst_q[p];
            assign cnt_urgent_over[p*16 +: 16]  = uover_q[p];
        end
    endgenerate

    // ---------------------------------------------------------------------------------------------------
    // wait timers, candidates, arbitration
    // ---------------------------------------------------------------------------------------------------
    reg [15:0]        wc [0:NPORTS-1];
    reg [NPORTS-1:0]  starved;
    wire [NPORTS-1:0] issue_p;                          // a piece of this port was issued this cycle
    integer q;
    always @(posedge clk) begin
        for (q = 0; q < NPORTS; q = q + 1) begin
            if (rst || !hv[q] || issue_p[q]) begin wc[q] <= 16'd0; starved[q] <= 1'b0; end
            else begin
                if (wc[q] != 16'hffff) wc[q] <= wc[q] + 16'd1;
                starved[q] <= (wc[q] >= STARVE_CLK[15:0]);
            end
        end
    end
    reg [NPORTS-1:0] gated;
    always @* begin
        for (q = 0; q < NPORTS; q = q + 1)
            gated[q] = BATCH_PORTS[q] & hwe[q] & (hcnt[q*4 +: 4] < WR_HI_REQ[3:0]) & (wc[q] < WR_DEFER_CLK[15:0]);
    end
    wire [NPORTS-1:0] cand = hv & (~gated | {NPORTS{~(|(hv & ~gated))}});
    wire [NPORTS-1:0] urg  = urgent & URGENT_CAPABLE[NPORTS-1:0] & cand;
    reg  [2:0]  last_port; reg last_v; reg [1:0] last_bank; reg [12:0] last_row;
    wire        cont_ok = last_v && cand[last_port] && hbank[last_port*2 +: 2] == last_bank &&
                          hrow[last_port*13 +: 13] == last_row && !(|(starved & ~(8'd1 << last_port)));
    wire sel_v, sel_wrr; wire [2:0] sel_p;
    reg  grant_ev; reg [2:0] grant_pt;
    tau_sdram_sv_arb #(.NP(NPORTS), .WEIGHT(WEIGHT)) u_arb (.clk(clk), .rst(rst), .cand(cand), .urgent(urg),
        .cont_ok(cont_ok), .last_port(last_port), .grant(grant_ev), .grant_port(grant_pt), .grant_wrr(sel_wrr),
        .sel_v(sel_v), .sel_p(sel_p), .sel_wrr(sel_wrr));

    // ---------------------------------------------------------------------------------------------------
    // state of the burst in progress (cur), the committed write piece (pq_*), write FIFO, read descriptors
    // ---------------------------------------------------------------------------------------------------
    reg         cur_v, cur_we, cur_last; reg [1:0] cur_bank; reg [2:0] cur_port; reg [9:0] cur_left; reg [7:0] cur_tag;
    reg         pq_v, pq_iss, pq_last; reg [2:0] pq_port; reg [9:0] pq_n, pq_pulled;
    reg [2:0]   bt_port; reg [3:0] bt_reqs; reg [9:0] bt_words;
    reg [15:0]  wf_d [0:WF_DEPTH-1];
    reg [1:0]   wf_b [0:WF_DEPTH-1];
    reg [WFW-1:0] wf_rp, wf_wp, wf_cnt;
    reg [W_LEAD-1:0] dl_v; reg [2:0] dl_p [0:W_LEAD-1];
    reg [9:0]   pidx [0:NPORTS-1];
    reg [2:0]   rq_port [0:RDQ_DEPTH-1]; reg [9:0] rq_n [0:RDQ_DEPTH-1]; reg rq_last [0:RDQ_DEPTH-1]; reg [7:0] rq_tag [0:RDQ_DEPTH-1];
    reg [RQW-1:0] rq_rp, rq_wp, rq_cnt;
    reg [9:0]   rd_rem; reg rd_act;
    reg [3:0]   err_q;
    reg [3:0]   o_v;
    reg [12:0]  o_row [0:3];

    // ---------------------------------------------------------------------------------------------------
    // target T
    // ---------------------------------------------------------------------------------------------------
    wire        lk_v  = pq_v & ~pq_iss;
    wire [2:0]  t_p   = lk_v ? pq_port : sel_p;
    wire        t_v   = lk_v ? hv[pq_port] : sel_v;
    wire        t_we  = hwe[t_p];
    wire [1:0]  t_bank = hbank[t_p*2 +: 2];
    wire [12:0] t_row  = hrow[t_p*13 +: 13];
    wire [9:0]  t_col  = hcol[t_p*10 +: 10];
    wire [9:0]  t_n    = hplen[t_p*10 +: 10];
    wire        t_last = hlast[t_p];
    wire [7:0]  t_tag  = htag[t_p*8 +: 8];
    wire        t_open = o_v[t_bank];
    wire        t_hit  = t_open && o_row[t_bank] == t_row && !row_aged[t_bank];

    wire        boundary = cur_v && (cur_left == 10'd0);
    // refresh: one per idle gap (no urgent request waiting), between grants from REF_SOFT_DEBT, forced from REF_HARD_DEBT
    reg  [1:0]  rfm;
    wire        uw      = |(urgent & URGENT_CAPABLE[NPORTS-1:0] & hv);
    wire        rf_go   = init_done & ~preempt & ref_due & ((ref_debt >= REF_HARD_DEBT[3:0]) |
                          (~uw & ((ref_debt >= REF_SOFT_DEBT[3:0]) | ~(|hv))));
    wire        run_ok   = init_done & ~preempt & (rfm == RF_NONE) & ~rf_go;
    assign      preempt_ack = preempt & ~cur_v & init_done;
    wire [9:0]  need_pulled = (pq_n < W_LEAD[9:0] + 10'd1) ? pq_n : (W_LEAD[9:0] + 10'd1);
    wire        rd_ok    = ok_rd[t_bank] && (rq_cnt < RDQ_DEPTH[RQW-1:0] - 1);
    wire        wr_ok    = ok_wr[t_bank] && lk_v && (pq_pulled >= need_pulled) && (wf_cnt != {WFW{1'b0}});
    wire        t_ready  = run_ok && t_v && t_hit && (t_we ? wr_ok : rd_ok);
    wire        issue    = t_ready && (boundary || !cur_v);
    wire        wr_issue = issue && t_we;
    wire        rd_issue = issue && !t_we;

    // write commitment (lock): first piece of a grant near the boundary or when idle; continuation of a batch at once
    wire        bc_ok  = cur_v && cur_we && hv[cur_port] && hwe[cur_port] && (bt_port == cur_port) &&
                         (bt_reqs < BATCH_REQS[3:0]) && ({1'b0, bt_words} + {1'b0, hplen[cur_port*10 +: 10]} <= BATCH_WORDS[10:0]);
    wire        pq_free = ~pq_v;
    wire        near    = !cur_v || (cur_left <= PULL_AHEAD[9:0]);
    wire        lock_b  = run_ok && pq_free && bc_ok;
    wire        lock_a  = run_ok && pq_free && !bc_ok && t_v && t_we && near;
    wire        do_lock = lock_b | lock_a;
    wire [2:0]  lk_pt   = lock_b ? cur_port : sel_p;
    wire [9:0]  lk_n    = hplen[lk_pt*10 +: 10];
    wire        lk_lst  = hlast[lk_pt];

    // ---------------------------------------------------------------------------------------------------
    // command selection
    // ---------------------------------------------------------------------------------------------------
    reg [3:0] others_want;
    integer k, b;
    always @* begin
        others_want = 4'd0;
        for (b = 0; b < 4; b = b + 1)
            for (k = 0; k < NPORTS; k = k + 1)
                if (hv[k] && k[2:0] != t_p && hbank[k*2 +: 2] == b[1:0] && o_v[b] && hrow[k*13 +: 13] == o_row[b])
                    others_want[b] = 1'b1;
    end
    reg [3:0] aged_pre;                 // an aged row that may be closed now
    integer ab;
    always @* begin
        aged_pre = 4'd0;
        for (ab = 0; ab < 4; ab = ab + 1)
            aged_pre[ab] = row_aged[ab] & o_v[ab] & ok_pre[ab] & ~(cur_v & (cur_bank == ab[1:0]));
    end
    always @* begin
        cmd = K_NOP; cmd_ba = 2'd0; cmd_a = 13'd0;
        if (rfm == RF_PREA) begin
            if (ok_prea) cmd = K_PREA;
        end else if (rfm == RF_REF) begin
            if (ok_ref) cmd = K_REF;
        end else if (boundary) begin
            if (issue) begin cmd = t_we ? K_WR : K_RD; cmd_ba = t_bank; cmd_a = {3'b000, t_col}; end
            else begin cmd = K_BST; end
        end else if (issue) begin
            cmd = t_we ? K_WR : K_RD; cmd_ba = t_bank; cmd_a = {3'b000, t_col};
        end else if (run_ok && t_v) begin
            if (!t_open) begin
                if (ok_act[t_bank]) begin cmd = K_ACT; cmd_ba = t_bank; cmd_a = t_row; end
            end else if (!t_hit) begin
                if (ok_pre[t_bank] && !(cur_v && cur_bank == t_bank) && !(cur_v && others_want[t_bank])) begin
                    cmd = K_PRE; cmd_ba = t_bank;
                end
            end
        end
        if (cmd == K_NOP && init_done && !preempt && rfm == RF_NONE && !(cur_v && boundary)) begin
            if (aged_pre[0]) begin cmd = K_PRE; cmd_ba = 2'd0; end
            else if (aged_pre[1]) begin cmd = K_PRE; cmd_ba = 2'd1; end
            else if (aged_pre[2]) begin cmd = K_PRE; cmd_ba = 2'd2; end
            else if (aged_pre[3]) begin cmd = K_PRE; cmd_ba = 2'd3; end
        end
    end

    // ---------------------------------------------------------------------------------------------------
    // write data: pull (w_next), delay line (W_LEAD), skid FIFO, beats
    // ---------------------------------------------------------------------------------------------------
    wire        beat_stream = cur_v && cur_we && (cur_left != 10'd0);
    wire        do_beat     = wr_issue | beat_stream;
    assign      wvalid      = do_beat & (wf_cnt != {WFW{1'b0}});
    assign      wdata       = wf_d[wf_rp];
    assign      be          = wf_b[wf_rp];
    wire        wf_pop      = wvalid;
    reg  [3:0]  infl;
    integer     di;
    always @* begin
        infl = 4'd0;
        for (di = 0; di < W_LEAD; di = di + 1) infl = infl + {3'd0, dl_v[di]};
    end
    wire        pull_want = pq_v && (pq_pulled < pq_n);
    wire        pull      = pull_want && (({1'b0, wf_cnt} + {2'b0, infl} + 1'b1 - {{WFW{1'b0}}, wf_pop}) <= WF_DEPTH);
    assign      w_next    = pull ? ({{(NPORTS-1){1'b0}}, 1'b1} << pq_port) : {NPORTS{1'b0}};
    generate
        for (p = 0; p < NPORTS; p = p + 1) begin : g_wi
            assign w_idx[p*10 +: 10] = pidx[p];
        end
    endgenerate
    wire        arrive   = dl_v[W_LEAD-1];
    wire [2:0]  arr_p    = dl_p[W_LEAD-1];

    // ---------------------------------------------------------------------------------------------------
    // read return
    // ---------------------------------------------------------------------------------------------------
    wire [2:0]  f_port  = rq_port[rq_rp];
    wire [9:0]  f_n     = rq_n[rq_rp];
    wire        f_last  = rq_last[rq_rp];
    wire [7:0]  f_tag   = rq_tag[rq_rp];
    wire [9:0]  f_rem   = rd_act ? rd_rem : f_n;
    wire        rv_ok   = rvalid && (rq_cnt != {RQW{1'b0}});
    wire        rv_end  = rv_ok && (f_rem == 10'd1);
    wire        rd_done = rv_end && f_last;
    generate
        for (p = 0; p < NPORTS; p = p + 1) begin : g_rd
            assign r_valid[p] = rv_ok && (f_port == p);
            assign r_last[p]  = rd_done && (f_port == p);
            assign r_data[p*16 +: 16] = rdata;
            assign done[p]     = r_last[p] | done_w_q[p] | done_b_q[p];
            assign done_tag[p*8 +: 8] = r_last[p] ? f_tag : (done_w_q[p] ? done_w_tag[p] : done_b_tag[p]);
        end
    endgenerate
    // write completion event: the cycle of the last beat (done is visible one cycle later)
    wire        wd_ev  = (wr_issue && t_last && t_n == 10'd1) || (beat_stream && cur_left == 10'd1 && cur_last);
    wire [2:0]  wd_p   = (wr_issue && t_last && t_n == 10'd1) ? t_p : cur_port;
    wire [7:0]  wd_tag = (wr_issue && t_last && t_n == 10'd1) ? t_tag : cur_tag;

    assign adv = issue ? ({{(NPORTS-1){1'b0}}, 1'b1} << t_p) : {NPORTS{1'b0}};
    assign issue_p = adv;
    assign dbg_err = err_q;

    always @* begin
        grant_ev = 1'b0; grant_pt = t_p;
        if (rd_issue) begin grant_ev = 1'b1; grant_pt = t_p; end
        else if (lock_a) begin grant_ev = 1'b1; grant_pt = sel_p; end
    end

    // ---------------------------------------------------------------------------------------------------
    // sequential state
    // ---------------------------------------------------------------------------------------------------
    integer i;
    reg [5:0] infl_n;
    always @(posedge clk) begin
        if (rst) begin
            cur_v <= 1'b0; cur_we <= 1'b0; cur_last <= 1'b0; cur_bank <= 2'd0; cur_port <= 3'd0; cur_left <= 10'd0; cur_tag <= 8'd0;
            pq_v <= 1'b0; pq_iss <= 1'b0; pq_last <= 1'b0; pq_port <= 3'd0; pq_n <= 10'd0; pq_pulled <= 10'd0;
            bt_port <= 3'd0; bt_reqs <= 4'd0; bt_words <= 10'd0;
            wf_rp <= {WFW{1'b0}}; wf_wp <= {WFW{1'b0}}; wf_cnt <= {WFW{1'b0}};
            dl_v <= {W_LEAD{1'b0}};
            rq_rp <= {RQW{1'b0}}; rq_wp <= {RQW{1'b0}}; rq_cnt <= {RQW{1'b0}}; rd_rem <= 10'd0; rd_act <= 1'b0;
            err_q <= 4'd0; o_v <= 4'd0; rfm <= RF_NONE;
            last_port <= 3'd0; last_v <= 1'b0; last_bank <= 2'd0; last_row <= 13'd0;
            done_b_q <= {NPORTS{1'b0}}; done_w_q <= {NPORTS{1'b0}};
            for (i = 0; i < NPORTS; i = i + 1) begin
                inflight[i] <= 6'd0; pidx[i] <= 10'd0; bad_q[i] <= 16'd0; req_cnt_q[i] <= 32'd0; word_cnt_q[i] <= 32'd0;
                busy_cnt_q[i] <= 32'd0; done_b_tag[i] <= 8'd0; done_w_tag[i] <= 8'd0;
                worst_q[i] <= 16'd0; uover_q[i] <= 16'd0; uwc_q[i] <= 16'd0;
            end
        end else begin
            // ---- burst in progress
            if (issue) begin
                cur_v <= 1'b1; cur_we <= t_we; cur_last <= t_last; cur_bank <= t_bank; cur_port <= t_p; cur_tag <= t_tag;
                cur_left <= t_n - 10'd1;
                last_port <= t_p; last_v <= 1'b1; last_bank <= t_bank; last_row <= t_row;
            end else if (boundary) begin
                cur_v <= 1'b0;
            end else if (cur_v && cur_left != 10'd0) begin
                cur_left <= cur_left - 10'd1;
            end
            // ---- refresh sequence
            if (banks_closed) rfm <= RF_NONE;
            else case (rfm)
                RF_NONE: if (rf_go && (boundary || !cur_v)) rfm <= RF_PREA;
                RF_PREA: if (cmd == K_PREA) rfm <= RF_REF;
                RF_REF:  if (cmd == K_REF) rfm <= RF_NONE;
                default: rfm <= RF_NONE;
            endcase
            // ---- open-row table
            if (banks_closed || cmd == K_PREA) o_v <= 4'd0;
            else if (cmd == K_ACT) begin o_v[cmd_ba] <= 1'b1; o_row[cmd_ba] <= cmd_a; end
            else if (cmd == K_PRE) o_v[cmd_ba] <= 1'b0;
            // ---- write commitment, pull cursor
            if (do_lock) begin
                pq_v <= 1'b1; pq_iss <= 1'b0; pq_port <= lk_pt; pq_n <= lk_n; pq_pulled <= 10'd0; pq_last <= lk_lst;
                if (lock_b) begin
                    bt_words <= bt_words + lk_n; bt_reqs <= bt_reqs + {3'd0, lk_lst};
                end else begin
                    bt_port <= lk_pt; bt_words <= lk_n; bt_reqs <= {3'd0, lk_lst};
                end
            end else begin
                if (wr_issue) pq_iss <= 1'b1;
                if (pull) pq_pulled <= pq_pulled + 10'd1;
                if ((pq_iss || wr_issue) && (pq_pulled + {9'd0, pull} == pq_n)) pq_v <= 1'b0;
            end
            if (pull) begin
                if (pq_last && pq_pulled == pq_n - 10'd1) pidx[pq_port] <= 10'd0;
                else pidx[pq_port] <= pidx[pq_port] + 10'd1;
            end
            // ---- delay line and write FIFO
            dl_v[0] <= pull; dl_p[0] <= pq_port;
            for (i = 1; i < W_LEAD; i = i + 1) begin dl_v[i] <= dl_v[i-1]; dl_p[i] <= dl_p[i-1]; end
            if (arrive) begin
                wf_d[wf_wp] <= w_data[arr_p*16 +: 16];
                wf_b[wf_wp] <= w_be[arr_p*2 +: 2];
                wf_wp <= (wf_wp == WF_DEPTH[WFW-1:0] - 1) ? {WFW{1'b0}} : wf_wp + 1'b1;
            end
            if (wf_pop) wf_rp <= (wf_rp == WF_DEPTH[WFW-1:0] - 1) ? {WFW{1'b0}} : wf_rp + 1'b1;
            wf_cnt <= wf_cnt + {{(WFW-1){1'b0}}, arrive} - {{(WFW-1){1'b0}}, wf_pop};
            if (do_beat && wf_cnt == {WFW{1'b0}}) err_q[0] <= 1'b1;
            // ---- read descriptors and return
            if (rd_issue) begin
                rq_port[rq_wp] <= t_p; rq_n[rq_wp] <= t_n; rq_last[rq_wp] <= t_last; rq_tag[rq_wp] <= t_tag;
                rq_wp <= (rq_wp == RDQ_DEPTH[RQW-1:0] - 1) ? {RQW{1'b0}} : rq_wp + 1'b1;
            end
            if (rv_end) rq_rp <= (rq_rp == RDQ_DEPTH[RQW-1:0] - 1) ? {RQW{1'b0}} : rq_rp + 1'b1;
            rq_cnt <= rq_cnt + {{(RQW-1){1'b0}}, rd_issue} - {{(RQW-1){1'b0}}, rv_end};
            if (rv_ok) begin
                if (f_rem == 10'd1) rd_act <= 1'b0; else begin rd_act <= 1'b1; rd_rem <= f_rem - 10'd1; end
            end
            if (rvalid && rq_cnt == {RQW{1'b0}}) err_q[2] <= 1'b1;
            // ---- done flags, in-flight counts, counters
            done_w_q <= {NPORTS{1'b0}};
            if (wd_ev) begin done_w_q[wd_p] <= 1'b1; done_w_tag[wd_p] <= wd_tag; end
            done_b_q <= retire;
            for (i = 0; i < NPORTS; i = i + 1) begin
                if (retire[i]) done_b_tag[i] <= htag[i*8 +: 8];
                if (retire[i] && bad_q[i] != 16'hffff) bad_q[i] <= bad_q[i] + 16'd1;
                if (acc[i] && !acc_bad[i]) req_cnt_q[i] <= req_cnt_q[i] + 32'd1;
                infl_n = inflight[i];
                if (issue && t_last && t_p == i) infl_n = infl_n + 6'd1;
                if ((rd_done && f_port == i) || (wd_ev && wd_p == i)) infl_n = infl_n - 6'd1;
                inflight[i] <= infl_n;
                if (issue && t_p == i) word_cnt_q[i] <= word_cnt_q[i] + {22'd0, t_n};
                if (cur_v && cur_port == i) busy_cnt_q[i] <= busy_cnt_q[i] + 32'd1;
                // worst wait (grant minus request) and urgent waits above the documented bound
                if (issue_p[i] && wc[i] > worst_q[i]) worst_q[i] <= wc[i];
                if (urg[i] && !issue_p[i]) begin if (uwc_q[i] != 16'hffff) uwc_q[i] <= uwc_q[i] + 16'd1; end
                else uwc_q[i] <= 16'd0;
                if (issue_p[i] && urg[i] && uwc_q[i] > URGENT_ISSUE_BOUND) begin
                    if (uover_q[i] != 16'hffff) uover_q[i] <= uover_q[i] + 16'd1;
                end
            end
        end
    end
endmodule
`default_nettype wire
