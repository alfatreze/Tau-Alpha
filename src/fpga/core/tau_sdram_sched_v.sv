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
// Lint: constant width conversions of parameter arithmetic, loop temporaries in clocked blocks and reserved/unused
// inputs are intended.
/* verilator lint_off WIDTHEXPAND */
/* verilator lint_off WIDTHTRUNC */
/* verilator lint_off BLKSEQ */
/* verilator lint_off UNUSEDSIGNAL */
module tau_sdram_sched_v #(
    parameter integer NPORTS = 6,
    parameter integer QDEPTH = 2,
    parameter integer BANK_HASH = 2,
    parameter [7:0]   URGENT_CAPABLE = 8'h01,
    parameter [7:0]   URGENT_EARLY_CAPABLE = 8'h00,  // ports with the second (early) watermark level
    parameter integer SKEW_REGIONS = 8,
    parameter [8*16-1:0] BW_CAP_WORDS = 128'd0,      // per-port words per window; 0 = no cap
    parameter integer BW_WINDOW_CYCLES = 0,          // 0 = caps off
    parameter [7:0]   ENABLE_2D = 8'h02,
    parameter [7:0]   BATCH_PORTS = 8'h04,           // write ports whose writes are collected (decoder write)
    parameter [8*10-1:0] MAXG_RD = {10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd64, 10'd128},
    parameter [8*10-1:0] MAXG_WR = {10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd32, 10'd128, 10'd128},
    parameter [8*4-1:0]  WEIGHT  = {4'd1, 4'd1, 4'd1, 4'd1, 4'd2, 4'd4, 4'd4, 4'd4},
    parameter integer CLK_HZ = 100_000_000,
    parameter integer SAME_ROW_BREAK_NS = 60000,
    parameter integer W_LEAD = 2,
    parameter integer WF_DEPTH = 8,
    parameter integer RDQ_DEPTH = 8,
    parameter integer CNT_EN = 0,                    // 1: requests/words/busy/worst-wait/urgent-over counters (about 140 flops per port); 0 (default): removed, bad stays
    parameter integer CL = 2,                        // the PHY's CAS latency (read-to-write turnaround of the timing model)
    parameter integer LOOKAHEAD = 1,                 // 1: ACT/PRE for the next target while a burst streams
    parameter integer RAW_CHECK = 1,                 // 1: a read waits for an older overlapping write of another port
    parameter integer SAME_ROW_CONT = 1,             // 1: same-row pieces of the last port go back to back
    parameter integer BATCH_REQS = 4,
    parameter integer BATCH_WORDS = 128,
    parameter integer WR_HI_REQ = 2,                 // dagger: a batch port becomes eligible at this many queued requests
    parameter integer WR_DEFER_NS = 1000,            // dagger: ... or when its head has waited this long (write done <= 2 us)
    parameter integer REF_SOFT_DEBT = 4,             // dagger: refresh between grants at this debt when no urgent request waits
    parameter integer REF_HARD_DEBT = 7,             // refresh at the next burst boundary at this debt, whatever waits (PHY max 8)
    parameter integer L_MAX = 128,                   // documented urgent bound terms (see the header)
    parameter integer TWR_C = 2, parameter integer TRAS_C = 5, parameter integer TRP_C = 2, parameter integer TRCD_C = 2,
    parameter integer WRP_C = 2, parameter integer DAL_C = 4, parameter integer RRD_C = 2, parameter integer RC_PLAN = 6, parameter integer RW_C = 3,
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
    input  wire [NPORTS-1:0]    urgent,
    input  wire [NPORTS-1:0]    urgent_early,
    // region skew table (BANK_HASH 3): program before the first request; per-request skew does not exist
    input  wire                 sk_we,
    input  wire [2:0]           sk_idx,
    input  wire [2:0]           sk_base,
    input  wire [1:0]           sk_val,
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
    output wire [15:0]          dbg_pmiss,      // offers the PHY's permits did not allow (the scheduler's timing model was wrong): must stay 0
    // PHY command interface (contract section 3.3)
    output reg  [2:0]           cmd,
    output reg  [1:0]           cmd_ba,
    output reg  [12:0]          cmd_a,
    output reg                  wvalid,
    output reg  [15:0]          wdata,
    output reg  [1:0]           be,
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
    output reg                  preempt_ack,
    input  wire                 rvalid,
    input  wire [15:0]          rdata,
    input  wire [3:0]           row_aged,
    input  wire                 preempt,
    input  wire                 banks_closed
);
    // port-indexed selects as AND-OR muxes (no variable-index multiplication: no DSP block)
    function [1:0] m2(input [NPORTS*2-1:0] v, input [2:0] i);
        integer k; begin m2 = 2'd0; for (k = 0; k < NPORTS; k = k + 1) if (i == k) m2 = v[k*2 +: 2]; end
    endfunction
    function [7:0] m8(input [NPORTS*8-1:0] v, input [2:0] i);
        integer k; begin m8 = 8'd0; for (k = 0; k < NPORTS; k = k + 1) if (i == k) m8 = v[k*8 +: 8]; end
    endfunction
    function [9:0] m10(input [NPORTS*10-1:0] v, input [2:0] i);
        integer k; begin m10 = 10'd0; for (k = 0; k < NPORTS; k = k + 1) if (i == k) m10 = v[k*10 +: 10]; end
    endfunction
    function [12:0] m13(input [NPORTS*13-1:0] v, input [2:0] i);
        integer k; begin m13 = 13'd0; for (k = 0; k < NPORTS; k = k + 1) if (i == k) m13 = v[k*13 +: 13]; end
    endfunction
    function [15:0] m16(input [NPORTS*16-1:0] v, input [2:0] i);
        integer k; begin m16 = 16'd0; for (k = 0; k < NPORTS; k = k + 1) if (i == k) m16 = v[k*16 +: 16]; end
    endfunction
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
    // region skew table
    reg [SKEW_REGIONS-1:0]   sk_vld;
    reg [SKEW_REGIONS*3-1:0] sk_basev;
    reg [SKEW_REGIONS*2-1:0] sk_valv;
    always @(posedge clk) begin
        if (rst) sk_vld <= {SKEW_REGIONS{1'b0}};
        else if (sk_we && sk_idx < SKEW_REGIONS) begin
            sk_vld[sk_idx] <= 1'b1; sk_basev[sk_idx*3 +: 3] <= sk_base; sk_valv[sk_idx*2 +: 2] <= sk_val;
        end
    end
    reg [23:0] acc_cnt;
    always @(posedge clk) acc_cnt <= rst ? 24'd0 : acc_cnt + 24'd1;
    wire [NPORTS*QDEPTH-1:0]    e_v, e_we;
    wire [NPORTS*QDEPTH*13-1:0] e_lo, e_hi;
    genvar p;
    generate
        for (p = 0; p < NPORTS; p = p + 1) begin : g_p
            tau_sdram_sv_pq #(.DEPTH(QDEPTH), .EN_2D(ENABLE_2D[p]), .BANK_HASH(BANK_HASH), .SKR(SKEW_REGIONS),
                .MAXG_RD(MAXG_RD[p*10 +: 10]), .MAXG_WR(MAXG_WR[p*10 +: 10])) u_pq (
                .clk(clk), .rst(rst), .req_valid(req_valid[p]), .req_ready(req_ready[p]), .req_we(req_we[p]),
                .req_addr(req_addr[p*25 +: 25]), .req_len(req_len[p*10 +: 10]), .req_tag(req_tag[p*8 +: 8]),
                .req_2d(req_2d[p]), .req_w(req_w[p*6 +: 6]), .req_h(req_h[p*6 +: 6]), .req_stride(req_stride[p*11 +: 11]),
                .sk_vld(sk_vld), .sk_base(sk_basev), .sk_val(sk_valv),
                .ent_v(e_v[p*QDEPTH +: QDEPTH]), .ent_we(e_we[p*QDEPTH +: QDEPTH]), .ent_lo(e_lo[p*QDEPTH*13 +: QDEPTH*13]),
                .ent_hi(e_hi[p*QDEPTH*13 +: QDEPTH*13]), .count(hcnt[p*4 +: 4]), .hd_v(hv[p]), .hd_bad(hbad[p]), .hd_we(hwe[p]),
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
    // ordering: a read is not granted while another port holds a queued write whose 4K-word row range overlaps the read's
    // (conservative, no ordering stamps and no multipliers: a newer overlapping write also holds the read back, which cannot deadlock
    // because writes are never held back by reads)
    reg [NPORTS-1:0] blocked;
    integer hq, he;
    reg [12:0] r_lo, r_hi, w_lo, w_hi;
    always @* begin
        blocked = {NPORTS{1'b0}}; w_lo = 13'd0; w_hi = 13'd0;
        for (q = 0; q < NPORTS; q = q + 1) begin
            r_lo = e_lo[(q*QDEPTH)*13 +: 13]; r_hi = e_hi[(q*QDEPTH)*13 +: 13];
            if (hv[q] && !hwe[q]) begin
                for (hq = 0; hq < NPORTS; hq = hq + 1)
                    for (he = 0; he < QDEPTH; he = he + 1) begin
                        w_lo = e_lo[(hq*QDEPTH+he)*13 +: 13]; w_hi = e_hi[(hq*QDEPTH+he)*13 +: 13];
                        if (hq != q && e_v[hq*QDEPTH+he] && e_we[hq*QDEPTH+he] && w_lo <= r_hi && r_lo <= w_hi)
                            blocked[q] = 1'b1;
                    end
            end
`ifdef MUT_NO_RAW
            blocked[q] = 1'b0;
`endif
            if (RAW_CHECK == 0) blocked[q] = 1'b0;
        end
    end
    // bandwidth caps: words per window per port (non-urgent competition only)
    reg [NPORTS*16-1:0] bw_used_v;
    reg [23:0] bw_win;
    wire [NPORTS-1:0] capped;
    genvar cp;
    generate
        for (cp = 0; cp < NPORTS; cp = cp + 1) begin : g_cap
`ifdef MUT_NOCAP
            assign capped[cp] = 1'b0;
`else
            assign capped[cp] = (BW_WINDOW_CYCLES != 0) && (BW_CAP_WORDS[cp*16 +: 16] != 16'd0) && (bw_used_v[cp*16 +: 16] >= BW_CAP_WORDS[cp*16 +: 16]);
`endif
        end
    endgenerate
    // ---------------------------------------------------------------------------------------------------
    // decision pipeline: every stage is a register bank fed by a few logic levels
    //   A: per-port flags from the registered head pieces (RAW block, batch gate, cap, row hit, same row, other-wants)
    //   B: candidate / urgent / early / continuation vectors and the batch-continuation flag
    //   C: arbitration result (urgent > early > continuation > WRR); batch continuation overrides
    //   D: the target T: its piece fields and row-hit flag selected by port
    // A table or head change (an issue, ACT, PRE, PREA, banks_closed) makes the stages stale for up to 5 cycles: T is blanked
    // (`blank_q`) meanwhile, so nothing is decided from stale data. Commands are decided in the cycle of the offer from registers
    // and the PHY permits only.
    // ---------------------------------------------------------------------------------------------------
    reg [NPORTS-1:0] blocked_q, gated_q, capped_q, hhit_q, hopen_q, hsame_q, hfit_q;
    reg [3:0]        ow_q;
    reg              uw_q;
    reg  [2:0]  last_port; reg last_v; reg [1:0] last_bank; reg [12:0] last_row;
    reg  [3:0]  o_v;
    reg  [12:0] o_row [0:3];
    reg         cur_v, cur_we; reg [2:0] cur_port;
    reg [2:0]   bt_port; reg [3:0] bt_reqs; reg [9:0] bt_words;
    reg  [1:0]  rfm;
    integer sa;
    always @(posedge clk) begin
        blocked_q <= blocked; gated_q <= gated; capped_q <= capped;
        uw_q <= |(urgent & URGENT_CAPABLE[NPORTS-1:0] & hv);
        for (sa = 0; sa < NPORTS; sa = sa + 1) begin
            hopen_q[sa] <= o_v[hbank[sa*2 +: 2]];
            hhit_q[sa]  <= o_v[hbank[sa*2 +: 2]] && o_row[hbank[sa*2 +: 2]] == hrow[sa*13 +: 13] && !row_aged[hbank[sa*2 +: 2]];
            hsame_q[sa] <= (hbank[sa*2 +: 2] == last_bank) && (hrow[sa*13 +: 13] == last_row);
            hfit_q[sa]  <= ({1'b0, bt_words} + {1'b0, hplen[sa*10 +: 10]}) <= BATCH_WORDS[10:0];
        end
    end
    reg [3:0] ow_c;
    integer ok_, ob;
    always @* begin
        ow_c = 4'd0;
        for (ob = 0; ob < 4; ob = ob + 1)
            for (ok_ = 0; ok_ < NPORTS; ok_ = ok_ + 1)
                if (hv[ok_] && hbank[ok_*2 +: 2] == ob[1:0] && o_v[ob] && hrow[ok_*13 +: 13] == o_row[ob]) ow_c[ob] = 1'b1;
    end
    always @(posedge clk) ow_q <= ow_c;

    wire [NPORTS-1:0] hv_u  = hv & ~blocked_q;
    wire [NPORTS-1:0] cand0 = hv_u & (~gated_q | {NPORTS{~(|(hv_u & ~gated_q))}});
    wire [NPORTS-1:0] urg   = urgent & URGENT_CAPABLE[NPORTS-1:0] & cand0;
    wire [NPORTS-1:0] cand_nc = cand0 & ~capped_q;
    wire [NPORTS-1:0] cand  = (|cand_nc) ? (cand_nc | urg) : cand0;
    wire [NPORTS-1:0] early = urgent_early & URGENT_EARLY_CAPABLE[NPORTS-1:0] & cand;
    wire        cont_c  = (SAME_ROW_CONT != 0) && last_v && cand[last_port] && hsame_q[last_port] && !(|(starved & ~(8'd1 << last_port)));
`ifdef MUT_BATCHUNL
    wire        bc_c    = cur_v && cur_we && hv[cur_port] && hwe[cur_port] && (bt_port == cur_port);
`else
    wire        bc_c    = cur_v && cur_we && hv[cur_port] && hwe[cur_port] && (bt_port == cur_port) && (bt_reqs < BATCH_REQS[3:0]) && hfit_q[cur_port];
`endif
    reg [NPORTS-1:0] cand_q, urg_q, early_q; reg cont_q, bc_q;
    always @(posedge clk) begin
        cand_q <= cand; urg_q <= urg; early_q <= early; cont_q <= cont_c; bc_q <= bc_c;
    end
    wire sel_v, sel_wrr; wire [2:0] sel_p;
    reg  grant_ev; reg [2:0] grant_pt; wire t_wrr;
    tau_sdram_sv_arb #(.NP(NPORTS), .WEIGHT(WEIGHT)) u_arb (.clk(clk), .rst(rst), .cand(cand_q), .urgent(urg_q), .early(early_q),
        .cont_ok(cont_q), .last_port(last_port), .grant(grant_ev), .grant_port(grant_pt), .grant_wrr(t_wrr),
        .sel_v(sel_v), .sel_p(sel_p), .sel_wrr(sel_wrr));
    reg c_v_q, c_wrr_q, c_bc_q; reg [2:0] c_p_q;
    always @(posedge clk) begin
        c_v_q <= bc_q ? 1'b1 : sel_v; c_p_q <= bc_q ? cur_port : sel_p; c_wrr_q <= bc_q ? 1'b0 : sel_wrr; c_bc_q <= bc_q;
    end

    // ---------------------------------------------------------------------------------------------------
    // state of the burst in progress (cur), the committed write piece (pq_*), write FIFO, read descriptors
    // ---------------------------------------------------------------------------------------------------
    reg         cur_last; reg [1:0] cur_bank; reg [9:0] cur_left; reg [7:0] cur_tag;
    reg         pq_v, pq_iss, pq_last; reg [2:0] pq_port; reg [9:0] pq_n, pq_pulled;
    reg [15:0]  wf_d [0:WF_DEPTH-1];
    reg [1:0]   wf_b [0:WF_DEPTH-1];
    reg [WFW-1:0] wf_rp, wf_wp, wf_cnt;
    reg [W_LEAD-1:0] dl_v; reg [2:0] dl_p [0:W_LEAD-1];
    reg [9:0]   pidx [0:NPORTS-1];
    reg [2:0]   rq_port [0:RDQ_DEPTH-1]; reg [9:0] rq_n [0:RDQ_DEPTH-1]; reg rq_last [0:RDQ_DEPTH-1]; reg [7:0] rq_tag [0:RDQ_DEPTH-1];
    reg [RQW-1:0] rq_rp, rq_wp, rq_cnt;
    reg [9:0]   rd_rem; reg rd_act;
    reg [3:0]   err_q;

    // ---------------------------------------------------------------------------------------------------
    // target T
    // ---------------------------------------------------------------------------------------------------
    wire        lk_v  = pq_v & ~pq_iss;
    reg  [3:0]  blank_q;                  // after an issue: the head advanced, stages A..D are stale
    reg  [2:0]  tq_q;                     // after a table change: the row-hit flags of stages A..D are stale (column issue and prep wait)
    reg         fh_v, fh_kill, td_fhm;    // fast open-row cache: the bank of the last ACT and its row (always current)
    reg  [1:0]  fh_bank; reg [12:0] fh_row;
    reg         pa_v; reg [1:0] pa_bank; reg [12:0] pa_row;   // ACT to follow the PRE just issued for the target
    reg         td_v; reg [2:0] td_p; reg td_we, td_last, td_hit, td_open, td_wrr, td_bc;
    reg  [1:0]  td_bank; reg [12:0] td_row; reg [9:0] td_col, td_n; reg [7:0] td_tag;
    wire [2:0]  d_p = lk_v ? pq_port : c_p_q;
    wire        d_v = lk_v ? hv[pq_port] : c_v_q;
    always @(posedge clk) begin
        td_v <= d_v; td_p <= d_p; td_we <= hwe[d_p]; td_last <= hlast[d_p]; td_bank <= m2(hbank, d_p);
        td_row <= m13(hrow, d_p); td_col <= m10(hcol, d_p); td_n <= m10(hplen, d_p); td_tag <= m8(htag, d_p);
        td_fhm <= fh_v && fh_bank == m2(hbank, d_p) && fh_row == m13(hrow, d_p);
        td_hit <= hhit_q[d_p]; td_open <= hopen_q[d_p]; td_wrr <= lk_v ? 1'b0 : c_wrr_q; td_bc <= lk_v ? 1'b0 : c_bc_q;
    end
    wire        t_v    = td_v & (blank_q == 4'd0);
    wire [2:0]  t_p    = td_p;
    wire        t_we   = td_we;
    wire [1:0]  t_bank = td_bank;
    wire [12:0] t_row  = td_row;
    wire [9:0]  t_col  = td_col;
    wire [9:0]  t_n    = td_n;
    wire        t_last = td_last;
    wire [7:0]  t_tag  = td_tag;
    wire        quiet  = (tq_q == 3'd0);
    wire        fh_ok  = td_fhm & ~fh_kill;
    wire        t_open = (td_open & quiet) | fh_ok;
    wire        t_hit  = (td_hit & quiet & !row_aged[td_bank]) | (fh_ok & !row_aged[td_bank]);
    assign      t_wrr  = td_wrr;

    // ---------------------------------------------------------------------------------------------------
    // registered offer: cmd / cmd_ba / cmd_a / wvalid / wdata / be / preempt_ack are flip-flops that drive the PHY ports directly.
    // The offer of cycle d+1 is decided in cycle d (cmd_n ...). The permits used for it come from the scheduler's own timing model
    // (tau_sdram_sv_tm), which sees the registered offers; a command other than BST or a chained column command is only decided in a
    // cycle in which the registered offer is a NOP (`quietc`), so the model's one-cycle-ahead permits are exact. The PHY's own permits
    // are compared with the offer in the cycle it is made (dbg_pmiss): a mismatch means the model is wrong.
    // ---------------------------------------------------------------------------------------------------
    reg  [5:0]  hold_q;                    // the PHY owns the bus (forced refresh after our preempt acknowledge): offer nothing
    reg  [2:0]  cmd_n; reg [1:0] ba_n; reg [12:0] a_n;
    wire        wv_n;
`ifdef MUT_NOQUIET
    wire        quietc = 1'b1;
`else
    wire        quietc = (cmd == K_NOP);
`endif
    wire [3:0]  p_act, p_rd, p_wr, p_pre, m_open; wire p_prea, p_ref;
    tau_sdram_sv_tm #(.TRCD_C(TRCD_C), .TRP_C(TRP_C), .TRAS_C(TRAS_C), .TRC_C(RC_PLAN), .TRFC_C(TRFC_C), .WRP_C(WRP_C), .DAL_C(DAL_C),
                      .RRD_C(RRD_C), .RW_C((CL == 3) ? RW_C + 1 : RW_C)) u_tm (
        .clk(clk), .rst(rst), .init_done(init_done), .cmd_q(cmd), .ba_q(cmd_ba), .beat_q(wvalid & (be != 2'b00)),
        .banks_closed(banks_closed), .p_act(p_act), .p_rd(p_rd), .p_wr(p_wr), .p_pre(p_pre), .p_prea(p_prea), .p_ref(p_ref), .m_open(m_open));
    reg         dwp_v; reg [2:0] dwp_p; reg [7:0] dwp_tag;
    reg  [15:0] pmiss_q;
    wire        miss = ((cmd == K_ACT) & ~ok_act[cmd_ba]) | ((cmd == K_RD) & ~ok_rd[cmd_ba]) | ((cmd == K_WR) & ~ok_wr[cmd_ba]) |
                       ((cmd == K_PRE) & ~ok_pre[cmd_ba]) | ((cmd == K_PREA) & ~ok_prea) | ((cmd == K_REF) & ~ok_ref);
    assign      dbg_pmiss = pmiss_q;
    always @(posedge clk) begin
        if (rst) pmiss_q <= 16'd0;
        else if (miss && pmiss_q != 16'hffff) pmiss_q <= pmiss_q + 16'd1;
    end
    always @(posedge clk) begin
        if (rst) begin hold_q <= 6'd0; cmd <= K_NOP; cmd_ba <= 2'd0; cmd_a <= 13'd0; wvalid <= 1'b0; wdata <= 16'd0; be <= 2'b11; preempt_ack <= 1'b0; end
        else begin
            cmd <= cmd_n; cmd_ba <= ba_n; cmd_a <= a_n; wvalid <= wv_n; wdata <= wf_d[wf_rp]; be <= wf_b[wf_rp];
            preempt_ack <= preempt & ~cur_v & init_done;
            if (preempt & ~cur_v & init_done) hold_q <= 6'd32;
            else if (banks_closed) hold_q <= TRFC_C + 2;
            else if (hold_q != 6'd0) hold_q <= hold_q - 6'd1;
        end
    end

    wire        boundary = cur_v && (cur_left == 10'd0);
    // refresh: one per idle gap (no urgent request waiting), between grants from REF_SOFT_DEBT, forced from REF_HARD_DEBT
    wire        uw      = uw_q;
`ifdef MUT_NOHARD
    wire        rf_go   = init_done & ~preempt & ref_due & ((1'b0) |
`else
    wire        rf_go   = init_done & ~preempt & ref_due & ((ref_debt >= REF_HARD_DEBT[3:0]) |
`endif
                          (~uw & ((ref_debt >= REF_SOFT_DEBT[3:0]) | ~(|hv))));
    reg         rf_go_q;
    always @(posedge clk) rf_go_q <= rf_go;
    wire        run_ok   = init_done & ~preempt & (rfm == RF_NONE) & ~rf_go_q & (hold_q == 6'd0);
    wire [9:0]  need_pulled = (pq_n < W_LEAD[9:0] + 10'd1) ? pq_n : (W_LEAD[9:0] + 10'd1);
    wire        rd_ok    = p_rd[t_bank] && (rq_cnt < RDQ_DEPTH[RQW-1:0] - 1);
    wire        wr_ok    = p_wr[t_bank] && lk_v && (pq_pulled >= need_pulled) && (wf_cnt != {WFW{1'b0}});
    wire        t_ready  = run_ok && t_v && t_hit && (t_we ? wr_ok : rd_ok);
`ifdef MUT_NOCHAIN
    wire        issue    = t_ready && !cur_v;
`else
    wire        issue    = t_ready && (boundary || !cur_v);
`endif
    wire        wr_issue = issue && t_we;
    wire        rd_issue = issue && !t_we;

    // write commitment (lock): first piece of a grant near the boundary or when idle; continuation of a batch at once
    wire        pq_free = ~pq_v;
    wire        near    = !cur_v || (cur_left <= PULL_AHEAD[9:0]);
    wire        do_lock = run_ok && pq_free && t_v && t_we && (near || td_bc);
    wire        lock_b  = do_lock && td_bc;
    wire        lock_a  = do_lock && !td_bc;
    wire [2:0]  lk_pt   = t_p;
    wire [9:0]  lk_n    = t_n;
    wire        lk_lst  = t_last;

    // ---------------------------------------------------------------------------------------------------
    // command selection
    // ---------------------------------------------------------------------------------------------------
    wire [3:0] others_want = ow_q;
    integer k, b;
    reg [3:0] aged_pre;                 // an aged row that may be closed now
    integer ab;
    always @* begin
        aged_pre = 4'd0;
        for (ab = 0; ab < 4; ab = ab + 1)
`ifdef MUT_NOAGE
            aged_pre[ab] = 1'b0;
`else
            aged_pre[ab] = row_aged[ab] & o_v[ab] & p_pre[ab] & ~(cur_v & (cur_bank == ab[1:0]));
`endif
    end
    always @* begin
        cmd_n = K_NOP; ba_n = 2'd0; a_n = 13'd0;
        if (rfm == RF_PREA) begin
            if (p_prea && quietc) cmd_n = K_PREA;
        end else if (rfm == RF_REF) begin
            if (p_ref && quietc) cmd_n = K_REF;
        end else if (boundary) begin
            if (issue) begin cmd_n = t_we ? K_WR : K_RD; ba_n = t_bank; a_n = {3'b000, t_col}; end
`ifdef MUT_NOBST
            else begin cmd_n = K_NOP; end
`else
            else begin cmd_n = K_BST; end
`endif
        end else if (issue) begin
            cmd_n = t_we ? K_WR : K_RD; ba_n = t_bank; a_n = {3'b000, t_col};
        end else if (run_ok && pa_v && p_act[pa_bank] && quietc && (LOOKAHEAD != 0 || !cur_v)) begin
            cmd_n = K_ACT; ba_n = pa_bank; a_n = pa_row;
        end else if (run_ok && t_v && (quiet || fh_ok) && (LOOKAHEAD != 0 || !cur_v)) begin
            if (!t_open) begin
                if (p_act[t_bank] && quietc) begin cmd_n = K_ACT; ba_n = t_bank; a_n = t_row; end
            end else if (!t_hit) begin
                if (p_pre[t_bank] && quietc && !(cur_v && cur_bank == t_bank) && `ifdef MUT_PRE_UNPROT
                    1'b1
`else
                    !(cur_v && others_want[t_bank])
`endif
                    ) begin
                    cmd_n = K_PRE; ba_n = t_bank;
                end
            end
        end
        if (cmd_n == K_NOP && init_done && !preempt && rfm == RF_NONE && !(cur_v && boundary)) begin
            if (!quietc) begin end
            else if (aged_pre[0]) begin cmd_n = K_PRE; ba_n = 2'd0; end
            else if (aged_pre[1]) begin cmd_n = K_PRE; ba_n = 2'd1; end
            else if (aged_pre[2]) begin cmd_n = K_PRE; ba_n = 2'd2; end
            else if (aged_pre[3]) begin cmd_n = K_PRE; ba_n = 2'd3; end
        end
        if (hold_q != 6'd0) cmd_n = K_NOP;
    end

    // ---------------------------------------------------------------------------------------------------
    // write data: pull (w_next), delay line (W_LEAD), skid FIFO, beats
    // ---------------------------------------------------------------------------------------------------
    wire        beat_stream = cur_v && cur_we && (cur_left != 10'd0);
    wire        do_beat     = wr_issue | beat_stream;
    assign      wv_n        = do_beat & (wf_cnt != {WFW{1'b0}});
    wire        wf_pop      = wv_n;
    reg  [3:0]  infl;
    integer     di;
    always @* begin
        infl = 4'd0;
        for (di = 0; di < W_LEAD; di = di + 1) infl = infl + {3'd0, dl_v[di]};
    end
    wire        pull_want = pq_v && (pq_pulled < pq_n);
    // the pull does not look at this cycle's pop (keeps w_next independent of the command decision); steady state still pulls every cycle
    wire        pull      = pull_want && (({1'b0, wf_cnt} + {2'b0, infl} + 1'b1) <= WF_DEPTH);
    assign      w_next    = pull ? ({{(NPORTS-1){1'b0}}, 1'b1} << pq_port) : {NPORTS{1'b0}};
    generate
        for (p = 0; p < NPORTS; p = p + 1) begin : g_wi
            assign w_idx[p*10 +: 10] = pidx[p];
        end
    endgenerate
`ifdef MUT_WLEAD_OFF
    wire        arrive   = dl_v[0];
    wire [2:0]  arr_p    = dl_p[0];
`else
    wire        arrive   = dl_v[W_LEAD-1];
    wire [2:0]  arr_p    = dl_p[W_LEAD-1];
`endif

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
`ifdef MUT_RLAST
    wire        rd_done = rv_ok && f_last && (f_rem == 10'd2);
`else
    wire        rd_done = rv_end && f_last;
`endif
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
`ifdef MUT_DONE_EARLY
    wire        wd_ev  = (wr_issue && t_last) || (beat_stream && cur_left == 10'd1 && cur_last && 1'b0);
`else
    wire        wd_ev  = (wr_issue && t_last && t_n == 10'd1) || (beat_stream && cur_left == 10'd1 && cur_last);
`endif
    wire [2:0]  wd_p   = (wr_issue && t_last && t_n == 10'd1) ? t_p : cur_port;
    wire [7:0]  wd_tag = (wr_issue && t_last && t_n == 10'd1) ? t_tag : cur_tag;

    assign adv = issue ? ({{(NPORTS-1){1'b0}}, 1'b1} << t_p) : {NPORTS{1'b0}};
    assign issue_p = adv;
    assign dbg_err = err_q;

    always @* begin
        grant_ev = 1'b0; grant_pt = t_p;
        if (rd_issue) begin grant_ev = 1'b1; grant_pt = t_p; end
        else if (lock_a) begin grant_ev = 1'b1; grant_pt = t_p; end
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
            err_q <= 4'd0; o_v <= 4'd0; rfm <= RF_NONE; bw_win <= 24'd0; blank_q <= 4'd7; tq_q <= 3'd4; fh_v <= 1'b0; fh_kill <= 1'b0; pa_v <= 1'b0;
            last_port <= 3'd0; last_v <= 1'b0; last_bank <= 2'd0; last_row <= 13'd0;
            done_b_q <= {NPORTS{1'b0}}; done_w_q <= {NPORTS{1'b0}}; dwp_v <= 1'b0;
            for (i = 0; i < NPORTS; i = i + 1) begin
                inflight[i] <= 6'd0; pidx[i] <= 10'd0; bad_q[i] <= 16'd0; req_cnt_q[i] <= 32'd0; word_cnt_q[i] <= 32'd0;
                busy_cnt_q[i] <= 32'd0; done_b_tag[i] <= 8'd0; done_w_tag[i] <= 8'd0;
                worst_q[i] <= 16'd0; bw_used_v[i*16 +: 16] <= 16'd0; uover_q[i] <= 16'd0; uwc_q[i] <= 16'd0;
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
            // ---- stale-stage blanking
            if (issue) blank_q <= 4'd5;
            else if (blank_q != 4'd0) blank_q <= blank_q - 4'd1;
            if (banks_closed || cmd_n == K_ACT || cmd_n == K_PRE || cmd_n == K_PREA) tq_q <= 3'd4;
            else if (tq_q != 3'd0) tq_q <= tq_q - 3'd1;
            fh_kill <= banks_closed || cmd_n == K_ACT || cmd_n == K_PRE || cmd_n == K_PREA;
            if (banks_closed || cmd_n == K_PREA) fh_v <= 1'b0;
            else if (cmd_n == K_ACT) begin fh_v <= 1'b1; fh_bank <= ba_n; fh_row <= a_n; end
            else if (cmd_n == K_PRE && fh_bank == ba_n) fh_v <= 1'b0;
            if (banks_closed || cmd_n == K_PREA) pa_v <= 1'b0;
            else if (cmd_n == K_ACT && pa_v && ba_n == pa_bank) pa_v <= 1'b0;
            else if (cmd_n == K_PRE && !(rfm != RF_NONE) && !boundary && t_v) begin pa_v <= 1'b1; pa_bank <= ba_n; pa_row <= t_row; end
            // ---- refresh sequence
            if (banks_closed) rfm <= RF_NONE;
            else case (rfm)
                RF_NONE: if (rf_go_q && (boundary || !cur_v)) rfm <= RF_PREA;
                RF_PREA: if (cmd_n == K_PREA) rfm <= RF_REF;
                RF_REF:  if (cmd_n == K_REF) rfm <= RF_NONE;
                default: rfm <= RF_NONE;
            endcase
            // ---- open-row table
`ifdef MUT_BCLOSED
            if (cmd_n == K_PREA) o_v <= 4'd0;
`else
            if (banks_closed || cmd_n == K_PREA) o_v <= 4'd0;
`endif
            else if (cmd_n == K_ACT) begin o_v[ba_n] <= 1'b1; o_row[ba_n] <= a_n; end
            else if (cmd_n == K_PRE) o_v[ba_n] <= 1'b0;
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
                wf_d[wf_wp] <= m16(w_data, arr_p);
                wf_b[wf_wp] <= m2(w_be, arr_p);
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
            bw_win <= (BW_WINDOW_CYCLES != 0 && bw_win == BW_WINDOW_CYCLES - 1) ? 24'd0 : bw_win + 24'd1;
            done_w_q <= {NPORTS{1'b0}};
            // the beat is offered one cycle after it is decided: the write done follows one cycle later still
            dwp_v <= wd_ev; dwp_p <= wd_p; dwp_tag <= wd_tag;
            if (dwp_v) begin done_w_q[dwp_p] <= 1'b1; done_w_tag[dwp_p] <= dwp_tag; end
            done_b_q <= retire;
            for (i = 0; i < NPORTS; i = i + 1) begin
                if (retire[i]) done_b_tag[i] <= htag[i*8 +: 8];
                if (retire[i] && bad_q[i] != 16'hffff) bad_q[i] <= bad_q[i] + 16'd1;
                if (CNT_EN != 0 && acc[i] && !acc_bad[i]) req_cnt_q[i] <= req_cnt_q[i] + 32'd1;
                infl_n = inflight[i];
                if (issue && t_last && t_p == i) infl_n = infl_n + 6'd1;
                if ((rd_done && f_port == i) || (dwp_v && dwp_p == i)) infl_n = infl_n - 6'd1;
                inflight[i] <= infl_n;
                if (CNT_EN != 0 && issue && t_p == i) word_cnt_q[i] <= word_cnt_q[i] + {22'd0, t_n};
                if (CNT_EN != 0 && cur_v && cur_port == i) busy_cnt_q[i] <= busy_cnt_q[i] + 32'd1;
                if (rst || (BW_WINDOW_CYCLES != 0 && bw_win == BW_WINDOW_CYCLES - 1)) bw_used_v[i*16 +: 16] <= 16'd0;
                else if (issue_p[i]) bw_used_v[i*16 +: 16] <= bw_used_v[i*16 +: 16] + {6'd0, t_n};
                // worst wait (grant minus request) and urgent waits above the documented bound
                if (CNT_EN != 0 && issue_p[i] && wc[i] > worst_q[i]) worst_q[i] <= wc[i];
                if (CNT_EN == 0) uwc_q[i] <= 16'd0;
                else if (urg[i] && !issue_p[i]) begin if (uwc_q[i] != 16'hffff) uwc_q[i] <= uwc_q[i] + 16'd1; end
                else uwc_q[i] <= 16'd0;
                if (CNT_EN != 0 && issue_p[i] && urg[i] && uwc_q[i] > URGENT_ISSUE_BOUND) begin
                    if (uover_q[i] != 16'hffff) uover_q[i] <= uover_q[i] + 16'd1;
                end
            end
        end
    end
endmodule
`default_nettype wire
