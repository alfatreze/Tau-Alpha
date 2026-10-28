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
    // refresh and row-age bookkeeping
    // ---------------------------------------------------------------------------------------------------------
    localparam integer TDAL_PS = TWR_PS + TRP_PS;              // contract section 2: tDAL = tWR + tRP (4 clk at 100 MHz)
    localparam longint TREF_LIMIT_PS = TREF_PS + TREF_SLACK_PS;
    longint row_ts [0:8191];        // time each row index was last refreshed by REF (8,192 timestamps)
    integer ref_ctr;                // the internal refresh row counter
    reg     tref_flag;
    reg     tras_flag [0:3];
    integer refreshes;
    integer k_r;
    longint t_edge;                 // time of the latest chip clock edge (for hold checks)
    longint t_chg_cmd, t_chg_adr, t_chg_cke, t_chg_dq;

    initial begin
        ref_ctr = 0; tref_flag = 1'b0; refreshes = 0; t_edge = -64'sd10_000_000_000_000;
        t_chg_cmd = -64'sd10_000_000_000_000; t_chg_adr = -64'sd10_000_000_000_000;
        t_chg_cke = -64'sd10_000_000_000_000; t_chg_dq = -64'sd10_000_000_000_000;
        for (k_r = 0; k_r < 8192; k_r = k_r + 1) row_ts[k_r] = 64'sd0;
        for (k_r = 0; k_r < 4; k_r = k_r + 1) tras_flag[k_r] = 1'b0;
    end

    // ---------------------------------------------------------------------------------------------------------
    // setup / hold at the chip pins: change timestamps, checked at the chip edge (setup) and at the change (hold)
    // ---------------------------------------------------------------------------------------------------------
    always @(dram_ras_n or dram_cas_n or dram_we_n or dram_dqm) begin
        t_chg_cmd = $time;
        if (($time - t_edge) >= 0 && ($time - t_edge) < TCMH_PS) err(`SE_HOLD, "command/DQM changed inside tCMH");
    end
    always @(dram_a or dram_ba) begin
        t_chg_adr = $time;
        if (($time - t_edge) >= 0 && ($time - t_edge) < TAH_PS) err(`SE_HOLD, "address/bank changed inside tAH");
    end
    always @(dram_cke) begin
        t_chg_cke = $time;
        if (($time - t_edge) >= 0 && ($time - t_edge) < TCKH_PS) err(`SE_HOLD, "CKE changed inside tCKH");
    end

    // ---------------------------------------------------------------------------------------------------------
    // the chip clock edge
    // ---------------------------------------------------------------------------------------------------------
    reg [2:0]  cmd;
    reg        cmd_known;
    reg        a10;
    integer    bsel;
    integer    bi;
    reg        was_done;
    longint    per;

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
                        for (k_r = 0; k_r < 8192; k_r = k_r + 1) row_ts[k_r] = t_now;   // rows start aging at init_done
                    end else err(`SE_INIT_ORDER, "expected EMRS after MRS");
                end
                default: ;
            endcase
        end
    endtask

    task do_precharge(input integer b);
        begin
            if (bank_open[b]) begin
                if (t_now - t_act[b] < TRAS_PS)             err(`SE_TRAS, "PRE before tRAS");
                if (edge_n - e_wr_last[b] < TDPL_CLK)       err(`SE_TDPL, "PRE before tDPL after the last written word");
                if (t_now - t_wr_last[b] < TWR_PS)          err(`SE_TWR,  "PRE before tWR after the last written word");
            end
            bank_open[b] = 1'b0;
            tras_flag[b] = 1'b0;
            t_pre[b]     = t_now;
        end
    endtask

    task check_trp_all(input [8*20-1:0] what);
        integer q;
        begin
            for (q = 0; q < 4; q = q + 1)
                if (t_now - t_pre[q] < TRP_PS) err(`SE_TRP, what);
        end
    endtask

    always @(posedge dram_clk) begin
        edge_n = edge_n + 1;
        t_prev = t_now;
        t_now  = $time;
        t_edge = t_now;
        if (edge_n == 0) t_first = t_now;

        // clock period
        if (edge_n > 0) begin
            per = t_now - t_prev;
            if (per < ((mr_cl == 3) ? TCK_MIN_CL3_PS : TCK_MIN_CL2_PS)) err(`SE_TCK, "chip clock period below tCK(min)");
        end

        // setup of command, DQM, address and CKE at this edge
        if (t_now - t_chg_cmd < TCMS_PS) err(`SE_SETUP, "command/DQM setup (tCMS)");
        if (t_now - t_chg_adr < TAS_PS)  err(`SE_SETUP, "address/bank setup (tAS)");
        if (t_now - t_chg_cke < TCKS_PS) err(`SE_SETUP, "CKE setup (tCKS)");

        cmd   = {dram_ras_n, dram_cas_n, dram_we_n};
        cmd_known = !(^{dram_ras_n, dram_cas_n, dram_we_n} === 1'bx);
        a10   = dram_a[10];

        // refresh ageing, tRAS(max): checked every clock once the part is initialised
        if (init_done) begin
            if (!tref_flag && (t_now - row_ts[ref_ctr] > TREF_LIMIT_PS)) begin
                err(`SE_TREF, "a row exceeded the refresh limit");
                tref_flag = 1'b1;
            end
            for (bi = 0; bi < 4; bi = bi + 1)
                if (bank_open[bi] && !tras_flag[bi] && (t_now - t_act[bi] > TRAS_MAX_PS)) begin
                    err(`SE_TRAS_MAX, "bank open longer than tRAS(max)");
                    tras_flag[bi] = 1'b1;
                end
        end

        if (dram_cke !== 1'b1) begin
            err(`SE_CKE_LOW, "CKE not high at a chip clock edge");
        end else if (!cmd_known) begin
            err(`SE_UNKNOWN, "X/Z on RAS/CAS/WE");
        end else begin
            if (cmd != C_NOP && cmd != C_BST && (^{dram_a, dram_ba} === 1'bx) && cmd != C_REF)
                err(`SE_UNKNOWN, "X/Z on the address or bank pins");
            was_done = init_done;
            if (!was_done && cmd != C_NOP) init_cmd(cmd);
            if (cmd != C_NOP) begin
                if (t_now - t_ref < TRFC_PS)       err(`SE_TRFC, "command inside tRFC after REF");
                if (edge_n - e_mrs < TMRD_CLK)     err(`SE_TMRD, "command inside tMRD after MRS/EMRS");
            end
            case (cmd)
                C_NOP: begin
                    if (!was_done) nop_edges = nop_edges + 1;
                end
                C_ACT: begin
                    bsel = dram_ba;
                    if (bank_open[bsel])                       err(`SE_ACT_OPEN, "ACT to an open bank");
                    if (t_now - t_pre[bsel] < TRP_PS)          err(`SE_TRP, "ACT before tRP after PRE");
                    if (t_now - t_act[bsel] < TRC_PS)          err(`SE_TRC, "ACT to ACT (same bank) before tRC");
                    if (edge_n - e_act_any < TRRD_CLK)         err(`SE_TRRD, "ACT to ACT before tRRD");
                    if (t_now - t_wr_last[bsel] < TDAL_PS)     err(`SE_TDAL, "ACT before tDAL after the last written word");
                    bank_open[bsel] = 1'b1;
                    bank_row[bsel]  = dram_a;
                    t_act[bsel]     = t_now;
                    tras_flag[bsel] = 1'b0;
                    t_wr_last[bsel] = -64'sd10_000_000_000_000;
                    e_wr_last[bsel] = -1000;
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
                    for (bi = 0; bi < 4; bi = bi + 1) begin
                        if (bank_open[bi])                      err(`SE_REF_OPEN, "REF with an open bank");
                        if (t_now - t_pre[bi] < TRP_PS)         err(`SE_TRP, "REF before tRP after PRE");
                        if (t_now - t_act[bi] < TRC_PS)         err(`SE_ACT_REF, "REF before tRC after ACT");
                    end
                    if (rs_open(0) || ws_open(0))               err(`SE_REF_OPEN, "REF with an open burst");
                    if (init_done && !tref_flag && (t_now - row_ts[ref_ctr] > TREF_LIMIT_PS))
                        err(`SE_TREF, "row refreshed after the refresh limit");
                    row_ts[ref_ctr] = t_now;
                    ref_ctr   = (ref_ctr + 1) % 8192;
                    tref_flag = 1'b0;
                    refreshes = refreshes + 1;
                    t_ref     = t_now;
                end
                C_MRS: begin
                    for (bi = 0; bi < 4; bi = bi + 1) begin
                        if (bank_open[bi])                      err(`SE_REF_OPEN, "MRS with an open bank");
                    end
                    check_trp_all("MRS before tRP");
                    if (was_done) err(`SE_MRS_LATE, "mode set after init_done");
                    e_mrs = edge_n;
                end
                C_RD, C_WR: begin
                    if (!bank_open[dram_ba])                    err(`SE_COL_CLOSED, "column command to a closed bank");
                    if (t_now - t_act[dram_ba] < TRCD_PS)       err(`SE_TRCD, "column command before tRCD");
                    if (edge_n - e_col_last < TCCD_CLK)         err(`SE_TCCD, "column command inside tCCD");
                    if (a10)                                    err(`SE_A10_COL, "A10=1 on a column command");
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
