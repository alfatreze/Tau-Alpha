// Tau V SDRAM scheduler: per-port request queue and piece generator. MIT licence.
// Clean-room, from docs/features/SDRAM_CONTROLLER_CONTRACT.md v0.6 sections 4 and 5.
//
// A shift-register FIFO of requests; entry 0 is the head. The head is consumed in "pieces": a piece never crosses a
// 1,024-word page boundary, never exceeds the port's maximum grant, and for a 2D read is never longer than the rest
// of its current row (each row is at least one piece). `adv` consumes one piece (its length is hd_plen); the last
// piece of a request pops the entry. A rejected request (bad length or bad 2D geometry) is queued like any other (so
// completion stays in order) with `hd_bad` set; the owner retires it with `pop_bad`.
`default_nettype none
// Lint: constant width conversions of parameter arithmetic, loop temporaries in clocked blocks and reserved/unused
// inputs are intended.
/* verilator lint_off WIDTHEXPAND */
/* verilator lint_off WIDTHTRUNC */
/* verilator lint_off BLKSEQ */
/* verilator lint_off UNUSEDSIGNAL */
module tau_sdram_sv_pq #(
    parameter integer DEPTH     = 4,
    parameter integer EN_2D     = 0,        // this port accepts 2D read requests
    parameter integer BANK_HASH = 2,        // 0 plain, 1 ^[21:20], 2 ^[21:20]^[23:22], 3 per-region skew table
    parameter integer MAXG_RD   = 128,
    parameter integer MAXG_WR   = 128,
    parameter integer SKR       = 8             // skew region table entries
) (
    input  wire        clk,
    input  wire        rst,
    // request side (handshake: accepted when req_valid & req_ready)
    input  wire        req_valid,
    output wire        req_ready,           // registered
    input  wire        req_we,
    input  wire [24:0] req_addr,
    input  wire [9:0]  req_len,
    input  wire [7:0]  req_tag,
    input  wire        req_2d,
    input  wire [5:0]  req_w,
    input  wire [5:0]  req_h,
    input  wire [10:0] req_stride,
    input  wire [SKR-1:0]   sk_vld,         // region table (BANK_HASH 3): entry valid, base = addr[24:22], 2-bit skew
    input  wire [SKR*3-1:0] sk_base,
    input  wire [SKR*2-1:0] sk_val,
    output wire [DEPTH-1:0]    ent_v,       // entry holds a good request
    output wire [DEPTH-1:0]    ent_we,
    output wire [DEPTH*13-1:0] ent_lo,      // conservative 4K-word row range of the whole request
    output wire [DEPTH*13-1:0] ent_hi,
    // head
    output wire [3:0]  count,
    output wire        hd_v,                // head exists and is a good request
    output wire        hd_bad,              // head exists and is a rejected request
    output wire        hd_we,
    output wire [1:0]  hd_bank,
    output wire [12:0] hd_row,
    output wire [9:0]  hd_col,
    output wire [9:0]  hd_plen,             // words of the current piece (1..512)
    output wire        hd_last,             // the current piece completes the request
    output wire [7:0]  hd_tag,
    output wire        hd_2d,
    input  wire        adv,                 // the current piece was issued
    input  wire        pop_bad,             // retire a bad head
    output wire        accepted,            // a request was taken this cycle (any kind)
    output wire        accepted_bad
);
    function [1:0] bank_of(input [24:0] a, input [1:0] sk);
        begin
            case (BANK_HASH)
                0:       bank_of = a[11:10];
                1:       bank_of = a[11:10] ^ a[21:20];
                3:       bank_of = a[11:10] ^ sk;
`ifdef MUT_HASH_DROP
                default: bank_of = a[11:10] ^ a[21:20];
`else
                default: bank_of = a[11:10] ^ a[21:20] ^ a[23:22];
`endif
            endcase
        end
    endfunction

    // request validity (contract 4 and 5)
    wire [11:0] area  = req_w * req_h;
`ifdef MUT_BADREQ
    wire        bad1d = (req_len == 10'd0) || (req_len > 10'd600);
`else
    wire        bad1d = (req_len == 10'd0) || (req_len > 10'd512);
`endif
    wire        bad2d = (EN_2D == 0) || req_we || (req_w == 6'd0) || (req_h == 6'd0) || (area > 12'd512);
    wire        bad_n = req_2d ? bad2d : bad1d;

    reg [DEPTH-1:0]  e_we, e_2d, e_bad;
    reg [24:0]       e_addr [0:DEPTH-1];
    reg [9:0]        e_rem  [0:DEPTH-1];     // words left in the request (1D) or in the current row (2D)
    reg [7:0]        e_tag  [0:DEPTH-1];
    reg [5:0]        e_w    [0:DEPTH-1];
    reg [5:0]        e_rows [0:DEPTH-1];     // rows left including the current one
    reg [10:0]       e_str  [0:DEPTH-1];
    reg [24:0]       e_rb   [0:DEPTH-1];     // address of the start of the current row
    reg [12:0]       e_lo   [0:DEPTH-1];
    reg [12:0]       e_hi   [0:DEPTH-1];
    reg [3:0]        cnt_q;
    reg              ready_q;

    wire push = req_valid & ready_q;
    assign accepted = push;
    assign accepted_bad = push & bad_n;
    assign req_ready = ready_q;
    assign count = cnt_q;

    // head piece
    wire c_v, c_bad, c_we, c_last, c_2d; wire [1:0] c_bank; wire [12:0] c_row; wire [9:0] c_col, c_plen; wire [7:0] c_tag;
    wire [24:0] h_addr = e_addr[0];
    wire [10:0] room   = 11'd1024 - {1'b0, h_addr[9:0]};
    wire [9:0]  maxg   = e_we[0] ? MAXG_WR[9:0] : MAXG_RD[9:0];
    wire [9:0]  m1     = (e_rem[0] < maxg) ? e_rem[0] : maxg;
`ifdef MUT_NO_PAGE_SPLIT
    wire [9:0]  plen   = m1;
`else
    wire [9:0]  plen   = ({1'b0, m1} < room) ? m1 : room[9:0];
`endif
    wire        plast  = (e_rem[0] == plen) && (!e_2d[0] || e_rows[0] == 6'd1);
    wire        hv     = (cnt_q != 4'd0);
    assign c_v = hv & ~e_bad[0];
    assign c_bad = hv & e_bad[0];
    assign c_we = e_we[0];
    reg [1:0] skew_h;
    integer si;
    always @* begin
        skew_h = 2'd0;
        for (si = SKR - 1; si >= 0; si = si - 1)
            if (sk_vld[si] && sk_base[si*3 +: 3] == h_addr[24:22]) skew_h = sk_val[si*2 +: 2];
    end
`ifdef MUT_SKEW_IGN
    assign c_bank = bank_of(h_addr, 2'd0);
`else
    assign c_bank = bank_of(h_addr, skew_h);
`endif
    genvar ge;
    generate
        for (ge = 0; ge < DEPTH; ge = ge + 1) begin : g_ent
            assign ent_v[ge]  = (ge < cnt_q) && !e_bad[ge];
            assign ent_we[ge] = e_we[ge];
            assign ent_lo[ge*13 +: 13] = e_lo[ge];
            assign ent_hi[ge*13 +: 13] = e_hi[ge];
        end
    endgenerate
    assign c_row = h_addr[24:12];
    assign c_col = h_addr[9:0];
    assign c_plen = plen;
    assign c_last = plast;
    assign c_tag = e_tag[0];
    assign c_2d = e_2d[0];

    // the head piece is registered (one cycle behind the entry state): the scheduler's decision pipeline blanks itself after an
    // advance (see tau_sdram_sched_v.sv); this takes the piece-length arithmetic out of the decision path
    reg  r_v, r_bad, r_we, r_last, r_2d; reg [1:0] r_bank; reg [12:0] r_row; reg [9:0] r_col, r_plen; reg [7:0] r_tag;
    always @(posedge clk) begin
        if (rst) begin r_v <= 1'b0; r_bad <= 1'b0; r_we <= 1'b0; r_last <= 1'b0; r_2d <= 1'b0; r_bank <= 2'd0; r_row <= 13'd0; r_col <= 10'd0; r_plen <= 10'd0; r_tag <= 8'd0; end
        else begin r_v <= c_v; r_bad <= c_bad; r_we <= c_we; r_last <= c_last; r_2d <= c_2d; r_bank <= c_bank; r_row <= c_row; r_col <= c_col; r_plen <= c_plen; r_tag <= c_tag; end
    end
    assign hd_v = r_v; assign hd_bad = r_bad; assign hd_we = r_we; assign hd_last = r_last; assign hd_2d = r_2d; assign hd_bank = r_bank;
    assign hd_row = r_row; assign hd_col = r_col; assign hd_plen = r_plen; assign hd_tag = r_tag;
    wire do_adv = adv & hd_v;
    wire pop    = (do_adv & plast) | (pop_bad & hd_bad);
    wire [3:0] cnt_n = cnt_q + {3'd0, push} - {3'd0, pop};
    wire [9:0] rem_n = e_rem[0] - plen;

    integer i;
    reg [3:0] widx;
    always @(posedge clk) begin
        if (rst) begin
            cnt_q <= 4'd0; ready_q <= 1'b0;
            e_we <= {DEPTH{1'b0}}; e_2d <= {DEPTH{1'b0}}; e_bad <= {DEPTH{1'b0}};
        end else begin
            cnt_q   <= cnt_n;
            ready_q <= (cnt_n < DEPTH[3:0]);
            if (pop) begin
                for (i = 0; i < DEPTH - 1; i = i + 1) begin
                    e_we[i] <= e_we[i+1]; e_2d[i] <= e_2d[i+1]; e_bad[i] <= e_bad[i+1];
                    e_addr[i] <= e_addr[i+1]; e_rem[i] <= e_rem[i+1]; e_tag[i] <= e_tag[i+1]; e_w[i] <= e_w[i+1];
                    e_rows[i] <= e_rows[i+1]; e_str[i] <= e_str[i+1]; e_rb[i] <= e_rb[i+1]; e_lo[i] <= e_lo[i+1]; e_hi[i] <= e_hi[i+1];
                end
            end else if (do_adv) begin
                if (rem_n != 10'd0) begin
                    e_addr[0] <= h_addr + {15'd0, plen};
                    e_rem[0]  <= rem_n;
                end else begin                                   // 2D: next row (not the last piece)
                    e_rows[0] <= e_rows[0] - 6'd1;
`ifdef MUT_2D_STRIDE
                    e_rb[0]   <= e_rb[0] + {14'd0, e_str[0]} + 25'd1;
                    e_addr[0] <= e_rb[0] + {14'd0, e_str[0]} + 25'd1;
`else
                    e_rb[0]   <= e_rb[0] + {14'd0, e_str[0]};
                    e_addr[0] <= e_rb[0] + {14'd0, e_str[0]};
`endif
                    e_rem[0]  <= {4'd0, e_w[0]};
                end
            end
            if (push) begin
                widx = cnt_q - {3'd0, pop};
                for (i = 0; i < DEPTH; i = i + 1) begin
                    if (widx == i[3:0]) begin
                        e_we[i]   <= req_we & ~req_2d;           // 2D writes are rejected (bad2d)
                        e_2d[i]   <= req_2d & (EN_2D != 0);
                        e_bad[i]  <= bad_n;
                        e_addr[i] <= req_addr;
                        e_rem[i]  <= req_2d ? {4'd0, req_w} : req_len;
                        e_tag[i]  <= req_tag;
                        e_w[i]    <= req_w;
                        e_rows[i] <= req_h;
                        e_str[i]  <= req_stride;
                        e_rb[i]   <= req_addr;
                        e_lo[i]   <= req_addr[24:12];
                        e_hi[i]   <= req_2d ? (req_addr + {3'd0, req_h, 11'd0}) >> 12 : (req_addr + {15'd0, req_len} - 25'd1) >> 12;
                    end
                end
            end
        end
    end
    wire unused_pq = &{1'b0, e_bad[0]};
endmodule
`default_nettype wire
