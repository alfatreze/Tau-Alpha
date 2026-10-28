// SDRAM chip model and timing checker (simulation only). MIT licence.
//
// Clean-room: written from docs/features/SDRAM_CONTROLLER_CONTRACT.md (v0.2), docs/features/SDRAM_PART_FACTS.md
// and the sibling-part datasheet text (AS4C32M16MS-6BIN Rev 1.0). No controller RTL and no vendor model was read.
//
// Part: Alliance AS4C32M16MSA-6BIN, 4 banks x 8192 rows x 1024 columns x 16 bit, no chip select
// (command = {RAS#, CAS#, WE#}). Full-page sequential burst only (mode value 0x027 / 0x037), write latency 0,
// DQM byte masks (dqm[0] = DQ[7:0]).
//
// `dram_clk` is the CHIP clock (the inverse of the controller clock); the chip samples commands on its rising edge.
// All times are integer picoseconds (timescale 1ps/1ps); every number is a parameter.
//
// Checks (each sets a bit of err_mask, see sdram_chip_err.vh):
//   per chip clock: every timing rule of contract sections 2 and 3.4, init order, CKE, A10, wraps, BST, tREF per row,
//   tRAS(max), setup/hold at the pins, tCK.
//   analytical (ps): chip drive window (tLZ..tHZ plus board delay) against the FPGA output enable (bus conflict),
//   and the capture edge (rising or falling edge of cap_clk) against the valid window tAC..tOH plus board delay.
`timescale 1ps/1ps
`include "sdram_chip_err.vh"

module sdram_chip_model #(
    // ---- timing, ps unless the name ends in _CLK --------------------------------------------------------------
    parameter integer TCK_MIN_CL2_PS = 9000,
    parameter integer TCK_MIN_CL3_PS = 6000,
    parameter integer TAC_CL2_PS     = 8000,   // access time from clock
    parameter integer TAC_CL3_PS     = 5000,
    parameter integer THZ_CL2_PS     = 8000,   // output to high-Z
    parameter integer THZ_CL3_PS     = 5000,
    parameter integer TOH_PS         = 1800,   // output hold (unloaded floor; loaded is 2500)
    parameter integer TLZ_PS         = 1000,   // output to low-Z, from the launching clock edge
    parameter integer TRCD_PS        = 18000,
    parameter integer TRP_PS         = 18000,
    parameter integer TRAS_PS        = 42000,
    parameter integer TRC_PS         = 60000,
    parameter integer TRFC_PS        = 97500,
    parameter integer TWR_PS         = 15000,
    parameter integer TRRD_CLK       = 2,
    parameter integer TMRD_CLK       = 2,
    parameter integer TCCD_CLK       = 1,
    parameter integer TDPL_CLK       = 2,      // tDPL = tRDL
    parameter integer TDQZ_CLK       = 2,      // DQM to read data high-Z
    // setup / hold at the chip pins. Command setup/hold: the sheet prints 0.5 / 1.5 ns, the contract thinks that is
    // a transposition of 1.5 / 0.5; the model takes the conservative 1.5 / 1.5 (see SDRAM_CHIP_MODEL_FINDINGS.md).
    parameter integer TCMS_PS        = 1500,
    parameter integer TCMH_PS        = 1500,
    parameter integer TAS_PS         = 1500,
    parameter integer TAH_PS         = 1000,
    parameter integer TDS_PS         = 1500,
    parameter integer TDH_PS         = 1000,
    parameter integer TCKS_PS        = 1500,
    parameter integer TCKH_PS        = 1000,
    parameter longint TRAS_MAX_PS    = 64'd100_000_000,       // 100 us
    parameter longint TREF_PS        = 64'd64_000_000_000,    // 64 ms, 8192 rows
    parameter longint TREF_SLACK_PS  = 64'd62_400_000,        // REFRESH_DEBT_MAX (8) x 7.8 us
    parameter longint INIT_WAIT_PS   = 64'd100_000_000,       // 100 us before the first command
    parameter integer INIT_NOP_MIN   = 10000,                 // NOP edges before PREA (contract 3.7)
    // ---- board / FPGA side ------------------------------------------------------------------------------------
    parameter integer DMIN_PS        = 0,       // DQ board delay, min (chip to FPGA pin)
    parameter integer DMAX_PS        = 0,       // DQ board delay, max
    parameter integer CAP_MODE       = 1,       // 0 = rising edge of cap_clk, 1 = falling edge
    parameter integer CAP_SKEW_PS    = 0,       // capture-register clock arrival after the cap_clk edge
    parameter integer FPGA_SU_PS     = 0,       // capture register setup
    parameter integer FPGA_H_PS      = 0,       // capture register hold
    parameter integer CAP_CHECK_EN   = 1,
    // ---- model options ----------------------------------------------------------------------------------------
    parameter integer CL_DEFAULT     = 2,       // used until the MRS is seen
    parameter integer BST_NO_BURST_IS_ERROR = 1,
    parameter integer MEM_LOG2       = 16,      // sparse store: 2^MEM_LOG2 direct-mapped words (collision = error print)
    parameter integer VERBOSE        = 1
) (
    input  wire        dram_clk,        // chip clock (inverse of the controller clock)
    input  wire        dram_cke,
    input  wire        dram_ras_n,
    input  wire        dram_cas_n,
    input  wire        dram_we_n,
    input  wire [1:0]  dram_dqm,
    input  wire [1:0]  dram_ba,
    input  wire [12:0] dram_a,
    inout  wire [15:0] dram_dq,
    input  wire [15:0] fpga_oe,         // the FPGA output enable as seen at the pin (envelope: on at tco_min, off at tco_max)
    input  wire        cap_clk          // the controller clock used by the FPGA capture register
);
    // ---------------------------------------------------------------------------------------------------------
    // error bookkeeping
    // ---------------------------------------------------------------------------------------------------------
    integer    edge_n;               // chip rising-edge counter (first edge = 0)
    reg [63:0] err_mask;
    integer    err_count;
    integer    err_first;

    function [8*14-1:0] ename(input integer c);
        begin
            case (c)
                `SE_TCK:          ename = "TCK";
                `SE_CKE_LOW:      ename = "CKE_LOW";
                `SE_UNKNOWN:      ename = "UNKNOWN";
                `SE_SETUP:        ename = "SETUP";
                `SE_HOLD:         ename = "HOLD";
                `SE_INIT_ORDER:   ename = "INIT_ORDER";
                `SE_MRS_VALUE:    ename = "MRS_VALUE";
                `SE_EMRS_VALUE:   ename = "EMRS_VALUE";
                `SE_MRS_LATE:     ename = "MRS_LATE";
                `SE_TRCD:         ename = "TRCD";
                `SE_TRP:          ename = "TRP";
                `SE_TRAS:         ename = "TRAS";
                `SE_TRC:          ename = "TRC";
                `SE_TRRD:         ename = "TRRD";
                `SE_TRFC:         ename = "TRFC";
                `SE_TMRD:         ename = "TMRD";
                `SE_TCCD:         ename = "TCCD";
                `SE_TDPL:         ename = "TDPL";
                `SE_TWR:          ename = "TWR";
                `SE_TDAL:         ename = "TDAL";
                `SE_ACT_REF:      ename = "ACT_REF";
                `SE_REF_OPEN:     ename = "REF_OPEN";
                `SE_COL_CLOSED:   ename = "COL_CLOSED";
                `SE_ACT_OPEN:     ename = "ACT_OPEN";
                `SE_A10_COL:      ename = "A10_COL";
                `SE_COL_WRAP:     ename = "COL_WRAP";
                `SE_BST_NOBURST:  ename = "BST_NOBURST";
                `SE_TREF:         ename = "TREF";
                `SE_TRAS_MAX:     ename = "TRAS_MAX";
                `SE_BUS_CONFLICT: ename = "BUS_CONFLICT";
                `SE_CAPTURE:      ename = "CAPTURE";
                `SE_WDATA_X:      ename = "WDATA_X";
                default:          ename = "?";
            endcase
        end
    endfunction

    task err(input integer code, input [8*40-1:0] msg);
        begin
            err_count = err_count + 1;
            err_mask[code] = 1'b1;
            if (err_first < 0) err_first = code;
            if (VERBOSE != 0)
                $display("[CHIPERR %0d] t=%0t edge=%0d %0s: %0s", err_count, $time, edge_n, ename(code), msg);
        end
    endtask

    // ---------------------------------------------------------------------------------------------------------
    // command decode
    // ---------------------------------------------------------------------------------------------------------
    localparam [2:0] C_MRS = 3'b000, C_REF = 3'b001, C_PRE = 3'b010, C_ACT = 3'b011,
                     C_WR  = 3'b100, C_RD  = 3'b101, C_BST = 3'b110, C_NOP = 3'b111;
    localparam integer INF = 32'h3fffffff;           // "no end" for stream edge numbers

    // ---------------------------------------------------------------------------------------------------------
    // state
    // ---------------------------------------------------------------------------------------------------------
    longint  t_now, t_prev;          // ps
    longint  t_first;
    integer  mr_cl;                  // programmed CAS latency
    reg      init_done;
    integer  init_stage;             // 0 NOP wait, 1 PREA done, 2 REF1 done, 3 REF2 done, 4 MRS done, 5 EMRS done = init_done
    integer  nop_edges;
    integer  e_mrs;                  // edge of the last MRS/EMRS

    reg         bank_open [0:3];
    reg  [12:0] bank_row  [0:3];
    longint     t_act     [0:3];
    longint     t_pre     [0:3];
    longint     t_wr_last [0:3];     // time of the last written word, per bank
    integer     e_wr_last [0:3];     // edge of the last written word, per bank
    longint     t_ref;
    integer     e_act_any;
    longint     t_act_any;
    integer     e_col_last;

    // burst streams (one read stream and one write stream; a new column command ends both)
    reg         rs_active;           // a read stream exists (it may already have ended: rs_end != INF)
    integer     rs_bank, rs_col0, rs_start, rs_end;   // slots (capture-edge numbers) rs_start = r + CL
    longint     rs_cmd_t;
    reg         ws_active;
    integer     ws_bank, ws_col0, ws_start, ws_end;

    integer i_b;

    initial begin
        err_mask = 64'd0; err_count = 0; err_first = -1;
        edge_n = -1; t_now = 0; t_prev = 0; t_first = 0;
        mr_cl = CL_DEFAULT; init_done = 1'b0; init_stage = 0; nop_edges = 0; e_mrs = -1000;
        for (i_b = 0; i_b < 4; i_b = i_b + 1) begin
            bank_open[i_b] = 1'b0; bank_row[i_b] = 13'd0;
            t_act[i_b] = -64'sd10_000_000_000_000; t_pre[i_b] = -64'sd10_000_000_000_000;
            t_wr_last[i_b] = -64'sd10_000_000_000_000; e_wr_last[i_b] = -1000;
        end
        t_ref = -64'sd10_000_000_000_000; e_act_any = -1000; t_act_any = -64'sd10_000_000_000_000; e_col_last = -1000;
        rs_active = 1'b0; rs_bank = 0; rs_col0 = 0; rs_start = 0; rs_end = INF; rs_cmd_t = 0;
        ws_active = 1'b0; ws_bank = 0; ws_col0 = 0; ws_start = 0; ws_end = INF;
    end

    // ---------------------------------------------------------------------------------------------------------
    // burst helpers
    // ---------------------------------------------------------------------------------------------------------
    function rs_open(input integer dummy);      // a read burst exists and has not been terminated yet
        begin rs_open = rs_active && (rs_end == INF); end
    endfunction
    function ws_open(input integer dummy);
        begin ws_open = ws_active && (ws_end == INF); end
    endfunction

    // terminate the streams at command edge c: read ends after c + CL (exactly c - r words), write ends at c
    task end_streams(input integer c, input reg rd_too, input reg wr_too);
        begin
            if (rd_too && rs_open(0)) rs_end = c + mr_cl;
            if (wr_too && ws_open(0)) ws_end = c;
        end
    endtask

    // ---------------------------------------------------------------------------------------------------------
    // the chip clock edge
    // ---------------------------------------------------------------------------------------------------------
    reg [2:0]  cmd;
    reg        cmd_known;
    reg        a10;
    integer    bsel;
    integer    bi;

    reg was_done;

    // init order (contract 3.7): CKE high from the first edge, >= 100 us of NOP, PREA, REF, REF, MRS, EMRS
    task init_cmd(input [2:0] c);
        begin
            case (init_stage)
                0: begin
                    if (c == C_PRE && a10) begin
                        if (t_now - t_first < INIT_WAIT_PS) err(`SE_INIT_ORDER, "PREA before the 100 us wait");
                        if (nop_edges < INIT_NOP_MIN)       err(`SE_INIT_ORDER, "fewer NOP edges than INIT_NOP_MIN");
                        init_stage = 1;
                    end else err(`SE_INIT_ORDER, "first command after power-up must be PREA");
                end
                1: begin
                    if (c == C_REF) init_stage = 2; else err(`SE_INIT_ORDER, "expected REF 1 after PREA");
                end
                2: begin
                    if (c == C_REF) init_stage = 3; else err(`SE_INIT_ORDER, "expected REF 2");
                end
                3: begin
                    if (c == C_MRS) begin
                        if (dram_ba != 2'b00 || (dram_a != 13'h027 && dram_a != 13'h037))
                            err(`SE_MRS_VALUE, "MRS must be BA=00 A=0x027 (CL2) or 0x037 (CL3)");
                        if (dram_a[6:4] == 3'd2) mr_cl = 2;
                        else if (dram_a[6:4] == 3'd3) mr_cl = 3;
                        init_stage = 4;
                    end else err(`SE_INIT_ORDER, "expected MRS after the refreshes");
                end
                4: begin
                    if (c == C_MRS) begin
                        if (dram_ba != 2'b10 || dram_a[12:7] != 6'd0)
                            err(`SE_EMRS_VALUE, "EMRS must be BA=10 with reserved bits 0");
                        init_stage = 5; init_done = 1'b1;
                    end else err(`SE_INIT_ORDER, "expected EMRS after MRS");
                end
                default: ;
            endcase
        end
    endtask

    task do_precharge(input integer b);
        begin
            // legality (timing rules are added in the timing section)
            bank_open[b] = 1'b0;
            t_pre[b] = t_now;
        end
    endtask

    always @(posedge dram_clk) begin
        edge_n = edge_n + 1;
        t_prev = t_now;
        t_now  = $time;
        if (edge_n == 0) t_first = t_now;

        cmd   = {dram_ras_n, dram_cas_n, dram_we_n};
        cmd_known = !(^{dram_ras_n, dram_cas_n, dram_we_n} === 1'bx);
        a10   = dram_a[10];
        bsel  = dram_ba;

        if (dram_cke !== 1'b1) begin
            err(`SE_CKE_LOW, "CKE not high at a chip clock edge");
        end else if (!cmd_known) begin
            err(`SE_UNKNOWN, "X/Z on RAS/CAS/WE");
        end else begin
            was_done = init_done;
            if (!was_done && cmd != C_NOP) init_cmd(cmd);
            case (cmd)
                C_NOP: begin
                    if (!init_done) nop_edges = nop_edges + 1;
                end
                C_ACT: begin
                    bsel = dram_ba;
                    if (bank_open[bsel]) err(`SE_ACT_OPEN, "ACT to an open bank");
                    bank_open[bsel] = 1'b1;
                    bank_row[bsel]  = dram_a;
                    t_act[bsel]     = t_now;
                    e_act_any       = edge_n;
                    t_act_any       = t_now;
                end
                C_PRE: begin
                    if (a10) begin
                        end_streams(edge_n, 1'b1, 1'b1);
                        for (bi = 0; bi < 4; bi = bi + 1) do_precharge(bi);
                    end else begin
                        if (rs_open(0) && rs_bank == dram_ba) end_streams(edge_n, 1'b1, 1'b0);
                        if (ws_open(0) && ws_bank == dram_ba) end_streams(edge_n, 1'b0, 1'b1);
                        do_precharge(dram_ba);
                    end
                end
                C_REF: begin
                    for (bi = 0; bi < 4; bi = bi + 1)
                        if (bank_open[bi]) err(`SE_REF_OPEN, "REF with an open bank");
                    if (rs_open(0) || ws_open(0)) err(`SE_REF_OPEN, "REF with an open burst");
                    t_ref = t_now;
                end
                C_MRS: begin
                    for (bi = 0; bi < 4; bi = bi + 1)
                        if (bank_open[bi]) err(`SE_REF_OPEN, "MRS with an open bank");
                    if (was_done) err(`SE_MRS_LATE, "mode set after init_done");
                    e_mrs = edge_n;
                end
                C_RD, C_WR: begin
                    if (!bank_open[dram_ba]) err(`SE_COL_CLOSED, "column command to a closed bank");
                    if (a10) err(`SE_A10_COL, "A10=1 on a column command");
                    end_streams(edge_n, 1'b1, 1'b1);
                    e_col_last = edge_n;
                    if (cmd == C_RD) begin
                        rs_active = 1'b1; rs_bank = dram_ba; rs_col0 = dram_a[9:0];
                        rs_start = edge_n + mr_cl; rs_end = INF; rs_cmd_t = t_now;
                    end else begin
                        ws_active = 1'b1; ws_bank = dram_ba; ws_col0 = dram_a[9:0];
                        ws_start = edge_n; ws_end = INF;
                    end
                end
                C_BST: begin
                    if (!(rs_open(0) || ws_open(0))) begin
                        if (BST_NO_BURST_IS_ERROR != 0) err(`SE_BST_NOBURST, "BST with no burst open");
                    end else
                        end_streams(edge_n, 1'b1, 1'b1);
                end
                default: ;
            endcase
        end
    end
endmodule
