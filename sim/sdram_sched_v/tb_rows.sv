// Checkpoint-2 test: open-row table and ACT/PRE look-ahead of tau_sdram_sched_v against the PHY and the chip model.
// Requests are queued but never granted (no column commands yet): the scheduler must open the target's row, and
// when an urgent request for another row of the same bank arrives, close and re-open (PRE then ACT). Legal offers only:
// phy_fault 0, no chip error.
`timescale 1ps/1ps
`include "sdram_chip_err.vh"
module tb_rows;
    localparam NP = 6;
    reg clk = 0; always #5000 clk = ~clk;
    reg rst = 1;
    reg [NP-1:0] req_valid = 0, req_we = 0, req_2d = 0, urgent = 0;
    reg [NP*25-1:0] req_addr = 0; reg [NP*10-1:0] req_len = 0; reg [NP*8-1:0] req_tag = 0;
    reg [NP*6-1:0] req_w = 0, req_h = 0; reg [NP*11-1:0] req_stride = 0; reg [NP*2-1:0] req_skew = 0;
    wire [NP-1:0] req_ready, done; wire [NP*8-1:0] done_tag; wire [NP*16-1:0] cnt_bad;
    wire [2:0] cmd; wire [1:0] cmd_ba; wire [12:0] cmd_a;
    wire [3:0] ok_act, ok_rd, ok_wr, ok_pre, row_aged, ref_debt;
    wire ok_prea, ok_ref, ok_bst, init_done, rvalid, phy_fault, preempt, banks_closed, ref_due;
    wire [15:0] rdata, fault_count, dq_conflicts; wire [3:0] fault_cause; wire [31:0] refreshes; wire [4:0] rd_lat;
    wire [12:0] c_a; wire [1:0] c_ba, c_dqm; wire c_clk, c_cke, c_ras, c_cas, c_we; wire [15:0] dq;
    tau_sdram_phy #(.CLK_HZ(100_000_000), .INIT_WAIT_CYCLES(200), .SIM_SHORT_INIT(1)) phy (
        .clk(clk), .rst(rst), .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .wvalid(1'b0), .wdata(16'd0), .be(2'b11),
        .ok_act(ok_act), .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_prea(ok_prea), .ok_ref(ok_ref), .ok_bst(ok_bst),
        .init_done(init_done), .rvalid(rvalid), .rdata(rdata), .phy_fault(phy_fault), .fault_count(fault_count),
        .fault_cause(fault_cause), .row_aged(row_aged), .preempt(preempt), .preempt_ack(1'b0), .banks_closed(banks_closed),
        .ref_due(ref_due), .ref_debt(ref_debt), .refreshes(refreshes), .dq_conflicts(dq_conflicts), .rd_lat(rd_lat),
        .dram_a(c_a), .dram_ba(c_ba), .dram_dq(dq), .dram_dqm(c_dqm), .dram_clk(c_clk), .dram_cke(c_cke),
        .dram_ras_n(c_ras), .dram_cas_n(c_cas), .dram_we_n(c_we));
    sdram_chip_model #(.INIT_NOP_MIN(200), .INIT_WAIT_PS(200 * 10000)) chip (.dram_clk(c_clk), .dram_cke(c_cke),
        .dram_ras_n(c_ras), .dram_cas_n(c_cas), .dram_we_n(c_we), .dram_dqm(c_dqm), .dram_ba(c_ba), .dram_a(c_a),
        .dram_dq(dq), .fpga_oe(phy.u_core.dq_oe), .cap_clk(clk));
    tau_sdram_sched_v #(.NPORTS(NP)) dut (.clk(clk), .rst(rst), .req_valid(req_valid), .req_ready(req_ready), .req_we(req_we),
        .req_addr(req_addr), .req_len(req_len), .req_tag(req_tag), .req_2d(req_2d), .req_w(req_w), .req_h(req_h),
        .req_stride(req_stride), .req_skew(req_skew), .urgent(urgent), .done(done), .done_tag(done_tag), .cnt_bad(cnt_bad),
        .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .ok_act(ok_act), .ok_pre(ok_pre), .init_done(init_done),
        .row_aged(row_aged), .preempt(preempt), .banks_closed(banks_closed));
    integer fail = 0;
    task chk(input c, input [255:0] m); begin if (!c) begin $display("FAIL t=%0t: %0s", $time, m); fail = fail + 1; end end endtask
    task push(input integer p, input [24:0] a);
        begin
            req_valid[p] <= 1; req_we[p] <= 0; req_addr[p*25 +: 25] <= a; req_len[p*10 +: 10] <= 10'd8; req_tag[p*8 +: 8] <= p;
            @(posedge clk); #1; req_valid[p] <= 0; @(posedge clk); #1;
        end
    endtask
    // plain mapping (BANK_HASH 2 with upper bits zero): bank = addr[11:10], row = addr[24:12]
    initial begin
        repeat (3) @(posedge clk); rst <= 0;
        wait (init_done); repeat (4) @(posedge clk); #1;
        chk(dut.o_v == 4'b0000, "all closed after init");
        push(1, {13'd5, 2'd1, 10'd0});          // port 1 wants bank 1 row 5
        repeat (6) @(posedge clk); #1;
        chk(dut.o_v == 4'b0010 && dut.o_row[1] == 13'd5, "ACT bank 1 row 5");
        // an urgent port 0 request for bank 1 row 9: the target becomes port 0, so bank 1 is precharged and re-opened
        urgent[0] <= 1;
        push(0, {13'd9, 2'd1, 10'd0});
        repeat (12) @(posedge clk); #1;
        chk(dut.o_v == 4'b0010 && dut.o_row[1] == 13'd9, "PRE then ACT bank 1 row 9");
        // another bank
        urgent[0] <= 0;
        repeat (2) @(posedge clk);
        push(2, {13'd3, 2'd2, 10'd0});
        repeat (6) @(posedge clk); #1;
        chk(phy_fault === 1'b0, "no PHY fault");
        chk(chip.err_count == 0, "no chip error");
        if (fail == 0) $display("TEST rows PASS"); else $display("TEST rows FAIL (%0d)", fail);
        $finish;
    end
endmodule
