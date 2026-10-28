// Directed test bench for src/fpga/core/tau_sdram_phy_core.sv / tau_sdram_phy.sv against sim/sdram_chip_model.sv.
// MIT licence. Clean-room: written from the contract (v0.3, 3 and 7.2) and the chip model's ports and documentation.
//
// Usage (see run_tests.sh):  vvp tb.vvp +TEST=<name>     one test per run; prints "TEST <name> PASS" or "TEST <name> FAIL".
// Model, board and PHY parameters are top-level parameters here (iverilog -Ptb_sdram_phy.NAME=value).
//
// Cycle convention: "cycle a" is the interval after controller posedge a. The scheduler-side inputs set in cycle a
// are registered by the PHY at posedge a+1 (the launch edge E); the pins change after E plus the output delay; the
// chip samples half a period later (chip clock = inverse of clk). rdata/rvalid are logged per cycle (value after the
// posedge that starts the cycle).
`timescale 1ps/1ps
`include "sdram_chip_err.vh"

module tb_sdram_phy #(
    parameter integer P_PS        = 10000,   // controller clock period
    parameter integer CLSEL       = 2,
    parameter integer CAPMODE     = 1,       // 0 rising, 1 falling
    parameter integer RDEX        = 0,       // RD_EXTRA
    parameter integer TOP         = 0,       // 1: instantiate tau_sdram_phy (tri-state, DDIO clock), no pin envelope
    parameter integer DMIN        = 0,
    parameter integer DMAX        = 0,
    parameter integer TAC2        = 8000,
    parameter integer TAC3        = 5000,
    parameter integer THZ2        = 8000,
    parameter integer THZ3        = 5000,
    parameter integer TOH         = 1800,
    parameter integer TLZ         = 1000,
    parameter integer TCO_MIN     = 1500,    // FPGA output delay (clock to pin), command/address/data
    parameter integer TCO_MAX     = 3500,
    parameter integer OE_TCO_MIN  = 1500,    // FPGA DQ output enable: on at MIN, off at MAX
    parameter integer OE_TCO_MAX  = 3500,
    parameter integer CAPSKEW     = 0,
    parameter integer FSU         = 0,
    parameter integer FH          = 0,
    parameter integer TRC_X       = 60000,   // tRC for both the PHY and the chip model
    parameter integer INITCYC     = 200,     // PHY INIT_WAIT_CYCLES (10000 for the real wait)
    parameter integer DEBTMAX     = 8,
    parameter integer REFIPS      = 7_800_000,
    parameter integer ROWFORCE    = 90000,
    parameter integer TMO         = 136,
    parameter longint TREFPS      = 64'd64_000_000_000
);
    // ------------------------------------------------------------------------------------------------------
    // clocks and reset
    // ------------------------------------------------------------------------------------------------------
    reg clk = 1'b0;
    always #(P_PS/2) clk = ~clk;
    reg rst = 1'b1;

    // ------------------------------------------------------------------------------------------------------
    // scheduler-side driver (set by the test tasks after a posedge, sampled by the PHY at the next posedge)
    // ------------------------------------------------------------------------------------------------------
    localparam [2:0] K_NOP = 3'd0, K_ACT = 3'd1, K_RD = 3'd2, K_WR = 3'd3, K_PRE = 3'd4, K_PREA = 3'd5, K_REF = 3'd6, K_BST = 3'd7;
    reg [2:0]  u_cmd = K_NOP;
    reg [1:0]  u_ba  = 2'b00;
    reg [12:0] u_a   = 13'd0;
    reg        u_wv  = 1'b0;
    reg [15:0] u_wd  = 16'd0;
    reg [1:0]  u_be  = 2'b11;
    reg        u_ack = 1'b0;

    wire [3:0] ok_act, ok_rd, ok_wr, ok_pre;
    wire       ok_prea, ok_ref, ok_bst, init_done, rvalid, phy_fault, preempt, banks_closed, ref_due;
    wire [15:0] rdata, fault_count, dq_conflicts;
    wire [3:0] fault_cause, row_aged, ref_debt;
    wire [31:0] refreshes;
    wire [4:0] rd_lat;

    // ------------------------------------------------------------------------------------------------------
    // PHY + pins + chip model
    // ------------------------------------------------------------------------------------------------------
    wire        c_ras, c_cas, c_we, c_cke, c_clk;
    wire [1:0]  c_dqm, c_ba;
    wire [12:0] c_a;
    wire [15:0] c_oe;
    wire [15:0] dram_dq;                 // the shared bus (chip drives it, the PHY drives it through the envelope)

    // envelope registers (TOP = 0)
    reg        p_ras = 1'b1, p_cas = 1'b1, p_we = 1'b1, p_cke = 1'b1;
    reg [1:0]  p_ba = 2'b00, p_dqm = 2'b11;
    reg [12:0] p_a = 13'd0;
    reg [15:0] p_dq = 16'd0;
    reg [15:0] p_oe = 16'd0;

    localparam integer PH_PARAMS_OK = 1;

    generate
        if (TOP == 0) begin : g_core
            wire        w_ras, w_cas, w_we, w_cke;
            wire [1:0]  w_dqm, w_ba;
            wire [12:0] w_a;
            wire [15:0] w_dq_o, w_dq_oe;
            tau_sdram_phy_core #(
                .CLK_HZ((1_000_000_000 / P_PS) * 1000), .CL(CLSEL), .RD_EXTRA(RDEX), .CAP_MODE(CAPMODE),
                .TRC_PS(TRC_X), .INIT_WAIT_CYCLES(INITCYC), .SIM_SHORT_INIT(INITCYC < 10000),
                .REFRESH_DEBT_MAX(DEBTMAX), .REFRESH_INTERVAL_PS(REFIPS), .ROW_FORCE_NS(ROWFORCE), .PREEMPT_TIMEOUT(TMO)
            ) dut (
                .clk(clk), .rst(rst), .cmd(u_cmd), .cmd_ba(u_ba), .cmd_a(u_a), .wvalid(u_wv), .wdata(u_wd), .be(u_be),
                .ok_act(ok_act), .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_prea(ok_prea), .ok_ref(ok_ref),
                .ok_bst(ok_bst), .init_done(init_done), .rvalid(rvalid), .rdata(rdata), .phy_fault(phy_fault),
                .fault_count(fault_count), .fault_cause(fault_cause), .row_aged(row_aged), .preempt(preempt),
                .preempt_ack(u_ack), .banks_closed(banks_closed), .ref_due(ref_due), .ref_debt(ref_debt),
                .refreshes(refreshes), .dq_conflicts(dq_conflicts), .rd_lat(rd_lat),
                .pin_cke(w_cke), .pin_ras_n(w_ras), .pin_cas_n(w_cas), .pin_we_n(w_we), .pin_dqm(w_dqm),
                .pin_ba(w_ba), .pin_a(w_a), .dq_o(w_dq_o), .dq_oe(w_dq_oe), .dq_i(dram_dq)
            );
            // pin envelope: X from tco_min, final value from tco_max (sampled just after the launching edge)
            reg [15:0] s_dq, s_oe;
            always @(posedge clk) begin
                #1;
                s_dq = w_dq_o; s_oe = w_dq_oe;
                #(TCO_MIN - 1);
                p_ras = 1'bx; p_cas = 1'bx; p_we = 1'bx; p_ba = 2'bxx; p_a = 13'bx; p_dqm = 2'bxx; p_cke = 1'bx;
                p_dq = 16'bx;
                #(TCO_MAX - TCO_MIN);
                p_ras = w_ras; p_cas = w_cas; p_we = w_we; p_ba = w_ba; p_a = w_a; p_dqm = w_dqm; p_cke = w_cke;
                p_dq = s_dq;
            end
            always @(posedge clk) begin
                #1;
                #(OE_TCO_MIN - 1);
                p_oe = p_oe | w_dq_oe;               // turning on: earliest
                #(OE_TCO_MAX - OE_TCO_MIN);
                p_oe = p_oe & w_dq_oe;               // turning off: latest
            end
            genvar gi;
            for (gi = 0; gi < 16; gi = gi + 1) begin : g_dq
                assign dram_dq[gi] = p_oe[gi] ? p_dq[gi] : 1'bz;
            end
            assign c_ras = p_ras; assign c_cas = p_cas; assign c_we = p_we; assign c_cke = p_cke;
            assign c_dqm = p_dqm; assign c_ba = p_ba; assign c_a = p_a; assign c_oe = p_oe;
            assign #(0) c_clk = ~clk;
        end else begin : g_top
            tau_sdram_phy #(
                .CLK_HZ((1_000_000_000 / P_PS) * 1000), .CL(CLSEL), .RD_EXTRA(RDEX), .CAP_MODE(CAPMODE),
                .TRC_PS(TRC_X), .INIT_WAIT_CYCLES(INITCYC), .SIM_SHORT_INIT(INITCYC < 10000),
                .REFRESH_DEBT_MAX(DEBTMAX), .REFRESH_INTERVAL_PS(REFIPS), .ROW_FORCE_NS(ROWFORCE), .PREEMPT_TIMEOUT(TMO)
            ) dut (
                .clk(clk), .rst(rst), .cmd(u_cmd), .cmd_ba(u_ba), .cmd_a(u_a), .wvalid(u_wv), .wdata(u_wd), .be(u_be),
                .ok_act(ok_act), .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_prea(ok_prea), .ok_ref(ok_ref),
                .ok_bst(ok_bst), .init_done(init_done), .rvalid(rvalid), .rdata(rdata), .phy_fault(phy_fault),
                .fault_count(fault_count), .fault_cause(fault_cause), .row_aged(row_aged), .preempt(preempt),
                .preempt_ack(u_ack), .banks_closed(banks_closed), .ref_due(ref_due), .ref_debt(ref_debt),
                .refreshes(refreshes), .dq_conflicts(dq_conflicts), .rd_lat(rd_lat),
                .dram_a(c_a), .dram_ba(c_ba), .dram_dq(dram_dq), .dram_dqm(c_dqm), .dram_clk(c_clk),
                .dram_cke(c_cke), .dram_ras_n(c_ras), .dram_cas_n(c_cas), .dram_we_n(c_we)
            );
            assign c_oe = dut.u_core.dq_oe;          // zero-delay output enable (analysis only)
        end
    endgenerate

    sdram_chip_model #(
        .TAC_CL2_PS(TAC2), .TAC_CL3_PS(TAC3), .THZ_CL2_PS(THZ2), .THZ_CL3_PS(THZ3), .TOH_PS(TOH), .TLZ_PS(TLZ),
        .TRC_PS(TRC_X), .TREF_PS(TREFPS), .INIT_NOP_MIN(INITCYC), .INIT_WAIT_PS(INITCYC * P_PS),
        .DMIN_PS(DMIN), .DMAX_PS(DMAX), .CAP_MODE(CAPMODE), .CAP_SKEW_PS(CAPSKEW), .FPGA_SU_PS(FSU), .FPGA_H_PS(FH),
        .CL_DEFAULT(CLSEL)
    ) chip (
        .dram_clk(c_clk), .dram_cke(c_cke), .dram_ras_n(c_ras), .dram_cas_n(c_cas), .dram_we_n(c_we),
        .dram_dqm(c_dqm), .dram_ba(c_ba), .dram_a(c_a), .dram_dq(dram_dq), .fpga_oe(c_oe), .cap_clk(clk)
    );

    // ------------------------------------------------------------------------------------------------------
    // per-cycle log of the read outputs (value visible in cycle j = after posedge j)
    // ------------------------------------------------------------------------------------------------------
    reg [15:0] rd_vis [0:8191];
    reg        rv_vis [0:8191];
    integer    cycn;
    initial cycn = 0;
    always @(posedge clk) begin
        cycn = cycn + 1;
        #(P_PS/2 - 1);
        rd_vis[cycn & 8191] = rdata;
        rv_vis[cycn & 8191] = rvalid;
    end

    // ------------------------------------------------------------------------------------------------------
    // test tasks: each task occupies exactly one cycle
    // ------------------------------------------------------------------------------------------------------
    integer tb_fail = 0;
    integer a_cyc;                 // cycle index of the last presented command

    task cyc(input [2:0] c, input [1:0] ba, input [12:0] a, input wv, input [15:0] wd, input [1:0] bem);
        begin
            u_cmd = c; u_ba = ba; u_a = a; u_wv = wv; u_wd = wd; u_be = bem;
            a_cyc = cycn;
            @(posedge clk);
            #1;
        end
    endtask
    task nop(input integer n);
        integer k;
        begin for (k = 0; k < n; k = k + 1) cyc(K_NOP, 2'b00, 13'd0, 1'b0, 16'd0, 2'b11); end
    endtask
    task act(input [1:0] b, input [12:0] row);   cyc(K_ACT, b, row, 1'b0, 16'd0, 2'b11); endtask
    integer rd_a;
    task rd (input [1:0] b, input [9:0] col);    begin cyc(K_RD,  b, {3'b000, col}, 1'b0, 16'd0, 2'b11); rd_a = a_cyc; end endtask
    task rda(input [1:0] b, input [9:0] col);    cyc(K_RD,  b, {2'b00, 1'b1, col}, 1'b0, 16'd0, 2'b11); endtask    // A10 = 1 (illegal)
    task wr (input [1:0] b, input [9:0] col, input [15:0] d, input [1:0] bem); cyc(K_WR, b, {3'b000, col}, 1'b1, d, bem); endtask
    task wra(input [1:0] b, input [9:0] col, input [15:0] d); cyc(K_WR, b, {2'b00, 1'b1, col}, 1'b1, d, 2'b11); endtask
    task wb (input [15:0] d, input [1:0] bem);   cyc(K_NOP, 2'b00, 13'd0, 1'b1, d, bem); endtask    // write data beat
    task wgap;                                   cyc(K_NOP, 2'b00, 13'd0, 1'b0, 16'hdead, 2'b00); endtask  // wvalid 0: nothing written
    task pre(input [1:0] b);                     cyc(K_PRE, b, 13'd0, 1'b0, 16'd0, 2'b11); endtask
    task prea;                                   cyc(K_PREA, 2'b00, 13'd0, 1'b0, 16'd0, 2'b11); endtask
    task refr;                                   cyc(K_REF, 2'b00, 13'd0, 1'b0, 16'd0, 2'b11); endtask
    task bst;                                    cyc(K_BST, 2'b00, 13'd0, 1'b0, 16'd0, 2'b11); endtask

    // wait for init_done (the PHY runs the init by itself); the scheduler offers NOP
    task wait_init;
        integer k;
        begin
            k = 0;
            while (!init_done && k < 40000) begin nop(1); k = k + 1; end
            if (!init_done) begin $display("  init_done never rose"); tb_fail = tb_fail + 1; end
            nop(1);
        end
    endtask
    task do_reset(input integer n);
        integer k;
        begin
            rst = 1'b1;
            for (k = 0; k < n; k = k + 1) nop(1);
            rst = 1'b0;
        end
    endtask

    // ------------------------------------------------------------------------------------------------------
    // checks
    // ------------------------------------------------------------------------------------------------------
    reg [8*40-1:0] tname;
    task check(input cond, input [8*56-1:0] msg);
        begin if (!cond) begin $display("  CHECK FAILED: %0s", msg); tb_fail = tb_fail + 1; end end
    endtask
    task expect_clean;                       // no chip error, no PHY fault, no DQ conflict
        begin
            nop(4);
            if (chip.err_count != 0) begin
                $display("  expected no chip errors, got %0d (first code %0d, mask %h)", chip.err_count, chip.err_first, chip.err_mask);
                tb_fail = tb_fail + 1;
            end
            if (phy_fault !== 1'b0 || fault_count !== 16'd0) begin
                $display("  expected no PHY fault, got fault=%b count=%0d cause=%0d", phy_fault, fault_count, fault_cause);
                tb_fail = tb_fail + 1;
            end
            if (dq_conflicts !== 16'd0) begin $display("  PHY dq_conflicts = %0d", dq_conflicts); tb_fail = tb_fail + 1; end
        end
    endtask
    task finish_test;
        begin
            nop(4);
            if (tb_fail == 0) $display("TEST %0s PASS", tname); else $display("TEST %0s FAIL (%0d)", tname, tb_fail);
            $finish;
        end
    endtask

    // ------------------------------------------------------------------------------------------------------
    // power-up: hold reset for a few cycles, then let the PHY run its init by itself
    // ------------------------------------------------------------------------------------------------------
    task powerup;
        begin
            @(posedge clk); #1;
            nop(3);
            rst = 1'b0;
            wait_init;
        end
    endtask

    // ------------------------------------------------------------------------------------------------------
    // tests
    // ------------------------------------------------------------------------------------------------------
    integer ti;
    initial begin
        if (!$value$plusargs("TEST=%s", tname)) tname = "init";
        // ---- init ----
        if (tname == "init") begin
            powerup;
            check(init_done, "init_done");
            check(chip.init_done, "chip init_done");
            check(chip.mr_cl == CLSEL, "chip CL");
            check(chip.refreshes == 2, "two init refreshes");
            check(rd_lat == ((CAPMODE == 1) ? CLSEL + 2 : CLSEL + 1) + RDEX, "rd_lat");
            expect_clean;
        end else begin
            $display("unknown test %0s", tname); tb_fail = tb_fail + 1;
        end
        finish_test;
    end
endmodule
