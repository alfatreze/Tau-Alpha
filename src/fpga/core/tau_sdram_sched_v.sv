// Tau V SDRAM scheduler (reference implementation v1). MIT licence.
// CHECKPOINT 2: queues, arbitration, open-row table and ACT/PRE look-ahead (no column commands yet).
`default_nettype none
module tau_sdram_sched_v #(
    parameter integer NPORTS = 6,
    parameter integer QDEPTH = 4,
    parameter integer BANK_HASH = 2,
    parameter [7:0]   URGENT_CAPABLE = 8'h01,
    parameter [7:0]   ENABLE_2D = 8'h02,
    parameter [8*10-1:0] MAXG_RD = {10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd64, 10'd128},
    parameter [8*10-1:0] MAXG_WR = {10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd32, 10'd128, 10'd128},
    parameter [8*4-1:0]  WEIGHT  = {4'd1, 4'd1, 4'd1, 4'd1, 4'd2, 4'd4, 4'd4, 4'd4},
    parameter integer CLK_HZ = 100_000_000,
    parameter integer SAME_ROW_BREAK_NS = 60000
) (
    input  wire                 clk,
    input  wire                 rst,
    // client ports (contract section 4)
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
    output wire [NPORTS-1:0]    done,
    output wire [NPORTS*8-1:0]  done_tag,
    output wire [NPORTS*16-1:0] cnt_bad,
    // PHY command interface (contract section 3.3)
    output reg  [2:0]           cmd,
    output reg  [1:0]           cmd_ba,
    output reg  [12:0]          cmd_a,
    input  wire [3:0]           ok_act,
    input  wire [3:0]           ok_pre,
    input  wire                 init_done,
    input  wire [3:0]           row_aged,
    input  wire                 preempt,
    input  wire                 banks_closed
);
    localparam [2:0] K_NOP = 3'd0, K_ACT = 3'd1, K_PRE = 3'd4;
    localparam integer STARVE_CLK = (CLK_HZ / 1000) * (SAME_ROW_BREAK_NS / 1000) / 1000;

    // ---------------------------------------------------------------------------------------------------
    // per-port queues
    // ---------------------------------------------------------------------------------------------------
    wire [NPORTS-1:0]   hv, hbad, hwe, hlast, h2d, acc_bad;
    wire [NPORTS*2-1:0] hbank;
    wire [NPORTS*13-1:0] hrow;
    wire [NPORTS*10-1:0] hplen;
    wire [NPORTS*8-1:0]  htag;
    wire [NPORTS*4-1:0]  hcnt;
    genvar p;
    generate
        for (p = 0; p < NPORTS; p = p + 1) begin : g_p
            wire [9:0] hc; wire acc;
            wire retire = hbad[p];
            tau_sdram_sv_pq #(.DEPTH(QDEPTH), .EN_2D(ENABLE_2D[p]), .BANK_HASH(BANK_HASH),
                .MAXG_RD(MAXG_RD[p*10 +: 10]), .MAXG_WR(MAXG_WR[p*10 +: 10])) u_pq (
                .clk(clk), .rst(rst), .req_valid(req_valid[p]), .req_ready(req_ready[p]), .req_we(req_we[p]),
                .req_addr(req_addr[p*25 +: 25]), .req_len(req_len[p*10 +: 10]), .req_tag(req_tag[p*8 +: 8]),
                .req_2d(req_2d[p]), .req_w(req_w[p*6 +: 6]), .req_h(req_h[p*6 +: 6]), .req_stride(req_stride[p*11 +: 11]),
                .req_skew(req_skew[p*2 +: 2]), .count(hcnt[p*4 +: 4]), .hd_v(hv[p]), .hd_bad(hbad[p]), .hd_we(hwe[p]),
                .hd_bank(hbank[p*2 +: 2]), .hd_row(hrow[p*13 +: 13]), .hd_col(hc), .hd_plen(hplen[p*10 +: 10]),
                .hd_last(hlast[p]), .hd_tag(htag[p*8 +: 8]), .hd_2d(h2d[p]),
                .adv(1'b0), .pop_bad(retire), .accepted(acc), .accepted_bad(acc_bad[p]));
            reg d_q; reg [7:0] t_q; reg [15:0] bad_q;
            always @(posedge clk) begin
                if (rst) begin d_q <= 1'b0; t_q <= 8'd0; bad_q <= 16'd0; end
                else begin
                    d_q <= retire; if (retire) t_q <= htag[p*8 +: 8];
                    if (retire && bad_q != 16'hffff) bad_q <= bad_q + 1'b1;
                end
            end
            assign done[p] = d_q; assign done_tag[p*8 +: 8] = t_q; assign cnt_bad[p*16 +: 16] = bad_q;
        end
    endgenerate

    // ---------------------------------------------------------------------------------------------------
    // wait timers, candidates, arbitration
    // ---------------------------------------------------------------------------------------------------
    reg [15:0] wc [0:NPORTS-1];
    reg  [NPORTS-1:0] starved;
    integer q;
    always @(posedge clk) begin
        for (q = 0; q < NPORTS; q = q + 1) begin
            if (rst || !hv[q]) begin wc[q] <= 16'd0; starved[q] <= 1'b0; end
            else begin
                if (wc[q] != 16'hffff) wc[q] <= wc[q] + 16'd1;
                starved[q] <= (wc[q] >= STARVE_CLK[15:0]);
            end
        end
    end
    wire [NPORTS-1:0] urg = urgent & URGENT_CAPABLE[NPORTS-1:0] & hv;
    reg  [2:0]  last_port; reg last_v; reg [1:0] last_bank; reg [12:0] last_row;
    wire [2:0]  lp = last_port;
    wire        cont_ok = last_v && hv[lp] && hbank[lp*2 +: 2] == last_bank && hrow[lp*13 +: 13] == last_row &&
                          !(|(starved & ~(8'd1 << lp)));
    wire sel_v, sel_wrr; wire [2:0] sel_p;
    tau_sdram_sv_arb #(.NP(NPORTS), .WEIGHT(WEIGHT)) u_arb (.clk(clk), .rst(rst), .cand(hv), .urgent(urg),
        .cont_ok(cont_ok), .last_port(last_port), .grant(1'b0), .grant_port(3'd0), .grant_wrr(1'b0),
        .sel_v(sel_v), .sel_p(sel_p), .sel_wrr(sel_wrr));
    always @(posedge clk) begin
        if (rst) begin last_port <= 3'd0; last_v <= 1'b0; last_bank <= 2'd0; last_row <= 13'd0; end
    end

    // ---------------------------------------------------------------------------------------------------
    // open-row table and ACT / PRE look-ahead for the selected target T
    // ---------------------------------------------------------------------------------------------------
    reg [3:0]  o_v;
    reg [12:0] o_row [0:3];
    wire [1:0]  tb = hbank[sel_p*2 +: 2];
    wire [12:0] tr = hrow[sel_p*13 +: 13];
    wire        t_hit  = o_v[tb] && o_row[tb] == tr && !row_aged[tb];
    wire        t_open = o_v[tb];
    wire        cur_v = 1'b0;         // (data movement is added in the next checkpoint)
    reg [3:0]   others_want;          // some other head wants the row now open in bank b
    integer k, b;
    always @* begin
        others_want = 4'd0;
        for (b = 0; b < 4; b = b + 1)
            for (k = 0; k < NPORTS; k = k + 1)
                if (hv[k] && k[2:0] != sel_p && hbank[k*2 +: 2] == b[1:0] && o_v[b] && hrow[k*13 +: 13] == o_row[b])
                    others_want[b] = 1'b1;
    end
    always @* begin
        cmd = K_NOP; cmd_ba = 2'd0; cmd_a = 13'd0;
        if (init_done && !preempt && sel_v) begin
            if (!t_open) begin
                if (ok_act[tb]) begin cmd = K_ACT; cmd_ba = tb; cmd_a = tr; end
            end else if (!t_hit) begin
                if (ok_pre[tb] && !(cur_v && others_want[tb])) begin cmd = K_PRE; cmd_ba = tb; end
            end
        end
    end
    always @(posedge clk) begin
        if (rst || banks_closed) o_v <= 4'd0;
        else if (cmd == K_ACT) begin o_v[cmd_ba] <= 1'b1; o_row[cmd_ba] <= cmd_a; end
        else if (cmd == K_PRE) o_v[cmd_ba] <= 1'b0;
    end
endmodule
`default_nettype wire
