// Equivalence test: the flat B-916 PHY core against the reference core (sim/sdram_phy/ref/, the core as of
// b1e3ddc5), same random stimulus including illegal offers, every output compared every cycle. MIT licence.
// Address/bank pins are compared only where they matter (ACT, RD, WR, MRS: all bits; PREA/PRE: A10); on NOP, REF and
// BST cycles the new core passes the offered value through (the chip ignores it).
`timescale 1ps/1ps
module tb_equiv #(
    parameter integer CALIB = 0,
    parameter integer BISTON = 0,
    parameter integer PERFON = 0,
    parameter integer CLSEL = 2,
    parameter integer CAPMODE = 0,
    parameter integer RDEX = 1,
    parameter integer NCYC = 200000,
    parameter integer SEED = 1,
    parameter integer DEBT = 3,
    parameter integer REFIPS = 400000
);
    reg clk = 1'b0;
    always #5000 clk = ~clk;
    reg rst = 1'b1;
    reg [2:0] cmd = 0; reg [1:0] cmd_ba = 0; reg [12:0] cmd_a = 0; reg wvalid = 0; reg [15:0] wdata = 0; reg [1:0] be = 0;
    reg preempt_ack = 0, idle_ok = 0, bist_start = 0, perf_snap = 0, perf_clear = 0;
    reg [15:0] dq_i = 0;

    wire [3:0] ok_act[0:1], ok_rd[0:1], ok_wr[0:1], ok_pre[0:1];
    wire [1:0] ok_prea, ok_ref, ok_bst, init_done, rvalid, phy_fault, preempt, banks_closed, ref_due, cal_done, cal_fault, bist_busy, bist_done;
    wire [15:0] rdata[0:1], fault_count[0:1], dq_conflicts[0:1];
    wire [3:0] fault_cause[0:1], row_aged[0:1], ref_debt[0:1], cal_best[0:1];
    wire [31:0] refreshes[0:1], bist_errors[0:1];
    wire [4:0] rd_lat[0:1];
    wire [7:0] cal_window[0:1];
    wire [24:0] bist_first[0:1];
    wire [511:0] perf[0:1];
    wire [1:0] pin_cke, pin_ras, pin_cas, pin_we;
    wire [1:0] pin_dqm[0:1], pin_ba[0:1];
    wire [12:0] pin_a[0:1];
    wire [15:0] dq_o[0:1], dq_oe[0:1];

`define PHYPARAMS \
    .CLK_HZ(100_000_000), .CL(CLSEL), .RD_EXTRA(RDEX), .CAP_MODE(CAPMODE), .INIT_WAIT_CYCLES(100), .SIM_SHORT_INIT(1), \
    .REFRESH_DEBT_MAX(DEBT), .REFRESH_INTERVAL_PS(REFIPS), .PREEMPT_TIMEOUT(30), .ROW_FORCE_NS(20000), .OPEN_ROW_MAX_NS(10000), \
    .CALIBRATE(CALIB), .CAL_BANK(1), .CAL_ROW(5), .CAL_LEN(32), .BIST(BISTON), .BIST_PAGE_LO(0), .BIST_PAGE_HI(1), .PERF(PERFON)
`define PORTS(I) \
    .clk(clk), .rst(rst), .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .wvalid(wvalid), .wdata(wdata), .be(be), \
    .ok_act(ok_act[I]), .ok_rd(ok_rd[I]), .ok_wr(ok_wr[I]), .ok_pre(ok_pre[I]), .ok_prea(ok_prea[I]), .ok_ref(ok_ref[I]), .ok_bst(ok_bst[I]), \
    .init_done(init_done[I]), .rvalid(rvalid[I]), .rdata(rdata[I]), .phy_fault(phy_fault[I]), .fault_count(fault_count[I]), \
    .fault_cause(fault_cause[I]), .row_aged(row_aged[I]), .preempt(preempt[I]), .preempt_ack(preempt_ack), .banks_closed(banks_closed[I]), \
    .ref_due(ref_due[I]), .ref_debt(ref_debt[I]), .refreshes(refreshes[I]), .dq_conflicts(dq_conflicts[I]), .rd_lat(rd_lat[I]), \
    .idle_ok(idle_ok), .cal_window(cal_window[I]), .cal_best(cal_best[I]), .cal_done(cal_done[I]), .cal_fault(cal_fault[I]), \
    .bist_start(bist_start), .bist_busy(bist_busy[I]), .bist_done(bist_done[I]), .bist_errors(bist_errors[I]), .bist_first_addr(bist_first[I]), \
    .perf_snap(perf_snap), .perf_clear(perf_clear), .perf_snapshot(perf[I]), \
    .pin_cke(pin_cke[I]), .pin_ras_n(pin_ras[I]), .pin_cas_n(pin_cas[I]), .pin_we_n(pin_we[I]), .pin_dqm(pin_dqm[I]), \
    .pin_ba(pin_ba[I]), .pin_a(pin_a[I]), .dq_o(dq_o[I]), .dq_oe(dq_oe[I]), .dq_i(dq_i)

    tau_sdram_phy_core_ref #(`PHYPARAMS) u_ref (`PORTS(0));
    tau_sdram_phy_core     #(`PHYPARAMS) u_new (`PORTS(1));

    integer seed = SEED;
    function integer urand(input integer m);
        integer v;
        begin v = $random(seed); if (v < 0) v = -v; urand = v % m; end
    endfunction

    integer cyc = 0, errs = 0, pick, k;
    reg [2:0] c; reg [1:0] bb;
    always @(posedge clk) begin
        #100;
        cyc = cyc + 1;
        if (cyc == 4) rst = 1'b0;
        if (cyc == 5000 && urand(2) == 0) begin rst = 1'b1; end       // a reset in the middle of whatever is running
        if (cyc == 5003) rst = 1'b0;
        // stimulus for the next cycle
        pick = urand(100);
        bb = urand(4);
        c = urand(8);
        cmd = c; cmd_ba = bb; cmd_a = urand(8192);
        if (pick < 55) begin                                           // a legal command by the new core's permits
            k = urand(8);
            case (k)
                0: if (ok_act[1][bb]) begin cmd = 1; end
                1: if (ok_rd[1][bb])  begin cmd = 2; end
                2: if (ok_wr[1][bb])  begin cmd = 3; end
                3: if (ok_pre[1][bb]) begin cmd = 4; end
                4: if (ok_prea[1])    begin cmd = 5; end
                5: if (ok_ref[1])     begin cmd = 6; end
                6: if (ok_bst[1])     begin cmd = 7; end
                default: cmd = 0;
            endcase
            if (cmd == 2 || cmd == 3) cmd_a = (urand(3) == 0) ? 1000 + urand(24) : urand(8);
            else if (cmd != 1) cmd_a = 0;
            else cmd_a = urand(4);
            if (urand(25) == 0) cmd_a[10] = 1'b1;                      // sometimes A10 on a command that must not have it
        end else if (pick < 75) cmd = 0;
        wvalid = urand(6) != 0; wdata = urand(65536); be = urand(4);
        preempt_ack = urand(40) == 0;
        idle_ok = urand(30) != 0;
        bist_start = urand(900) == 0;
        perf_snap = urand(50) == 0; perf_clear = urand(2000) == 0;
        dq_i = urand(65536);
    end

    // compare on the falling edge, after both cores have settled
    task cmp(input [255:0] name, input [511:0] a, input [511:0] b);
        begin if (a !== b) begin errs = errs + 1; if (errs < 20) $display("MISMATCH %0s cycle %0d: ref %h new %h", name, cyc, a, b); end end
    endtask
    reg [2:0] pc0, pc1;
    always @(negedge clk) if (cyc > 1) begin
        cmp("ok_act", ok_act[0], ok_act[1]); cmp("ok_rd", ok_rd[0], ok_rd[1]); cmp("ok_wr", ok_wr[0], ok_wr[1]);
        cmp("ok_pre", ok_pre[0], ok_pre[1]); cmp("ok_prea", ok_prea[0], ok_prea[1]); cmp("ok_ref", ok_ref[0], ok_ref[1]);
        cmp("ok_bst", ok_bst[0], ok_bst[1]); cmp("init_done", init_done[0], init_done[1]);
        cmp("rvalid", rvalid[0], rvalid[1]); cmp("rdata", rdata[0], rdata[1]);
        cmp("phy_fault", phy_fault[0], phy_fault[1]); cmp("fault_count", fault_count[0], fault_count[1]);
        cmp("fault_cause", fault_cause[0], fault_cause[1]); cmp("row_aged", row_aged[0], row_aged[1]);
        cmp("preempt", preempt[0], preempt[1]); cmp("banks_closed", banks_closed[0], banks_closed[1]);
        cmp("ref_due", ref_due[0], ref_due[1]); cmp("ref_debt", ref_debt[0], ref_debt[1]);
        cmp("refreshes", refreshes[0], refreshes[1]); cmp("dq_conflicts", dq_conflicts[0], dq_conflicts[1]);
        cmp("rd_lat", rd_lat[0], rd_lat[1]);
        cmp("cal_window", cal_window[0], cal_window[1]); cmp("cal_best", cal_best[0], cal_best[1]);
        cmp("cal_done", cal_done[0], cal_done[1]); cmp("cal_fault", cal_fault[0], cal_fault[1]);
        cmp("bist_busy", bist_busy[0], bist_busy[1]); cmp("bist_done", bist_done[0], bist_done[1]);
        cmp("bist_errors", bist_errors[0], bist_errors[1]); cmp("bist_first", bist_first[0], bist_first[1]);
        cmp("perf", perf[0], perf[1]);
        cmp("pin_cke", pin_cke[0], pin_cke[1]);
        cmp("pin_cmd", {pin_ras[0], pin_cas[0], pin_we[0]}, {pin_ras[1], pin_cas[1], pin_we[1]});
        cmp("pin_dqm", pin_dqm[0], pin_dqm[1]); cmp("dq_o", dq_o[0], dq_o[1]); cmp("dq_oe", dq_oe[0], dq_oe[1]);
        pc0 = {pin_ras[0], pin_cas[0], pin_we[0]};
        if (pc0 == 3'b011 || pc0 == 3'b101 || pc0 == 3'b100 || pc0 == 3'b000) begin
            cmp("pin_a", pin_a[0], pin_a[1]); cmp("pin_ba", pin_ba[0], pin_ba[1]);
        end else if (pc0 == 3'b010) cmp("pin_a10", pin_a[0][10], pin_a[1][10]);
    end

    initial begin
        #(NCYC * 10000);
        if (errs == 0) $display("EQUIV PASS (%0d cycles, refreshes %0d, faults %0d, bist errors %0d, cal window %h)", cyc, refreshes[1], fault_count[1], bist_errors[1], cal_window[1]);
        else $display("EQUIV FAIL (%0d mismatches)", errs);
        $finish;
    end
endmodule
