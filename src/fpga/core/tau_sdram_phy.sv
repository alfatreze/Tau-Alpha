// SDRAM PHY (top): pins, DQ tri-state, DDIO chip clock, and the PHY core. MIT licence.
// Interface: docs/features/SDRAM_CONTROLLER_CONTRACT.md v0.3 sections 1-3. Parameters: see tau_sdram_phy_core.sv.
`default_nettype none
module tau_sdram_phy #(
    parameter integer CLK_HZ              = 100_000_000,
    parameter integer CL                  = 2,
    parameter integer RD_EXTRA            = 1,
    parameter integer CAP_MODE            = 0,            // 0 = RISE (default), 1 = FALL
    parameter integer TRCD_PS             = 18000,
    parameter integer TRP_PS              = 18000,
    parameter integer TRAS_PS             = 42000,
    parameter integer TRC_PS              = 60000,
    parameter integer TRFC_PS             = 97500,
    parameter integer TWR_PS              = 15000,
    parameter integer TRRD_CLK            = 2,
    parameter integer TMRD_CLK            = 2,
    parameter integer TDPL_CLK            = 2,
    parameter integer TCK_MIN_CL2_PS      = 9000,
    parameter integer TCK_MIN_CL3_PS      = 6000,
    parameter integer THZ_CL2_PS          = 8000,
    parameter integer THZ_CL3_PS          = 5000,
    parameter integer OE_ON_MIN_PS        = 1500,
    parameter integer DQ_BOARD_MAX_PS     = 3600,
    parameter integer OPEN_ROW_MAX_NS     = 60000,
    parameter integer ROW_FORCE_NS        = 90000,
    parameter integer REFRESH_INTERVAL_PS = 7_800_000,
    parameter integer REFRESH_DEBT_MAX    = 8,
    parameter integer INIT_WAIT_CYCLES    = 10_000,
    parameter integer PREEMPT_TIMEOUT     = 136,
    parameter integer SIM_SHORT_INIT      = 0,
    parameter integer CALIBRATE           = 0,
    parameter integer CAL_BANK            = 3,
    parameter integer CAL_ROW             = 8191,
    parameter integer CAL_LEN             = 64,
    parameter integer BIST                = 0,
    parameter integer BIST_PAGE_LO        = 0,
    parameter integer BIST_PAGE_HI        = 32767,
    parameter integer PERF                = 0
) (
    input  wire        clk,
    input  wire        rst,
    input  wire [2:0]  cmd,
    input  wire [1:0]  cmd_ba,
    input  wire [12:0] cmd_a,
    input  wire        wvalid,
    input  wire [15:0] wdata,
    input  wire [1:0]  be,
    output wire [3:0]  ok_act,
    output wire [3:0]  ok_rd,
    output wire [3:0]  ok_wr,
    output wire [3:0]  ok_pre,
    output wire        ok_prea,
    output wire        ok_ref,
    output wire        ok_bst,
    output wire        init_done,
    output wire        rvalid,
    output wire [15:0] rdata,
    output wire        phy_fault,
    output wire [15:0] fault_count,
    output wire [3:0]  fault_cause,
    output wire [3:0]  row_aged,
    output wire        preempt,
    input  wire        preempt_ack,
    output wire        banks_closed,
    output wire        ref_due,
    output wire [3:0]  ref_debt,
    output wire [31:0] refreshes,
    output wire [15:0] dq_conflicts,
    output wire [4:0]  rd_lat,
    // optional features (see tau_sdram_phy_core.sv; unused inputs may be left unconnected when the parameter is 0)
    input  wire        idle_ok,
    output wire [7:0]  cal_window,
    output wire [3:0]  cal_best,
    output wire        cal_done,
    output wire        cal_fault,
    input  wire        bist_start,
    output wire        bist_busy,
    output wire        bist_done,
    output wire [31:0] bist_errors,
    output wire [24:0] bist_first_addr,
    input  wire        perf_snap,
    input  wire        perf_clear,
    output wire [511:0] perf_snapshot,
    // pins
    output wire [12:0] dram_a,
    output wire [1:0]  dram_ba,
    inout  wire [15:0] dram_dq,
    output wire [1:0]  dram_dqm,
    output wire        dram_clk,
    output wire        dram_cke,
    output wire        dram_ras_n,
    output wire        dram_cas_n,
    output wire        dram_we_n
);
    wire [15:0] dq_o, dq_oe, dq_i;
    genvar i;
    generate
        for (i = 0; i < 16; i = i + 1) begin : g_dq
            assign dram_dq[i] = dq_oe[i] ? dq_o[i] : 1'bz;
        end
    endgenerate
    assign dq_i = dram_dq;

    tau_sdram_clk_out u_clk (.clk(clk), .dram_clk(dram_clk));

    tau_sdram_phy_core #(
        .CLK_HZ(CLK_HZ), .CL(CL), .RD_EXTRA(RD_EXTRA), .CAP_MODE(CAP_MODE),
        .TRCD_PS(TRCD_PS), .TRP_PS(TRP_PS), .TRAS_PS(TRAS_PS), .TRC_PS(TRC_PS), .TRFC_PS(TRFC_PS), .TWR_PS(TWR_PS),
        .TRRD_CLK(TRRD_CLK), .TMRD_CLK(TMRD_CLK), .TDPL_CLK(TDPL_CLK),
        .TCK_MIN_CL2_PS(TCK_MIN_CL2_PS), .TCK_MIN_CL3_PS(TCK_MIN_CL3_PS),
        .THZ_CL2_PS(THZ_CL2_PS), .THZ_CL3_PS(THZ_CL3_PS), .OE_ON_MIN_PS(OE_ON_MIN_PS), .DQ_BOARD_MAX_PS(DQ_BOARD_MAX_PS),
        .OPEN_ROW_MAX_NS(OPEN_ROW_MAX_NS), .ROW_FORCE_NS(ROW_FORCE_NS),
        .REFRESH_INTERVAL_PS(REFRESH_INTERVAL_PS), .REFRESH_DEBT_MAX(REFRESH_DEBT_MAX),
        .INIT_WAIT_CYCLES(INIT_WAIT_CYCLES), .PREEMPT_TIMEOUT(PREEMPT_TIMEOUT), .SIM_SHORT_INIT(SIM_SHORT_INIT),
        .CALIBRATE(CALIBRATE), .CAL_BANK(CAL_BANK), .CAL_ROW(CAL_ROW), .CAL_LEN(CAL_LEN), .BIST(BIST),
        .BIST_PAGE_LO(BIST_PAGE_LO), .BIST_PAGE_HI(BIST_PAGE_HI), .PERF(PERF)
    ) u_core (
        .clk(clk), .rst(rst), .cmd(cmd), .cmd_ba(cmd_ba), .cmd_a(cmd_a), .wvalid(wvalid), .wdata(wdata), .be(be),
        .ok_act(ok_act), .ok_rd(ok_rd), .ok_wr(ok_wr), .ok_pre(ok_pre), .ok_prea(ok_prea), .ok_ref(ok_ref),
        .ok_bst(ok_bst), .init_done(init_done), .rvalid(rvalid), .rdata(rdata), .phy_fault(phy_fault),
        .fault_count(fault_count), .fault_cause(fault_cause), .row_aged(row_aged), .preempt(preempt),
        .preempt_ack(preempt_ack), .banks_closed(banks_closed), .ref_due(ref_due), .ref_debt(ref_debt),
        .refreshes(refreshes), .dq_conflicts(dq_conflicts), .rd_lat(rd_lat),
        .idle_ok(idle_ok), .cal_window(cal_window), .cal_best(cal_best), .cal_done(cal_done), .cal_fault(cal_fault),
        .bist_start(bist_start), .bist_busy(bist_busy), .bist_done(bist_done), .bist_errors(bist_errors),
        .bist_first_addr(bist_first_addr), .perf_snap(perf_snap), .perf_clear(perf_clear), .perf_snapshot(perf_snapshot),
        .pin_cke(dram_cke), .pin_ras_n(dram_ras_n), .pin_cas_n(dram_cas_n), .pin_we_n(dram_we_n),
        .pin_dqm(dram_dqm), .pin_ba(dram_ba), .pin_a(dram_a), .dq_o(dq_o), .dq_oe(dq_oe), .dq_i(dq_i)
    );
endmodule
`default_nettype wire
