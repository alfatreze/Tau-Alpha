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
    // FPGA I/O path delays (B-915: STA of the probe fit, slow 85C, nothing measured on a Pocket): the chip clock leaves
    // the FPGA through the DDIO output cell and the pad and reaches the chip LATE with respect to the clock of the
    // capture registers (CLKOUT: measured 3.6 ns, min = max - 0.4 ns assumed), and the DQ input pad + input cell adds
    // a delay before the capture register (DQIN: measured 2.7 ns, min = max - 0.4 ns assumed). They shift every
    // launched word (chip clock late: the whole drive window; DQ input path: the data seen by the FPGA) and are part
    // of the analytic capture window. Set all four to 0 for the ideal chip-clock/pad of the first tests.
    parameter integer CLKOUT_MIN_PS  = 3200,
    parameter integer CLKOUT_MAX_PS  = 3600,
    parameter integer DQIN_MIN_PS    = 2300,
    parameter integer DQIN_MAX_PS    = 2700,
    parameter integer CAP_MODE       = 0,       // 0 = rising edge of cap_clk (default since B-915: the PHY captures the rising edge), 1 = falling edge
    parameter integer CAP_SKEW_PS    = 0,       // capture-register clock arrival after the cap_clk edge
    parameter integer FPGA_SU_PS     = 0,       // capture register setup
    parameter integer FPGA_H_PS      = 0,       // capture register hold
    parameter integer CAP_CHECK_EN   = 1,
    // ---- model options ----------------------------------------------------------------------------------------
    parameter integer CL_DEFAULT     = 2,       // used until the MRS is seen
    parameter integer BST_NO_BURST_IS_ERROR = 1,
    parameter integer MEM_LOG2       = 16,      // sparse store: 2^MEM_LOG2 direct-mapped words (collision = error print)
    parameter integer MEM_COMPACT    = 0,       // 2 (needs MEM_LOG2 = 22; sim/sdram_replay): exact for row[9:7] = 0; 1 (needs MEM_LOG2 = 20): exact, collision-free store for a test that only uses
                                                //   rows with row[12] = 0 and row[7:4] = 0 (all banks, all columns); added by the
                                                //   sched_v tests, default 0 = the original hash
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

    task err(input integer code, input [8*64-1:0] msg);
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
    // read streams: a FIFO of descriptors, because a new READ at edge c ends the previous stream only at slot c+CL
    // (up to CL streams are in flight). A "slot" is a capture-edge number: word k of a READ at edge r is slot r+CL+k.
    localparam integer RQ = 8;
    integer     rq_start [0:RQ-1];
    integer     rq_end   [0:RQ-1];
    integer     rq_bank  [0:RQ-1];
    integer     rq_row   [0:RQ-1];
    integer     rq_col0  [0:RQ-1];
    integer     rq_cnt   [0:RQ-1];   // unmasked words launched
    reg         rq_wrap  [0:RQ-1];
    longint     rq_t     [0:RQ-1];   // chip edge time of the READ command
    integer     rq_head, rq_n;
    reg         ws_active;           // the write stream: data is consumed at the edge, no latency
    integer     ws_bank, ws_row, ws_col0, ws_start, ws_end;
    reg         ws_wrap;

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
        rq_head = 0; rq_n = 0;
        ws_active = 1'b0; ws_bank = 0; ws_row = 0; ws_col0 = 0; ws_start = 0; ws_end = INF; ws_wrap = 1'b0;
    end

    // ---------------------------------------------------------------------------------------------------------
    // burst helpers
    // ---------------------------------------------------------------------------------------------------------
    function integer rq_tail(input integer dummy);
        begin rq_tail = (rq_head + rq_n - 1) % RQ; end
    endfunction
    function rs_open(input integer dummy);      // a read burst exists and has not been terminated yet
        begin rs_open = (rq_n > 0) && (rq_end[rq_tail(0)] == INF); end
    endfunction
    function ws_open(input integer dummy);
        begin ws_open = ws_active && (ws_end == INF); end
    endfunction

    // terminate the streams at command edge c: read ends after c + CL (exactly c - r words), write ends at c
    task end_streams(input integer c, input reg rd_too, input reg wr_too);
        begin
            if (rd_too && rs_open(0)) rq_end[rq_tail(0)] = c + mr_cl;
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


    // ---------------------------------------------------------------------------------------------------------
    // data model: sparse direct-mapped store (64 MB would not fit a simulator array), per-byte valid flags
    // ---------------------------------------------------------------------------------------------------------
    localparam integer MEM_N = (1 << MEM_LOG2);
    reg [15:0] mem_d [0:MEM_N-1];
    reg [24:0] mem_a [0:MEM_N-1];
    reg [1:0]  mem_v [0:MEM_N-1];
    integer    mem_collisions, uninit_reads;
    integer    wr_words, wr_bursts, rd_words, rd_bursts, last_rd_n, last_rd_launched, last_rd_cap_half;
    integer    mi;
    initial begin
        for (mi = 0; mi < MEM_N; mi = mi + 1) begin mem_v[mi] = 2'b00; mem_a[mi] = 25'd0; mem_d[mi] = 16'hxxxx; end
        mem_collisions = 0; uninit_reads = 0; wr_words = 0; wr_bursts = 0; rd_words = 0; rd_bursts = 0;
        last_rd_n = 0; last_rd_launched = 0; last_rd_cap_half = 0;
    end
    function integer mhash(input [24:0] a);
        reg [24:0] h;
        begin
            if (MEM_COMPACT == 2) begin h = 25'd0; h[21:0] = {a[24:20], a[16:0]}; mhash = h & (MEM_N - 1); end   // replay harness: exact for row[9:7] = 0 (needs MEM_LOG2 = 22)
            else if (MEM_COMPACT != 0) begin h = 25'd0; h[19:0] = {a[24:23], a[21:18], a[13:10], a[9:0]}; mhash = h & (MEM_N - 1); end
            else begin h = a ^ (a >> MEM_LOG2) ^ (a >> (2 * MEM_LOG2)); mhash = h & (MEM_N - 1); end
        end
    endfunction
    task mem_wr(input integer b, input integer r, input integer c, input integer lane, input [7:0] v);
        reg [24:0] a; integer h;
        begin
            a = {b[1:0], r[12:0], c[9:0]}; h = mhash(a);
            if (mem_v[h] != 2'b00 && mem_a[h] != a) begin
                mem_collisions = mem_collisions + 1;
                $display("[CHIPMODEL] store collision at %0t (raise MEM_LOG2): old data dropped", $time);
                mem_v[h] = 2'b00;
            end
            mem_a[h] = a; mem_v[h][lane] = 1'b1;
            if (lane == 0) mem_d[h][7:0] = v; else mem_d[h][15:8] = v;
        end
    endtask
    function [15:0] mem_rd(input integer b, input integer r, input integer c, input [1:0] lanes);
        reg [24:0] a; integer h; reg [15:0] w;
        begin
            a = {b[1:0], r[12:0], c[9:0]}; h = mhash(a); w = 16'hxxxx;
            if (mem_a[h] == a && mem_v[h][0]) w[7:0]  = mem_d[h][7:0];
            if (mem_a[h] == a && mem_v[h][1]) w[15:8] = mem_d[h][15:8];
            mem_rd = w;
        end
    endfunction
    // back door for the test bench: 17 bits {valid-both-bytes, data}
    function [16:0] peek(input integer b, input integer r, input integer c);
        reg [24:0] a; integer h;
        begin
            a = {b[1:0], r[12:0], c[9:0]}; h = mhash(a);
            peek = {(mem_a[h] == a && mem_v[h] == 2'b11), (mem_a[h] == a) ? mem_d[h] : 16'hxxxx};
        end
    endfunction

    // read stream FIFO
    task rq_push(input integer b, input integer r, input integer c);
        integer t;
        begin
            if (rq_n == RQ) begin
                $display("[CHIPMODEL] read stream FIFO overflow"); rq_head = (rq_head + 1) % RQ; rq_n = rq_n - 1;
            end
            t = (rq_head + rq_n) % RQ;
            rq_start[t] = edge_n + mr_cl; rq_end[t] = INF; rq_bank[t] = b; rq_row[t] = r; rq_col0[t] = c;
            rq_cnt[t] = 0; rq_wrap[t] = 1'b0; rq_t[t] = t_now;
            rq_n = rq_n + 1;
        end
    endtask

    // DQM history: dqm_hist[j] is DQM at edge (edge_n - j)
    reg [1:0] dqm_hist [0:7];
    integer   dj;
    initial for (dj = 0; dj < 8; dj = dj + 1) dqm_hist[dj] = 2'b11;
    reg       dq_hold_arm;
    initial dq_hold_arm = 1'b0;
    always @(dram_dq) begin
        t_chg_dq = $time;
        if (dq_hold_arm && ($time - t_edge) >= 0 && ($time - t_edge) < TDH_PS) err(`SE_HOLD, "write data changed inside tDH");
    end

    // write data: latency 0, the word at chip edge w+k is written when DQM is low at that edge
    task write_step;
        integer idx, col, ln; reg any;
        begin
            dq_hold_arm = 1'b0;
            if (ws_active && edge_n >= ws_start && edge_n < ws_end) begin
                idx = edge_n - ws_start; col = ws_col0 + idx; any = 1'b0;
                if (col > 1023 && !ws_wrap) begin err(`SE_COL_WRAP, "write burst ran past column 1023"); ws_wrap = 1'b1; end
                col = col & 1023;
                for (ln = 0; ln < 2; ln = ln + 1) begin
                    if (dram_dqm[ln] === 1'bx || dram_dqm[ln] === 1'bz) err(`SE_UNKNOWN, "X on DQM during a write burst");
                    else if (dram_dqm[ln] === 1'b0) begin
                        if ((^dram_dq[ln*8 +: 8]) === 1'bx) err(`SE_WDATA_X, "unmasked write byte is X/Z");
                        if (t_now - t_chg_dq < TDS_PS) err(`SE_SETUP, "write data setup (tDS)");
                        mem_wr(ws_bank, ws_row, col, ln, dram_dq[ln*8 +: 8]);
                        any = 1'b1;
                    end
                end
                if (any) begin
                    e_wr_last[ws_bank] = edge_n; t_wr_last[ws_bank] = t_now; wr_words = wr_words + 1; dq_hold_arm = 1'b1;
                end
            end
        end
    endtask

    // read data: at chip edge E the chip launches the word whose capture slot is E+1 (valid tAC after E)
    reg [1:0]  lau_lane;            // byte lanes driven for the slot launched at this edge
    reg [15:0] lau_word;
    integer    lau_slot, lau_k, lau_stream;
    task read_launch_step;
        integer s, k, col, h; reg [1:0] msk;
        begin
            lau_lane = 2'b00; lau_word = 16'hxxxx; lau_stream = -1;
            s = edge_n + 1;
            while (rq_n > 0 && s >= rq_end[rq_head]) begin     // stream finished: record its statistics
                last_rd_n = rq_end[rq_head] - rq_start[rq_head]; last_rd_launched = rq_cnt[rq_head];
                rd_bursts = rd_bursts + 1;
                rq_head = (rq_head + 1) % RQ; rq_n = rq_n - 1;
            end
            if (rq_n > 0 && s >= rq_start[rq_head]) begin
                h = rq_head; k = s - rq_start[h]; col = rq_col0[h] + k;
                if (col > 1023 && !rq_wrap[h]) begin err(`SE_COL_WRAP, "read burst ran past column 1023"); rq_wrap[h] = 1'b1; end
                col = col & 1023;
                msk = dqm_hist[TDQZ_CLK - 1];            // DQM at edge s - tDQZ masks this slot
                lau_lane = ~msk; lau_slot = s; lau_k = k; lau_stream = h;
                lau_word = mem_rd(rq_bank[h], rq_row[h], col, 2'b11);
                if (lau_lane != 2'b00) begin
                    rq_cnt[h] = rq_cnt[h] + 1; rd_words = rd_words + 1;
                    if (^lau_word === 1'bx) uninit_reads = uninit_reads + 1;
                end
            end
        end
    endtask


    // ---------------------------------------------------------------------------------------------------------
    // picosecond analysis of the DQ bus
    //   chip side : at chip edge E the chip launches the word of capture slot E+1. The pin is unknown from
    //               E + tOH + dmin (first word: E + tLZ + dmin, when the output leaves high-Z) and valid from
    //               E + tAC + dmax until the next edge's tOH + dmin. After the last word the output goes high-Z
    //               at E' + tHZ + dmax (E' = the first edge that launches nothing).
    //   FPGA side : fpga_oe (per bit) is the FPGA output enable as seen at the pin.
    //   conflict  : chip drive window (win) and FPGA output enable both high = SE_BUS_CONFLICT.
    //   capture   : every launched, unmasked word must have at least one edge of the capture clock (rising or
    //               falling edge of cap_clk plus CAP_SKEW_PS, selected by CAP_MODE) inside
    //               [E + tAC + dmax + FPGA_SU, E + P + tOH + dmin - FPGA_H].
    // ---------------------------------------------------------------------------------------------------------
    reg  [1:0]  drv_en, drv_x, win, lane_drv;     // drv_*: pin state (delayed); win: drive window; lane_drv: logical
    reg  [15:0] drv_val;
    initial begin drv_en = 2'b00; drv_x = 2'b00; win = 2'b00; lane_drv = 2'b00; drv_val = 16'h0000; end
    assign dram_dq[7:0]  = drv_en[0] ? (drv_x[0] ? 8'bxxxxxxxx : drv_val[7:0])  : 8'bzzzzzzzz;
    assign dram_dq[15:8] = drv_en[1] ? (drv_x[1] ? 8'bxxxxxxxx : drv_val[15:8]) : 8'bzzzzzzzz;

    // bus conflict: evaluated at every change of the chip drive window or of the FPGA output enable
    always @(win or fpga_oe) begin
        if (win[0] === 1'b1 && ((|fpga_oe[7:0]) === 1'b1))
            err(`SE_BUS_CONFLICT, "FPGA drives DQ[7:0] while the chip may drive");
        if (win[1] === 1'b1 && ((|fpga_oe[15:8]) === 1'b1))
            err(`SE_BUS_CONFLICT, "FPGA drives DQ[15:8] while the chip may drive");
    end

    // capture-edge record (the FPGA's capture clock, in ps including the register clock skew)
    longint cap_t [0:7];
    integer cap_n, ci;
    initial begin cap_n = 0; for (ci = 0; ci < 8; ci = ci + 1) cap_t[ci] = -64'sd10_000_000_000_000; end
    task rec_cap;
        begin cap_t[cap_n % 8] = $time + CAP_SKEW_PS; cap_n = cap_n + 1; end
    endtask
    always @(posedge cap_clk) if (CAP_MODE == 0) rec_cap;
    always @(negedge cap_clk) if (CAP_MODE != 0) rec_cap;

    // launch records of the last 4 chip edges (evaluated two edges later, when the window has closed)
    longint lr_t   [0:3];
    reg [1:0] lr_lane [0:3];
    integer lr_k   [0:3];
    longint lr_rdt [0:3];
    integer lri;
    initial for (lri = 0; lri < 4; lri = lri + 1) begin lr_t[lri] = 0; lr_lane[lri] = 2'b00; lr_k[lri] = 0; lr_rdt[lri] = 0; end
    longint min_su_margin, min_h_margin;                 // smallest margin beyond FPGA_SU/FPGA_H seen on a found edge
    longint win_vs_rel, win_ve_rel;                      // word-0 valid window relative to the READ's controller launch edge
    integer cap_words_ok, cap_words_bad;
    initial begin
        min_su_margin = 64'sd1_000_000_000; min_h_margin = 64'sd1_000_000_000;
        win_vs_rel = 0; win_ve_rel = 0; cap_words_ok = 0; cap_words_bad = 0;
    end

    task drive_step;
        integer ln, tac, thz, xd, vd;
        reg [7:0] byt;
        begin
            tac = (mr_cl == 3) ? TAC_CL3_PS : TAC_CL2_PS;
            thz = (mr_cl == 3) ? THZ_CL3_PS : THZ_CL2_PS;
            vd  = tac + DMAX_PS + CLKOUT_MAX_PS + DQIN_MAX_PS;       // data valid at the FPGA register
            for (ln = 0; ln < 2; ln = ln + 1) begin
                byt = (ln == 0) ? lau_word[7:0] : lau_word[15:8];
                if (lau_lane[ln]) begin
                    xd = (lane_drv[ln] ? TOH_PS : TLZ_PS) + DMIN_PS + CLKOUT_MIN_PS;              // chip output turns on / old data may change
                    if (!lane_drv[ln]) win[ln] <= #(xd) 1'b1;
                    drv_en[ln]  <= #(xd) 1'b1;
                    drv_x[ln]   <= #(xd + (lane_drv[ln] ? DQIN_MIN_PS : 0)) 1'b1;               // the input path keeps the old word a little longer
                    if (ln == 0) drv_val[7:0]  <= #(vd) byt; else drv_val[15:8] <= #(vd) byt;
                    drv_x[ln]   <= #(vd) 1'b0;
                    lane_drv[ln] = 1'b1;
                end else if (lane_drv[ln]) begin
                    drv_x[ln]  <= #(TOH_PS + DMIN_PS + CLKOUT_MIN_PS + DQIN_MIN_PS) 1'b1;
                    drv_en[ln] <= #(thz + DMAX_PS + CLKOUT_MAX_PS) 1'b0;
                    drv_x[ln]  <= #(thz + DMAX_PS + CLKOUT_MAX_PS) 1'b0;
                    win[ln]    <= #(thz + DMAX_PS + CLKOUT_MAX_PS) 1'b0;
                    lane_drv[ln] = 1'b0;
                end
            end
            // launch records
            lr_t[edge_n % 4]    = t_now;
            lr_lane[edge_n % 4] = lau_lane;
            lr_k[edge_n % 4]    = (lau_stream >= 0) ? lau_k : -1;
            lr_rdt[edge_n % 4]  = (lau_stream >= 0) ? rq_t[lau_stream] : 64'sd0;
        end
    endtask

    // evaluate the launch made two edges ago: its valid window has closed, so every capture edge in it is recorded
    task capture_step;
        integer q, tac, kk; longint vs, ve, pp, found, ct, ctrl_launch;
        reg ok;
        begin
            if (CAP_CHECK_EN != 0 && edge_n >= 2) begin
                q   = (edge_n - 2) % 4;
                if (lr_lane[q] != 2'b00) begin
                    tac = (mr_cl == 3) ? TAC_CL3_PS : TAC_CL2_PS;
                    pp  = t_now - t_prev;                              // the clock period
                    vs  = lr_t[q] + tac + DMAX_PS + CLKOUT_MAX_PS + DQIN_MAX_PS + FPGA_SU_PS;
                    ve  = lr_t[q] + pp + TOH_PS + DMIN_PS + CLKOUT_MIN_PS + DQIN_MIN_PS - FPGA_H_PS;
                    ok  = 1'b0; found = 0;
                    for (kk = 0; kk < 8; kk = kk + 1) begin
                        ct = cap_t[kk];
                        if (ct >= vs && ct <= ve && !ok) begin ok = 1'b1; found = ct; end
                    end
                    if (lr_k[q] == 0) begin
                        ctrl_launch = lr_rdt[q] - pp / 2;                // READ launch edge at the FPGA (clock is inverted)
                        win_vs_rel  = vs - FPGA_SU_PS - ctrl_launch;
                        win_ve_rel  = ve + FPGA_H_PS - ctrl_launch;
                        if (ok) last_rd_cap_half = ((found - ctrl_launch) * 2 + pp / 2) / pp;
                    end
                    if (ok) begin
                        cap_words_ok = cap_words_ok + 1;
                        if (found - vs < min_su_margin) min_su_margin = found - vs;
                        if (ve - found < min_h_margin)  min_h_margin  = ve - found;
                    end else begin
                        cap_words_bad = cap_words_bad + 1;
                        err(`SE_CAPTURE, "no capture edge inside the valid window of a read word");
                    end
                end
            end
        end
    endtask

    task report;
        begin
            $display("[CHIPMODEL] CL=%0d CAP_MODE=%0d delay=%0d..%0d ps (+ clock out %0d..%0d, DQ in %0d..%0d): word0 window rel. to launch edge [%0d, %0d] ps, last capture at %0d half-cycles from launch",
                     mr_cl, CAP_MODE, DMIN_PS, DMAX_PS, CLKOUT_MIN_PS, CLKOUT_MAX_PS, DQIN_MIN_PS, DQIN_MAX_PS, win_vs_rel, win_ve_rel, last_rd_cap_half);
            $display("[CHIPMODEL] capture words ok=%0d bad=%0d, min margin setup=%0d hold=%0d ps (beyond FPGA_SU/FPGA_H)",
                     cap_words_ok, cap_words_bad, min_su_margin, min_h_margin);
            $display("[CHIPMODEL] errors=%0d mask=%h refreshes=%0d wr_words=%0d rd_words=%0d uninit_reads=%0d",
                     err_count, err_mask, refreshes, wr_words, rd_words, uninit_reads);
        end
    endtask

    always @(posedge dram_clk) begin
        edge_n = edge_n + 1;
        t_prev = t_now;
        t_now  = $time;
        t_edge = t_now;
        if (edge_n == 0) t_first = t_now;
        for (dj = 7; dj > 0; dj = dj - 1) dqm_hist[dj] = dqm_hist[dj-1];
        dqm_hist[0] = dram_dqm;

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
                        if (rs_open(0) && rq_bank[rq_tail(0)] == dram_ba) end_streams(edge_n, 1'b1, 1'b0);
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
                        rq_push(dram_ba, bank_row[dram_ba], dram_a[9:0]);
                    end else begin
                        ws_active = 1'b1; ws_bank = dram_ba; ws_row = bank_row[dram_ba]; ws_col0 = dram_a[9:0];
                        ws_start = edge_n; ws_end = INF; ws_wrap = 1'b0;
                        wr_bursts = wr_bursts + 1;
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
        write_step;
        read_launch_step;
        drive_step;
        capture_step;
    end
endmodule
