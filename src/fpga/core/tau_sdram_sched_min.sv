// Minimal SDRAM scheduler (bring-up and area floor). MIT licence. Clean-room, from docs/features/SDRAM_CONTROLLER_CONTRACT.md
// sections 3.3-3.7 (PHY command interface) and 4 (client port protocol).
//
// One client port, one request at a time, in order, one open row (a single bank is open at any time), no queue, no
// look-ahead, no chaining, no arbitration, no urgency, no 2D (a 2D request is rejected as bad_req), BANK_HASH 0..2.
//   * request: accepted when req_valid & req_ready (registered). len 0 or > 512 (or req_2d): rejected, `done` at once, bad counter.
//   * a request is split at 1,024-word page boundaries; each piece: [PRE] [ACT] RD|WR ... BST (an open row that matches is reused).
//   * reads: RD, BST exactly n cycles later; r_valid / r_data / r_last straight from the PHY; done with r_last.
//   * writes (no FIFO): the first word is pulled with `w_next` only when the bank's WR is permitted; the WR is offered exactly
//     W_LEAD cycles later, so `w_data` goes straight to the PHY (the contract's W_LEAD is measured to the PHY wdata cycle);
//     BST after the last beat; done in the cycle after the last beat.
//   * refresh: between pieces (and when idle) whenever the PHY's debt is non-zero: PREA, REF. The piece is at most 512 words, shorter
//     than the 780-cycle refresh interval, so the debt never reaches the forced level. preempt is acknowledged at once when no burst runs.
//   * an open row that the PHY reports aged is closed when the unit is idle.
// Counters: requests, words, bad. Everything is a parameter or a constant of the contract; nothing is tunable.
`default_nettype none
/* verilator lint_off WIDTHEXPAND */
/* verilator lint_off WIDTHTRUNC */
/* verilator lint_off UNUSEDSIGNAL */
module tau_sdram_sched_min #(
    parameter integer BANK_HASH = 2,
    parameter integer W_LEAD    = 2,
    parameter integer CL        = 2     // the PHY's CAS latency (read-to-write turnaround of the timing model)
) (
    input  wire        clk,
    input  wire        rst,
    // client port (section 4)
    input  wire        req_valid,
    output reg         req_ready,
    input  wire        req_we,
    input  wire [24:0] req_addr,
    input  wire [9:0]  req_len,
    input  wire [7:0]  req_tag,
    input  wire        req_2d,
    output wire        w_next,
    output wire [9:0]  w_idx,
    input  wire [15:0] w_data,
    input  wire [1:0]  w_be,
    output wire        r_valid,
    output wire [15:0] r_data,
    output wire        r_last,
    output wire        done,
    output wire [7:0]  done_tag,
    output wire [31:0] cnt_requests,
    output wire [31:0] cnt_words,
    output wire [15:0] cnt_bad,
    output reg         dbg_pmiss,       // sticky: an offer the PHY's permits did not allow (the timing model was wrong); must stay 0
    // PHY command interface (section 3.3)
    output reg  [2:0]  cmd,
    output reg  [1:0]  cmd_ba,
    output reg  [12:0] cmd_a,
    output reg         wvalid,
    output reg  [15:0] wdata,
    output reg  [1:0]  be,
    input  wire [3:0]  ok_act,
    input  wire [3:0]  ok_rd,
    input  wire [3:0]  ok_wr,
    input  wire [3:0]  ok_pre,
    input  wire        ok_prea,
    input  wire        ok_ref,
    input  wire        init_done,
    input  wire        rvalid,
    input  wire [15:0] rdata,
    input  wire [3:0]  row_aged,
    input  wire        preempt,
    output reg         preempt_ack,
    input  wire        banks_closed,
    input  wire        ref_due
);
`ifdef MUT_MIN_NOREF
    wire ref_due_i = 1'b0;
`else
    wire ref_due_i = ref_due;
`endif
    localparam [2:0] K_NOP = 3'd0, K_ACT = 3'd1, K_RD = 3'd2, K_WR = 3'd3, K_PRE = 3'd4, K_PREA = 3'd5, K_REF = 3'd6, K_BST = 3'd7;
    localparam [3:0] S_IDLE = 4'd0, S_BAD = 4'd1, S_SEL = 4'd2, S_PRE = 4'd3, S_ACT = 4'd4, S_COL = 4'd5, S_RD = 4'd6, S_WR = 4'd7,
                     S_BST = 4'd8, S_FIN = 4'd9, S_PREA = 4'd10, S_REF = 4'd11, S_D1 = 4'd12, S_D2 = 4'd13;

    reg  [3:0]  st;
    reg         we_q; reg [24:0] addr_q; reg [9:0] rem_q; reg [7:0] tag_q;
    reg         o_v; reg [1:0] o_bank; reg [12:0] o_row;
    reg  [9:0]  cnt_q;                // read: cycles to the BST; write: words still to pull
    reg  [9:0]  plen_q;               // words of the current piece
    reg  [9:0]  widx_q;               // request word index of the next pull
    reg  [9:0]  rd_left_q;            // words of the request still to arrive
    reg         wr_first_q;           // the WR of this piece is still to be offered
    reg  [W_LEAD-1:0] pv_q;           // pull valid, delayed
    reg         last_piece_q;
    reg         done_w_q, done_b_q, ref_ret;
    reg  [31:0] req_c_q, word_c_q; reg [15:0] bad_c_q;

    // address decode of the current piece, registered one stage after the address (the FSM waits two cycles in S_D1/S_D2)
    reg  [1:0]  bank_q; reg [9:0] plen_r; reg hit_q;
    wire [1:0]  bank_c = (BANK_HASH == 0) ? addr_q[11:10] :
                         (BANK_HASH == 1) ? (addr_q[11:10] ^ addr_q[21:20]) : (addr_q[11:10] ^ addr_q[21:20] ^ addr_q[23:22]);
    wire [12:0] row  = addr_q[24:12];
    wire [10:0] room = 11'd1024 - {1'b0, addr_q[9:0]};
`ifdef MUT_MIN_NOSPLIT
    wire [9:0]  plen = rem_q;
`else
    wire [9:0]  plen = ({1'b0, rem_q} < room) ? rem_q : room[9:0];
`endif
    always @(posedge clk) begin
        bank_q <= bank_c; plen_r <= plen;
        hit_q  <= o_v && o_bank == bank_q && o_row == row && !row_aged[bank_q];
    end
    wire [1:0]  bank = bank_q;
    wire        hit  = hit_q;
`ifdef MUT_MIN_BAD
    wire        bad  = req_2d || req_len == 10'd0 || req_len > 10'd600;
`else
    wire        bad  = req_2d || req_len == 10'd0 || req_len > 10'd512;
`endif

    // write data path: pulls are consecutive, the beats follow W_LEAD cycles later
    wire        pulling = (st == S_WR) && (cnt_q != 10'd0);
    wire        beat    = pv_q[W_LEAD-1];
    assign w_next  = pulling;
    assign w_idx   = widx_q;
    wire        wr_go   = beat && wr_first_q;
    wire        last_beat = beat && (cnt_q == 10'd0) && ((pv_q & ~({{(W_LEAD-1){1'b0}}, 1'b1} << (W_LEAD-1))) == {W_LEAD{1'b0}});

    // read data path
    assign r_valid = rvalid;
    assign r_data  = rdata;
    assign r_last  = rvalid && (rd_left_q == 10'd1);
    assign done    = r_last | done_w_q | done_b_q;
    assign done_tag = tag_q;
    assign cnt_requests = req_c_q; assign cnt_words = word_c_q; assign cnt_bad = bad_c_q;

    // registered offer: the offer of cycle d+1 is decided in cycle d from the timing model (tau_sdram_sv_tm.sv), which watches the
    // registered offers. A command other than BST is decided only in a cycle whose registered offer is a NOP (`quietc`), so the model's
    // permits (valid for the next cycle if this one is a NOP) are exact. The PHY's permits are only compared (dbg_pmiss).
    reg  [2:0]  cmd_n; reg [1:0] ba_n; reg [12:0] a_n;
    reg  [5:0]  hold_q;
`ifdef MUT_MIN_NOQUIET
    wire        quietc = 1'b1;
`else
    wire        quietc = (cmd == K_NOP);
`endif
    wire [3:0]  p_act, p_rd, p_wr, p_pre, m_open; wire p_prea, p_ref;
    tau_sdram_sv_tm #(.RW_C((CL == 3) ? 4 : 3)) u_tm (
        .clk(clk), .rst(rst), .init_done(init_done), .cmd_q(cmd), .ba_q(cmd_ba), .beat_q(wvalid & (be != 2'b00)),
        .banks_closed(banks_closed), .p_act(p_act), .p_rd(p_rd), .p_wr(p_wr), .p_pre(p_pre), .p_prea(p_prea), .p_ref(p_ref), .m_open(m_open));
    wire        miss = ((cmd == K_ACT) & ~ok_act[cmd_ba]) | ((cmd == K_RD) & ~ok_rd[cmd_ba]) | ((cmd == K_WR) & ~ok_wr[cmd_ba]) |
                       ((cmd == K_PRE) & ~ok_pre[cmd_ba]) | ((cmd == K_PREA) & ~ok_prea) | ((cmd == K_REF) & ~ok_ref);
    wire        ack_n = preempt && (st == S_IDLE || st == S_SEL || st == S_FIN) && init_done;
    always @* begin
        cmd_n = K_NOP; ba_n = 2'd0; a_n = 13'd0;
        case (st)
            S_IDLE: if (init_done && !preempt && o_v && row_aged[o_bank] && p_pre[o_bank] && quietc) begin cmd_n = K_PRE; ba_n = o_bank; end
            S_PRE:  if (p_pre[o_bank] && quietc) begin cmd_n = K_PRE; ba_n = o_bank; end
            S_ACT:  if (p_act[bank] && quietc) begin cmd_n = K_ACT; ba_n = bank; a_n = row; end
            S_COL:  if (!we_q && p_rd[bank] && quietc) begin cmd_n = K_RD; ba_n = bank; a_n = {3'b000, addr_q[9:0]}; end
            S_WR:   if (wr_go) begin cmd_n = K_WR; ba_n = o_bank; a_n = {3'b000, addr_q[9:0]}; end
`ifdef MUT_MIN_NOBST
            S_RD:   ;
`else
            S_RD:   if (cnt_q == 10'd0) cmd_n = K_BST;
`endif
            S_BST:  cmd_n = K_BST;
            S_PREA: if (p_prea && quietc) cmd_n = K_PREA;
            S_REF:  if (p_ref && quietc) cmd_n = K_REF;
            default: ;
        endcase
        if (hold_q != 6'd0) cmd_n = K_NOP;
    end
    reg         dwp_q;
    always @(posedge clk) begin
        if (rst) begin
            cmd <= K_NOP; cmd_ba <= 2'd0; cmd_a <= 13'd0; wvalid <= 1'b0; wdata <= 16'd0; be <= 2'b11; preempt_ack <= 1'b0;
            hold_q <= 6'd0; dbg_pmiss <= 1'b0;
        end else begin
            cmd <= cmd_n; cmd_ba <= ba_n; cmd_a <= a_n; wvalid <= beat; wdata <= w_data; be <= w_be; preempt_ack <= ack_n;
            if (miss) dbg_pmiss <= 1'b1;
            if (ack_n) hold_q <= 6'd32;
            else if (banks_closed) hold_q <= 6'd12;
            else if (hold_q != 6'd0) hold_q <= hold_q - 6'd1;
        end
    end

    always @(posedge clk) begin
        if (rst) begin
            st <= S_IDLE; req_ready <= 1'b0; o_v <= 1'b0; dwp_q <= 1'b0; pv_q <= {W_LEAD{1'b0}}; done_w_q <= 1'b0; done_b_q <= 1'b0;
            req_c_q <= 32'd0; word_c_q <= 32'd0; bad_c_q <= 16'd0; rd_left_q <= 10'd0; widx_q <= 10'd0; wr_first_q <= 1'b0;
            cnt_q <= 10'd0; plen_q <= 10'd0; rem_q <= 10'd0; tag_q <= 8'd0; we_q <= 1'b0; addr_q <= 25'd0; last_piece_q <= 1'b0;
            o_bank <= 2'd0; o_row <= 13'd0; ref_ret <= 1'b0;
        end else begin
            done_w_q <= dwp_q; dwp_q <= 1'b0; done_b_q <= 1'b0;
            pv_q <= {pv_q[W_LEAD-2:0], pulling} ;
            if (banks_closed || cmd_n == K_PREA) o_v <= 1'b0;
            if (rvalid && rd_left_q != 10'd0) rd_left_q <= rd_left_q - 10'd1;
            req_ready <= (st == S_IDLE) && init_done && !(req_valid && req_ready) && !(rd_left_q != 10'd0);
            case (st)
                S_IDLE: begin
                    if (cmd_n == K_PRE) o_v <= 1'b0;
                    if (req_valid && req_ready) begin
                        we_q <= req_we; addr_q <= req_addr; rem_q <= req_len; tag_q <= req_tag; widx_q <= 10'd0;
                        rd_left_q <= (req_we || bad) ? 10'd0 : req_len;
                        req_ready <= 1'b0;
                        if (bad) begin st <= S_BAD; bad_c_q <= bad_c_q + 16'd1; end
                        else begin st <= S_D1; req_c_q <= req_c_q + 32'd1; word_c_q <= word_c_q + {22'd0, req_len}; end
                    end else if (ref_due_i && init_done && !preempt && hold_q == 6'd0) begin st <= S_PREA; ref_ret <= 1'b0; req_ready <= 1'b0; end
                end
                S_BAD: begin done_b_q <= 1'b1; st <= S_IDLE; end
                S_SEL: begin
                    plen_q <= plen_r;
                    if (preempt) st <= S_SEL;
                    else if (ref_due_i) begin st <= S_PREA; ref_ret <= 1'b1; end
                    else st <= hit ? S_COL : (o_v ? S_PRE : S_ACT);
                end
                S_D1: st <= S_D2;
                S_D2: st <= S_SEL;
                S_PRE: if (cmd_n == K_PRE) begin o_v <= 1'b0; st <= S_ACT; end
                S_ACT: if (cmd_n == K_ACT) begin o_v <= 1'b1; o_bank <= bank; o_row <= row; st <= S_COL; end
                S_COL: begin
                    if (!we_q) begin
                        if (cmd_n == K_RD) begin cnt_q <= plen_q - 10'd1; st <= S_RD; end
                    end else if (p_wr[bank] && hold_q == 6'd0) begin
                        cnt_q <= plen_q; wr_first_q <= 1'b1; st <= S_WR;
                    end
                end
                S_RD: begin
                    if (cnt_q == 10'd0) begin st <= S_FIN; end
                    else cnt_q <= cnt_q - 10'd1;
                end
                S_WR: begin
                    if (pulling) begin cnt_q <= cnt_q - 10'd1; widx_q <= widx_q + 10'd1; end
                    if (wr_go) wr_first_q <= 1'b0;
                    if (last_beat) begin
                        st <= S_BST;
                        `ifdef MUT_MIN_DONE
                        dwp_q <= 1'b1;
`else
                        if (rem_q == plen_q) dwp_q <= 1'b1;
`endif
                    end
                end
                S_BST: st <= S_FIN;
                S_FIN: begin
                    addr_q <= addr_q + {15'd0, plen_q}; rem_q <= rem_q - plen_q;
                    if (rem_q == plen_q) st <= S_IDLE; else st <= S_D1;
                end
                S_PREA: if (cmd_n == K_PREA) st <= S_REF;
                S_REF: if (cmd_n == K_REF) st <= ref_ret ? S_SEL : S_IDLE;
                default: st <= S_IDLE;
            endcase
        end
    end
endmodule
`default_nettype wire
