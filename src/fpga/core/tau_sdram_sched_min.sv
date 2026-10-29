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
    parameter integer W_LEAD    = 2
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
    // PHY command interface (section 3.3)
    output reg  [2:0]  cmd,
    output reg  [1:0]  cmd_ba,
    output reg  [12:0] cmd_a,
    output wire        wvalid,
    output wire [15:0] wdata,
    output wire [1:0]  be,
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
    output wire        preempt_ack,
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
                     S_BST = 4'd8, S_FIN = 4'd9, S_PREA = 4'd10, S_REF = 4'd11;

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

    // address decode of the current piece
    wire [1:0]  bank = (BANK_HASH == 0) ? addr_q[11:10] :
                       (BANK_HASH == 1) ? (addr_q[11:10] ^ addr_q[21:20]) : (addr_q[11:10] ^ addr_q[21:20] ^ addr_q[23:22]);
    wire [12:0] row  = addr_q[24:12];
    wire [10:0] room = 11'd1024 - {1'b0, addr_q[9:0]};
`ifdef MUT_MIN_NOSPLIT
    wire [9:0]  plen = rem_q;
`else
    wire [9:0]  plen = ({1'b0, rem_q} < room) ? rem_q : room[9:0];
`endif
    wire        hit  = o_v && o_bank == bank && o_row == row && !row_aged[bank];
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
    assign wvalid  = beat;
    assign wdata   = w_data;
    assign be      = w_be;
    wire        wr_go   = beat && wr_first_q;
    wire        last_beat = beat && (cnt_q == 10'd0) && ((pv_q & ~({{(W_LEAD-1){1'b0}}, 1'b1} << (W_LEAD-1))) == {W_LEAD{1'b0}});

    // read data path
    assign r_valid = rvalid;
    assign r_data  = rdata;
    assign r_last  = rvalid && (rd_left_q == 10'd1);
    assign done    = r_last | done_w_q | done_b_q;
    assign done_tag = tag_q;
    assign cnt_requests = req_c_q; assign cnt_words = word_c_q; assign cnt_bad = bad_c_q;
    assign preempt_ack = preempt && (st == S_IDLE || st == S_SEL || st == S_FIN);

    always @* begin
        cmd = K_NOP; cmd_ba = 2'd0; cmd_a = 13'd0;
        case (st)
            S_IDLE: if (init_done && !preempt && o_v && row_aged[o_bank] && ok_pre[o_bank]) begin cmd = K_PRE; cmd_ba = o_bank; end
            S_PRE:  if (ok_pre[o_bank]) begin cmd = K_PRE; cmd_ba = o_bank; end
            S_ACT:  if (ok_act[bank]) begin cmd = K_ACT; cmd_ba = bank; cmd_a = row; end
            S_COL:  if (!we_q && ok_rd[bank]) begin cmd = K_RD; cmd_ba = bank; cmd_a = {3'b000, addr_q[9:0]}; end
            S_WR:   if (wr_go) begin cmd = K_WR; cmd_ba = o_bank; cmd_a = {3'b000, addr_q[9:0]}; end
`ifdef MUT_MIN_NOBST
            S_RD:   ;
`else
            S_RD:   if (cnt_q == 10'd0) cmd = K_BST;
`endif
            S_BST:  cmd = K_BST;
            S_PREA: if (ok_prea) cmd = K_PREA;
            S_REF:  if (ok_ref) cmd = K_REF;
            default: ;
        endcase
    end

    always @(posedge clk) begin
        if (rst) begin
            st <= S_IDLE; req_ready <= 1'b0; o_v <= 1'b0; pv_q <= {W_LEAD{1'b0}}; done_w_q <= 1'b0; done_b_q <= 1'b0;
            req_c_q <= 32'd0; word_c_q <= 32'd0; bad_c_q <= 16'd0; rd_left_q <= 10'd0; widx_q <= 10'd0; wr_first_q <= 1'b0;
            cnt_q <= 10'd0; plen_q <= 10'd0; rem_q <= 10'd0; tag_q <= 8'd0; we_q <= 1'b0; addr_q <= 25'd0; last_piece_q <= 1'b0;
            o_bank <= 2'd0; o_row <= 13'd0; ref_ret <= 1'b0;
        end else begin
            done_w_q <= 1'b0; done_b_q <= 1'b0;
            pv_q <= {pv_q[W_LEAD-2:0], pulling} ;
            if (banks_closed || cmd == K_PREA) o_v <= 1'b0;
            if (rvalid && rd_left_q != 10'd0) rd_left_q <= rd_left_q - 10'd1;
            req_ready <= (st == S_IDLE) && init_done && !(req_valid && req_ready) && !(rd_left_q != 10'd0);
            case (st)
                S_IDLE: begin
                    if (cmd == K_PRE) o_v <= 1'b0;
                    if (req_valid && req_ready) begin
                        we_q <= req_we; addr_q <= req_addr; rem_q <= req_len; tag_q <= req_tag; widx_q <= 10'd0;
                        rd_left_q <= (req_we || bad) ? 10'd0 : req_len;
                        req_ready <= 1'b0;
                        if (bad) begin st <= S_BAD; bad_c_q <= bad_c_q + 16'd1; end
                        else begin st <= S_SEL; req_c_q <= req_c_q + 32'd1; word_c_q <= word_c_q + {22'd0, req_len}; end
                    end else if (ref_due_i && init_done && !preempt) begin st <= S_PREA; ref_ret <= 1'b0; req_ready <= 1'b0; end
                end
                S_BAD: begin done_b_q <= 1'b1; st <= S_IDLE; end
                S_SEL: begin
                    plen_q <= plen;
                    if (preempt) st <= S_SEL;
                    else if (ref_due_i) begin st <= S_PREA; ref_ret <= 1'b1; end
                    else st <= hit ? S_COL : (o_v ? S_PRE : S_ACT);
                end
                S_PRE: if (cmd == K_PRE) begin o_v <= 1'b0; st <= S_ACT; end
                S_ACT: if (cmd == K_ACT) begin o_v <= 1'b1; o_bank <= bank; o_row <= row; st <= S_COL; end
                S_COL: begin
                    if (!we_q) begin
                        if (cmd == K_RD) begin cnt_q <= plen_q - 10'd1; st <= S_RD; end
                    end else if (ok_wr[bank]) begin
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
                        done_w_q <= 1'b1;
`else
                        if (rem_q == plen_q) done_w_q <= 1'b1;
`endif
                    end
                end
                S_BST: st <= S_FIN;
                S_FIN: begin
                    addr_q <= addr_q + {15'd0, plen_q}; rem_q <= rem_q - plen_q;
                    if (rem_q == plen_q) st <= S_IDLE; else st <= S_SEL;
                end
                S_PREA: if (cmd == K_PREA) st <= S_REF;
                S_REF: if (cmd == K_REF) st <= ref_ret ? S_SEL : S_IDLE;
                default: st <= S_IDLE;
            endcase
        end
    end
endmodule
`default_nettype wire
