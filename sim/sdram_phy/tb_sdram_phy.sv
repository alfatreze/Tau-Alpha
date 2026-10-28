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
    parameter integer NRAND       = 4000,    // cycles of the random driver
    parameter integer RDCHK       = 1,       // 1: the random driver checks rvalid/rdata (needs the read path)
    parameter longint TREFPS      = 64'd64_000_000_000
);
    localparam integer RD_LAT_X = ((CAPMODE == 1) ? CLSEL + 2 : CLSEL + 1) + RDEX;
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
    task wgap;                                   cyc(K_NOP, 2'b00, 13'd0, 1'b0, 16'hdead, 2'b11); endtask  // wvalid 0 (be 11 on purpose): nothing written
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
    // helpers for the negative (illegal-offer) tests
    // ------------------------------------------------------------------------------------------------------
    // after an illegal offer: exactly n faults, first cause c, the chip saw nothing wrong, no DQ conflict
    task expect_fault(input integer n, input integer c);
        begin
            nop(4);
            if (fault_count !== n[15:0]) begin $display("  expected fault_count %0d, got %0d", n, fault_count); tb_fail = tb_fail + 1; end
            if (c >= 0 && fault_cause !== c[3:0]) begin $display("  expected fault_cause %0d, got %0d", c, fault_cause); tb_fail = tb_fail + 1; end
            if ((n != 0) != (phy_fault === 1'b1)) begin $display("  phy_fault = %b", phy_fault); tb_fail = tb_fail + 1; end
            if (chip.err_count != 0) begin
                $display("  the illegal offer reached the chip: %0d chip errors (first code %0d, mask %h)", chip.err_count, chip.err_first, chip.err_mask);
                tb_fail = tb_fail + 1;
            end
            if (dq_conflicts !== 16'd0) begin $display("  PHY dq_conflicts = %0d", dq_conflicts); tb_fail = tb_fail + 1; end
        end
    endtask

    // ------------------------------------------------------------------------------------------------------
    // random legal driver (offers only what the permits allow) with a shadow memory and a read checker
    // ------------------------------------------------------------------------------------------------------
    reg [15:0] sh_d [0:1023];
    reg [1:0]  sh_v [0:1023];
    reg        b_open [0:3];
    reg [12:0] b_row  [0:3];
    reg        w_act;  reg [1:0] w_bank; reg [12:0] w_row; integer w_col;
    reg        r_act;  reg [1:0] r_bank; reg [12:0] r_row; integer r_col;
    integer    seed = 1;
    integer    n_rd_checked, n_rd_bad, n_rd_words;
    reg        exp_v [0:8191];
    reg [15:0] exp_d [0:8191];
    reg [1:0]  exp_m [0:8191];
    integer    ncmd [0:7];
    integer    rdchk_on;

    function integer shi(input [1:0] b, input [12:0] r, input integer c);
        begin shi = {b, r[1:0], c[5:0]}; end
    endfunction
    function integer urand(input integer m);
        integer v;
        begin v = $random(seed); if (v < 0) v = -v; urand = v % m; end
    endfunction

    task rnd_init;
        integer k;
        begin
            for (k = 0; k < 1024; k = k + 1) begin sh_v[k] = 2'b00; sh_d[k] = 16'h0; end
            for (k = 0; k < 8192; k = k + 1) begin exp_v[k] = 1'b0; exp_d[k] = 16'h0; exp_m[k] = 2'b00; end
            for (k = 0; k < 4; k = k + 1) begin b_open[k] = 1'b0; b_row[k] = 13'd0; end
            for (k = 0; k < 8; k = k + 1) ncmd[k] = 0;
            w_act = 1'b0; r_act = 1'b0; n_rd_checked = 0; n_rd_bad = 0; n_rd_words = 0; rdchk_on = 1;
        end
    endtask

    task sh_write(input [1:0] b, input [12:0] r, input integer col, input [15:0] d, input [1:0] bem);
        integer ix;
        begin
            ix = shi(b, r, col);
            if (bem[0]) begin sh_d[ix][7:0] = d[7:0]; sh_v[ix][0] = 1'b1; end
            if (bem[1]) begin sh_d[ix][15:8] = d[15:8]; sh_v[ix][1] = 1'b1; end
        end
    endtask

    // check the read outputs of the cycle that has just completed (a_cyc): rvalid must match the expectation exactly
    task rd_check;
        integer j; reg [15:0] g;
        begin
            j = a_cyc & 8191;
            if (rdchk_on != 0) begin
                if (rv_vis[j] !== exp_v[j]) begin
                    if (n_rd_bad < 5) $display("  rvalid mismatch in cycle %0d: got %b expected %b", a_cyc, rv_vis[j], exp_v[j]);
                    n_rd_bad = n_rd_bad + 1;
                end else if (exp_v[j]) begin
                    g = rd_vis[j];
                    n_rd_checked = n_rd_checked + 1;
                    if ((exp_m[j][0] && g[7:0] !== exp_d[j][7:0]) || (exp_m[j][1] && g[15:8] !== exp_d[j][15:8])) begin
                        if (n_rd_bad < 5) $display("  read data mismatch in cycle %0d: got %h expected %h (lanes %b)", a_cyc, g, exp_d[j], exp_m[j]);
                        n_rd_bad = n_rd_bad + 1;
                    end
                end
            end
            exp_v[j] = 1'b0;                 // slot reused by a later cycle
        end
    endtask

    // one cycle of the random driver. pw / pr / pref enable writes, reads and refreshes.
    reg [2:0]  d_cmd; reg [1:0] d_ba; reg [12:0] d_a; reg d_wv; reg [15:0] d_wd; reg [1:0] d_be;
    reg        t_rd, t_wr, got;
    integer    pick, bsel, ix2;
    task rnd_step(input integer pw, input integer pr, input integer pref);
        begin
            d_cmd = K_NOP; d_ba = 2'b00; d_a = 13'd0; d_wv = 1'b0; d_wd = 16'h0; d_be = 2'b11;
            if (w_act) begin
                d_wv = (urand(8) != 0); d_wd = urand(65536); d_be = (urand(6) == 0) ? urand(4) : 2'b11;
            end
            pick = urand(16); bsel = urand(4); got = 1'b0;
            case (pick)
                0, 1, 2: if (ok_act[bsel]) begin d_cmd = K_ACT; d_ba = bsel; d_a = urand(4); end
                3, 4, 5: if (pr != 0 && ok_rd[bsel]) begin d_cmd = K_RD; d_ba = bsel; d_a = urand(8); end
                6, 7, 8: if (pw != 0 && ok_wr[bsel]) begin d_cmd = K_WR; d_ba = bsel; d_a = urand(8); end
                9:  if (ok_pre[bsel]) begin d_cmd = K_PRE; d_ba = bsel; end
                10: if (ok_prea) begin d_cmd = K_PREA; end
                11: if (pref != 0 && ok_ref) begin d_cmd = K_REF; end
                12, 13: if (ok_bst) begin d_cmd = K_BST; end
                default: ;
            endcase
            if ((w_act && w_col >= 56) || (r_act && r_col >= 56)) begin
                if (ok_bst) begin d_cmd = K_BST; d_ba = 2'b00; d_a = 13'd0; end
            end
            // the refresh duty of a real scheduler: when the debt is high, end bursts, close the banks, refresh
            if (pref != 0 && ref_debt >= 4'd3) begin
                if (ok_bst) begin d_cmd = K_BST; d_ba = 2'b00; d_a = 13'd0; end
                else if ((b_open[0] || b_open[1] || b_open[2] || b_open[3]) && ok_prea) begin d_cmd = K_PREA; d_ba = 2'b00; d_a = 13'd0; end
                else if (!(b_open[0] || b_open[1] || b_open[2] || b_open[3]) && ok_ref) begin d_cmd = K_REF; d_ba = 2'b00; d_a = 13'd0; end
                else begin d_cmd = K_NOP; d_ba = 2'b00; d_a = 13'd0; end
            end
            if (d_cmd == K_WR) begin d_wv = 1'b1; d_wd = urand(65536); d_be = (urand(6) == 0) ? urand(4) : 2'b11; end
            // does this cycle end the read / write burst that is open?
            t_rd = r_act && (d_cmd == K_BST || d_cmd == K_PREA || d_cmd == K_RD || d_cmd == K_WR || (d_cmd == K_PRE && d_ba == r_bank));
            t_wr = w_act && (d_cmd == K_BST || d_cmd == K_PREA || d_cmd == K_RD || d_cmd == K_WR || (d_cmd == K_PRE && d_ba == w_bank));
            // a data beat of the open write burst is written unless this cycle ends it (a new WR starts its own)
            if (w_act && !t_wr && d_wv && d_be != 2'b00) begin
                sh_write(w_bank, w_row, w_col, d_wd, d_be);
            end else if (w_act && !t_wr) begin
                d_wv = d_wv;     // masked or invalid beat: nothing written
            end
            if (w_act && !t_wr) w_col = w_col + 1;
            if (t_wr) w_act = 1'b0;
            // a word of the open read burst is requested this cycle unless the cycle ends it
            if (r_act && !t_rd) begin
                ix2 = shi(r_bank, r_row, r_col);
                exp_v[(cycn + RD_LAT_X) & 8191] = 1'b1; exp_d[(cycn + RD_LAT_X) & 8191] = sh_d[ix2];
                exp_m[(cycn + RD_LAT_X) & 8191] = sh_v[ix2]; n_rd_words = n_rd_words + 1;
                r_col = r_col + 1;
            end
            if (t_rd) r_act = 1'b0;
            if (d_cmd == K_WR) begin
                w_act = 1'b1; w_bank = d_ba; w_row = b_row[d_ba]; w_col = d_a[5:0];
                sh_write(w_bank, w_row, w_col, d_wd, d_be); w_col = w_col + 1;
            end
            if (d_cmd == K_RD) begin
                r_act = 1'b1; r_bank = d_ba; r_row = b_row[d_ba]; r_col = d_a[5:0];
                ix2 = shi(r_bank, r_row, r_col);
                exp_v[(cycn + RD_LAT_X) & 8191] = 1'b1; exp_d[(cycn + RD_LAT_X) & 8191] = sh_d[ix2];
                exp_m[(cycn + RD_LAT_X) & 8191] = sh_v[ix2]; n_rd_words = n_rd_words + 1;
                r_col = r_col + 1;
            end
            if (d_cmd == K_ACT) begin b_open[d_ba] = 1'b1; b_row[d_ba] = d_a; end
            if (d_cmd == K_PRE) b_open[d_ba] = 1'b0;
            if (d_cmd == K_PREA) begin b_open[0] = 1'b0; b_open[1] = 1'b0; b_open[2] = 1'b0; b_open[3] = 1'b0; end
            ncmd[d_cmd] = ncmd[d_cmd] + 1;
            cyc(d_cmd, d_ba, d_a, d_wv, d_wd, d_be);
            rd_check;
        end
    endtask

    // compare the chip's memory with the shadow memory
    integer mem_bad;
    task mem_compare;
        integer k, bb, rr, cc; reg [15:0] w;
        begin
            mem_bad = 0;
            for (k = 0; k < 1024; k = k + 1) begin
                bb = k >> 8; rr = (k >> 6) & 3; cc = k & 63;
                if (sh_v[k] != 2'b00) begin
                    w = chip.mem_rd(bb, rr, cc, 2'b11);
                    if ((sh_v[k][0] && w[7:0] !== sh_d[k][7:0]) || (sh_v[k][1] && w[15:8] !== sh_d[k][15:8])) begin
                        if (mem_bad < 5) $display("  chip memory mismatch b%0d r%0d c%0d: chip %h shadow %h (lanes %b)", bb, rr, cc, w, sh_d[k], sh_v[k]);
                        mem_bad = mem_bad + 1;
                    end
                end
            end
            if (mem_bad != 0) tb_fail = tb_fail + 1;
        end
    endtask

    // ------------------------------------------------------------------------------------------------------
    // tests
    // ------------------------------------------------------------------------------------------------------
    // pattern data (as in the chip model's tests)
    function [15:0] pat(input [1:0] b, input [12:0] r, input [9:0] c);
        begin pat = {b, r[3:0], c[9:0]} ^ 16'h5a3c; end
    endfunction
    // write n pattern words (WR + n-1 data beats); the burst stays open
    task wr_pat(input [1:0] b, input [12:0] r, input [9:0] c0, input integer n);
        integer k;
        begin
            wr(b, c0, pat(b, r, c0), 2'b11);
            for (k = 1; k < n; k = k + 1) wb(pat(b, r, c0 + k), 2'b11);
        end
    endtask
    integer a_chain, a_first, ev_n, k_bc;
    integer ev_cyc [0:7];
    reg [3:0] dprev;
    // the n words of a read burst offered in cycle a0 must be on rdata with rvalid in cycles a0 + RD_LAT + k
    task check_burst(input integer a0, input integer n, input [1:0] b, input [12:0] r, input [9:0] c0);
        integer k, j;
        begin
            for (k = 0; k < n; k = k + 1) begin
                j = (a0 + RD_LAT_X + k) & 8191;
                if (rv_vis[j] !== 1'b1) begin $display("  rvalid low for word %0d (cycle %0d)", k, a0 + RD_LAT_X + k); tb_fail = tb_fail + 1; end
                else if (rd_vis[j] !== pat(b, r, c0 + k)) begin
                    $display("  word %0d: got %h expected %h", k, rd_vis[j], pat(b, r, c0 + k)); tb_fail = tb_fail + 1;
                end
            end
        end
    endtask
    // rvalid is high exactly for n cycles starting at a0 + RD_LAT and low in the 6 cycles around
    task check_one_valid_run(input integer a0, input integer n);
        integer k, j, cnt;
        begin
            cnt = 0;
            for (k = -3; k < RD_LAT_X + n + 6; k = k + 1) begin
                j = (a0 + k) & 8191;
                if (rv_vis[j] === 1'b1) begin
                    cnt = cnt + 1;
                    if (k < RD_LAT_X || k >= RD_LAT_X + n) begin $display("  stray rvalid at offset %0d", k); tb_fail = tb_fail + 1; end
                end
            end
            if (cnt != n) begin $display("  rvalid high in %0d cycles, expected %0d", cnt, n); tb_fail = tb_fail + 1; end
        end
    endtask

    function [16:0] pkf(input integer b, input integer r, input integer c);
        begin pkf = chip.peek(b, r, c); end
    endfunction
    function [15:0] mrd(input integer b, input integer r, input integer c);
        begin mrd = chip.mem_rd(b, r, c, 2'b11); end
    endfunction
    integer ti = 0;
    integer nrand;
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
            check(refreshes == 2, "PHY refresh counter after init");
            expect_clean;
        // ---- legal commands at the minimum spacing: every permit must be open exactly in time, the chip stays silent ----
        end else if (tname == "legal_min") begin
            powerup;
            act(0, 13'd5);                                  // c0
            nop(1);                                         // c1
            wr(0, 10'd0, 16'h1111, 2'b11);                  // c2   tRCD 2
            cyc(K_ACT, 2'd1, 13'd6, 1'b1, 16'h2222, 2'b11); // c3   tRRD, write beat rides with the ACT
            wb(16'h3333, 2'b11);                            // c4
            wb(16'h4444, 2'b11);                            // c5
            bst;                                            // c6   BST at w+n
            pre(0);                                         // c7   last word c5 + 2
            nop(1);                                         // c8
            act(0, 13'd7);                                  // c9   tRP, tRC, tDAL (c5 + 4)
            wr(1, 10'd0, 16'h5555, 2'b11);                  // c10  bank 1 opened at c3
            wb(16'h6666, 2'b01);                            // c11  low byte only
            bst;                                            // c12
            nop(1);                                         // c13
            prea;                                           // c14  bank 0: tRAS from c9 = c14, bank 1: last word c11 + 2 = c13
            nop(1);                                         // c15
            refr;                                           // c16  tRP 2, tRC from c9
            nop(9);                                         // c17..c25
            act(2, 13'd9);                                  // c26  tRFC 10
            nop(1);
            wr(2, 10'd3, 16'h7777, 2'b10);
            bst;
            expect_clean;
            check(pkf(0, 5, 0) == 17'h11111, "b0 r5 c0");
            check(pkf(0, 5, 1) == 17'h12222, "b0 r5 c1 (the beat that rides with the ACT)");
            check(pkf(0, 5, 2) == 17'h13333, "b0 r5 c2");
            check(pkf(0, 5, 3) == 17'h14444, "b0 r5 c3");
            check(pkf(1, 6, 0) == 17'h15555, "b1 r6 c0");
            check(mrd(1, 6, 1) === 16'hxx66, "b1 r6 c1 low byte only");
            check(mrd(2, 9, 3) === 16'h77xx, "b2 r9 c3 high byte only");
            check(refreshes == 3, "refresh counter");
        // ---- silent BST ----
        end else if (tname == "silent_bst") begin
            powerup;
            bst; nop(2); bst;
            expect_fault(0, -1);
        // ---- illegal offers: each becomes NOP + fault and never reaches the chip ----
        end else if (tname == "n_act_open") begin
            powerup; act(0, 13'd5); nop(7); act(0, 13'd6); expect_fault(1, 1);
        end else if (tname == "n_rd_closed") begin
            powerup; rd(0, 10'd0); expect_fault(1, 2);
        end else if (tname == "n_wr_closed") begin
            powerup; wr(0, 10'd0, 16'h1234, 2'b11); expect_fault(1, 3);
        end else if (tname == "n_a10_rd") begin
            powerup; act(0, 13'd5); nop(2); rda(0, 10'd0); expect_fault(1, 11);
        end else if (tname == "n_a10_wr") begin
            powerup; act(0, 13'd5); nop(2); wra(0, 10'd0, 16'h1234); expect_fault(1, 11);
        end else if (tname == "n_trcd") begin
            powerup; act(0, 13'd5); rd(0, 10'd0); rd(0, 10'd0); nop(2); bst; expect_fault(1, 2);
        end else if (tname == "n_tras") begin
            powerup; act(0, 13'd5); nop(3); pre(0); pre(0); expect_fault(1, 4);
        end else if (tname == "n_trp") begin
            powerup; act(0, 13'd5); nop(4); pre(0); act(0, 13'd6); act(0, 13'd6); expect_fault(1, 1);
        end else if (tname == "n_trc") begin                 // run with TRC_X = 80000 (tRC 8 clocks, above tRAS + tRP)
            powerup; act(0, 13'd5); nop(4); pre(0); nop(1); act(0, 13'd6); act(0, 13'd6); expect_fault(1, 1);
        end else if (tname == "n_trrd") begin
            powerup; act(0, 13'd5); act(1, 13'd6); act(1, 13'd6); expect_fault(1, 1);
        end else if (tname == "n_ref_open") begin
            powerup; act(0, 13'd5); nop(7); refr; expect_fault(1, 6);
        end else if (tname == "n_trfc_act") begin
            powerup; refr; nop(8); act(0, 13'd5); act(0, 13'd5); expect_fault(1, 1);
        end else if (tname == "n_trfc_ref") begin
            powerup; refr; nop(8); refr; refr; expect_fault(1, 6);
        end else if (tname == "n_wr_in_read") begin
            powerup; act(0, 13'd5); nop(1); rd(0, 10'd0); nop(2); wr(0, 10'd4, 16'h4321, 2'b11); bst; expect_fault(1, 3);
        end else if (tname == "legal_rd_ref") begin
            powerup; act(0, 13'd5); nop(1); rd(0, 10'd0); nop(2); bst; pre(0); nop(1); refr; nop(10); act(0, 13'd5); nop(1); expect_fault(0, -1);
        end else if (tname == "n_turn") begin                 // RD, BST at r+4, WR at BST+2 (illegal) then BST+3 (legal)
            powerup; act(0, 13'd5); nop(1); rd(0, 10'd0); nop(3); bst; nop(1);
            wr(0, 10'd200, 16'h7E57, 2'b11); wr(0, 10'd200, 16'h7E57, 2'b11); wb(16'h7E58, 2'b11); bst;
            expect_fault(1, 3);
            check(pkf(0, 5, 200) == 17'h17E57, "write after the turnaround landed");
        end else if (tname == "n_tdpl") begin
            powerup; act(0, 13'd5); nop(1);
            wr(0, 10'd0, 16'h0a0a, 2'b11); wb(16'h0b0b, 2'b11); wb(16'h0c0c, 2'b11); wb(16'h0d0d, 2'b11);
            pre(0); pre(0);
            expect_fault(1, 4);
            check(pkf(0, 5, 3) == 17'h10d0d, "last word before the PRE");
            check(pkf(0, 5, 4) === 17'h0xxxx || pkf(0, 5, 4) === 17'h0xxxx, "no word after");
        end else if (tname == "n_prea_young") begin
            powerup; act(0, 13'd5); nop(1); act(1, 13'd6); nop(1); prea; nop(2); prea; expect_fault(1, 5);
        end else if (tname == "n_pre_closed") begin
            powerup; pre(1); expect_fault(1, 4);
        end else if (tname == "n_init_offer") begin
            @(posedge clk); #1; nop(3); rst = 1'b0; nop(5); act(0, 13'd1); wait_init; expect_fault(1, 12);
        end else if (tname == "n_wrap_rd") begin
            powerup; act(0, 13'd5); nop(1); rd(0, 10'd1020); nop(3); nop(1); expect_fault(1, 8);
        end else if (tname == "n_wrap_wr") begin
            powerup; act(0, 13'd5); nop(1); wr(0, 10'd1022, 16'haaaa, 2'b11); wb(16'hbbbb, 2'b11); wb(16'hcccc, 2'b11);
            expect_fault(1, 8);
            check(pkf(0, 5, 1023) == 17'h1bbbb, "last word below the wrap");
            check(pkf(0, 5, 1022) == 17'h1aaaa, "first word");
        end else if (tname == "fault_sticky") begin
            powerup; pre(1); nop(5);
            check(phy_fault === 1'b1, "fault set");
            do_reset(3); wait_init;
            check(phy_fault === 1'b0 && fault_count === 16'd0 && fault_cause === 4'd0, "reset clears the fault");
        // ---- read path: latency, rvalid shape, data ----
        end else if (tname == "rd_data") begin
            powerup;
            act(0, 13'd5); nop(1);
            wr_pat(0, 13'd5, 10'd0, 16);
            bst; nop(2);
            rd(0, 10'd4); nop(7); bst; nop(RD_LAT_X + 4);
            check(rd_lat == RD_LAT_X, "rd_lat port");
            check_burst(rd_a, 8, 0, 13'd5, 10'd4);
            check_one_valid_run(rd_a, 8);
            expect_clean;
            $display("  RD_LAT = %0d (CL %0d, CAP_MODE %0d, RD_EXTRA %0d)", rd_lat, CLSEL, CAPMODE, RDEX);
        end else if (tname == "rd_chain") begin       // RD at r+n ends the first burst after exactly n words, no gap
            powerup;
            act(0, 13'd5); nop(1);
            wr_pat(0, 13'd5, 10'd0, 16);
            bst; nop(2);
            rd(0, 10'd0); a_first = rd_a; nop(3); rd(0, 10'd8); a_chain = a_cyc; nop(3); bst; nop(RD_LAT_X + 4);
            check_burst(a_first, 4, 0, 13'd5, 10'd0);
            check_burst(a_chain, 4, 0, 13'd5, 10'd8);
            check_one_valid_run(a_first, 8);
            expect_clean;
        end else if (tname == "wr_gap") begin         // wvalid = 0 cycles write nothing
            powerup;
            act(0, 13'd5); nop(1);
            wr(0, 10'd0, 16'h1000, 2'b11); wgap; wb(16'h1002, 2'b11); wgap; wb(16'h1004, 2'b11); wb(16'h1005, 2'b01); wgap;
            bst; nop(2);
            check(pkf(0, 5, 0) == 17'h11000, "col 0");
            check(pkf(0, 5, 1) === 17'h0xxxx, "col 1 (gap) untouched");
            check(pkf(0, 5, 2) == 17'h11002, "col 2");
            check(pkf(0, 5, 3) === 17'h0xxxx, "col 3 (gap) untouched");
            check(pkf(0, 5, 4) == 17'h11004, "col 4");
            check(mrd(0, 5, 5) === 16'hxx05, "col 5 low byte only");
            check(pkf(0, 5, 6) === 17'h0xxxx, "col 6 (gap) untouched");
            expect_clean;
        end else if (tname == "n_rise_worst") begin  // RISE capture at the worst-case tAC: the chip model must flag it (documented limit)
            powerup;
            act(0, 13'd5); nop(1);
            wr_pat(0, 13'd5, 10'd0, 16);
            bst; nop(2);
            rd(0, 10'd4); nop(7); bst; nop(RD_LAT_X + 4);
            nop(4);
            check(chip.err_mask[`SE_CAPTURE], "capture error flagged for RISE at tAC 8 ns");
            check(phy_fault === 1'b0, "no PHY fault");
            $display("TEST %0s PASS", tname); if (tb_fail != 0) $display("TEST %0s FAIL", tname); $finish;
        // ---- refresh debt ----
        end else if (tname == "refresh_debt") begin
            powerup;
            ev_n = 0; dprev = ref_debt;
            for (ti = 0; ti < 780 * 3 + 40; ti = ti + 1) begin
                nop(1);
                if (ref_debt != dprev) begin if (ev_n < 8) ev_cyc[ev_n] = a_cyc; ev_n = ev_n + 1; dprev = ref_debt; end
            end
            check(ev_n == 3, "three debt increments in 2380 cycles");
            check(ev_cyc[1] - ev_cyc[0] == 780 && ev_cyc[2] - ev_cyc[1] == 780, "debt increments every 780 cycles");
            check(ref_debt == 3 && ref_due === 1'b1, "debt 3, ref_due");
            refr; nop(1); check(ref_debt == 2, "REF takes one away");
            nop(9); refr; nop(9); refr; nop(10); check(ref_debt == 0 && ref_due === 1'b0, "debt 0 after three REF");
            check(refreshes == 5, "PHY refresh counter");
            nop(20);
            check(chip.refreshes == refreshes, "chip saw every REF");
            expect_clean;
        // ---- preemption, no scheduler at all ----
        end else if (tname == "preempt_timeout") begin
            powerup;
            while (!preempt && ti < 2000) begin nop(1); ti = ti + 1; end
            check(preempt === 1'b1 && ref_debt == DEBTMAX, "preempt at the debt limit");
            ev_n = 0;
            while (preempt && ev_n < 1000) begin nop(1); ev_n = ev_n + 1; end
            check(ev_n >= TMO && ev_n <= TMO + 3, "preempt waits PREEMPT_TIMEOUT");
            k_bc = 0;
            while (!banks_closed && k_bc < 100) begin nop(1); k_bc = k_bc + 1; end
            check(banks_closed === 1'b1, "banks_closed pulse");
            nop(1);
            check(banks_closed === 1'b0, "banks_closed is one cycle");
            check(fault_count == 1 && fault_cause == 4'd9 && phy_fault === 1'b1, "forced refresh is a fault, cause 9");
            check(refreshes == 3, "one forced REF");
            nop(30);
            check(chip.err_count == 0, "chip clean after the forced sequence");
            check(chip.refreshes == refreshes, "chip saw the REF");
        end else if (tname == "preempt_ack") begin      // a read burst is open; the scheduler ends it and acknowledges
            powerup;
            act(0, 13'd5); nop(1); rd(0, 10'd0);
            while (!preempt && ti < 2000) begin nop(1); ti = ti + 1; end
            check(preempt === 1'b1, "preempt raised");
            bst; cyc(K_NOP, 2'b00, 13'd0, 1'b0, 16'd0, 2'b11); u_ack = 1'b1; nop(1); u_ack = 1'b0;     // ack in a NOP cycle
            check(preempt === 1'b0, "preempt drops after the ack");
            k_bc = 0;
            while (!banks_closed && k_bc < 100) begin nop(1); k_bc = k_bc + 1; end
            check(k_bc < 30, "banks_closed well before the timeout");
            nop(1);
            check(fault_count == 1 && fault_cause == 4'd9, "forced refresh counted");
            k_bc = 0;
            while (!ok_act[0] && k_bc < 60) begin nop(1); k_bc = k_bc + 1; end
            check(ok_act[0] === 1'b1, "ACT permitted again after tRFC");
            act(0, 13'd7); nop(1); wr(0, 10'd0, 16'h5151, 2'b11); bst;
            nop(30);
            check(chip.err_count == 0, "chip clean");
            check(pkf(0, 7, 0) == 17'h15151, "normal operation after the forced refresh");
        end else if (tname == "preempt_wburst") begin    // a write burst is open, no ack: the PHY ends it
            powerup;
            act(0, 13'd5); nop(1);
            wr(0, 10'd0, 16'h2000, 2'b11);
            ti = 0;
            while (!banks_closed && ti < 1000) begin wb(16'h2001 + ti, 2'b11); ti = ti + 1; end
            check(banks_closed === 1'b1, "banks_closed after a forced close of a write burst");
            nop(30);
            check(chip.err_count == 0, "chip clean");
            check(pkf(0, 5, 3) == 17'h12003, "data before the forced BST was written");
            check(fault_count >= 1 && fault_cause == 4'd9, "forced refresh counted");
            check(dq_conflicts == 0, "no DQ conflict");
        end else if (tname == "preempt_young_row") begin // the forced PREA must wait for tRAS of a row opened just before
            powerup;
            while (!preempt && ti < 2000) begin nop(1); ti = ti + 1; end
            act(0, 13'd5); cyc(K_NOP, 2'b00, 13'd0, 1'b0, 16'd0, 2'b11); u_ack = 1'b1; nop(1); u_ack = 1'b0;
            k_bc = 0;
            while (!banks_closed && k_bc < 100) begin nop(1); k_bc = k_bc + 1; end
            check(banks_closed === 1'b1, "banks_closed");
            nop(30);
            check(chip.err_count == 0, "chip clean (PREA waited for tRAS)");
        // ---- aged rows ----
        end else if (tname == "aged_row") begin
            powerup;
            act(0, 13'd5); k_bc = a_cyc;
            while (row_aged[0] !== 1'b1 && ti < 8000) begin nop(1); ti = ti + 1; end
            check(a_cyc - k_bc >= 6000 && a_cyc - k_bc <= 6003, "row_aged at 60 us");
            while (!preempt && ti < 12000) begin nop(1); ti = ti + 1; end
            check(a_cyc - k_bc >= 9000 && a_cyc - k_bc <= 9004, "forced close at 90 us");
            while (!banks_closed && ti < 12000) begin nop(1); ti = ti + 1; end
            check(banks_closed === 1'b1, "banks_closed after the aged-row close");
            check(fault_count == 1 && fault_cause == 4'd10, "aged row is a fault, cause 10");
            nop(30);
            check(chip.err_count == 0, "chip clean (no tRAS(max) violation)");
        end else if (tname == "aged_row_sched") begin    // the scheduler closes the aged row itself: no fault
            powerup;
            act(0, 13'd5);
            while (row_aged[0] !== 1'b1 && ti < 8000) begin nop(1); ti = ti + 1; end
            check(row_aged[0] === 1'b1, "row_aged request");
            pre(0); nop(3);
            check(row_aged[0] === 1'b0, "row_aged clears");
            for (ti = 0; ti < 6000; ti = ti + 1) begin nop(1); if (preempt) tb_fail = tb_fail + 1; end
            expect_clean;
        // ---- reset in a burst ----
        end else if (tname == "reset_rburst" || tname == "reset_wburst") begin
            powerup;
            act(0, 13'd5); nop(1);
            wr_pat(0, 13'd5, 10'd0, 16); bst; nop(2);
            if (tname == "reset_rburst") begin rd(0, 10'd0); nop(7); end
            else begin wr(0, 10'd32, 16'h3200, 2'b11); wb(16'h3201, 2'b11); wb(16'h3202, 2'b11); end
            do_reset(2);
            check(init_done === 1'b0 && ok_act === 4'd0 && !rvalid, "reset: init restarts, permits closed");
            wait_init;
            check(init_done === 1'b1, "init_done again");
            check(refreshes == 2 && fault_count == 0, "counters restarted");
            // the chip model sees a second init: only the two mode-register commands after init_done are flagged
            check(chip.err_count == 2 && chip.err_mask == 64'h100, "only MRS_LATE twice from the second init");
            // normal operation after the re-init (bank state was lost: ACT again)
            act(0, 13'd5); nop(1);
            rd(0, 10'd4); nop(3); bst; nop(RD_LAT_X + 4);
            check_burst(rd_a, 4, 0, 13'd5, 10'd4);
            check(chip.err_count == 2, "no further chip errors");
            check(phy_fault === 1'b0 && dq_conflicts == 0, "no PHY fault");
        // ---- turnaround ----
        end else if (tname == "turn_rw") begin            // earliest conflict-free WR after a read, found by polling the permit
            powerup;
            act(0, 13'd5); nop(1);
            wr_pat(0, 13'd5, 10'd0, 16); bst; nop(2);
            for (ti = 1; ti <= 8; ti = ti + 3) begin
                rd(0, 10'd0); nop(ti - 1); bst;               // BST at r + ti
                k_bc = 1;
                while (!ok_wr[0] && k_bc < 12) begin nop(1); k_bc = k_bc + 1; end
                check(k_bc == 3, "earliest WR is BST + 3 (permit)");
                wr(0, 10'd200 + ti, 16'h7000 + ti, 2'b11); wb(16'h7100 + ti, 2'b11); bst;
                nop(1);
                check(pkf(0, 5, 200 + ti) == {1'b1, 16'h7000 + ti[15:0]}, "write after the read landed");
                check(pkf(0, 5, 201 + ti) == {1'b1, 16'h7100 + ti[15:0]}, "second word landed");
                nop(2);
            end
            expect_clean;
        end else if (tname == "wr_rd_turn") begin         // RD directly at w + n (tCDL 1)
            powerup;
            act(0, 13'd5); nop(1);
            wr_pat(0, 13'd5, 10'd0, 4);
            rd(0, 10'd0); a_first = rd_a; nop(3); bst; nop(RD_LAT_X + 4);
            check_burst(a_first, 4, 0, 13'd5, 10'd0);
            expect_clean;
        // ---- long run: no scheduler at all for 66 ms; the PHY alone must keep every row inside the refresh limit ----
        end else if (tname == "tref_idle") begin
            powerup;
            for (ti = 0; ti < 6_600_000; ti = ti + 1) nop(1);
            check(chip.err_count == 0, "no chip error in 66 ms with no scheduler (tREF)");
            check(chip.refreshes > 8192, "more than 8192 refreshes");
            check(chip.err_mask[`SE_TREF] === 1'b0, "no tREF error");
            $display("  refreshes %0d, forced events (fault_count) %0d", refreshes, fault_count);
        // ---- random legal driver: permits against the chip model ----
        end else if (tname == "rand") begin
            if (!$value$plusargs("SEED=%d", seed)) seed = 7;
            powerup; rnd_init;
            rdchk_on = RDCHK;
            nrand = NRAND;
            for (ti = 0; ti < nrand; ti = ti + 1) rnd_step(1, 1, 1);
            // drain: end the bursts
            if (ok_bst) bst;
            nop(10);
            mem_compare;
            $display("  random: acts %0d rd %0d wr %0d pre %0d prea %0d ref %0d bst %0d; read words %0d checked %0d bad %0d",
                     ncmd[1], ncmd[2], ncmd[3], ncmd[4], ncmd[5], ncmd[6], ncmd[7], n_rd_words, n_rd_checked, n_rd_bad);
            check(ncmd[1] > 50 && ncmd[3] > 50 && ncmd[2] > 50, "random driver covered the commands");
            check(n_rd_bad == 0, "read data / rvalid");
            expect_clean;
        end else begin
            $display("unknown test %0s", tname); tb_fail = tb_fail + 1;
        end
        finish_test;
    end
endmodule
