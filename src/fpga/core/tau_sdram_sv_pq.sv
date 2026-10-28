// Tau V SDRAM scheduler: per-port request queue and piece generator. MIT licence.
// Clean-room, from docs/features/SDRAM_CONTROLLER_CONTRACT.md v0.6 sections 4 and 5.
//
// A shift-register FIFO of requests; entry 0 is the head. The head is consumed in "pieces": a piece never crosses a
// 1,024-word page boundary, never exceeds the port's maximum grant, and for a 2D read is never longer than the rest
// of its current row (each row is at least one piece). `adv` consumes one piece (its length is hd_plen); the last
// piece of a request pops the entry. A rejected request (bad length or bad 2D geometry) is queued like any other (so
// completion stays in order) with `hd_bad` set; the owner retires it with `pop_bad`.
`default_nettype none
module tau_sdram_sv_pq #(
    parameter integer DEPTH     = 4,
    parameter integer EN_2D     = 0,        // this port accepts 2D read requests
    parameter integer BANK_HASH = 2,        // 0 plain, 1 ^[21:20], 2 ^[21:20]^[23:22], 3 client skew
    parameter integer MAXG_RD   = 128,
    parameter integer MAXG_WR   = 128
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
    input  wire [1:0]  req_skew,
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
    reg [1:0]        e_sk   [0:DEPTH-1];
    reg [3:0]        cnt_q;
    reg              ready_q;

    wire push = req_valid & ready_q;
    assign accepted = push;
    assign accepted_bad = push & bad_n;
    assign req_ready = ready_q;
    assign count = cnt_q;

    // head piece
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
    assign hd_v    = hv & ~e_bad[0];
    assign hd_bad  = hv & e_bad[0];
    assign hd_we   = e_we[0];
    assign hd_bank = bank_of(h_addr, e_sk[0]);
    assign hd_row  = h_addr[24:12];
    assign hd_col  = h_addr[9:0];
    assign hd_plen = plen;
    assign hd_last = plast;
    assign hd_tag  = e_tag[0];
    assign hd_2d   = e_2d[0];

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
                    e_rows[i] <= e_rows[i+1]; e_str[i] <= e_str[i+1]; e_rb[i] <= e_rb[i+1]; e_sk[i] <= e_sk[i+1];
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
                        e_2d[i]   <= req_2d;
                        e_bad[i]  <= bad_n;
                        e_addr[i] <= req_addr;
                        e_rem[i]  <= req_2d ? {4'd0, req_w} : req_len;
                        e_tag[i]  <= req_tag;
                        e_w[i]    <= req_w;
                        e_rows[i] <= req_h;
                        e_str[i]  <= req_stride;
                        e_rb[i]   <= req_addr;
                        e_sk[i]   <= req_skew;
                    end
                end
            end
        end
    end
    wire unused_pq = &{1'b0, e_bad[0]};
endmodule
`default_nettype wire
