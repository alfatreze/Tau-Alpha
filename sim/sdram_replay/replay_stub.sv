// Stub scheduler for the replay harness (MIT licence). NOT a model of tau_sdram_sched_v: it speaks the contract section 4
// port protocol with a fixed, simple timing (one request at a time, round robin over the ports, a fixed start-up latency,
// then one word per cycle) and keeps the data in a flat array. It exists to develop and test the harness logic
// (the .treq reader, fusing, fences, barriers, shadow memory, statistics) much faster than the real
// PHY + scheduler + chip model, and as the "fast mode" of the harness. Numbers from the stub say nothing about the controller.
//
// Memory: 2^22 words, indexed by {a[24:22], a[18:0]} (the harness checks that a[21:19] is 0; see the README).
`timescale 1ps/1ps
module replay_stub #(
    parameter integer NP = 6,
    parameter integer QD = 4,
    parameter integer W_LEAD = 2,
    parameter integer RD_START = 18,      // cycles from the start of a read to its first word
    parameter integer WR_START = 6,       // cycles from the start of a write to its first w_next
    parameter integer WR_DONE = 3,        // cycles from the last data beat to done
    parameter integer GAP = 2             // idle cycles between requests
) (
    input  wire                 clk,
    input  wire                 rst,
    input  wire [NP-1:0]        req_valid,
    output reg  [NP-1:0]        req_ready,
    input  wire [NP-1:0]        req_we,
    input  wire [NP*25-1:0]     req_addr,
    input  wire [NP*10-1:0]     req_len,
    input  wire [NP*8-1:0]      req_tag,
    input  wire [NP-1:0]        req_2d,
    input  wire [NP*6-1:0]      req_w,
    input  wire [NP*6-1:0]      req_h,
    input  wire [NP*11-1:0]     req_stride,
    output reg  [NP-1:0]        w_next,
    output reg  [NP*10-1:0]     w_idx,
    input  wire [NP*16-1:0]     w_data,
    output reg  [NP-1:0]        r_valid,
    output reg  [NP*16-1:0]     r_data,
    output reg  [NP-1:0]        r_last,
    output reg  [NP-1:0]        done,
    output reg  [NP*8-1:0]      done_tag
);
    reg [15:0] mem [0:(1<<22)-1];
    function integer mx(input [24:0] a);
        begin mx = {a[24:22], a[18:0]}; end
    endfunction
    integer mi;
    initial for (mi = 0; mi < (1 << 22); mi = mi + 1) mem[mi] = 16'hxxxx;
    task poke(input [24:0] a, input [15:0] d);
        begin mem[mx(a)] = d; end
    endtask
    function [16:0] peek(input [24:0] a);
        begin peek = {(^mem[mx(a)] !== 1'bx), mem[mx(a)]}; end
    endfunction

    // per-port request FIFOs
    reg [24:0] f_addr [0:NP*QD-1];
    reg [9:0]  f_len  [0:NP*QD-1];
    reg [7:0]  f_tag  [0:NP*QD-1];
    reg        f_we   [0:NP*QD-1];
    reg        f_2d   [0:NP*QD-1];
    reg [5:0]  f_w    [0:NP*QD-1];
    reg [5:0]  f_h    [0:NP*QD-1];
    reg [10:0] f_str  [0:NP*QD-1];
    integer    f_n [0:NP-1], f_h0 [0:NP-1];
    integer    p, ix, srv_p, srv_k, srv_n, srv_t, srv_st, rr, c, k, srv_c, srv_w2d, dr;
    reg [24:0] srv_a, srv_rb; reg srv_2d; reg [7:0] srv_tag; reg [10:0] srv_str;
    // delay line from w_next to the data beat: stage j holds the word pulled j cycles ago
    reg        wq_v [0:7];
    reg [24:0] wq_a [0:7];
    integer    wq_p [0:7];
    integer    wk;

    always @(posedge clk) begin
        if (rst) begin
            req_ready <= {NP{1'b0}}; w_next <= 0; r_valid <= 0; r_last <= 0; done <= 0; w_idx <= 0;
            for (p = 0; p < NP; p = p + 1) begin f_n[p] = 0; f_h0[p] = 0; end
            for (wk = 0; wk < 8; wk = wk + 1) wq_v[wk] = 1'b0;
            srv_st = 0; rr = 0; srv_t = 0;
        end else begin
            w_next <= 0; r_valid <= 0; r_last <= 0; done <= 0;
            // write data beats
            for (wk = 7; wk >= 1; wk = wk - 1) begin wq_v[wk] = wq_v[wk-1]; wq_a[wk] = wq_a[wk-1]; wq_p[wk] = wq_p[wk-1]; end
            wq_v[0] = 1'b0;
            if (wq_v[W_LEAD + 1]) mem[mx(wq_a[W_LEAD + 1])] = w_data[wq_p[W_LEAD + 1]*16 +: 16];
            // accept
            for (p = 0; p < NP; p = p + 1) begin
                if (req_valid[p] && req_ready[p]) begin
                    ix = p * QD + ((f_h0[p] + f_n[p]) % QD);
                    f_addr[ix] = req_addr[p*25 +: 25]; f_len[ix] = req_len[p*10 +: 10]; f_tag[ix] = req_tag[p*8 +: 8];
                    f_we[ix] = req_we[p]; f_2d[ix] = req_2d[p]; f_w[ix] = req_w[p*6 +: 6]; f_h[ix] = req_h[p*6 +: 6];
                    f_str[ix] = req_stride[p*11 +: 11];
                    f_n[p] = f_n[p] + 1;
                end
            end
            // server
            case (srv_st)
                0: begin                                   // idle: pick the next port, round robin
                    srv_t = srv_t + 1;
                    if (srv_t >= GAP) begin
                        for (c = 0; c < NP; c = c + 1) begin
                            p = (rr + c) % NP;
                            if (srv_st == 0 && f_n[p] > 0) begin
                                ix = p * QD + f_h0[p];
                                srv_p = p; srv_a = f_addr[ix]; srv_rb = f_addr[ix]; srv_2d = f_2d[ix]; srv_tag = f_tag[ix];
                                srv_w2d = f_w[ix]; srv_str = f_str[ix];
                                srv_n = f_2d[ix] ? f_w[ix] * f_h[ix] : f_len[ix];
                                srv_k = 0; srv_c = 0; srv_t = 0; srv_st = f_we[ix] ? 1 : 2;
                                f_h0[p] = (f_h0[p] + 1) % QD; f_n[p] = f_n[p] - 1;
                                rr = (p + 1) % NP;
                            end
                        end
                    end
                end
                1: begin                                   // write: pull one word per cycle (w_next with its index)
                    srv_t = srv_t + 1;
                    if (srv_t > WR_START && srv_k < srv_n) begin
                        w_next[srv_p] <= 1'b1; w_idx[srv_p*10 +: 10] <= srv_k;
                        wq_v[0] = 1'b1; wq_a[0] = srv_a; wq_p[0] = srv_p;
                        srv_k = srv_k + 1; srv_a = srv_a + 1;
                        if (srv_k == srv_n) begin srv_st = 3; srv_t = 0; end
                    end
                end
                3: begin                                   // write: wait for the last beat, then done
                    srv_t = srv_t + 1;
                    if (srv_t > W_LEAD + 1 + WR_DONE) begin
                        done[srv_p] <= 1'b1; done_tag[srv_p*8 +: 8] <= srv_tag; srv_st = 0; srv_t = 0;
                    end
                end
                2: begin                                   // read: latency, then one word per cycle
                    srv_t = srv_t + 1;
                    if (srv_t > RD_START && srv_k < srv_n) begin
                        r_valid[srv_p] <= 1'b1; r_data[srv_p*16 +: 16] <= mem[mx(srv_a)];
                        if (srv_k == srv_n - 1) begin
                            r_last[srv_p] <= 1'b1; done[srv_p] <= 1'b1; done_tag[srv_p*8 +: 8] <= srv_tag; srv_st = 0; srv_t = 0;
                        end
                        srv_k = srv_k + 1; srv_c = srv_c + 1;
                        if (srv_2d && srv_c == srv_w2d) begin srv_c = 0; srv_rb = srv_rb + srv_str; srv_a = srv_rb; end
                        else srv_a = srv_a + 1;
                    end
                end
                default: srv_st = 0;
            endcase
            for (p = 0; p < NP; p = p + 1) req_ready[p] <= (f_n[p] < QD - 1);
        end
    end
endmodule
