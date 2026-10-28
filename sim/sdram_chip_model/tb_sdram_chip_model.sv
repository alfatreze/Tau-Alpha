// Directed test bench for sim/sdram_chip_model.sv. MIT licence. Clean-room: the reference command driver below was
// written from docs/features/SDRAM_CONTROLLER_CONTRACT.md only (no controller RTL was read).
//
// Usage (see run_tests.sh):  vvp tb.vvp +TEST=<name>     one test per run; prints "TEST <name> PASS" or "TEST <name> FAIL".
// Model/board parameters are top-level parameters here (iverilog -P tb_sdram_chip_model.NAME=value) so that one test
// can be run at a different capture mode, board delay, CAS latency or pin timing.
//
// Cycle convention of the reference driver: "cycle a" is the interval after controller posedge a. A command
// presented in cycle a is registered by the driver at posedge a+1 (= E, the launch edge; the pins change after E plus
// the output delay); the chip samples it half a period later (the chip clock is the inverse of clk).
`timescale 1ps/1ps
`include "sdram_chip_err.vh"

module tb_sdram_chip_model #(
    parameter integer P_PS        = 10000,   // controller clock period
    parameter integer CLSEL       = 2,
    parameter integer CAPMODE     = 1,       // 0 rising, 1 falling
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
    parameter integer CKPHASE     = 0,       // extra delay of the chip clock (ps)
    parameter integer TCCD        = 1,
    parameter integer TRFC        = 97500,
    parameter longint TREFPS      = 64'd64_000_000_000,
    parameter integer INITNOP     = 10000,
    parameter longint INITWAIT    = 64'd100_000_000,
    parameter integer M_TCMH      = 1500,    // model hold/setup overrides (to prove the checks fire)
    parameter integer M_TCMS      = 1500
);
    // ------------------------------------------------------------------------------------------------------
    // clocks
    // ------------------------------------------------------------------------------------------------------
    reg clk = 1'b0;
    always #(P_PS/2) clk = ~clk;
    wire dram_clk;
    assign #(CKPHASE) dram_clk = ~clk;

    // ------------------------------------------------------------------------------------------------------
    // reference command driver
    // ------------------------------------------------------------------------------------------------------
    // PHY-side inputs (set by the test tasks after posedge, registered at the next posedge)
    reg [2:0]  u_cmd  = 3'b111;
    reg [1:0]  u_ba   = 2'b00;
    reg [12:0] u_a    = 13'd0;
    reg [1:0]  u_dqm  = 2'b11;
    reg [15:0] u_wd   = 16'd0;
    reg [15:0] u_oe   = 16'd0;
    reg        u_cke  = 1'b1;
    // pins (envelope: X from tco_min, final value from tco_max)
    reg        p_ras = 1'b1, p_cas = 1'b1, p_we = 1'b1, p_cke = 1'b1;
    reg [1:0]  p_ba = 2'b00, p_dqm = 2'b11;
    reg [12:0] p_a = 13'd0;
    reg [15:0] p_dq = 16'd0;
    reg [15:0] p_oe = 16'd0;

    reg [2:0]  s_cmd; reg [1:0] s_ba, s_dqm; reg [12:0] s_a; reg [15:0] s_wd, s_oe; reg s_cke;
    always @(posedge clk) begin
        s_cmd = u_cmd; s_ba = u_ba; s_a = u_a; s_dqm = u_dqm; s_cke = u_cke;
        #(TCO_MIN);
        p_ras = 1'bx; p_cas = 1'bx; p_we = 1'bx; p_ba = 2'bxx; p_a = 13'bx; p_dqm = 2'bxx; p_cke = 1'bx;
        #(TCO_MAX - TCO_MIN);
        {p_ras, p_cas, p_we} = s_cmd; p_ba = s_ba; p_a = s_a; p_dqm = s_dqm; p_cke = s_cke;
    end
    always @(posedge clk) begin
        s_wd = u_wd;
        #(TCO_MIN);
        p_dq = 16'bx;
        #(TCO_MAX - TCO_MIN);
        p_dq = s_wd;
    end
    always @(posedge clk) begin
        s_oe = u_oe;
        #(OE_TCO_MIN);
        p_oe = p_oe | s_oe;               // turning on: earliest
        #(OE_TCO_MAX - OE_TCO_MIN);
        p_oe = p_oe & s_oe;               // turning off: latest
    end

    wire [15:0] dram_dq;
    genvar gi;
    generate for (gi = 0; gi < 16; gi = gi + 1) begin : g_dq
        assign dram_dq[gi] = p_oe[gi] ? p_dq[gi] : 1'bz;
    end endgenerate

    // ------------------------------------------------------------------------------------------------------
    // chip model
    // ------------------------------------------------------------------------------------------------------
    sdram_chip_model #(
        .TAC_CL2_PS(TAC2), .TAC_CL3_PS(TAC3), .THZ_CL2_PS(THZ2), .THZ_CL3_PS(THZ3), .TOH_PS(TOH), .TLZ_PS(TLZ),
        .TCCD_CLK(TCCD), .TRFC_PS(TRFC), .TREF_PS(TREFPS), .INIT_NOP_MIN(INITNOP), .INIT_WAIT_PS(INITWAIT),
        .DMIN_PS(DMIN), .DMAX_PS(DMAX), .CAP_MODE(CAPMODE), .CAP_SKEW_PS(CAPSKEW), .FPGA_SU_PS(FSU), .FPGA_H_PS(FH),
        .CL_DEFAULT(CLSEL), .TCMH_PS(M_TCMH), .TCMS_PS(M_TCMS)
    ) chip (
        .dram_clk(dram_clk), .dram_cke(p_cke), .dram_ras_n(p_ras), .dram_cas_n(p_cas), .dram_we_n(p_we),
        .dram_dqm(p_dqm), .dram_ba(p_ba), .dram_a(p_a), .dram_dq(dram_dq), .fpga_oe(p_oe), .cap_clk(clk)
    );

    // ------------------------------------------------------------------------------------------------------
    // reference capture register(s): what a PHY would do (one register stage per mode)
    // ------------------------------------------------------------------------------------------------------
    reg [15:0] cap_f;               // falling-edge capture (FALL mode)
    reg [15:0] rdata_vis [0:8191];  // value visible in cycle j (register output after posedge j)
    integer    cycn;                // number of posedges seen: cycle index
    reg [15:0] rdata_q;
    always @(negedge clk) begin
        #(CAPSKEW);
        cap_f = dram_dq;
    end
    always @(posedge clk) begin
        #(CAPSKEW);
        if (CAPMODE == 0) rdata_q = dram_dq; else rdata_q = cap_f;
    end
    // note: for CAPMODE 0 the capture register is the rising-edge register itself; for CAPMODE 1 one rising register
    // transfers the falling-edge capture. The visible value is rdata_q after the edge.
    initial cycn = 0;
    always @(posedge clk) begin
        cycn = cycn + 1;
        #(P_PS/2 - 1);
        rdata_vis[cycn & 8191] = rdata_q;
    end

    // ------------------------------------------------------------------------------------------------------
    // test tasks: each task occupies exactly one cycle
    // ------------------------------------------------------------------------------------------------------
    localparam [2:0] K_MRS = 3'b000, K_REF = 3'b001, K_PRE = 3'b010, K_ACT = 3'b011,
                     K_WR  = 3'b100, K_RD  = 3'b101, K_BST = 3'b110, K_NOP = 3'b111;

    integer tb_fail = 0;
    integer a_cyc;                 // cycle index of the last presented command

    task cyc(input [2:0] c, input [1:0] ba, input [12:0] a, input [1:0] dqm, input [15:0] wd, input [15:0] oe);
        begin
            u_cmd = c; u_ba = ba; u_a = a; u_dqm = dqm; u_wd = wd; u_oe = oe;
            a_cyc = cycn;
            @(posedge clk);
            #1;
        end
    endtask

    task nop(input integer n);
        integer k;
        begin for (k = 0; k < n; k = k + 1) cyc(K_NOP, 2'b00, 13'd0, 2'b00, 16'd0, 16'd0); end
    endtask
    task nopm(input integer n);       // NOP with DQM high
        integer k;
        begin for (k = 0; k < n; k = k + 1) cyc(K_NOP, 2'b00, 13'd0, 2'b11, 16'd0, 16'd0); end
    endtask
    task act(input [1:0] b, input [12:0] row);   cyc(K_ACT, b, row, 2'b00, 16'd0, 16'd0); endtask
    task rd (input [1:0] b, input [9:0] col);    cyc(K_RD,  b, {3'b000, col}, 2'b00, 16'd0, 16'd0); endtask
    task rda(input [1:0] b, input [9:0] col);    cyc(K_RD,  b, {2'b00, 1'b1, col}, 2'b00, 16'd0, 16'd0); endtask   // A10 = 1 (illegal)
    task wr (input [1:0] b, input [9:0] col, input [15:0] d);  cyc(K_WR, b, {3'b000, col}, 2'b00, d, 16'hffff); endtask
    task wr_m(input [1:0] b, input [9:0] col, input [15:0] d, input [1:0] m); cyc(K_WR, b, {3'b000, col}, m, d, 16'hffff); endtask
    task wd (input [15:0] d);                    cyc(K_NOP, 2'b00, 13'd0, 2'b00, d, 16'hffff); endtask
    task wd_m(input [15:0] d, input [1:0] m);    cyc(K_NOP, 2'b00, 13'd0, m, d, 16'hffff); endtask
    task pre(input [1:0] b);                     cyc(K_PRE, b, 13'd0, 2'b11, 16'd0, 16'd0); endtask
    task prea;                                   cyc(K_PRE, 2'b00, 13'h0400, 2'b11, 16'd0, 16'd0); endtask
    task refr;                                   cyc(K_REF, 2'b00, 13'd0, 2'b11, 16'd0, 16'd0); endtask
    task bst;                                    cyc(K_BST, 2'b00, 13'd0, 2'b11, 16'd0, 16'd0); endtask
    task mrs(input [12:0] v);                    cyc(K_MRS, 2'b00, v, 2'b11, 16'd0, 16'd0); endtask
    task emrs(input [12:0] v);                   cyc(K_MRS, 2'b10, v, 2'b11, 16'd0, 16'd0); endtask

    // data pattern
    function [15:0] pat(input [1:0] b, input [12:0] r, input [9:0] c);
        begin pat = {b, r[3:0], c[9:0]} ^ 16'h5a3c; end
    endfunction

    // power-up and the contract's init sequence (real 100 us wait; 10,000 NOP edges)
    task init_seq(input integer cl);
        begin
            nopm(10000);
            prea; nopm(1);                 // tRP: 18 ns = 2 clocks
            refr; nopm(9);                 // tRFC 97.5 ns = 10 clocks
            refr; nopm(9);
            mrs(cl == 3 ? 13'h037 : 13'h027); nopm(1);   // tMRD 2
            emrs(13'h000); nopm(1);
        end
    endtask

    // ------------------------------------------------------------------------------------------------------
    // checks
    // ------------------------------------------------------------------------------------------------------
    reg [8*40-1:0] tname;
    task expect_clean;
        begin
            nopm(3);                    // let the last command reach the chip (it samples half a clock late)
            if (chip.err_count != 0) begin
                $display("  expected no chip errors, got %0d (first code %0d)", chip.err_count, chip.err_first);
                tb_fail = tb_fail + 1;
            end
        end
    endtask
    task expect_err(input integer code);
        begin
            nopm(3);
            if (!chip.err_mask[code]) begin
                $display("  expected chip error code %0d not raised (err_count=%0d mask=%h)", code, chip.err_count, chip.err_mask);
                tb_fail = tb_fail + 1;
            end
        end
    endtask
    task check(input cond, input [8*48-1:0] msg);
        begin if (!cond) begin $display("  CHECK FAILED: %0s", msg); tb_fail = tb_fail + 1; end end
    endtask

    // ------------------------------------------------------------------------------------------------------
    // tests
    // ------------------------------------------------------------------------------------------------------
    task finish_test;
        begin
            nopm(4);
            if (tb_fail == 0) $display("TEST %0s PASS", tname); else $display("TEST %0s FAIL (%0d)", tname, tb_fail);
            $finish;
        end
    endtask

    // ------------------------------------------------------------------------------------------------------
    // init variants (the legal one is init_seq)
    // ------------------------------------------------------------------------------------------------------
    task init_custom(input integer nnop, input integer nref, input [12:0] mrsv, input do_emrs, input integer gap_mrd);
        integer k;
        begin
            nopm(nnop);
            prea; nopm(1);
            for (k = 0; k < nref; k = k + 1) begin refr; nopm(9); end
            mrs(mrsv); nopm(gap_mrd);
            if (do_emrs) begin emrs(13'h000); nopm(1); end
        end
    endtask

    // ------------------------------------------------------------------------------------------------------
    // tests
    // ------------------------------------------------------------------------------------------------------
    integer ti;
    initial begin
        if (!$value$plusargs("TEST=%s", tname)) tname = "init_cl2";
        @(posedge clk); #1;
        // ---- legal sequences (must be clean) ----
        if (tname == "init_cl2") begin
            init_seq(2);
            check(chip.init_done, "init_done");
            expect_clean;
        end else if (tname == "init_cl3") begin
            init_seq(3);
            check(chip.init_done && chip.mr_cl == 3, "init_done CL3");
            expect_clean;
        end else if (tname == "legal_timing") begin
            init_seq(2);
            act(0, 13'd5);  nop(1);
            act(1, 13'd6);  nop(2);          // tRRD 2 clk (ACT b0 at c0, b1 at c2)
            pre(0); nop(1);                  // c5: tRAS 50 ns >= 42
            pre(1); nop(1);                  // c7
            act(0, 13'd7);  nop(4);          // c9: tRP 40 ns, tRC 90 ns
            act(1, 13'd8);  nop(4);          // tRRD, tRP
            pre(0); nop(1);
            prea; nop(1);                    // PREA: every bank precharged, tRP 2 clk
            refr; nop(9);                    // tRFC 100 ns >= 97.5
            act(2, 13'd9); nop(1);
            expect_clean;
        end
        // ---- negative: init order ----
        else if (tname == "n_init_short_wait") begin
            init_custom(5000, 2, 13'h027, 1, 1);
            expect_err(`SE_INIT_ORDER);
        end else if (tname == "n_init_one_ref") begin
            init_custom(10000, 1, 13'h027, 1, 1);
            expect_err(`SE_INIT_ORDER);
        end else if (tname == "n_init_no_emrs") begin
            init_custom(10000, 2, 13'h027, 0, 1);
            act(0, 13'd1);                   // first command after MRS while EMRS is still expected
            expect_err(`SE_INIT_ORDER);
        end else if (tname == "n_init_mrs_value") begin
            init_custom(10000, 2, 13'h227, 1, 1);   // A9 = 1 (single-location write): the old note
            expect_err(`SE_MRS_VALUE);
        end else if (tname == "n_init_first_not_prea") begin
            nopm(10000); refr; nopm(9);
            expect_err(`SE_INIT_ORDER);
        end else if (tname == "n_tmrd") begin
            init_custom(10000, 2, 13'h027, 0, 0);   // EMRS in the very next clock
            emrs(13'h000); nopm(1);
            expect_err(`SE_TMRD);
        end else if (tname == "n_mrs_late") begin
            init_seq(2);
            mrs(13'h027); nopm(2);
            expect_err(`SE_MRS_LATE);
        end else if (tname == "n_cke_low") begin
            init_seq(2);
            u_cke = 1'b0; nop(1); u_cke = 1'b1; nop(2);
            expect_err(`SE_CKE_LOW);
        end
        // ---- negative: per-bank / global timing ----
        else if (tname == "n_trcd") begin
            init_seq(2);
            act(0, 13'd5); rd(0, 10'd0); nop(1); bst; nopm(3);   // RD in the next clock: 10 ns < 18 ns
            expect_err(`SE_TRCD);
        end else if (tname == "n_trp") begin
            init_seq(2);
            act(0, 13'd5); nop(4); pre(0); act(0, 13'd6); nopm(2);   // ACT 1 clk after PRE
            expect_err(`SE_TRP);
        end else if (tname == "n_tras") begin
            init_seq(2);
            act(0, 13'd5); nop(3); pre(0); nopm(3);                  // PRE at 40 ns < 42 ns
            expect_err(`SE_TRAS);
        end else if (tname == "n_trc") begin
            init_seq(2);
            act(0, 13'd5); nop(2); pre(0); nop(1); act(0, 13'd6); nopm(2);  // ACT at 50 ns < 60 ns (and tRAS)
            expect_err(`SE_TRC);
        end else if (tname == "n_trrd") begin
            init_seq(2);
            act(0, 13'd5); act(1, 13'd6); nopm(3);                    // ACT to ACT in the next clock
            expect_err(`SE_TRRD);
        end else if (tname == "n_trfc_act") begin
            init_seq(2);
            refr; nop(8); act(0, 13'd6); nopm(2);                     // ACT 9 clk (90 ns) after REF < 97.5 ns
            expect_err(`SE_TRFC);
        end else if (tname == "n_trfc_ref") begin
            init_seq(2);
            refr; nop(8); refr; nopm(2);
            expect_err(`SE_TRFC);
        end else if (tname == "n_act_ref") begin
            init_seq(2);
            act(0, 13'd5); nop(2); pre(0); nop(1); refr; nopm(10);    // REF 50 ns after ACT < tRC 60 ns
            expect_err(`SE_ACT_REF);
        end else if (tname == "n_ref_open") begin
            init_seq(2);
            act(0, 13'd5); nop(5); refr; nopm(10);
            expect_err(`SE_REF_OPEN);
        end else if (tname == "n_ref_trp") begin
            init_seq(2);
            act(0, 13'd5); nop(4); pre(0); refr; nopm(10);            // REF in the clock after PRE: tRP
            expect_err(`SE_TRP);
        end else if (tname == "n_col_closed") begin
            init_seq(2);
            rd(0, 10'd3); nopm(3);
            expect_err(`SE_COL_CLOSED);
        end else if (tname == "n_act_open") begin
            init_seq(2);
            act(0, 13'd5); nop(6); act(0, 13'd6); nopm(2);
            expect_err(`SE_ACT_OPEN);
        end else if (tname == "n_a10_col") begin
            init_seq(2);
            act(0, 13'd5); nop(1); rda(0, 10'd0); nop(1); bst; nopm(3);
            expect_err(`SE_A10_COL);
        end else if (tname == "n_bst_noburst") begin
            init_seq(2);
            bst; nopm(2);
            expect_err(`SE_BST_NOBURST);
        end else if (tname == "n_tras_max") begin
            init_seq(2);
            act(0, 13'd5); nopm(10050);                               // open for 100.5 us
            expect_err(`SE_TRAS_MAX);
        end else if (tname == "n_tref") begin                         // run with -P ...TREFPS=100000000 (100 us)
            init_seq(2);
            nopm(30000);                                              // 300 us without REF; limit 100 us + 62.4 us
            expect_err(`SE_TREF);
        end else if (tname == "n_setup") begin                        // run with TCO_MAX=4500 (500 ps setup < 1500)
            init_seq(2);
            nopm(2);
            expect_err(`SE_SETUP);
        end else if (tname == "n_hold") begin                         // run with M_TCMH=7000 (> 5000 + tco_min)
            init_seq(2);
            nopm(2);
            expect_err(`SE_HOLD);
        end else begin
            $display("unknown test %0s", tname); tb_fail = tb_fail + 1;
        end
        finish_test;
    end
    initial begin #2_000_000_000; $display("TEST %0s FAIL (timeout)", tname); $finish; end
endmodule
