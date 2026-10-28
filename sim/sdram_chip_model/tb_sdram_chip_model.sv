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
    parameter longint INITWAIT    = 64'd100_000_000
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
        .CL_DEFAULT(CLSEL)
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
            if (chip.err_count != 0) begin
                $display("  expected no chip errors, got %0d (first code %0d)", chip.err_count, chip.err_first);
                tb_fail = tb_fail + 1;
            end
        end
    endtask
    task expect_err(input integer code);
        begin
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

    initial begin
        if (!$value$plusargs("TEST=%s", tname)) tname = "init_cl2";
        @(posedge clk); #1;
        if (tname == "init_cl2") begin
            init_seq(2);
            check(chip.init_done, "init_done");
            expect_clean;
            finish_test;
        end else begin
            $display("unknown test %0s", tname); tb_fail = tb_fail + 1; finish_test;
        end
    end
    initial begin #2_000_000_000; $display("TEST %0s FAIL (timeout)", tname); $finish; end
endmodule
