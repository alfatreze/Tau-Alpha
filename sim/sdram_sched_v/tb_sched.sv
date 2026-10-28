// System test bench for tau_sdram_sched_v: scheduler + PHY + chip model + simple client models. MIT licence.
// Written from the contract (v0.6 sections 4, 5, 7.3). One test per run: vvp tb.vvp +TEST=<name>, prints
// "TEST <name> PASS" or "TEST <name> FAIL". Model, PHY and scheduler parameters are top-level parameters.
//
// Client models (one per port): a request list (the tests fill a global request table), a pull model for write data
// (answers `w_next` exactly W_LEAD cycles later with a pattern that depends only on the word address and the
// request's generation), a read checker against a shadow memory, a completion queue (done order and tag, r_last in
// the same cycle as done for reads), and at every write done a back-door comparison with the chip model's memory.
// Every legal test ends with: phy_fault 0, zero chip-model errors, zero DQ conflicts, no scheduler internal error.
`timescale 1ps/1ps
`include "sdram_chip_err.vh"

module tb_sched #(
    parameter integer P_PS      = 10000,
    parameter integer CLSEL     = 2,
    parameter integer BANK_HASH = 2,
    parameter integer W_LEAD    = 2,
    parameter integer WF_DEPTH  = 8,
    parameter integer QDEPTH    = 4,
    parameter integer INITCYC   = 200,
    parameter integer REFIPS    = 7_800_000,
    parameter integer DEBTMAX   = 8,
    parameter integer LMAX_T    = 512,
    parameter integer NRAND     = 20000,
    parameter integer TMO_CYC   = 4000000
);
    localparam NP  = 6;
    localparam NRQ = 4096;
    localparam [7:0] EN2D = 8'h02;

    reg clk = 1'b0;
    always #(P_PS/2) clk = ~clk;
    reg rst = 1'b1;
    integer cyc = 0;
    always @(posedge clk) cyc <= cyc + 1;

    // ------------------------------------------------------------------------------------------------
    // DUT, PHY, chip model
    // ------------------------------------------------------------------------------------------------
    reg  [NP-1:0]    req_valid = 0, req_we = 0, req_2d = 0;
    reg  [NP*25-1:0] req_addr = 0;
    reg  [NP*10-1:0] req_len = 0;
    reg  [NP*8-1:0]  req_tag = 0;
    reg  [NP*6-1:0]  req_w = 0, req_h = 0;
    reg  [NP*11-1:0] req_stride = 0;
    reg  [NP*2-1:0]  req_skew = 0;
    reg  [NP*16-1:0] w_data = 0;
    reg  [NP*2-1:0]  w_be = 0;
    wire [NP-1:0]    urgent;
    wire [NP-1:0]    req_ready, w_next, r_valid, r_last, done;
    wire [NP*10-1:0] w_idx;
    wire [NP*16-1:0] r_data;
    wire [NP*8-1:0]  done_tag;
    wire [NP*32-1:0] cnt_requests, cnt_words, cnt_busy;
    wire [NP*16-1:0] cnt_bad;
    wire [3:0]       dbg_err;
    wire [2:0]  cmd;  wire [1:0] cmd_ba; wire [12:0] cmd_a;
    wire        wvalid; wire [15:0] wdata; wire [1:0] be;
    wire [3:0]  ok_act, ok_rd, ok_wr, ok_pre, row_aged, ref_debt;
    wire        ok_prea, ok_ref, ok_bst, init_done, rvalid, phy_fault, preempt, banks_closed, ref_due, preempt_ack;
    wire [15:0] rdata, fault_count, dq_conflicts; wire [3:0] fault_cause; wire [31:0] refreshes; wire [4:0] rd_lat;
    wire [12:0] c_a; wire [1:0] c_ba, c_dqm; wire c_clk, c_cke, c_ras, c_cas, c_we; wire [15:0] dq;

    tau_sdram_phy #(.CLK_HZ((1_000_000_000 / P_PS) * 1000), .CL(CLSEL), .INIT_WAIT_CYCLES(INITCYC), .SIM_SHORT_INIT(1),
                    .REFRESH_INTERVAL_PS(REFIPS), .REFRESH_DEBT_MAX(DEBTMAX)) phy (
        .clk(clk), .rst(rst), .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .wvalid(wvalid), .wdata(wdata), .be(be),
        .ok_act(ok_act), .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_prea(ok_prea), .ok_ref(ok_ref), .ok_bst(ok_bst),
        .init_done(init_done), .rvalid(rvalid), .rdata(rdata), .phy_fault(phy_fault), .fault_count(fault_count),
        .fault_cause(fault_cause), .row_aged(row_aged), .preempt(preempt), .preempt_ack(preempt_ack), .banks_closed(banks_closed),
        .ref_due(ref_due), .ref_debt(ref_debt), .refreshes(refreshes), .dq_conflicts(dq_conflicts), .rd_lat(rd_lat),
        .dram_a(c_a), .dram_ba(c_ba), .dram_dq(dq), .dram_dqm(c_dqm), .dram_clk(c_clk), .dram_cke(c_cke),
        .dram_ras_n(c_ras), .dram_cas_n(c_cas), .dram_we_n(c_we));
    sdram_chip_model #(.CL_DEFAULT(CLSEL), .INIT_NOP_MIN(INITCYC), .INIT_WAIT_PS(INITCYC * P_PS), .MEM_LOG2(20), .MEM_COMPACT(1), .VERBOSE(1)) chip (
        .dram_clk(c_clk), .dram_cke(c_cke), .dram_ras_n(c_ras), .dram_cas_n(c_cas), .dram_we_n(c_we), .dram_dqm(c_dqm),
        .dram_ba(c_ba), .dram_a(c_a), .dram_dq(dq), .fpga_oe(phy.u_core.dq_oe), .cap_clk(clk));
    tau_sdram_sched_v #(.NPORTS(NP), .QDEPTH(QDEPTH), .BANK_HASH(BANK_HASH), .W_LEAD(W_LEAD), .WF_DEPTH(WF_DEPTH),
                        .ENABLE_2D(EN2D)) dut (
        .clk(clk), .rst(rst), .req_valid(req_valid), .req_ready(req_ready), .req_we(req_we), .req_addr(req_addr),
        .req_len(req_len), .req_tag(req_tag), .req_2d(req_2d), .req_w(req_w), .req_h(req_h), .req_stride(req_stride),
        .req_skew(req_skew), .urgent(urgent), .w_next(w_next), .w_idx(w_idx), .w_data(w_data), .w_be(w_be),
        .r_valid(r_valid), .r_data(r_data), .r_last(r_last), .done(done), .done_tag(done_tag),
        .cnt_requests(cnt_requests), .cnt_words(cnt_words), .cnt_busy(cnt_busy), .cnt_bad(cnt_bad), .dbg_err(dbg_err),
        .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .wvalid(wvalid), .wdata(wdata), .be(be),
        .ok_act(ok_act), .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_bst(ok_bst), .init_done(init_done),
        .rvalid(rvalid), .rdata(rdata), .row_aged(row_aged), .preempt(preempt), .banks_closed(banks_closed));
    assign preempt_ack = 1'b0;

    // ------------------------------------------------------------------------------------------------
    // request table
    // ------------------------------------------------------------------------------------------------
    integer         rq_port [0:NRQ-1];
    reg             rq_we   [0:NRQ-1];
    reg  [24:0]     rq_addr [0:NRQ-1];
    integer         rq_len  [0:NRQ-1];
    reg             rq_2d   [0:NRQ-1];
    integer         rq_w    [0:NRQ-1];
    integer         rq_h    [0:NRQ-1];
    integer         rq_str  [0:NRQ-1];
    reg  [1:0]      rq_skew [0:NRQ-1];
    integer         rq_gen  [0:NRQ-1];
    integer         rq_bem  [0:NRQ-1];
    integer         rq_rel  [0:NRQ-1];
    integer         rq_dep  [0:NRQ-1];
    reg             rq_bad  [0:NRQ-1];
    reg             rq_acc  [0:NRQ-1];
    reg             rq_done [0:NRQ-1];
    integer         rq_acyc [0:NRQ-1];
    integer         rq_fcyc [0:NRQ-1];
    integer         rq_dcyc [0:NRQ-1];
    integer         nrq = 0, ndone = 0, nacc = 0;
    integer         cur_gen = 1, cur_bem = 0;
    integer         last_rq;
    integer         tb_fail = 0;
    reg [8*40-1:0]  tname;

    function integer rq_total(input integer i);
        begin rq_total = rq_2d[i] ? rq_w[i] * rq_h[i] : rq_len[i]; end
    endfunction
    function [24:0] rq_waddr(input integer i, input integer k);     // address of word k of request i
        reg [24:0] a;
        begin
            if (rq_2d[i]) a = rq_addr[i] + (k / rq_w[i]) * rq_str[i] + (k % rq_w[i]);
            else          a = rq_addr[i] + k;
            rq_waddr = a;
        end
    endfunction
    function [24:0] A(input integer region, input integer off);
        begin A = {1'b0, region[3:0], 4'b0000, off[15:0]}; end
    endfunction
    task add_rq(input integer port, input we, input [24:0] addr, input integer len, input integer rel, input integer dep);
        integer i;
        begin
            i = nrq;
            rq_port[i] = port; rq_we[i] = we; rq_addr[i] = addr; rq_len[i] = len; rq_2d[i] = 1'b0; rq_w[i] = 0; rq_h[i] = 0; rq_str[i] = 0;
            rq_skew[i] = 2'b00; rq_gen[i] = cur_gen; rq_bem[i] = cur_bem; rq_rel[i] = rel; rq_dep[i] = dep;
            rq_bad[i] = (len == 0) || (len > 512); rq_acc[i] = 1'b0; rq_done[i] = 1'b0; rq_acyc[i] = -1; rq_fcyc[i] = -1; rq_dcyc[i] = -1;
            if (addr[24] || addr[19:16] != 4'd0) begin $display("tb: test address %h outside the shadow range", addr); tb_fail = tb_fail + 1; end
            nrq = nrq + 1; last_rq = i;
        end
    endtask
    task add_2d(input integer port, input [24:0] addr, input integer w, input integer h, input integer stride, input integer rel, input integer dep);
        integer i;
        begin
            add_rq(port, 1'b0, addr, 0, rel, dep);
            i = last_rq;
            rq_2d[i] = 1'b1; rq_w[i] = w; rq_h[i] = h; rq_str[i] = stride;
            rq_bad[i] = ((EN2D >> port) & 1) == 0 || w == 0 || h == 0 || w * h > 512 || w > 63 || h > 63 || stride > 2047;
        end
    endtask

    // data patterns: a function of the word address and the generation only
    function [15:0] pat(input [24:0] a, input integer g);
        reg [31:0] x;
        begin
            x = {7'd0, a} * 32'h9E3779B1 + g * 32'h7F4A7C15;
            x = x ^ (x >> 15);
            x = x * 32'h2C1B3C6D;
            pat = x[31:16] ^ x[15:0];
        end
    endfunction
    function [1:0] bepat(input [24:0] a, input integer m);
        begin
            if (m == 0) bepat = 2'b11;
            else case (a[2:0]) 3'd0: bepat = 2'b11; 3'd1: bepat = 2'b01; 3'd2: bepat = 2'b10; 3'd3: bepat = 2'b00;
                               3'd4: bepat = 2'b11; 3'd5: bepat = 2'b01; 3'd6: bepat = 2'b10; default: bepat = 2'b11; endcase
        end
    endfunction

    // shadow memory (what the chip must hold once the writes accepted so far have executed)
    reg [15:0] sh_d [0:(1<<20)-1];
    reg [1:0]  sh_v [0:(1<<20)-1];
    function integer sidx(input [24:0] a);
        begin sidx = {a[23:20], a[15:0]}; end
    endfunction
    // the contract's bank function, written independently of the scheduler
    function [1:0] tb_bank(input [24:0] a, input [1:0] sk);
        begin
            case (BANK_HASH)
                0: tb_bank = a[11:10];
                1: tb_bank = a[11:10] ^ a[21:20];
                3: tb_bank = a[11:10] ^ sk;
                default: tb_bank = a[11:10] ^ a[21:20] ^ a[23:22];
            endcase
        end
    endfunction

    // ------------------------------------------------------------------------------------------------
    // client models
    // ------------------------------------------------------------------------------------------------
    integer cur_rq [0:NP-1];
    integer nxt    [0:NP-1];
    integer cq_buf [0:NP*128-1];  integer cq_h [0:NP-1], cq_t [0:NP-1];
    integer rx_buf [0:NP*128-1];  integer rx_h [0:NP-1], rx_t [0:NP-1], rx_k [0:NP-1];
    integer wx_buf [0:NP*128-1];  integer wx_h [0:NP-1], wx_t [0:NP-1], wx_k [0:NP-1];
    reg [15:0] wpd_d [0:NP*8-1];
    reg [1:0]  wpd_b [0:NP*8-1];
    reg [NP-1:0] urg_mode = 0;           // urgent[p] = urg_mode[p] & the port has requests outstanding
    reg [NP-1:0] urg_force = 0;          // urgent[p] forced high
    reg [NP-1:0] pend = 0;
    assign urgent = (urg_mode & pend) | urg_force;
    integer words_wr = 0, words_rd = 0, nbad_done = 0;
    reg     quiet = 1'b0;                // no new client activity (set by tests while they wait)
    reg     cli_stop = 1'b0;             // stop issuing new requests
    reg     expect_fault = 1'b0;         // fault-path tests: the PHY is allowed to fault
    reg     expect_no_done = 1'b0;

    task fail1(input [8*60-1:0] m);
        begin $display("  FAIL t=%0t cyc=%0d: %0s", $time, cyc, m); tb_fail = tb_fail + 1; end
    endtask

    // compare the chip's memory with the shadow for the bytes request i wrote (called at its done)
    task chk_chip_request(input integer i);
        integer k, nb; reg [24:0] a; reg [16:0] pk; reg [1:0] bw, sv; reg [15:0] d;
        begin
            nb = 0;
            for (k = 0; k < rq_len[i]; k = k + 1) begin
                a = rq_waddr(i, k);
                bw = bepat(a, rq_bem[i]);
                if (bw != 2'b00) begin
                    pk = chip.peek(tb_bank(a, rq_skew[i]), a[24:12], a[9:0]);
                    d = sh_d[sidx(a)];
                    if (bw[0] && (pk[7:0]  !== d[7:0]))  nb = nb + 1;
                    if (bw[1] && (pk[15:8] !== d[15:8])) nb = nb + 1;
                end
            end
            if (nb != 0) begin $display("  chip memory differs from the request's data in %0d bytes (request %0d, port %0d, addr %h, len %0d)", nb, i, rq_port[i], rq_addr[i], rq_len[i]); tb_fail = tb_fail + 1; end
        end
    endtask

    integer p, i, k, a_i, e_i;
    reg [24:0] ea; reg [15:0] ed; reg [1:0] bw;
    reg do_present;
    always @(posedge clk) begin
        if (rst) begin
            for (p = 0; p < NP; p = p + 1) begin
                cur_rq[p] = -1; nxt[p] = 0; cq_h[p] = 0; cq_t[p] = 0; rx_h[p] = 0; rx_t[p] = 0; rx_k[p] = 0; wx_h[p] = 0; wx_t[p] = 0; wx_k[p] = 0;
            end
            req_valid <= 0;
        end else begin
            for (p = 0; p < NP; p = p + 1) begin
                // ---- acceptance of the request being presented
                if (req_valid[p] && req_ready[p]) begin
                    i = cur_rq[p];
                    rq_acc[i] = 1'b1; rq_acyc[i] = cyc; nacc = nacc + 1;
                    cq_buf[p*128 + (cq_t[p] % 128)] = i; cq_t[p] = cq_t[p] + 1;
                    if (!rq_bad[i]) begin
                        if (rq_we[i]) begin
                            wx_buf[p*128 + (wx_t[p] % 128)] = i; wx_t[p] = wx_t[p] + 1;
                            for (k = 0; k < rq_len[i]; k = k + 1) begin
                                ea = rq_waddr(i, k); bw = bepat(ea, rq_bem[i]); ed = pat(ea, rq_gen[i]);
                                if (bw[0]) begin sh_d[sidx(ea)][7:0]  = ed[7:0];  sh_v[sidx(ea)][0] = 1'b1; end
                                if (bw[1]) begin sh_d[sidx(ea)][15:8] = ed[15:8]; sh_v[sidx(ea)][1] = 1'b1; end
                            end
                        end else begin
                            rx_buf[p*128 + (rx_t[p] % 128)] = i; rx_t[p] = rx_t[p] + 1;
                        end
                    end
                    req_valid[p] <= 1'b0;
                    cur_rq[p] = -1;
                end
                // ---- present the next request
                if (cur_rq[p] == -1 && !cli_stop) begin
                    while (nxt[p] < nrq && rq_port[nxt[p]] != p) nxt[p] = nxt[p] + 1;
                    if (nxt[p] < nrq) begin
                        i = nxt[p];
                        if (cyc >= rq_rel[i] && (rq_dep[i] < 0 || rq_done[rq_dep[i]])) begin
                            cur_rq[p] = i; nxt[p] = nxt[p] + 1;
                            req_valid[p] <= 1'b1; req_we[p] <= rq_we[i]; req_addr[p*25 +: 25] <= rq_addr[i];
                            req_len[p*10 +: 10] <= rq_len[i]; req_tag[p*8 +: 8] <= i[7:0];
                            req_2d[p] <= rq_2d[i]; req_w[p*6 +: 6] <= rq_w[i]; req_h[p*6 +: 6] <= rq_h[i];
                            req_stride[p*11 +: 11] <= rq_str[i]; req_skew[p*2 +: 2] <= rq_skew[i];
                        end
                    end
                end
                // ---- write data pull
                for (k = W_LEAD - 1; k >= 1; k = k - 1) begin wpd_d[p*8 + k] = wpd_d[p*8 + k - 1]; wpd_b[p*8 + k] = wpd_b[p*8 + k - 1]; end
                wpd_d[p*8] = 16'hdead; wpd_b[p*8] = 2'b00;
                if (w_next[p]) begin
                    if (wx_h[p] == wx_t[p]) fail1("w_next with no write request outstanding");
                    else begin
                        i = wx_buf[p*128 + (wx_h[p] % 128)];
                        if (w_idx[p*10 +: 10] != wx_k[p]) begin
                            $display("  w_idx %0d, expected %0d (port %0d request %0d)", w_idx[p*10 +: 10], wx_k[p], p, i); tb_fail = tb_fail + 1;
                        end
                        ea = rq_waddr(i, wx_k[p]);
                        wpd_d[p*8] = pat(ea, rq_gen[i]); wpd_b[p*8] = bepat(ea, rq_bem[i]);
                        wx_k[p] = wx_k[p] + 1; words_wr = words_wr + 1;
                        if (wx_k[p] == rq_len[i]) begin wx_k[p] = 0; wx_h[p] = wx_h[p] + 1; end
                    end
                end
                w_data[p*16 +: 16] <= wpd_d[p*8 + W_LEAD - 1];
                w_be[p*2 +: 2]     <= wpd_b[p*8 + W_LEAD - 1];
                // ---- read data check
                if (r_valid[p]) begin
                    if (rx_h[p] == rx_t[p]) fail1("r_valid with no read request outstanding");
                    else begin
                        i = rx_buf[p*128 + (rx_h[p] % 128)];
                        ea = rq_waddr(i, rx_k[p]); ed = sh_d[sidx(ea)]; bw = sh_v[sidx(ea)];
                        if (rq_fcyc[i] < 0) rq_fcyc[i] = cyc;
                        if ((bw[0] && r_data[p*16 +: 8] !== ed[7:0]) || (bw[1] && r_data[p*16+8 +: 8] !== ed[15:8]) || bw != 2'b11) begin
                            if (tb_fail < 20) $display("  read mismatch: port %0d request %0d word %0d addr %h: got %h want %h (valid %b)", p, i, rx_k[p], ea, r_data[p*16 +: 16], ed, bw);
                            tb_fail = tb_fail + 1;
                        end
                        words_rd = words_rd + 1;
                        if (r_last[p] != (rx_k[p] == rq_total(i) - 1)) begin
                            $display("  r_last %b at word %0d of %0d (port %0d request %0d)", r_last[p], rx_k[p], rq_total(i), p, i); tb_fail = tb_fail + 1;
                        end
                        rx_k[p] = rx_k[p] + 1;
                        if (rx_k[p] == rq_total(i)) begin rx_k[p] = 0; rx_h[p] = rx_h[p] + 1; end
                    end
                end else if (r_last[p]) fail1("r_last without r_valid");
                // ---- completion
                if (done[p]) begin
                    if (cq_h[p] == cq_t[p]) fail1("done with no request outstanding");
                    else begin
                        i = cq_buf[p*128 + (cq_h[p] % 128)]; cq_h[p] = cq_h[p] + 1;
                        if (done_tag[p*8 +: 8] != i[7:0]) begin $display("  done tag %0d, expected %0d (port %0d, in order)", done_tag[p*8 +: 8], i[7:0], p); tb_fail = tb_fail + 1; end
                        if (rq_done[i]) fail1("request completed twice");
                        if (!rq_bad[i] && !rq_we[i] && !r_last[p]) fail1("read done without r_last in the same cycle");
                        if (r_last[p] && (rq_bad[i] || rq_we[i])) fail1("r_last on a request that is not a good read");
                        if (!rq_bad[i] && rq_we[i]) begin
                            if (wx_h[p] != wx_t[p] && wx_buf[p*128 + (wx_h[p] % 128)] == i) fail1("write done before all data was pulled");
                            chk_chip_request(i);
                        end
                        rq_done[i] = 1'b1; rq_dcyc[i] = cyc; ndone = ndone + 1;
                        if (rq_bad[i]) nbad_done = nbad_done + 1;
                    end
                end
                pend[p] <= (cq_h[p] != cq_t[p]);
            end
        end
    end

    // ------------------------------------------------------------------------------------------------
    // monitors on the command stream (chained bursts, refresh counting, direction changes, urgent latency)
    // ------------------------------------------------------------------------------------------------
    integer n_act = 0, n_pre = 0, n_rd = 0, n_wr = 0, n_bst = 0, n_dirchg = 0, n_ref = 0, n_prea = 0;
    reg last_dir_v = 0, last_dir_w = 0;
    always @(posedge clk) if (!rst && init_done) begin
        case (cmd)
            3'd1: n_act = n_act + 1;
            3'd2: begin n_rd = n_rd + 1; if (last_dir_v && last_dir_w) n_dirchg = n_dirchg + 1; last_dir_v = 1; last_dir_w = 0; end
            3'd3: begin n_wr = n_wr + 1; if (last_dir_v && !last_dir_w) n_dirchg = n_dirchg + 1; last_dir_v = 1; last_dir_w = 1; end
            3'd4: n_pre = n_pre + 1;
            3'd5: n_prea = n_prea + 1;
            3'd6: n_ref = n_ref + 1;
            3'd7: n_bst = n_bst + 1;
            default: ;
        endcase
    end

    // ------------------------------------------------------------------------------------------------
    // sequencing helpers
    // ------------------------------------------------------------------------------------------------
    task powerup;
        integer n;
        begin
            @(posedge clk); #1;
            repeat (3) @(posedge clk);
            rst <= 1'b0;
            n = 0;
            while (!init_done && n < 40000) begin @(posedge clk); n = n + 1; end
            if (!init_done) begin $display("  init_done never rose"); tb_fail = tb_fail + 1; end
            repeat (4) @(posedge clk);
        end
    endtask
    // wait until every accepted request of the table is done (rejected ones included), or a time-out
    task run_all(input integer maxcyc);
        integer c0;
        begin
            c0 = cyc;
            while (ndone < nrq && (cyc - c0) < maxcyc) @(posedge clk);
            if (ndone < nrq) begin
                $display("  TIME-OUT: %0d of %0d requests done after %0d cycles", ndone, nrq, cyc - c0); tb_fail = tb_fail + 1;
            end
            repeat (12) @(posedge clk);
        end
    endtask
    task end_checks;
        begin
            repeat (6) @(posedge clk);
            if (!expect_fault && (phy_fault !== 1'b0 || fault_count !== 16'd0)) begin
                $display("  PHY fault: count=%0d cause=%0d", fault_count, fault_cause); tb_fail = tb_fail + 1;
            end
            if (chip.err_count != 0) begin
                $display("  chip model errors: %0d (first code %0d, mask %h)", chip.err_count, chip.err_first, chip.err_mask); tb_fail = tb_fail + 1;
            end
            if (dq_conflicts !== 16'd0) begin $display("  PHY dq_conflicts = %0d", dq_conflicts); tb_fail = tb_fail + 1; end
            if (dbg_err != 4'd0) begin $display("  scheduler internal error flags %b", dbg_err); tb_fail = tb_fail + 1; end
            if (chip.mem_collisions != 0) begin $display("  chip store collisions %0d", chip.mem_collisions); tb_fail = tb_fail + 1; end
        end
    endtask
    task finish_test;
        begin
            end_checks;
            if (tb_fail == 0) $display("TEST %0s PASS", tname); else $display("TEST %0s FAIL (%0d)", tname, tb_fail);
            $finish;
        end
    endtask

    integer seed = 1;
    function integer urand(input integer m);
        integer v;
        begin v = $random(seed); if (v < 0) v = -v; urand = v % m; end
    endfunction

    // ------------------------------------------------------------------------------------------------
    // tests
    // ------------------------------------------------------------------------------------------------
    integer L, r, off, wp, rp, wi, ri, n1, pp;
    integer cursor [0:15];
    integer wtab [0:NP*64-1]; reg [24:0] waddr [0:NP*64-1]; integer wlen [0:NP*64-1];
    localparam NPER = 40;
    initial begin
        tname = "none";
        if (!$value$plusargs("TEST=%s", tname)) tname = "basic";
        powerup;
        case (tname)
            "basic": begin
                cur_gen = 1;
                add_rq(3, 1'b1, A(0, 100), 10, 0, -1); wi = last_rq;
                add_rq(1, 1'b0, A(0, 100), 10, 0, wi);
                add_rq(4, 1'b0, A(0, 104), 4, 0, wi);
                add_rq(5, 1'b1, A(1, 0), 1, 0, -1);
                add_rq(2, 1'b1, A(1, 1000), 200, 0, -1);
                run_all(20000);
            end
            "lengths": begin     // contract 7.3: every request length 1..LMAX_T, writes then reads of the same words
                for (r = 0; r < 16; r = r + 1) cursor[r] = 0;
                for (L = 1; L <= LMAX_T; L = L + 1) begin
                    r = L % 8; off = cursor[r]; cursor[r] = cursor[r] + L + (L % 5);
                    cur_gen = L; cur_bem = 0;
                    wp = (L % 4 == 0) ? 2 : (L % 4 == 1) ? 3 : (L % 4 == 2) ? 4 : 5;
                    rp = (L % 3 == 0) ? 1 : (L % 3 == 1) ? 4 : 0;
                    add_rq(wp, 1'b1, A(r, off), L, 0, -1); wi = last_rq;
                    add_rq(rp, 1'b0, A(r, off), L, 0, wi);
                end
                run_all(TMO_CYC);
            end

            "splits": begin      // page-boundary splits: start columns near the end of a page, many lengths, both directions
                n1 = 0;
                for (r = 0; r < 6; r = r + 1) begin
                    for (L = 0; L < 11; L = L + 1) begin
                        off = (r == 0) ? 0 : (r == 1) ? 1 : (r == 2) ? 500 : (r == 3) ? 1000 : (r == 4) ? 1020 : 1023;
                        wi = (L == 0) ? 1 : (L == 1) ? 2 : (L == 2) ? 3 : (L == 3) ? 24 : (L == 4) ? 25 : (L == 5) ? 64 : (L == 6) ? 65 :
                             (L == 7) ? 128 : (L == 8) ? 129 : (L == 9) ? 300 : 512;
                        cur_gen = 100 + n1; cur_bem = 0;
                        add_rq((n1 % 2) ? 2 : 3, 1'b1, A(2 + n1 / 32, (n1 % 32) * 2048 + off), wi, 0, -1); ri = last_rq;
                        add_rq((n1 % 3 == 0) ? 1 : 4, 1'b0, A(2 + n1 / 32, (n1 % 32) * 2048 + off), wi, 0, ri);
                        n1 = n1 + 1;
                    end
                end
                run_all(TMO_CYC);
            end
            "be": begin          // byte enables: a full write, then partial writes (be 00, 01, 10, 11 by address), then a read
                cur_gen = 1; cur_bem = 0;
                add_rq(3, 1'b1, A(5, 0), 512, 0, -1); wi = last_rq;
                add_rq(3, 1'b1, A(5, 512), 512, 0, -1); wi = last_rq;
                cur_gen = 2; cur_bem = 1;
                add_rq(4, 1'b1, A(5, 3), 700, 0, wi); wi = last_rq;
                add_rq(2, 1'b1, A(5, 800), 150, 0, wi); wi = last_rq;
                add_rq(5, 1'b1, A(5, 1), 1, 0, wi); wi = last_rq;
                cur_gen = 3; cur_bem = 0;
                add_rq(1, 1'b0, A(5, 0), 512, 0, wi);
                add_rq(1, 1'b0, A(5, 512), 512, 0, wi);
                run_all(TMO_CYC);
            end
            "order": begin       // order and done semantics: read after a done write (other port), and in-port order
                for (r = 0; r < 12; r = r + 1) begin
                    cur_gen = 10 + r; cur_bem = (r % 3 == 0);
                    L = 1 + urand(300);
                    add_rq(3, 1'b1, A(6, r * 400), L, 0, -1); wi = last_rq;
                    add_rq(4, 1'b0, A(6, r * 400), L, 0, wi);              // read on another port after the done
                    add_rq(2, 1'b1, A(7, r * 400), L, 0, -1);              // same port: write then read, queued back to back
                    add_rq(2, 1'b0, A(7, r * 400), L, 0, -1);
                    add_rq(2, 1'b1, A(7, r * 400 + 1), 3, 0, -1);          // and an overlapping write after the read
                    add_rq(2, 1'b0, A(7, r * 400), 8, 0, -1);
                end
                run_all(TMO_CYC);
            end
            "all_ports": begin   // all six ports at once: each writes its own region, then reads other ports' data
                for (pp = 0; pp < NP; pp = pp + 1) cursor[pp] = 0;
                for (n1 = 0; n1 < NPER; n1 = n1 + 1) begin
                    for (pp = 0; pp < NP; pp = pp + 1) begin
                        L = 1 + urand(200); cur_gen = pp * 1000 + n1; cur_bem = (n1 % 4 == 3);
                        add_rq(pp, 1'b1, A(8 + pp, cursor[pp]), L, 0, -1);
                        wtab[pp * NPER + n1] = last_rq; waddr[pp * NPER + n1] = A(8 + pp, cursor[pp]); wlen[pp * NPER + n1] = L;
                        cursor[pp] = cursor[pp] + L + 1;
                    end
                end
                for (n1 = 0; n1 < NPER; n1 = n1 + 1) begin
                    for (pp = 0; pp < NP; pp = pp + 1) begin
                        r = urand(NP); off = urand(NPER);
                        add_rq(pp, 1'b0, waddr[r * NPER + off], wlen[r * NPER + off], 0, wtab[r * NPER + off]);
                    end
                end
                run_all(TMO_CYC);
            end
            default: begin $display("unknown test %0s", tname); tb_fail = tb_fail + 1; end
        endcase
        finish_test;
    end
    initial begin
        #(64'd1_000_000 * 100_000);
        $display("TEST %0s FAIL (global time-out)", tname);
        $finish;
    end
endmodule
