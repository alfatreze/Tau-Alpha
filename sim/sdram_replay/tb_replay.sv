// SDRAM replay (throughput) harness: PHY + tau_sdram_sched_v + chip model driven by Tau V's timed request stream (.treq).
// MIT licence. Clean-room: written from docs/features/SDRAM_CONTROLLER_CONTRACT.md (section 4 port protocol, 5 sched_v,
// 7.3, 7.4) and Tau V's docs/TREQ_TAU_V_SPEC.md; no legacy controller source was read. iverilog 4-state (the chip model needs it).
//
//   ports (the numbering of the .treq spec):  0 scan-out (generated), 1 decoder read (2D fusing), 2 decoder write (from the file),
//                                              CPU_P CPU single-word read every 2 us (generated), DMA_P 64-word writes at 2 MB/s (generated)
//   -DSTUB   replaces PHY + scheduler + chip model with replay_stub.sv (harness development; its numbers mean nothing)
//
// Every run prints RESULT / PIC / PORT lines and a final "VERDICT PASS|FAIL" line; tools/replay_summary.py reads them.
// Options (plusargs): see README.md.
`timescale 1ps/1ps

module tb_replay #(
    parameter integer P_PS        = 10000,
    parameter integer CLSEL       = 2,
    parameter integer BANK_HASH   = 2,
    parameter integer W_LEAD      = 2,
    parameter integer WF_DEPTH    = 8,
    parameter integer QDEPTH      = 4,
    parameter integer WR_HI_REQ   = 2,
    parameter integer WR_DEFER_NS = 1000,
    parameter integer REF_SOFT    = 4,
    parameter integer REF_HARD    = 7,
    parameter integer BATCH_REQS  = 4,
    parameter integer BATCH_WORDS = 128,
    parameter integer INITCYC     = 200,
    parameter integer REFIPS      = 7_800_000,
    parameter integer DEBTMAX     = 8,
    parameter [7:0]   EN2D        = 8'h02,
    parameter [8*4-1:0]  WEIGHT_P  = {4'd1, 4'd1, 4'd1, 4'd1, 4'd2, 4'd4, 4'd4, 4'd4},
    parameter [8*10-1:0] MAXG_RD_P = {10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd64, 10'd128},
    parameter [8*10-1:0] MAXG_WR_P = {10'd128, 10'd128, 10'd128, 10'd128, 10'd128, 10'd32, 10'd128, 10'd128},
    parameter integer CPU_P       = 3,          // the harness' CPU port (the .treq spec numbering; sched_v's default weight vector assumes 4)
    parameter integer DMA_P       = 4
);
    localparam integer NP = 6;
    localparam integer RN = 128;                // per-port in-flight ring
    localparam integer SHN = 1 << 22;
    localparam [24:0]  SCAN_BASE = 25'h1C00000; // region 7
    localparam [24:0]  CPU_BASE  = 25'h1800000; // region 6
    localparam [24:0]  DMA_BASE  = 25'h1400000; // region 5

    reg clk = 1'b0;
    always #(P_PS/2) clk = ~clk;
    reg rst = 1'b1;
    integer cyc = 0;
    always @(posedge clk) cyc <= cyc + 1;

    // ------------------------------------------------------------------------------------------------
    // harness-side port signals, DUT-side signals, and the mutation shim (runtime plusargs, default off)
    // ------------------------------------------------------------------------------------------------
    reg  [NP-1:0]    req_valid = 0, req_we = 0, req_2d = 0;
    reg  [NP*25-1:0] req_addr = 0;
    reg  [NP*10-1:0] req_len = 0;
    reg  [NP*8-1:0]  req_tag = 0;
    reg  [NP*6-1:0]  req_w = 0, req_h = 0;
    reg  [NP*11-1:0] req_stride = 0;
    reg  [NP*16-1:0] w_data = 0;
    wire [NP-1:0]    urgent;
    wire [NP-1:0]    req_ready, w_next, r_valid, r_last, done;
    wire [NP*10-1:0] w_idx;
    wire [NP*16-1:0] r_data;
    wire [NP*8-1:0]  done_tag;
    wire [NP-1:0]    d_req_valid, d_req_ready, d_r_valid, d_r_last, d_done;
    wire [NP*16-1:0] d_w_data, d_r_data;
    reg  [NP-1:0]    shim_drop = 0, shim_stall = 0, shim_nolast = 0;
    reg  [NP*16-1:0] shim_wflip = 0, shim_rflip = 0;
    assign d_req_valid = req_valid & ~shim_drop & ~shim_stall;
    assign req_ready   = d_req_ready & ~shim_stall;
    assign d_w_data    = w_data ^ shim_wflip;
    assign r_data      = d_r_data ^ shim_rflip;
    assign r_last      = d_r_last & ~shim_nolast;

    wire [NP*32-1:0] cnt_requests, cnt_words, cnt_busy;
    wire [NP*16-1:0] cnt_bad, cnt_worst_wait, cnt_urgent_over;
    wire [3:0]       dbg_err;
    wire             init_done, phy_fault;
    wire [15:0]      fault_count, dq_conflicts;
    wire [31:0]      refreshes;
    integer          urgent_bound = 155;
    wire [2:0]  cmd;  wire [1:0] cmd_ba; wire [12:0] cmd_a;
    wire        wvalid, rvalid;

`ifdef STUB
    replay_stub #(.NP(NP), .QD(QDEPTH), .W_LEAD(W_LEAD)) dut (
        .clk(clk), .rst(rst), .req_valid(d_req_valid), .req_ready(d_req_ready), .req_we(req_we), .req_addr(req_addr),
        .req_len(req_len), .req_tag(req_tag), .req_2d(req_2d), .req_w(req_w), .req_h(req_h), .req_stride(req_stride),
        .w_next(w_next), .w_idx(w_idx), .w_data(d_w_data), .r_valid(d_r_valid), .r_data(d_r_data), .r_last(d_r_last),
        .done(d_done), .done_tag(done_tag));
    reg [7:0] stub_init = 0;
    always @(posedge clk) if (!rst && stub_init != 8'hff) stub_init <= stub_init + 1;
    assign init_done = (stub_init == 8'hff);
    assign phy_fault = 1'b0; assign fault_count = 16'd0; assign dq_conflicts = 16'd0; assign refreshes = 32'd0;
    assign cnt_requests = 0; assign cnt_words = 0; assign cnt_busy = 0; assign cnt_bad = 0; assign cnt_worst_wait = 0;
    assign cnt_urgent_over = 0; assign dbg_err = 4'd0;
    assign cmd = 3'd0; assign cmd_ba = 2'd0; assign cmd_a = 13'd0; assign wvalid = 1'b0; assign rvalid = 1'b0;
`else
    wire [31:0] unused32;
    wire [1:0]  be; wire [15:0] wdata;
    wire [3:0]  ok_act, ok_rd, ok_wr, ok_pre, row_aged, ref_debt;
    wire        ok_prea, ok_ref, ok_bst, preempt, banks_closed, ref_due, preempt_ack;
    wire [15:0] rdata; wire [3:0] fault_cause; wire [4:0] rd_lat;
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
    sdram_chip_model #(.CL_DEFAULT(CLSEL), .INIT_NOP_MIN(INITCYC), .INIT_WAIT_PS(INITCYC * P_PS), .MEM_LOG2(22), .MEM_COMPACT(2), .VERBOSE(1)) chip (
        .dram_clk(c_clk), .dram_cke(c_cke), .dram_ras_n(c_ras), .dram_cas_n(c_cas), .dram_we_n(c_we), .dram_dqm(c_dqm),
        .dram_ba(c_ba), .dram_a(c_a), .dram_dq(dq), .fpga_oe(phy.u_core.dq_oe), .cap_clk(clk));
    wire [NP-1:0] d_urgent_early = {NP{1'b0}};
    reg  [1:0] skt [0:7];
    reg        sk_we = 0; reg [2:0] sk_idx = 0, sk_base = 0; reg [1:0] sk_val = 0;
    tau_sdram_sched_v #(.NPORTS(NP), .QDEPTH(QDEPTH), .BANK_HASH(BANK_HASH), .W_LEAD(W_LEAD), .WF_DEPTH(WF_DEPTH),
                        .ENABLE_2D(EN2D), .WR_HI_REQ(WR_HI_REQ), .WR_DEFER_NS(WR_DEFER_NS), .REF_SOFT_DEBT(REF_SOFT),
                        .REF_HARD_DEBT(REF_HARD), .BATCH_REQS(BATCH_REQS), .BATCH_WORDS(BATCH_WORDS), .RD_LAT(CLSEL + 2),
                        .WEIGHT(WEIGHT_P), .MAXG_RD(MAXG_RD_P), .MAXG_WR(MAXG_WR_P), .CLK_HZ((1_000_000_000 / P_PS) * 1000)) dut (
        .clk(clk), .rst(rst), .req_valid(d_req_valid), .req_ready(d_req_ready), .req_we(req_we), .req_addr(req_addr),
        .req_len(req_len), .req_tag(req_tag), .req_2d(req_2d), .req_w(req_w), .req_h(req_h), .req_stride(req_stride),
        .urgent(urgent), .urgent_early(d_urgent_early), .sk_we(sk_we), .sk_idx(sk_idx), .sk_base(sk_base), .sk_val(sk_val),
        .w_next(w_next), .w_idx(w_idx), .w_data(d_w_data), .w_be({NP{2'b11}}),
        .r_valid(d_r_valid), .r_data(d_r_data), .r_last(d_r_last), .done(d_done), .done_tag(done_tag),
        .cnt_requests(cnt_requests), .cnt_words(cnt_words), .cnt_busy(cnt_busy), .cnt_bad(cnt_bad), .cnt_worst_wait(cnt_worst_wait),
        .cnt_urgent_over(cnt_urgent_over), .dbg_err(dbg_err),
        .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .wvalid(wvalid), .wdata(wdata), .be(be),
        .ok_act(ok_act), .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_bst(ok_bst), .ok_prea(ok_prea), .ok_ref(ok_ref), .init_done(init_done),
        .ref_due(ref_due), .ref_debt(ref_debt), .preempt_ack(preempt_ack),
        .rvalid(rvalid), .rdata(rdata), .row_aged(row_aged), .preempt(preempt), .banks_closed(banks_closed));
    initial urgent_bound = dut.URGENT_BOUND_CLK;
`endif
    assign r_valid = d_r_valid;
    assign done    = d_done;

    // ------------------------------------------------------------------------------------------------
    // data patterns and the shadow memory (write version per word; a word is never read before it is written)
    // ------------------------------------------------------------------------------------------------
    function [15:0] pat(input [24:0] a, input [31:0] s);     // data = f(address, version)
        reg [31:0] x;
        begin
            x = {7'd0, a} * 32'h9E3779B1 + s * 32'h7F4A7C15;
            x = x ^ (x >> 15);
            x = x * 32'h2C1B3C6D;
            pat = x[31:16] ^ x[15:0];
        end
    endfunction
    reg [31:0] sh_s [0:SHN-1];                               // version of the last write accepted to the word (0 = never written)
    function integer sidx(input [24:0] a);
        begin sidx = {a[24:22], a[18:0]}; end
    endfunction
    function addr_ok(input [24:0] a);                         // the store compaction is exact only when a[21:19] = 0
        begin addr_ok = (a[21:19] == 3'd0); end
    endfunction
    function [1:0] tb_bank(input [24:0] a);                   // the contract's bank function, written independently of the scheduler
        begin
            case (BANK_HASH)
                0: tb_bank = a[11:10];
                1: tb_bank = a[11:10] ^ a[21:20];
                3: tb_bank = a[11:10] ^ skt3(a[24:22]);
                default: tb_bank = a[11:10] ^ a[21:20] ^ a[23:22];
            endcase
        end
    endfunction
    function [1:0] skt3(input [2:0] r);
        begin
            case (r) 3'd0: skt3 = 2'd0; 3'd1: skt3 = 2'd1; 3'd2: skt3 = 2'd2; 3'd3: skt3 = 2'd3;
                     3'd4: skt3 = 2'd2; 3'd5: skt3 = 2'd1; 3'd6: skt3 = 2'd3; default: skt3 = 2'd0; endcase
        end
    endfunction
    task poke(input [24:0] a, input [15:0] d);                // back-door write into the memory under test
        begin
`ifdef STUB
            dut.poke(a, d);
`else
            chip.mem_wr(tb_bank(a), a[24:12], a[9:0], 0, d[7:0]);
            chip.mem_wr(tb_bank(a), a[24:12], a[9:0], 1, d[15:8]);
`endif
        end
    endtask
    function [16:0] peek(input [24:0] a);                     // {valid, data}
        begin
`ifdef STUB
            peek = dut.peek(a);
`else
            peek = chip.peek(tb_bank(a), a[24:12], a[9:0]);
`endif
        end
    endfunction

    // ------------------------------------------------------------------------------------------------
    // options, counters
    // ------------------------------------------------------------------------------------------------
    reg [8*512-1:0] treq_name, scan_name;
    integer limit_pics = 0, skip_pics = 0, skipping = 0, limit_mbs = 0, period_ovr = 0, ahead_ovr = -1, tmo = 200000, maxcyc = 0;
    reg opt_nofuse = 0, opt_norel = 0, opt_no_scan = 0, opt_no_cpu = 0, opt_no_dma = 0, opt_nochipchk = 0, opt_nopreload = 0, opt_verbose = 0;
    reg opt_mut_nofence = 0, opt_mut_nobarrier = 0, opt_mut_norelgate = 0;
    integer mut_scanstall = 0, mut_drop = 0, mut_wcorrupt = 0, mut_rcorrupt = 0, mut_nolast = 0, mut_stall = 0;
    integer sc_cap = 1200, sc_start = 600, sc_line = 600;
    integer T0 = 0, T0_first = 0;                                          // cycle of the stream's time zero

    integer n_act = 0, n_rdc = 0, n_wrc = 0, n_pre = 0, n_prea = 0, n_ref = 0, n_bst = 0, n_cmd = 0, n_wv = 0, n_rv = 0;
    integer tb_fail = 0;                                     // protocol / integrity failures counted by the harness
    integer n_mism = 0, n_race = 0, n_uninit = 0, n_chip_diff = 0, n_proto = 0, n_hang = 0;
    integer n_fence_viol = 0, n_barrier_viol = 0, n_rel_viol = 0, n_addr_bad = 0;
    task fail1(input [8*60-1:0] m);
        begin n_proto = n_proto + 1; if (n_proto <= 10) $display("  FAIL t=%0t cyc=%0d: %0s", $time, cyc, m); end
    endtask

    // ------------------------------------------------------------------------------------------------
    // per-port in-flight rings (a port is read-only or write-only here; completion is in order)
    // ------------------------------------------------------------------------------------------------
    reg [24:0] q_addr [0:NP*RN-1];
    integer    q_len  [0:NP*RN-1];
    reg        q_we   [0:NP*RN-1];
    reg        q_2d   [0:NP*RN-1];
    integer    q_w    [0:NP*RN-1];
    integer    q_str  [0:NP*RN-1];
    integer    q_seq  [0:NP*RN-1];                           // write: its version; read: the write version counter at acceptance
    integer    q_acc  [0:NP*RN-1];
    integer    q_fw   [0:NP*RN-1];
    integer    q_u    [0:NP*RN-1];
    integer    q_t [0:NP-1], q_pl [0:NP-1], q_rd [0:NP-1], q_dn [0:NP-1];
    // cursors of the head read / head write pull
    integer    rx_k [0:NP-1], wx_k [0:NP-1], rx_c [0:NP-1];
    reg [24:0] rx_a [0:NP-1], rx_rb [0:NP-1], wx_a [0:NP-1];
    reg [15:0] wpd_d [0:NP*8-1];
    // the request being presented on a port (until accepted)
    reg        sl_v [0:NP-1];
    reg        sl_we [0:NP-1], sl_2d [0:NP-1];
    reg [24:0] sl_addr [0:NP-1];
    integer    sl_len [0:NP-1], sl_w [0:NP-1], sl_h [0:NP-1], sl_str [0:NP-1], sl_id [0:NP-1];
    integer    req_id [0:NP-1];
    // statistics per port
    integer    p_req [0:NP-1], p_words [0:NP-1], p_lat_n [0:NP-1], p_lat_max [0:NP-1], p_fw_max [0:NP-1], p_dn_words [0:NP-1];
    reg [63:0] p_lat_sum [0:NP-1], p_fw_sum [0:NP-1];
    integer    wseq = 1;                                     // write version counter (1 = the preload)
    integer    dec_rd_acc = 0, dec_rd_last = 0, dec_rd_dn = 0, dec_wr_acc = 0, dec_wr_dn = 0;
    function integer dec_out(input integer dummy);
        begin dec_out = (dec_rd_acc - dec_rd_dn) + (dec_wr_acc - dec_wr_dn); end
    endfunction
    integer    glob_words_rd = 0, glob_words_wr = 0;
    integer    progress_cyc = 0;

    integer ii, jj, kk;
    initial begin
        for (ii = 0; ii < NP; ii = ii + 1) begin
            q_t[ii] = 0; q_pl[ii] = 0; q_rd[ii] = 0; q_dn[ii] = 0; rx_k[ii] = 0; wx_k[ii] = 0; rx_c[ii] = 0;
            sl_v[ii] = 0; req_id[ii] = 0; p_req[ii] = 0; p_words[ii] = 0; p_lat_n[ii] = 0; p_lat_max[ii] = 0; p_fw_max[ii] = 0;
            p_lat_sum[ii] = 0; p_fw_sum[ii] = 0; p_dn_words[ii] = 0;
            for (jj = 0; jj < 8; jj = jj + 1) wpd_d[ii*8 + jj] = 16'hdead;
        end
    end

    // ------------------------------------------------------------------------------------------------
    // the .treq reader: 16-byte records, chunked $fread into a ring (the files are 50-64 MB)
    // ------------------------------------------------------------------------------------------------
    reg [127:0] rb [0:4095];
    reg [127:0] chunk [0:1023];
    integer rb_h = 0, rb_t = 0, fd_t = 0, fd_eof = 0, fd_n = 0;
    function [7:0]  rf_port (input [127:0] r); begin rf_port  = r[127:120]; end endfunction
    function [7:0]  rf_kind (input [127:0] r); begin rf_kind  = r[119:112]; end endfunction
    function [15:0] rf_words(input [127:0] r); begin rf_words = {r[103:96], r[111:104]}; end endfunction
    function [31:0] rf_addr (input [127:0] r); begin rf_addr  = {r[71:64], r[79:72], r[87:80], r[95:88]}; end endfunction
    function [31:0] rf_rel  (input [127:0] r); begin rf_rel   = {r[39:32], r[47:40], r[55:48], r[63:56]}; end endfunction
    function [15:0] rf_flags(input [127:0] r); begin rf_flags = {r[23:16], r[31:24]}; end endfunction
    function [15:0] rf_aux  (input [127:0] r); begin rf_aux   = {r[7:0], r[15:8]}; end endfunction
    function [127:0] peekrec(input integer k); begin peekrec = rb[(rb_h + k) % 4096]; end endfunction
    task rb_fill;
        integer got, k;
        begin
            while (!fd_eof && (rb_t - rb_h) < 128) begin
                got = $fread(chunk, fd_t, 0, 1024);
                got = got / 16;
                if (got <= 0) fd_eof = 1;
                else begin
                    for (k = 0; k < got; k = k + 1) rb[(rb_t + k) % 4096] = chunk[k];
                    rb_t = rb_t + got;
                end
            end
        end
    endtask
    function integer ns2c(input [31:0] ns);                   // nanoseconds to cycles, rounded up
        reg [63:0] t;
        begin t = ns; t = t * 1000; ns2c = (t + P_PS - 1) / P_PS; end
    endfunction

    // ------------------------------------------------------------------------------------------------
    // the feeder: ports 1 and 2 from the file, strictly in file order
    // ------------------------------------------------------------------------------------------------
    integer fps1000 = 30000, ahead = 1, mbrate = 67500, period_c = 3333333;
    integer first_pic = -1, fd_wake = 0, fd_done = 0, fd_go = 0, fd_wait_rel = 0, fd_blocked = 0, after_end = 0;
    integer pic_n = -1, pic_ty = 0, pic_rel_c = 0, pic_begin = 0, prev_reached = 0, pic_mbs = 0, pic_lastrel = 0;
    integer pic_recs = 0, pic_reqs = 0, pic_words = 0, pic_cmd0 = 0, npics = 0, skip_pic = 0, last_mb = -1;
    integer tot_recs = 0, tot_reqs = 0, tot_mbs = 0, tot_pics_late = 0, tot_pics_over = 0, busy_max = 0, lag_max = 0;
    integer tot_fused_reads = 0, tot_unfused_reads = 0;
    reg [127:0] fr;
    integer fn, fw2, fstep, fport, fkind, frelc, ffl, fneed;
    integer pic_dl_c, dummy_i;
    reg [24:0] fa0, fprev, fa2;
    reg        fcont;

    task present(input integer p, input we, input [24:0] a, input integer len, input is2d, input integer w, input integer h, input integer str);
        begin
            if (p == 1 || p == 2) begin if (dec_out(0) == 0) progress_cyc = cyc; end
            sl_v[p] = 1'b1; sl_we[p] = we; sl_addr[p] = a; sl_len[p] = is2d ? w * h : len; sl_2d[p] = is2d;
            sl_w[p] = w; sl_h[p] = h; sl_str[p] = str; sl_id[p] = req_id[p]; req_id[p] = req_id[p] + 1;
            if (!addr_ok(a) || !addr_ok(a + (is2d ? (h - 1) * str + w : len) - 1)) begin
                n_addr_bad = n_addr_bad + 1;
                if (n_addr_bad <= 3) $display("  FAIL: address %h of port %0d outside the supported store range (a[21:19] must be 0)", a, p);
            end
            req_valid[p] <= 1'b1; req_we[p] <= we; req_addr[p*25 +: 25] <= a; req_len[p*10 +: 10] <= is2d ? 10'd0 : len[9:0];
            req_tag[p*8 +: 8] <= sl_id[p]; req_2d[p] <= is2d; req_w[p*6 +: 6] <= w; req_h[p*6 +: 6] <= h; req_stride[p*11 +: 11] <= str;
        end
    endtask

    task pic_end_report(input integer reached);
        integer begin_c, busy, dl, late, lag, over;
        begin
            if (opt_norel) begin
                begin_c = prev_reached; lag = 0;
                // flat-out run with an explicit budget (+period_cycles): picture n must be reached by T0 + (n - first + 1 + ahead) x period
                dl = T0 + (pic_n - first_pic + 1 + ahead) * period_ovr; late = (period_ovr > 0 && reached > dl) ? 1 : 0;
            end
            else begin
                begin_c = (T0 + pic_rel_c > prev_reached) ? T0 + pic_rel_c : prev_reached;
                dl = pic_dl_c + T0; late = (reached > dl) ? 1 : 0;
                lag = reached - (T0 + pic_lastrel);
            end
            busy = reached - begin_c;
            over = (busy * 100 > 90 * period_c) ? 1 : 0;
            if (late) tot_pics_late = tot_pics_late + 1;
            if (over) tot_pics_over = tot_pics_over + 1;
            if (busy > busy_max) busy_max = busy;
            if (lag > lag_max) lag_max = lag;
            $display("PIC n=%0d type=%0d begin=%0d reached=%0d busy=%0d period=%0d deadline=%0d late=%0d over90=%0d lag=%0d span=%0d mbs=%0d recs=%0d reqs=%0d words=%0d cmds=%0d",
                     pic_n, pic_ty, begin_c - T0, reached - T0, busy, period_c, (opt_norel && period_ovr <= 0) ? 0 : dl - T0, late, over, lag, opt_norel ? 0 : pic_lastrel - pic_rel_c, pic_mbs, pic_recs, pic_reqs, pic_words, n_cmd - pic_cmd0);
            prev_reached = reached;
        end
    endtask

    task feeder;
        reg stop;
        begin
            stop = 1'b0; fd_wait_rel = 0; fd_blocked = 0;
            if (fd_go && !fd_done && cyc >= fd_wake) begin
                fd_wake = 0;
                while (!stop && !fd_done) begin
                    if (!fd_eof && (rb_t - rb_h) < 128) rb_fill;
                    if (rb_t == rb_h) fd_done = 1;
                    else begin
                        fr = peekrec(0); fkind = rf_kind(fr); fport = rf_port(fr); frelc = ns2c(rf_rel(fr)); ffl = rf_flags(fr);
                        if (fkind == 2) begin
                            case (rf_words(fr))
                                16'd0: begin            // STREAM: fps x 1000, MB rate, decode-ahead depth
                                    fps1000 = rf_addr(fr); mbrate = rf_rel(fr); ahead = (ahead_ovr >= 0) ? ahead_ovr : ffl;
                                    period_c = (period_ovr > 0) ? period_ovr : (1_000_000_000_000_000 / (fps1000 * P_PS));
                                    $display("STREAM fps=%0d.%03d mb_rate=%0d ahead=%0d period_cycles=%0d", fps1000/1000, fps1000%1000, mbrate, ahead, period_c);
                                    rb_h = rb_h + 1; tot_recs = tot_recs + 1;
                                end
                                16'd1: begin            // PIC_START: not before its release
                                    if (skip_pics > 0 && rf_addr(fr) < skip_pics) begin skipping = 1; rb_h = rb_h + 1; end   // fast-forward (shards)
                                    else begin
                                        if (skipping) begin skipping = 0; prev_reached = cyc; if (!opt_norel) T0 = cyc - frelc; end
                                        if (!opt_norel && !opt_mut_norelgate && cyc < T0 + frelc) begin stop = 1'b1; fd_wait_rel = 1; fd_wake = T0 + frelc; end
                                        else begin
                                            if (first_pic < 0) first_pic = rf_addr(fr);
                                            pic_n = rf_addr(fr); pic_ty = rf_aux(fr); pic_rel_c = frelc; pic_mbs = 0; pic_lastrel = 0;
                                            pic_recs = 0; pic_reqs = 0; pic_words = 0; pic_cmd0 = n_cmd; skip_pic = 0; last_mb = -1;
                                            pic_dl_c = (period_ovr > 0) ? (pic_n + ahead + 1) * period_ovr : 0;
                                            rb_h = rb_h + 1; tot_recs = tot_recs + 1;
                                        end
                                    end
                                end
                                default: begin          // PIC_END: a full barrier; its release is the deadline
                                  if (skipping) rb_h = rb_h + 1;
                                  else begin
                                    if (period_ovr <= 0) pic_dl_c = frelc;
                                    if (sl_v[1] || sl_v[2] || (dec_out(0) != 0 && !opt_mut_nobarrier)) begin stop = 1'b1; fd_blocked = 1; end
                                    else begin
                                        pic_end_report(cyc);
                                        rb_h = rb_h + 1; tot_recs = tot_recs + 1; npics = npics + 1; after_end = 1; progress_cyc = cyc;
                                        if (limit_pics > 0 && npics >= limit_pics) fd_done = 1;
                                    end
                                  end
                                end
                            endcase
                        end else begin
                            if (skipping) begin                  // fast-forward: the writes go straight into the memory and the shadow
                                if (fkind == 1) begin
                                    wseq = wseq + 1;
                                    for (fn = 0; fn < rf_words(fr); fn = fn + 1) begin
                                        fa2 = rf_addr(fr) + fn; poke(fa2, pat(fa2, wseq)); sh_s[sidx(fa2)] = wseq;
                                    end
                                end
                                rb_h = rb_h + 1;
                            end else if (skip_pic || (limit_mbs > 0 && rf_aux(fr) >= limit_mbs)) begin   // quick mode: drop the rest of the picture
                                skip_pic = 1; rb_h = rb_h + 1;
                            end else if (sl_v[1] || sl_v[2]) begin stop = 1'b1; fd_blocked = 1; end   // the previous record is not accepted yet
                            else if (!opt_norel && !opt_mut_norelgate && cyc < T0 + frelc) begin stop = 1'b1; fd_wait_rel = 1; fd_wake = T0 + frelc; end
                            else if ((ffl & 2) && !opt_mut_nofence && dec_rd_acc != dec_rd_last) begin stop = 1'b1; fd_blocked = 1; end
                            else begin
                                // monitors, independent of the gating above
                                if ((ffl & 2) && dec_rd_acc != dec_rd_last) n_fence_viol = n_fence_viol + 1;
                                if (after_end && dec_out(0) != 0) n_barrier_viol = n_barrier_viol + 1;
                                if (!opt_norel && cyc < T0 + frelc) n_rel_viol = n_rel_viol + 1;
                                after_end = 0;
                                if (rf_aux(fr) != last_mb) begin pic_mbs = pic_mbs + 1; tot_mbs = tot_mbs + 1; last_mb = rf_aux(fr); end
                                if (frelc > pic_lastrel) pic_lastrel = frelc;
                                fa0 = rf_addr(fr); fw2 = rf_words(fr); fn = 1; fstep = 0;
                                if (fport == 1 && fkind == 0 && !opt_nofuse && EN2D[1]) begin       // the fusing rule of contract 7.4
                                    fprev = fa0; fcont = 1'b1;
                                    while (fcont && fn < 63 && fw2 * (fn + 1) <= 512 && (rb_t - rb_h) > fn) begin
                                        fa2 = rf_addr(peekrec(fn));
                                        if (rf_port(peekrec(fn)) == 1 && rf_kind(peekrec(fn)) == 0 && rf_words(peekrec(fn)) == fw2 &&
                                            rf_aux(peekrec(fn)) == rf_aux(fr) && rf_flags(peekrec(fn)) == 0 &&
                                            ((fn == 1) ? (fa2 > fprev && fa2 - fprev <= 2047) : (fa2 - fprev == fstep))) begin
                                            if (fn == 1) fstep = fa2 - fprev;
                                            fprev = fa2; fn = fn + 1;
                                        end else fcont = 1'b0;
                                    end
                                end
                                if (fn > 1) begin
                                    present(1, 1'b0, fa0, 0, 1'b1, fw2, fn, fstep); tot_fused_reads = tot_fused_reads + fn;
                                end else begin
                                    present(fport, fkind == 1, fa0, fw2, 1'b0, 0, 0, 0); if (fport == 1) tot_unfused_reads = tot_unfused_reads + 1;
                                end
                                pic_recs = pic_recs + fn; tot_recs = tot_recs + fn; pic_reqs = pic_reqs + 1; tot_reqs = tot_reqs + 1;
                                pic_words = pic_words + ((fn > 1) ? fw2 * fn : fw2);
                                rb_h = rb_h + fn; stop = 1'b1;
                            end
                        end
                    end
                end
            end
        end
    endtask

    // ------------------------------------------------------------------------------------------------
    // generated ports
    // ------------------------------------------------------------------------------------------------
    // port 0: scan-out, a word FIFO drained at the display rate (see README for the model), the request pattern repeats
    reg [63:0] sc_mem [0:2047];
    integer sc_urg_thr = 600, sc_n = 0, sc_i = 0, sc_level = 0, sc_out = 0, sc_started = 0, sc_under = 0, sc_cons = 0, sc_lw = 0, sc_blank = 0;
    integer sc_wp = 247678, sc_acc = 0, sc_blank_cycles = 34013, sc_active_words = 432000, sc_under_cyc = 0, sc_frames = 0;
    integer sc_urg_since = -1, sc_urg_wait_max = 0, sc_urg_over = 0, sc_urg_n = 0, sc_p0_prev_last = 0, sc_level_min = 1000000;
    reg sc_urg = 0;
    assign urgent = {{(NP-1){1'b0}}, sc_urg};
    reg [63:0] sc_v;
    // CPU and DMA
    integer cpu_period = 200, cpu_next = 0, cpu_skipped = 0, cpu_issued = 0;
    integer dma_period = 6400, dma_next = 0, dma_skipped = 0, dma_issued = 0, dma_ptr = 0;
    reg [31:0] lfsr = 32'h1ACE_B00C;

    // ------------------------------------------------------------------------------------------------
    // statistics: commands, bus words, refresh
    // ------------------------------------------------------------------------------------------------
    always @(posedge clk) if (!rst && init_done) begin
        case (cmd)
            3'd1: begin n_act = n_act + 1; n_cmd = n_cmd + 1; end
            3'd2: begin n_rdc = n_rdc + 1; n_cmd = n_cmd + 1; end
            3'd3: begin n_wrc = n_wrc + 1; n_cmd = n_cmd + 1; end
            3'd4: begin n_pre = n_pre + 1; n_cmd = n_cmd + 1; end
            3'd5: begin n_prea = n_prea + 1; n_cmd = n_cmd + 1; end
            3'd6: begin n_ref = n_ref + 1; n_cmd = n_cmd + 1; end
            3'd7: begin n_bst = n_bst + 1; n_cmd = n_cmd + 1; end
            default: ;
        endcase
        if (wvalid) n_wv = n_wv + 1;
        if (rvalid) n_rv = n_rv + 1;
    end

    // ------------------------------------------------------------------------------------------------
    // the clock-edge process: acceptance, write pull, read check, completion, generators, feeder
    // ------------------------------------------------------------------------------------------------
    integer p, i, k, ix, wl;
    reg [24:0] ea;
    reg [15:0] ed;
    reg [31:0] sv;
    integer w2, wsv;
    integer sc_credit, sc_reqlen;
    task chk_word(input integer pp, input [24:0] a, input integer seqacc, input [15:0] d);
        begin
            sv = sh_s[sidx(a)];
            if (sv == 0) begin n_uninit = n_uninit + 1; end
            else if (sv > seqacc) n_race = n_race + 1;
            else if (d !== pat(a, sv)) begin
                n_mism = n_mism + 1;
                if (n_mism <= 10) $display("  read mismatch: port %0d addr %h got %h want %h (version %0d) cyc=%0d", pp, a, d, pat(a, sv), sv, cyc);
            end
        end
    endtask
    always @(posedge clk) begin
        if (rst) begin
            req_valid <= 0;
            for (p = 0; p < NP; p = p + 1) begin sl_v[p] = 0; end
        end else if (fd_go) begin
            // ---------------- acceptance
            for (p = 0; p < NP; p = p + 1) begin
                if (req_valid[p] && req_ready[p]) begin
                    ix = p * RN + (q_t[p] % RN);
                    q_addr[ix] = sl_addr[p]; q_len[ix] = sl_len[p]; q_we[ix] = sl_we[p]; q_2d[ix] = sl_2d[p]; q_w[ix] = sl_w[p]; q_str[ix] = sl_str[p];
                    q_acc[ix] = cyc; q_fw[ix] = -1;
                    if (sl_we[p]) begin
                        wseq = wseq + 1; q_seq[ix] = wseq;
                        for (k = 0; k < sl_len[p]; k = k + 1) sh_s[sidx(sl_addr[p] + k)] = wseq;
                    end else q_seq[ix] = wseq;
                    q_t[p] = q_t[p] + 1;
                    p_req[p] = p_req[p] + 1; p_words[p] = p_words[p] + sl_len[p];
                    if (p == 1) dec_rd_acc = dec_rd_acc + 1;
                    if (p == 2) dec_wr_acc = dec_wr_acc + 1;
                    if (p == 0) begin sc_out = sc_out + sl_len[p]; end
                    sl_v[p] = 1'b0; req_valid[p] <= 1'b0; if (p == 1 || p == 2) progress_cyc = cyc;
                end
            end
            // ---------------- write data pull (answered exactly W_LEAD cycles later)
            for (p = 0; p < NP; p = p + 1) begin
                for (k = W_LEAD - 1; k >= 1; k = k - 1) wpd_d[p*8 + k] = wpd_d[p*8 + k - 1];
                wpd_d[p*8] = 16'hdead;
                if (w_next[p]) begin
                    if (q_pl[p] == q_t[p]) fail1("w_next with no write request outstanding");
                    else begin
                        ix = p * RN + (q_pl[p] % RN);
                        if (wx_k[p] == 0) wx_a[p] = q_addr[ix];
                        if (w_idx[p*10 +: 10] != wx_k[p]) begin
                            n_proto = n_proto + 1; if (n_proto <= 10) $display("  w_idx %0d, expected %0d (port %0d)", w_idx[p*10 +: 10], wx_k[p], p);
                        end
                        wpd_d[p*8] = pat(wx_a[p], q_seq[ix]);
                        wx_a[p] = wx_a[p] + 1; wx_k[p] = wx_k[p] + 1; glob_words_wr = glob_words_wr + 1;
                        if (wx_k[p] == q_len[ix]) begin wx_k[p] = 0; q_pl[p] = q_pl[p] + 1; end
                    end
                end
                w_data[p*16 +: 16] <= wpd_d[p*8 + W_LEAD - 1];
            end
            // ---------------- read data check
            for (p = 0; p < NP; p = p + 1) begin
                if (r_valid[p]) begin
                    if (q_rd[p] == q_t[p]) fail1("r_valid with no read request outstanding");
                    else begin
                        ix = p * RN + (q_rd[p] % RN);
                        if (rx_k[p] == 0) begin
                            rx_a[p] = q_addr[ix]; rx_rb[p] = q_addr[ix]; rx_c[p] = 0; q_fw[ix] = cyc;
                            if (p == 0) begin                    // urgent wait: from the later of the request, the urgency and the previous request's last word
                                if (sc_urg && sc_urg_since >= 0) begin
                                    wl = q_acc[ix];
                                    if (sc_urg_since > wl) wl = sc_urg_since;
                                    if (sc_p0_prev_last > wl) wl = sc_p0_prev_last;
                                    wl = cyc - wl; sc_urg_n = sc_urg_n + 1;
                                    if (wl > sc_urg_wait_max) sc_urg_wait_max = wl;
                                    if (wl > urgent_bound) sc_urg_over = sc_urg_over + 1;
                                end
                            end
                        end
                        chk_word(p, rx_a[p], q_seq[ix], r_data[p*16 +: 16]);
                        glob_words_rd = glob_words_rd + 1;
                        if (p == 0) begin sc_level = sc_level + 1; sc_out = sc_out - 1; end
                        if (r_last[p] != (rx_k[p] == q_len[ix] - 1)) begin
                            n_proto = n_proto + 1; if (n_proto <= 10) $display("  r_last %b at word %0d of %0d (port %0d)", r_last[p], rx_k[p], q_len[ix], p);
                        end
                        rx_k[p] = rx_k[p] + 1; rx_c[p] = rx_c[p] + 1;
                        if (q_2d[ix] && rx_c[p] == q_w[ix]) begin rx_c[p] = 0; rx_rb[p] = rx_rb[p] + q_str[ix]; rx_a[p] = rx_rb[p]; end
                        else rx_a[p] = rx_a[p] + 1;
                        if (rx_k[p] == q_len[ix]) begin
                            rx_k[p] = 0; q_rd[p] = q_rd[p] + 1; if (p == 0) sc_p0_prev_last = cyc;
                            if (p == 1) dec_rd_last = dec_rd_last + 1;
                        end
                        if (p == 1) progress_cyc = cyc;
                    end
                end else if (r_last[p]) fail1("r_last without r_valid");
            end
            // ---------------- completion
            for (p = 0; p < NP; p = p + 1) begin
                if (done[p]) begin
                    if (q_dn[p] == q_t[p]) fail1("done with no request outstanding");
                    else begin
                        ix = p * RN + (q_dn[p] % RN);
                        if (done_tag[p*8 +: 8] != (q_dn[p] & 255)) begin
                            n_proto = n_proto + 1; if (n_proto <= 10) $display("  done tag %0d, expected %0d (port %0d)", done_tag[p*8 +: 8], q_dn[p] & 255, p);
                        end
                        if (!q_we[ix] && !r_last[p]) fail1("read done without r_last in the same cycle");
                        if (q_we[ix]) begin
                            if (q_pl[p] <= q_dn[p]) fail1("write done before all data was pulled");
                            if (!opt_nochipchk) begin
                                for (k = 0; k < q_len[ix]; k = k + 1) begin
                                    ea = q_addr[ix] + k;
                                    if (peek(ea) !== {1'b1, pat(ea, q_seq[ix])}) n_chip_diff = n_chip_diff + 1;
                                end
                            end
                        end
                        wl = cyc - q_acc[ix];
                        p_lat_n[p] = p_lat_n[p] + 1; p_lat_sum[p] = p_lat_sum[p] + wl; if (wl > p_lat_max[p]) p_lat_max[p] = wl;
                        if (!q_we[ix] && q_fw[ix] >= 0) begin
                            wl = q_fw[ix] - q_acc[ix]; p_fw_sum[p] = p_fw_sum[p] + wl; if (wl > p_fw_max[p]) p_fw_max[p] = wl;
                        end
                        p_dn_words[p] = p_dn_words[p] + q_len[ix];
                        q_dn[p] = q_dn[p] + 1;
                        if (p == 1) dec_rd_dn = dec_rd_dn + 1;
                        if (p == 2) dec_wr_dn = dec_wr_dn + 1;
                        if (p == 1 || p == 2) progress_cyc = cyc;
                    end
                end
            end
            // ---------------- scan-out: drain the FIFO at the display rate, issue the next pattern request on credit
            if (!opt_no_scan) begin
                if (!sc_started && sc_level >= sc_start) sc_started = 1;
                if (sc_started) begin
                    if (sc_blank > 0) sc_blank = sc_blank - 1;
                    else begin
                        sc_acc = sc_acc + 65536;
                        if (sc_acc >= sc_wp) begin
                            sc_acc = sc_acc - sc_wp;
                            if (sc_level > 0) begin
                                sc_level = sc_level - 1; sc_cons = sc_cons + 1; sc_lw = sc_lw + 1;
                                if (sc_lw == sc_active_words) begin sc_lw = 0; sc_blank = sc_blank_cycles; sc_frames = sc_frames + 1; end
                            end else begin sc_under = sc_under + 1; end
                        end
                    end
                    if (sc_level < sc_level_min) sc_level_min = sc_level;
                end
                if (sc_started && sc_level == 0) sc_under_cyc = sc_under_cyc + 1;
                sc_urg = sc_started && (sc_level < sc_urg_thr);
                if (sc_urg) begin if (sc_urg_since < 0) sc_urg_since = cyc; end else sc_urg_since = -1;
                if (!sl_v[0]) begin
                    sc_v = sc_mem[sc_i];
                    sc_reqlen = {sc_v[39:32], sc_v[47:40]};
                    sc_credit = sc_cap - sc_level - sc_out;
                    if (sc_credit >= sc_reqlen) begin
                        present(0, 1'b0, SCAN_BASE + {sc_v[7:0], sc_v[15:8], sc_v[23:16], sc_v[31:24]}, sc_reqlen, 1'b0, 0, 0, 0);
                        sc_i = (sc_i + 1) % sc_n;
                    end
                end
            end
            // ---------------- CPU: one random single-word read every 2 us
            if (!opt_no_cpu && cyc >= cpu_next && cyc >= T0) begin
                if (!sl_v[CPU_P]) begin
                    lfsr = lfsr ^ (lfsr << 13); lfsr = lfsr ^ (lfsr >> 17); lfsr = lfsr ^ (lfsr << 5);
                    present(CPU_P, 1'b0, CPU_BASE + {9'd0, lfsr[15:0]}, 1, 1'b0, 0, 0, 0); cpu_issued = cpu_issued + 1;
                end else cpu_skipped = cpu_skipped + 1;
                cpu_next = cpu_next + cpu_period;
            end
            // ---------------- DMA: 64-word writes at 2 MB/s
            if (!opt_no_dma && cyc >= dma_next && cyc >= T0) begin
                if (!sl_v[DMA_P]) begin
                    present(DMA_P, 1'b1, DMA_BASE + dma_ptr, 64, 1'b0, 0, 0, 0); dma_issued = dma_issued + 1;
                    dma_ptr = (dma_ptr + 64) % 131072;
                end else dma_skipped = dma_skipped + 1;
                dma_next = dma_next + dma_period;
            end
            // ---------------- the decoder feeder
            feeder;
        end
    end

    // ------------------------------------------------------------------------------------------------
    // shim counters for the mutation options
    // ------------------------------------------------------------------------------------------------
    integer sh_acc1 = 0, sh_wn2 = 0, sh_rv1 = 0, sh_rl1 = 0;
    reg [7:0] wfp = 0;
    always @(posedge clk) if (!rst) begin
        if (req_valid[1] && req_ready[1]) sh_acc1 = sh_acc1 + 1;
        wfp <= {wfp[6:0], (mut_wcorrupt > 0) && w_next[2] && sh_wn2 >= mut_wcorrupt && sh_wn2 < mut_wcorrupt + 4};
        if (w_next[2]) sh_wn2 = sh_wn2 + 1;
        if (d_r_valid[1]) sh_rv1 = sh_rv1 + 1;
        if (d_r_last[1]) sh_rl1 = sh_rl1 + 1;
    end
    always @* begin
        shim_drop  = 0; shim_stall = 0; shim_nolast = 0; shim_wflip = 0; shim_rflip = 0;
        if (mut_drop > 0 && sh_acc1 == mut_drop) shim_drop[1] = 1'b1;
        if (mut_scanstall > 0 && cyc - T0 > 3000 && cyc - T0 < 3000 + mut_scanstall) shim_stall[0] = 1'b1;
        if (mut_stall > 0 && (cyc % mut_stall) != 0) begin shim_stall[1] = 1'b1; shim_stall[2] = 1'b1; end
        if (mut_wcorrupt > 0 && wfp[W_LEAD - 1]) shim_wflip[2*16] = 1'b1;
        if (mut_rcorrupt > 0 && d_r_valid[1] && sh_rv1 == mut_rcorrupt) shim_rflip[16 + 3] = 1'b1;
        if (mut_nolast > 0 && d_r_last[1] && sh_rl1 == mut_nolast) shim_nolast[1] = 1'b1;
    end

    // ------------------------------------------------------------------------------------------------
    // sequencing
    // ------------------------------------------------------------------------------------------------
    integer fdscan, nscan, stall_cnt = 0, end_flag = 0, ni;
    reg [8*40-1:0] verdict_reason;
    integer n_fail_reasons;
    reg [31:0] ver_tmp;
    task preload;
        integer a;
        begin
            for (a = 0; a < sc_active_words; a = a + 1) begin poke(SCAN_BASE + a, pat(SCAN_BASE + a, 1)); sh_s[sidx(SCAN_BASE + a)] = 1; end
            for (a = 0; a < 65536; a = a + 1) begin poke(CPU_BASE + a, pat(CPU_BASE + a, 1)); sh_s[sidx(CPU_BASE + a)] = 1; end
        end
    endtask
    task powerup;
        integer n;
        begin
            @(posedge clk); #1;
            repeat (3) @(posedge clk);
            rst <= 1'b0;
`ifndef STUB
            for (n = 0; n < 8; n = n + 1) begin
                @(posedge clk); sk_we <= 1'b1; sk_idx <= n; sk_base <= n; sk_val <= skt3(n);
            end
            @(posedge clk); sk_we <= 1'b0;
`endif
            n = 0;
            while (!init_done && n < 60000) begin @(posedge clk); n = n + 1; end
            if (!init_done) begin $display("  init_done never rose"); tb_fail = tb_fail + 1; end
            repeat (4) @(posedge clk);
        end
    endtask

    // watchdog: the decoder has a request pending or outstanding and nothing moved on ports 1 and 2 for `tmo` cycles
    always @(posedge clk) if (fd_go && !fd_done && !end_flag) begin
        if ((dec_out(0) != 0 || sl_v[1] || sl_v[2]) && cyc - progress_cyc > tmo) begin
            n_hang = n_hang + 1;
            $display("  HANG: no progress for %0d cycles (outstanding decoder reads %0d writes %0d, pending request %0d %0d)", tmo, dec_rd_acc - dec_rd_dn, dec_wr_acc - dec_wr_dn, sl_v[1], sl_v[2]);
            end_flag = 1;
        end
    end

    task report;
        integer pp, nreq_all, ncmd, fails;
        real hit, cpr, rpm, rcpm, nocnt;
        begin
            nreq_all = 0;
            for (pp = 0; pp < NP; pp = pp + 1) nreq_all = nreq_all + p_req[pp];
            ncmd = n_cmd;
            $display("RESULT treq=%0s", treq_name);
            $display("RESULT cycles=%0d pictures=%0d mbs=%0d records=%0d decoder_requests=%0d fused_read_records=%0d unfused_reads=%0d", cyc - T0_first, npics, tot_mbs, tot_recs, tot_reqs, tot_fused_reads, tot_unfused_reads);
            rpm = (tot_mbs > 0) ? 1.0 * tot_reqs / tot_mbs : 0.0;
            rcpm = (tot_mbs > 0) ? 1.0 * (tot_fused_reads + tot_unfused_reads) / tot_mbs : 0.0;
            $display("RESULT requests_per_mb=%0.2f", rpm);
            $display("RESULT requests_all=%0d commands=%0d act=%0d rd=%0d wr=%0d pre=%0d prea=%0d ref=%0d bst=%0d", nreq_all, ncmd, n_act, n_rdc, n_wrc, n_pre, n_prea, n_ref, n_bst);
            cpr = (nreq_all > 0) ? 1.0 * ncmd / nreq_all : 0.0;
            $display("RESULT commands_per_request=%0.3f", cpr);
            hit = ((n_rdc + n_wrc) > 0) ? 1.0 - 1.0 * n_act / (n_rdc + n_wrc) : 0.0;
            $display("RESULT row_hit_rate=%0.4f   (1 - ACT / column commands)", hit);
`ifndef STUB
            $display("RESULT bus_words phy_rvalid=%0d phy_wvalid=%0d client_rd=%0d client_wr=%0d chip_rd_words=%0d chip_wr_words=%0d burst_tail_words=%0d",
                     n_rv, n_wv, glob_words_rd, glob_words_wr, chip.rd_words, chip.wr_words, chip.rd_words - glob_words_rd);
`endif
            $display("RESULT scan underflow_words=%0d underflow_cycles=%0d level_min=%0d consumed=%0d frames=%0d urgent_waits=%0d urgent_wait_max=%0d urgent_bound=%0d urgent_over=%0d",
                     sc_under, sc_under_cyc, sc_level_min, sc_cons, sc_frames, sc_urg_n, sc_urg_wait_max, urgent_bound, sc_urg_over);
            $display("RESULT generators cpu_issued=%0d cpu_skipped=%0d dma_issued=%0d dma_skipped=%0d", cpu_issued, cpu_skipped, dma_issued, dma_skipped);
            $display("RESULT integrity mismatches=%0d chip_diffs=%0d races=%0d unchecked_uninit_words=%0d", n_mism, n_chip_diff, n_race, n_uninit);
            $display("RESULT monitors fence_viol=%0d barrier_viol=%0d release_viol=%0d bad_address=%0d proto=%0d hang=%0d", n_fence_viol, n_barrier_viol, n_rel_viol, n_addr_bad, n_proto, n_hang);
`ifdef STUB
            $display("RESULT phy_fault=0 phy_fault_count=0 chip_errors=0 dq_conflicts=0 dbg_err=0 dut_bad_req=0 (stub: no PHY, no chip model)");
`else
            $display("RESULT phy_fault=%0d phy_fault_count=%0d chip_errors=%0d dq_conflicts=%0d dbg_err=%0d dut_bad_req=%0d refreshes=%0d",
                     phy_fault, fault_count, chip.err_count, dq_conflicts, dbg_err, |cnt_bad, refreshes);
`endif
            for (pp = 0; pp < NP; pp = pp + 1)
                $display("PORT %0d requests=%0d words=%0d lat_avg_x100=%0d lat_max=%0d first_word_max=%0d",
                         pp, p_req[pp], p_words[pp], (p_lat_n[pp] > 0) ? (p_lat_sum[pp] * 100) / p_lat_n[pp] : 0, p_lat_max[pp], p_fw_max[pp]);
            $display("RESULT pictures_late=%0d pictures_over90=%0d busy_max=%0d lag_max=%0d period=%0d norelease=%0d", tot_pics_late, tot_pics_over, busy_max, lag_max, period_c, opt_norel);
            fails = 0;
            verdict_reason = "";
            if (n_mism != 0) begin $display("  reason: %0d read mismatches", n_mism); fails = fails + 1; end
            if (n_chip_diff != 0) begin $display("  reason: %0d chip/shadow differences at write done", n_chip_diff); fails = fails + 1; end
            if (n_proto != 0) begin $display("  reason: %0d protocol errors", n_proto); fails = fails + 1; end
            if (n_hang != 0) begin $display("  reason: hang (a request was lost or never completed)"); fails = fails + 1; end
            if (n_fence_viol + n_barrier_viol + n_rel_viol != 0) begin $display("  reason: offer-rule violations fence %0d barrier %0d release %0d", n_fence_viol, n_barrier_viol, n_rel_viol); fails = fails + 1; end
            if (n_addr_bad != 0) begin $display("  reason: %0d addresses outside the supported range", n_addr_bad); fails = fails + 1; end
            if (sc_under != 0) begin $display("  reason: scan-out underflow (%0d words)", sc_under); fails = fails + 1; end
            if (tot_pics_late != 0) begin $display("  reason: %0d pictures past their deadline", tot_pics_late); fails = fails + 1; end
            if (tot_pics_over != 0) begin $display("  reason: %0d pictures with busy time above 90 %% of the period", tot_pics_over); fails = fails + 1; end
`ifndef STUB
            if (phy_fault || fault_count != 0) begin $display("  reason: phy_fault"); fails = fails + 1; end
            if (chip.err_count != 0) begin $display("  reason: %0d chip-model errors", chip.err_count); fails = fails + 1; end
            if (|cnt_bad) begin $display("  reason: the scheduler rejected a request (bad_req)"); fails = fails + 1; end
            if (dq_conflicts != 0) begin $display("  reason: %0d DQ conflicts", dq_conflicts); fails = fails + 1; end
`endif
            if (npics == 0) begin $display("  reason: no picture completed"); fails = fails + 1; end
            if (fails == 0) $display("VERDICT PASS"); else $display("VERDICT FAIL (%0d reasons)", fails);
        end
    endtask

    initial begin : main
        integer v;
        treq_name = "tnv3_tears_q30_48pic.treq"; scan_name = "scan_800x720_1frame.req";
        if ($value$plusargs("treq=%s", treq_name)) ;
        if ($value$plusargs("scan=%s", scan_name)) ;
        if ($value$plusargs("limit_pictures=%d", v)) limit_pics = v;
        if ($value$plusargs("limit_mbs=%d", v)) limit_mbs = v;
        if ($value$plusargs("skip_pictures=%d", v)) begin skip_pics = v; skipping = (v > 0); end
        if ($value$plusargs("period_cycles=%d", v)) period_ovr = v;
        if ($value$plusargs("ahead=%d", v)) ahead_ovr = v;
        if ($value$plusargs("tmo=%d", v)) tmo = v;
        if ($value$plusargs("maxcyc=%d", v)) maxcyc = v;
        if ($value$plusargs("scan_fifo=%d", v)) sc_cap = v;
        if ($value$plusargs("scan_urgent=%d", v)) sc_urg_thr = v;
        if ($value$plusargs("mut_drop=%d", v)) mut_drop = v;
        if ($value$plusargs("mut_wcorrupt=%d", v)) mut_wcorrupt = v;
        if ($value$plusargs("mut_rcorrupt=%d", v)) mut_rcorrupt = v;
        if ($value$plusargs("mut_nolast=%d", v)) mut_nolast = v;
        if ($value$plusargs("mut_stall=%d", v)) mut_stall = v;
        if ($value$plusargs("mut_scanstall=%d", v)) mut_scanstall = v;
        opt_nofuse = $test$plusargs("nofuse"); opt_norel = $test$plusargs("norelease");
        opt_no_scan = $test$plusargs("no_scan"); opt_no_cpu = $test$plusargs("no_cpu"); opt_no_dma = $test$plusargs("no_dma");
        opt_nochipchk = $test$plusargs("nochipchk"); opt_nopreload = $test$plusargs("nopreload"); opt_verbose = $test$plusargs("verbose");
        opt_mut_nofence = $test$plusargs("mut_nofence"); opt_mut_nobarrier = $test$plusargs("mut_nobarrier"); opt_mut_norelgate = $test$plusargs("mut_noreleasegate");
        // files
        fd_t = $fopen(treq_name, "rb");
        if (fd_t == 0) begin $display("cannot open %0s", treq_name); $finish; end
        fdscan = $fopen(scan_name, "rb");
        if (fdscan == 0) begin $display("cannot open %0s", scan_name); $finish; end
        nscan = $fread(sc_mem, fdscan);
        sc_n = nscan / 8;
        $fclose(fdscan);
        sc_wp = (65536 * (22675737 / P_PS) + (65536 * (22675737 % P_PS)) / P_PS) / sc_line;
        sc_blank_cycles = 15 * (22675737 / P_PS);
        cpu_period = 2_000_000 / P_PS; dma_period = 64_000_000 / P_PS;
        $display("TB treq=%0s scan=%0s scan_requests=%0d limit_pictures=%0d limit_mbs=%0d norelease=%0d nofuse=%0d", treq_name, scan_name, sc_n, limit_pics, limit_mbs, opt_norel, opt_nofuse);
        #1000;
        for (ni = 0; ni < SHN; ni = ni + 1) sh_s[ni] = 32'd0;
        if (!opt_nopreload) preload;
        powerup;
        T0 = cyc; T0_first = cyc; cpu_next = cyc; dma_next = cyc + dma_period / 2; progress_cyc = cyc;
        prev_reached = T0; fd_go = 1;
        while (!fd_done && !end_flag && (maxcyc == 0 || cyc - T0 < maxcyc)) @(posedge clk);
        if (!fd_done && !end_flag) begin n_hang = n_hang + 1; $display("  maxcyc reached before the stream ended"); end
        repeat (20) @(posedge clk);
        report;
        $finish;
    end
endmodule
