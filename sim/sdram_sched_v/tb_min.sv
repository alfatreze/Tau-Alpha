// Test bench for tau_sdram_sched_min: one client, PHY + chip model, shadow memory. MIT licence.
// vvp tb.vvp +TEST=<name>: basic | lengths | bad | refresh | aged. Legal tests end with phy_fault 0 and zero chip errors.
`timescale 1ps/1ps
`include "sdram_chip_err.vh"
module tb_min #(
    parameter integer CLSEL = 2,
    parameter integer BANK_HASH = 2,
    parameter integer REFIPS = 7_800_000,
    parameter integer LMAX_T = 512
);
    localparam P_PS = 10000, INITCYC = 200, W_LEAD = 2;
    reg clk = 0; always #5000 clk = ~clk;
    reg rst = 1; integer cyc = 0; always @(posedge clk) cyc <= cyc + 1;
    reg req_valid = 0, req_we = 0, req_2d = 0; reg [24:0] req_addr = 0; reg [9:0] req_len = 0; reg [7:0] req_tag = 0;
    reg [15:0] w_data = 0; reg [1:0] w_be = 2'b11;
    wire req_ready, w_next, r_valid, r_last, done; wire [9:0] w_idx; wire [15:0] r_data; wire [7:0] done_tag;
    wire [31:0] cnt_requests, cnt_words; wire [15:0] cnt_bad; wire dbg_pmiss;
    wire [2:0] cmd; wire [1:0] cmd_ba; wire [12:0] cmd_a; wire wvalid; wire [15:0] wdata; wire [1:0] be;
    wire [3:0] ok_act, ok_rd, ok_wr, ok_pre, row_aged, ref_debt;
    wire ok_prea, ok_ref, ok_bst, init_done, rvalid, phy_fault, preempt, banks_closed, ref_due, preempt_ack;
    wire [15:0] rdata, fault_count, dq_conflicts; wire [3:0] fault_cause; wire [31:0] refreshes; wire [4:0] rd_lat;
    wire [12:0] c_a; wire [1:0] c_ba, c_dqm; wire c_clk, c_cke, c_ras, c_cas, c_we; wire [15:0] dq;
    tau_sdram_phy #(.CLK_HZ(100_000_000), .CL(CLSEL), .INIT_WAIT_CYCLES(INITCYC), .SIM_SHORT_INIT(1), .REFRESH_INTERVAL_PS(REFIPS)) phy (
        .clk(clk), .rst(rst), .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .wvalid(wvalid), .wdata(wdata), .be(be),
        .ok_act(ok_act), .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_prea(ok_prea), .ok_ref(ok_ref), .ok_bst(ok_bst),
        .init_done(init_done), .rvalid(rvalid), .rdata(rdata), .phy_fault(phy_fault), .fault_count(fault_count),
        .fault_cause(fault_cause), .row_aged(row_aged), .preempt(preempt), .preempt_ack(preempt_ack), .banks_closed(banks_closed),
        .ref_due(ref_due), .ref_debt(ref_debt), .refreshes(refreshes), .dq_conflicts(dq_conflicts), .rd_lat(rd_lat),
        .dram_a(c_a), .dram_ba(c_ba), .dram_dq(dq), .dram_dqm(c_dqm), .dram_clk(c_clk), .dram_cke(c_cke),
        .dram_ras_n(c_ras), .dram_cas_n(c_cas), .dram_we_n(c_we));
    sdram_chip_model #(.CL_DEFAULT(CLSEL), .INIT_NOP_MIN(INITCYC), .INIT_WAIT_PS(INITCYC * P_PS), .MEM_LOG2(20), .MEM_COMPACT(1)) chip (
        .dram_clk(c_clk), .dram_cke(c_cke), .dram_ras_n(c_ras), .dram_cas_n(c_cas), .dram_we_n(c_we), .dram_dqm(c_dqm),
        .dram_ba(c_ba), .dram_a(c_a), .dram_dq(dq), .fpga_oe(phy.u_core.dq_oe), .cap_clk(clk));
    tau_sdram_sched_min #(.BANK_HASH(BANK_HASH), .W_LEAD(W_LEAD), .CL(CLSEL)) dut (
        .clk(clk), .rst(rst), .req_valid(req_valid), .req_ready(req_ready), .req_we(req_we), .req_addr(req_addr), .req_len(req_len),
        .req_tag(req_tag), .req_2d(req_2d), .w_next(w_next), .w_idx(w_idx), .w_data(w_data), .w_be(w_be), .r_valid(r_valid),
        .r_data(r_data), .r_last(r_last), .done(done), .done_tag(done_tag), .cnt_requests(cnt_requests), .cnt_words(cnt_words),
        .cnt_bad(cnt_bad), .dbg_pmiss(dbg_pmiss), .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .wvalid(wvalid), .wdata(wdata), .be(be), .ok_act(ok_act),
        .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_prea(ok_prea), .ok_ref(ok_ref), .init_done(init_done), .rvalid(rvalid),
        .rdata(rdata), .row_aged(row_aged), .preempt(preempt), .preempt_ack(preempt_ack), .banks_closed(banks_closed), .ref_due(ref_due));

    reg [15:0] sh_d [0:(1<<20)-1]; reg sh_v [0:(1<<20)-1];
    function integer sidx(input [24:0] a); begin sidx = {a[23:20], a[15:0]}; end endfunction
    function [24:0] A(input integer region, input integer off); begin A = {1'b0, region[3:0], 4'b0000, off[15:0]}; end endfunction
    function [15:0] pat(input [24:0] a, input integer g); reg [31:0] x; begin
        x = {7'd0, a} * 32'h9E3779B1 + g * 32'h7F4A7C15; x = x ^ (x >> 15); x = x * 32'h2C1B3C6D; pat = x[31:16] ^ x[15:0]; end endfunction
    function [1:0] tb_bank(input [24:0] a); begin
        case (BANK_HASH) 0: tb_bank = a[11:10]; 1: tb_bank = a[11:10] ^ a[21:20]; default: tb_bank = a[11:10] ^ a[21:20] ^ a[23:22]; endcase end endfunction

    integer fail = 0; reg [8*40-1:0] tname; reg [7:0] req_tag_exp = 0;
    task fail1(input [8*60-1:0] m); begin $display("  FAIL cyc=%0d: %0s", cyc, m); fail = fail + 1; end endtask
    // write data model: answers w_next W_LEAD cycles later
    reg [24:0] w_base = 0; integer w_gen = 1; reg [15:0] wp0, wp1;
    always @(posedge clk) begin
        wp0 <= pat(w_base + w_idx, w_gen);
        w_data <= wp0;
    end
    // the pipeline above registers the pattern for w_idx every cycle; w_data is the value for the w_idx of W_LEAD cycles ago
    // (w_idx only changes with a pull, so the pattern for the pulled index is the one registered in the pull cycle)
    // read model
    integer rd_k = 0, rd_exp_n = 0; reg [24:0] rd_base = 0; reg rd_active = 0, got_done = 0, got_last = 0;
    always @(posedge clk) begin
        if (r_valid) begin
            if (!rd_active) fail1("r_valid with no read outstanding");
            else begin
                if (!sh_v[sidx(rd_base + rd_k)] || r_data !== sh_d[sidx(rd_base + rd_k)]) begin
                    if (fail < 10) $display("  read mismatch word %0d addr %h got %h want %h", rd_k, rd_base + rd_k, r_data, sh_d[sidx(rd_base + rd_k)]); fail = fail + 1;
                end
                if (r_last != (rd_k == rd_exp_n - 1)) fail1("r_last position");
                rd_k = rd_k + 1;
            end
        end
        if (done) begin if (done_tag !== req_tag_exp) fail1("done tag"); got_done = 1; end
    end
    task issue(input we, input [24:0] addr, input integer len, input twod);
        integer w; integer k; reg acc;
        begin
            req_tag_exp = req_tag_exp + 1; got_done = 0;
            if (we) begin w_base = addr; end
            else begin rd_base = addr; rd_k = 0; rd_exp_n = len; rd_active = (len >= 1 && len <= 512 && !twod); end
            #1; req_valid = 1; req_we = we; req_addr = addr; req_len = len; req_tag = req_tag_exp; req_2d = twod;
            w = 0; acc = 0;
            while (!acc && w < 100000) begin @(posedge clk); acc = req_ready; w = w + 1; end   // req_ready as it was before this edge
            #1; req_valid = 0;
            if (we && len >= 1 && len <= 512 && !twod) for (k = 0; k < len; k = k + 1) begin sh_d[sidx(addr + k)] = pat(addr + k, w_gen); sh_v[sidx(addr + k)] = 1; end
            w = 0; while (!got_done && w < 100000) begin @(posedge clk); w = w + 1; end
            if (!got_done) begin $display("  no done: we=%b addr=%h len=%0d acc=%b", we, addr, len, acc); fail1("no done"); end
            else if (!we && rd_active && rd_k != len) fail1("read word count");
            #1; rd_active = 0;
            if (we && len >= 1 && len <= 512 && !twod) begin : chk
                integer j; reg [16:0] pk; reg [24:0] a;
                for (j = 0; j < len; j = j + 1) begin
                    a = addr + j; pk = chip.peek(tb_bank(a), a[24:12], a[9:0]);
                    if (pk[15:0] !== sh_d[sidx(a)]) begin if (fail < 10) $display("  chip memory differs at %h at the done", a); fail = fail + 1; end
                end
            end
        end
    endtask
    task end_checks; begin
        repeat (8) @(posedge clk);
        if (phy_fault !== 1'b0) begin $display("  PHY fault count=%0d cause=%0d", fault_count, fault_cause); fail = fail + 1; end
        if (chip.err_count != 0) begin $display("  chip errors %0d mask %h", chip.err_count, chip.err_mask); fail = fail + 1; end
        if (dbg_pmiss) begin $display("  timing model mismatch"); fail = fail + 1; end
        if (dq_conflicts !== 16'd0) begin $display("  dq conflicts"); fail = fail + 1; end
    end endtask
    integer L, r, n, off; integer seed = 3;
    function integer urand(input integer m); integer v; begin v = $random(seed); if (v < 0) v = -v; urand = v % m; end endfunction
    initial begin
        tname = "basic"; if (!$value$plusargs("TEST=%s", tname)) tname = "basic";
        @(posedge clk); repeat (3) @(posedge clk); rst <= 0;
        n = 0; while (!init_done && n < 40000) begin @(posedge clk); n = n + 1; end
        repeat (4) @(posedge clk);
        case (tname)
            "basic": begin
                w_gen = 1; issue(1, A(0, 100), 10, 0); issue(0, A(0, 100), 10, 0); issue(0, A(0, 104), 4, 0);
                w_gen = 2; issue(1, A(1, 1000), 200, 0); issue(0, A(1, 1000), 200, 0);   // crosses a page
                w_gen = 3; issue(1, A(2, 1023), 2, 0); issue(0, A(2, 1023), 2, 0);
                w_gen = 4; issue(1, A(3, 0), 1, 0); issue(0, A(3, 0), 1, 0);
                w_gen = 5; issue(1, A(4, 3000), 512, 0); issue(0, A(4, 3000), 512, 0);    // several pages and banks
                if (cnt_requests != 11 || cnt_words != 1454) begin $display("  counters %0d %0d", cnt_requests, cnt_words); fail = fail + 1; end
            end
            "lengths": begin
                for (L = 1; L <= LMAX_T; L = L + 1) begin
                    w_gen = L; off = ((L - 1) * 97 + (L % 11) * 1003) % 60000;
                    issue(1, A(L % 8, off), L, 0); issue(0, A(L % 8, off), L, 0);
                end
            end
            "bad": begin
                w_gen = 1; issue(1, A(0, 0), 100, 0);
                issue(0, A(0, 0), 0, 0); issue(0, A(0, 0), 513, 0); issue(0, A(0, 0), 1023, 0); issue(0, A(0, 0), 4, 1);
                issue(0, A(0, 0), 100, 0);
                if (cnt_bad != 4) begin $display("  bad counter %0d", cnt_bad); fail = fail + 1; end
            end
            "refresh": begin       // 60,000 cycles of mixed traffic with the real refresh interval
                for (r = 0; r < 200; r = r + 1) begin
                    L = 1 + urand(300); off = urand(30000); w_gen = 10 + r;
                    issue(1, A(5 + r % 3, off), L, 0); issue(0, A(5 + r % 3, off), L, 0);
                end
                $display("  refreshes %0d cycles %0d", refreshes, cyc);
                if (refreshes < 20) begin $display("  too few refreshes"); fail = fail + 1; end
            end
            "aged": begin          // an idle open row is closed by the scheduler before 90 us
                w_gen = 1; issue(1, A(0, 0), 8, 0); issue(0, A(0, 0), 8, 0);
                repeat (12000) @(posedge clk);
                if (dut.o_v) begin $display("  row still open"); fail = fail + 1; end
            end
            default: begin $display("unknown test"); fail = fail + 1; end
        endcase
        end_checks;
        if (fail == 0) $display("TEST %0s PASS", tname); else $display("TEST %0s FAIL (%0d)", tname, fail);
        $finish;
    end
    initial begin #(64'd1_000_000 * 20_000); $display("TEST %0s FAIL (global time-out)", tname); $finish; end
endmodule
