// Tau V SDRAM scheduler (reference implementation v1). MIT licence.  CHECKPOINT 1: skeleton, queues, arbiter.
`default_nettype none
module tau_sdram_sched_v #(
    parameter integer NPORTS = 6,
    parameter integer QDEPTH = 4,
    parameter integer BANK_HASH = 2,
    parameter [7:0]   ENABLE_2D = 8'h02,
    parameter [8*10-1:0] MAXG_RD = {10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd64, 10'd128},
    parameter [8*10-1:0] MAXG_WR = {10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd32, 10'd128, 10'd128}
) (
    input  wire                 clk,
    input  wire                 rst,
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
    output wire [NPORTS-1:0]    done,
    output wire [NPORTS*8-1:0]  done_tag,
    output wire [NPORTS*16-1:0] cnt_bad
);
    wire [NPORTS-1:0] hd_bad, acc_bad;
    genvar p;
    generate
        for (p = 0; p < NPORTS; p = p + 1) begin : g_p
            wire [3:0] cnt; wire hv, hw, hl, h2; wire [1:0] hb; wire [12:0] hr; wire [9:0] hc, hp; wire [7:0] ht; wire acc;
            wire retire = hd_bad[p];
            tau_sdram_sv_pq #(.DEPTH(QDEPTH), .EN_2D(ENABLE_2D[p]), .BANK_HASH(BANK_HASH),
                .MAXG_RD(MAXG_RD[p*10 +: 10]), .MAXG_WR(MAXG_WR[p*10 +: 10])) u_pq (
                .clk(clk), .rst(rst), .req_valid(req_valid[p]), .req_ready(req_ready[p]), .req_we(req_we[p]),
                .req_addr(req_addr[p*25 +: 25]), .req_len(req_len[p*10 +: 10]), .req_tag(req_tag[p*8 +: 8]),
                .req_2d(req_2d[p]), .req_w(req_w[p*6 +: 6]), .req_h(req_h[p*6 +: 6]), .req_stride(req_stride[p*11 +: 11]),
                .req_skew(req_skew[p*2 +: 2]), .count(cnt), .hd_v(hv), .hd_bad(hd_bad[p]), .hd_we(hw), .hd_bank(hb),
                .hd_row(hr), .hd_col(hc), .hd_plen(hp), .hd_last(hl), .hd_tag(ht), .hd_2d(h2),
                .adv(1'b0), .pop_bad(retire), .accepted(acc), .accepted_bad(acc_bad[p]));
            reg d_q; reg [7:0] t_q; reg [15:0] bad_q;
            always @(posedge clk) begin
                if (rst) begin d_q <= 1'b0; t_q <= 8'd0; bad_q <= 16'd0; end
                else begin
                    d_q <= retire; if (retire) t_q <= ht;
                    if (retire && bad_q != 16'hffff) bad_q <= bad_q + 1'b1;
                end
            end
            assign done[p] = d_q; assign done_tag[p*8 +: 8] = t_q; assign cnt_bad[p*16 +: 16] = bad_q;
        end
    endgenerate
endmodule
`default_nettype wire
