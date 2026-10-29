// SDRAM timing model for the scheduler's own planning. MIT licence. Clean-room, from the contract sections 2 and 3.4 (the rules)
// and the PHY's published permits (the permit definitions: the model reproduces them from its own state).
//
// The model sees every command the scheduler OFFERS (registered offer `cmd_q`, in the cycle it is offered) and keeps the per-bank and
// global timers of the rules: tRCD, tRAS, tRC, tRP, write recovery (tDPL/tWR), tDAL, tRRD, tRFC, read-to-write turnaround, open banks and
// open bursts. Its state in cycle d is the state after the offer of cycle d-1 (the PHY's `n_*` state). `p_*` are the permits that will be
// valid for an offer in cycle d+1 **provided the offer in cycle d is a NOP** (every counter one lower): the scheduler plans the
// registered offer of d+1 in cycle d from them, and only offers a command other than BST / a chained column command in a cycle after a NOP.
// The PHY's own registered permits are a cross-check in the scheduler (`dbg_pmiss`), never an input of the offer.
`default_nettype none
/* verilator lint_off WIDTHEXPAND */
/* verilator lint_off WIDTHTRUNC */
module tau_sdram_sv_tm #(
    parameter integer TRCD_C = 2,
    parameter integer TRP_C  = 2,
    parameter integer TRAS_C = 5,
    parameter integer TRC_C  = 6,
    parameter integer TRFC_C = 10,
    parameter integer WRP_C  = 2,       // last written word to PRE (tDPL / tWR)
    parameter integer DAL_C  = 4,       // last written word to ACT of the same bank (tDAL)
    parameter integer RRD_C  = 2,
    parameter integer RW_C   = 3        // the cycle of the BST that ends a read to the earliest WR
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        init_done,
    input  wire [2:0]  cmd_q,           // the command offered in this cycle
    input  wire [1:0]  ba_q,
    input  wire        beat_q,          // a write beat with at least one byte enable is offered in this cycle
    input  wire        banks_closed,    // the PHY forced a refresh (all banks closed, tRFC running)
    output wire [3:0]  p_act,
    output wire [3:0]  p_rd,
    output wire [3:0]  p_wr,
    output wire [3:0]  p_pre,
    output wire        p_prea,
    output wire        p_ref,
    output wire [3:0]  m_open
);
    localparam [2:0] K_ACT = 3'd1, K_RD = 3'd2, K_WR = 3'd3, K_PRE = 3'd4, K_PREA = 3'd5, K_REF = 3'd6, K_BST = 3'd7;
    localparam integer CW = 4;
    function [CW-1:0] dec(input [CW-1:0] x); begin dec = (x == {CW{1'b0}}) ? {CW{1'b0}} : x - 1'b1; end endfunction
`ifdef MUT_TM_Z
    function z1(input [CW-1:0] x); begin z1 = (x <= 4'd2); end endfunction
`else
    function z1(input [CW-1:0] x); begin z1 = (x <= {{(CW-1){1'b0}}, 1'b1}); end endfunction
`endif

    reg [3:0]    open_q;
    reg [CW-1:0] rcd_q [0:3]; reg [CW-1:0] ras_q [0:3]; reg [CW-1:0] rc_q [0:3];
    reg [CW-1:0] rp_q [0:3];  reg [CW-1:0] wrp_q [0:3]; reg [CW-1:0] dal_q [0:3];
    reg [CW-1:0] rrd_q, rfc_q, rw_q;
    reg          rd_open_q, wr_open_q; reg [1:0] rd_bank_q, wr_bank_q;
    assign m_open = open_q;

    wire e_act  = (cmd_q == K_ACT);  wire e_rd = (cmd_q == K_RD);  wire e_wr  = (cmd_q == K_WR);
    wire e_pre  = (cmd_q == K_PRE);  wire e_prea = (cmd_q == K_PREA); wire e_ref = (cmd_q == K_REF); wire e_bst = (cmd_q == K_BST);
    wire rd_open_n = e_rd | (rd_open_q & ~(e_bst | e_prea | e_wr | (e_pre & (ba_q == rd_bank_q))));
    wire wr_open_n = e_wr | (wr_open_q & ~(e_bst | e_prea | e_rd | (e_pre & (ba_q == wr_bank_q))));
    wire [1:0] wr_bank_n = e_wr ? ba_q : wr_bank_q;
    integer b;
    always @(posedge clk) begin
        if (rst) begin
            open_q <= 4'd0; rrd_q <= {CW{1'b0}}; rfc_q <= {CW{1'b0}}; rw_q <= {CW{1'b0}};
            rd_open_q <= 1'b0; wr_open_q <= 1'b0; rd_bank_q <= 2'd0; wr_bank_q <= 2'd0;
            for (b = 0; b < 4; b = b + 1) begin
                rcd_q[b] <= {CW{1'b0}}; ras_q[b] <= {CW{1'b0}}; rc_q[b] <= {CW{1'b0}}; rp_q[b] <= {CW{1'b0}}; wrp_q[b] <= {CW{1'b0}}; dal_q[b] <= {CW{1'b0}};
            end
        end else begin
            rd_open_q <= rd_open_n; wr_open_q <= wr_open_n; rd_bank_q <= e_rd ? ba_q : rd_bank_q; wr_bank_q <= wr_bank_n;
            rrd_q <= e_act ? RRD_C - 1 : dec(rrd_q);
            rfc_q <= (e_ref || banks_closed) ? TRFC_C - 1 : dec(rfc_q);
`ifdef MUT_TM_RW
            rw_q  <= (rd_open_q & ~rd_open_n) ? RW_C - 2 : dec(rw_q);
`else
            rw_q  <= (rd_open_q & ~rd_open_n) ? RW_C - 1 : dec(rw_q);
`endif
            for (b = 0; b < 4; b = b + 1) begin
                if (banks_closed) begin
                    open_q[b] <= 1'b0; rp_q[b] <= TRP_C - 1;
                end else begin
                    open_q[b] <= (e_act && ba_q == b) || (open_q[b] && !(e_prea || (e_pre && ba_q == b)));
`ifdef MUT_TM_RP
                    rp_q[b]   <= (e_prea || (e_pre && ba_q == b)) ? TRP_C - 2 : dec(rp_q[b]);
`else
                    rp_q[b]   <= (e_prea || (e_pre && ba_q == b)) ? TRP_C - 1 : dec(rp_q[b]);
`endif
                end
`ifdef MUT_TM_RCD
                rcd_q[b] <= (e_act && ba_q == b) ? TRCD_C - 2 : dec(rcd_q[b]);
`else
                rcd_q[b] <= (e_act && ba_q == b) ? TRCD_C - 1 : dec(rcd_q[b]);
`endif
                ras_q[b] <= (e_act && ba_q == b) ? TRAS_C - 1 : dec(ras_q[b]);
                rc_q[b]  <= (e_act && ba_q == b) ? TRC_C - 1  : dec(rc_q[b]);
                wrp_q[b] <= (e_act && ba_q == b) ? {CW{1'b0}} : ((beat_q && wr_bank_n == b) ? WRP_C - 1 : dec(wrp_q[b]));
                dal_q[b] <= (e_act && ba_q == b) ? {CW{1'b0}} : ((beat_q && wr_bank_n == b) ? DAL_C - 1 : dec(dal_q[b]));
            end
        end
    end
    // permits for the next cycle's offer, assuming this cycle's offer is a NOP
    wire [3:0] a_b, rd_b, pre_b, prea_b, ref_b;
    genvar g;
    generate
        for (g = 0; g < 4; g = g + 1) begin : gb
            assign a_b[g]    = ~open_q[g] & z1(rp_q[g]) & z1(rc_q[g]) & z1(dal_q[g]);
            assign rd_b[g]   = open_q[g] & z1(rcd_q[g]);
            assign pre_b[g]  = open_q[g] & z1(ras_q[g]) & z1(wrp_q[g]);
            assign prea_b[g] = ~open_q[g] | (z1(ras_q[g]) & z1(wrp_q[g]));
            assign ref_b[g]  = ~open_q[g] & z1(rp_q[g]) & z1(rc_q[g]);
        end
    endgenerate
    wire rfc_ok = z1(rfc_q);
    assign p_act  = a_b & {4{z1(rrd_q) & rfc_ok & init_done}};
    assign p_rd   = rd_b & {4{init_done}};
    assign p_wr   = rd_b & {4{~rd_open_q & z1(rw_q) & init_done}};
    assign p_pre  = pre_b & {4{init_done}};
    assign p_prea = (&prea_b) & rfc_ok & init_done;
    assign p_ref  = (&ref_b) & rfc_ok & ~(rd_open_q | wr_open_q) & init_done;
endmodule
`default_nettype wire
