// SDRAM PHY core: init, command path, permits, interlock, refresh timer, read capture. MIT licence.
// Clean-room: written from docs/features/SDRAM_CONTROLLER_CONTRACT.md v0.3 (sections 0-4, 7.2, 8), SDRAM_PART_FACTS.md,
// SDRAM_CHIP_MODEL_FINDINGS.md and the sibling-part datasheet text. No controller RTL was read.
// Decisions taken where the contract is silent are in docs/features/SDRAM_PHY_FINDINGS.md.
//
// This module has no tri-state and no clock output so that a test bench can put a pin-delay envelope around it;
// tau_sdram_phy.sv wraps it with the DQ tri-state and the DDIO chip clock.
//
// Cycle convention: a command offered in cycle a (inputs stable before the rising edge that ends cycle a) is
// registered into the pin registers at that edge (E); the chip samples it half a clock later (chip clock = ~clk).
// All times are integer picoseconds converted to clocks at elaboration (ceil of the exact ratio).
`default_nettype none
// Lint: the initial values give the pins their reset state before the first clock (CKE high from the first cycle); the
// constant width conversions of parameter arithmetic are intended.
/* verilator lint_off PROCASSINIT */
/* verilator lint_off WIDTHTRUNC */
/* verilator lint_off WIDTHEXPAND */
module tau_sdram_phy_core #(
    parameter integer CLK_HZ              = 100_000_000,
    parameter integer CL                  = 2,
    parameter integer RD_EXTRA            = 0,            // 0..2 extra read pipeline stages after the capture register
    parameter integer CAP_MODE            = 1,            // 1 = FALL (falling edge capture), 0 = RISE
    // times in ps (contract section 2)
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
    parameter integer THZ_CL2_PS          = 8000,         // chip: read data to high-Z
    parameter integer THZ_CL3_PS          = 5000,
    parameter integer OE_ON_MIN_PS        = 1500,         // FPGA: earliest DQ output-enable turn-on after the clock edge (assumption)
    parameter integer DQ_BOARD_MAX_PS     = 0,            // board delay added to the chip's high-Z time
    parameter integer OPEN_ROW_MAX_NS     = 60000,        // row_aged request
    parameter integer ROW_FORCE_NS        = 90000,        // forced close (fault)
    parameter integer REFRESH_INTERVAL_PS = 7_800_000,
    parameter integer REFRESH_DEBT_MAX    = 8,
    parameter integer INIT_WAIT_CYCLES    = 10_000,
    parameter integer PREEMPT_TIMEOUT     = 136,          // L_MAX + 8
    parameter integer SIM_SHORT_INIT      = 0,            // 1 allows INIT_WAIT_CYCLES below 100 us (simulation only)
    // optional features (contract 3.7b), all default off
    parameter integer CALIBRATE           = 0,            // init-time read-capture calibration (6 settings: FALL/RISE x RD_EXTRA 0..2)
    parameter integer CAL_BANK            = 3,            // reserved test row: bank, row, first CAL_LEN columns (chip coordinates)
    parameter integer CAL_ROW             = 8191,
    parameter integer CAL_LEN             = 64,
    parameter integer BIST                = 0,            // PHY-side march self-test
    parameter integer BIST_PAGE_LO        = 0,            // page = {row[12:0], bank[1:0]}, inclusive range
    parameter integer BIST_PAGE_HI        = 32767,
    parameter integer PERF                = 0             // performance counters
) (
    input  wire        clk,
    input  wire        rst,                 // synchronous, active high; a reset re-runs the init
    // ---- scheduler: command interface (section 3.3) ----
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
    // ---- optional features (unused and constant when the parameter is 0) ----
    input  wire        idle_ok,             // scheduler: quiet, banks closed, offering NOP (sampled when a BIST starts)
    output wire [7:0]  cal_window,          // settings that passed: bit k = (k/3 ? RISE : FALL) with RD_EXTRA k%3
    output wire [3:0]  cal_best,            // chosen setting, same index
    output wire        cal_done,
    output wire        cal_fault,           // no setting passed (also a phy_fault, cause 13); the PHY keeps its parameter setting
    input  wire        bist_start,          // a rising edge starts a run (remembered until idle_ok allows it)
    output wire        bist_busy,
    output wire        bist_done,
    output wire [31:0] bist_errors,
    output wire [24:0] bist_first_addr,     // {bank, row, col} of the first mismatch
    input  wire        perf_snap,           // strobe: copy the counters into perf_snapshot
    input  wire        perf_clear,
    output wire [511:0] perf_snapshot,
    // ---- pins (registered, one register per pin) ----
    output wire        pin_cke,
    output wire        pin_ras_n,
    output wire        pin_cas_n,
    output wire        pin_we_n,
    output wire [1:0]  pin_dqm,
    output wire [1:0]  pin_ba,
    output wire [12:0] pin_a,
    output wire [15:0] dq_o,
    output wire [15:0] dq_oe,
    input  wire [15:0] dq_i
);
    // ---------------------------------------------------------------------------------------------------------
    // command encoding (scheduler side)
    // ---------------------------------------------------------------------------------------------------------
    localparam [2:0] CMD_NOP = 3'd0, CMD_ACT = 3'd1, CMD_RD = 3'd2, CMD_WR = 3'd3,
                     CMD_PRE = 3'd4, CMD_PREA = 3'd5, CMD_REF = 3'd6, CMD_BST = 3'd7;

    // ---------------------------------------------------------------------------------------------------------
    // timing in clocks
    // ---------------------------------------------------------------------------------------------------------
    localparam integer TCK_PS = 1_000_000_000 / (CLK_HZ / 1000);
    function integer cdiv(input integer t);          // ceil of the exact ratio t / TCK_PS
        begin cdiv = (t + TCK_PS - 1) / TCK_PS; end
    endfunction
    function integer imax(input integer a, input integer b);
        begin imax = (a > b) ? a : b; end
    endfunction
    localparam integer TRCD_C = imax(1, cdiv(TRCD_PS));
    localparam integer TRP_C  = imax(1, cdiv(TRP_PS));
    localparam integer TRAS_C = imax(1, cdiv(TRAS_PS));
    localparam integer TRC_C  = imax(1, cdiv(TRC_PS));
    localparam integer TRFC_C = imax(1, cdiv(TRFC_PS));
    localparam integer TWR_C  = cdiv(TWR_PS);
    localparam integer WRP_C  = imax(imax(1, TDPL_CLK), TWR_C);          // last written word to PRE
    localparam integer DAL_C  = imax(WRP_C + TRP_C, cdiv(TWR_PS + TRP_PS));   // last written word to ACT (tDAL)
    localparam integer RRD_C  = imax(1, TRRD_CLK);
    localparam integer MRD_C  = imax(1, TMRD_CLK);
    localparam integer THZ_PS = (CL == 3) ? THZ_CL3_PS : THZ_CL2_PS;
    // read to write: WR may be offered TURN_C clocks after the last wanted read word L (the chip may drive until
    // half a clock + tHZ after the edge that launches L; the FPGA output enable arrives OE_ON_MIN_PS after the edge)
    localparam integer TURN_C = imax(1, cdiv(TCK_PS / 2 + THZ_PS + DQ_BOARD_MAX_PS - OE_ON_MIN_PS));
    localparam integer RW_C   = CL - 1 + TURN_C;                         // from the read-terminating cycle to the earliest WR offer
    localparam integer REFI_C = REFRESH_INTERVAL_PS / TCK_PS;
    localparam integer AGE_REQ_C   = (OPEN_ROW_MAX_NS * 1000) / TCK_PS;
    localparam integer AGE_FORCE_C = (ROW_FORCE_NS * 1000) / TCK_PS;
    // exported read latency (cycles from the accepting cycle to word 0 on rdata with rvalid = 1)
    localparam integer RD_LAT = ((CAP_MODE == 1) ? CL + 2 : CL + 1) + RD_EXTRA;

    localparam integer CW = 6;      // counter width for the timing counters
    localparam integer AW = 15;     // row age counter width

    // ---------------------------------------------------------------------------------------------------------
    // elaboration checks
    // ---------------------------------------------------------------------------------------------------------
    generate
        if (CL != 2 && CL != 3) begin : g_chk_cl
            initial $error("tau_sdram_phy: CL must be 2 or 3 (got %0d)", CL);
        end
        if (CL == 2 && TCK_PS < TCK_MIN_CL2_PS) begin : g_chk_tck2
            initial $error("tau_sdram_phy: CL2 needs tCK >= %0d ps, clock period is %0d ps", TCK_MIN_CL2_PS, TCK_PS);
        end
        if (CL == 3 && TCK_PS < TCK_MIN_CL3_PS) begin : g_chk_tck3
            initial $error("tau_sdram_phy: CL3 needs tCK >= %0d ps, clock period is %0d ps", TCK_MIN_CL3_PS, TCK_PS);
        end
        if (RD_EXTRA < 0 || RD_EXTRA > 2) begin : g_chk_rdx
            initial $error("tau_sdram_phy: RD_EXTRA must be 0..2 (got %0d)", RD_EXTRA);
        end
        if (CAP_MODE != 0 && CAP_MODE != 1) begin : g_chk_cap
            initial $error("tau_sdram_phy: CAP_MODE must be 0 (RISE) or 1 (FALL)");
        end
        if (CLK_HZ < 1_000_000 || CLK_HZ > 200_000_000) begin : g_chk_hz
            initial $error("tau_sdram_phy: CLK_HZ out of range");
        end
        if (SIM_SHORT_INIT == 0 && INIT_WAIT_CYCLES * TCK_PS < 100_000_000) begin : g_chk_init
            initial $error("tau_sdram_phy: INIT_WAIT_CYCLES * tCK must be at least 100 us (%0d cycles of %0d ps)", INIT_WAIT_CYCLES, TCK_PS);
        end
        if (INIT_WAIT_CYCLES < 2) begin : g_chk_init2
            initial $error("tau_sdram_phy: INIT_WAIT_CYCLES must be >= 2");
        end
        if (REFRESH_DEBT_MAX < 1 || REFRESH_DEBT_MAX > 15) begin : g_chk_debt
            initial $error("tau_sdram_phy: REFRESH_DEBT_MAX must be 1..15");
        end
        if (REFI_C > 65535) begin : g_chk_refi2
            initial $error("tau_sdram_phy: REFRESH_INTERVAL_PS too long for the 16-bit timer");
        end
        if (REFI_C < 1) begin : g_chk_refi
            initial $error("tau_sdram_phy: REFRESH_INTERVAL_PS shorter than one clock");
        end
        if (TRCD_C > 60 || TRP_C > 60 || TRAS_C > 60 || TRC_C > 60 || TRFC_C > 60 || DAL_C > 60 || RW_C > 60 || RRD_C > 60 || MRD_C > 60) begin : g_chk_cw
            initial $error("tau_sdram_phy: a timing parameter exceeds the %0d-bit counters", CW);
        end
        if (AGE_FORCE_C >= (1 << AW) - 2 || AGE_REQ_C >= AGE_FORCE_C) begin : g_chk_age
            initial $error("tau_sdram_phy: row age limits out of range (request %0d, force %0d clocks)", AGE_REQ_C, AGE_FORCE_C);
        end
        if (TRAS_C < 1 || TRC_C < TRAS_C || (TRCD_C > TRAS_C)) begin : g_chk_rel
            initial $error("tau_sdram_phy: inconsistent tRCD/tRAS/tRC");
        end
        if (CALIBRATE != 0 && (CAL_LEN < 2 || CAL_LEN > 1024 || CAL_BANK < 0 || CAL_BANK > 3 || CAL_ROW < 0 || CAL_ROW > 8191)) begin : g_chk_cal
            initial $error("tau_sdram_phy: CALIBRATE needs 2 <= CAL_LEN <= 1024, bank 0..3, row 0..8191");
        end
        if (BIST != 0 && (BIST_PAGE_LO < 0 || BIST_PAGE_HI > 32767 || BIST_PAGE_LO > BIST_PAGE_HI)) begin : g_chk_bist
            initial $error("tau_sdram_phy: BIST page range must satisfy 0 <= LO <= HI <= 32767");
        end
        if (PREEMPT_TIMEOUT < 1 || PREEMPT_TIMEOUT > 4095) begin : g_chk_tmo
            initial $error("tau_sdram_phy: PREEMPT_TIMEOUT must be 1..4095");
        end
    endgenerate
    // optional-feature plumbing (defined at the end of the module)
    wire        cal_end, cal_busy_w, n_cal_busy_w, f_cal;
    wire        bist_busy_w, n_bist_busy_w;
    wire        fe_busy   = cal_busy_w | bist_busy_w;           // calibration or BIST owns the command bus
    wire        n_fe_busy = n_cal_busy_w | n_bist_busy_w;
    wire        fe_v;
    wire [2:0]  fe_cmd;
    wire [1:0]  fe_ba;
    wire [12:0] fe_a;
    wire        fe_wv;
    wire [15:0] fe_wd;
    wire [1:0]  fe_be;
    wire [3:0]  tpend;
    wire        cal_set_v, cal_set_rise;
    wire [1:0]  cal_set_ext;
    reg         sel_rise_q = (CAP_MODE == 0);
    reg [1:0]   ext_q      = RD_EXTRA;

    // ---------------------------------------------------------------------------------------------------------
    // init sequencer (also after every reset): wait, PREA, tRP, REF, tRFC, REF, tRFC, MRS, tMRD, EMRS, tMRD
    // ---------------------------------------------------------------------------------------------------------
    localparam [2:0] I_WAIT = 3'd0, I_REF1 = 3'd1, I_REF2 = 3'd2, I_MRS = 3'd3, I_EMRS = 3'd4, I_FIN = 3'd5;
    localparam integer IW = 15;
    reg [2:0]    is_q   = I_WAIT;
    reg [IW-1:0] iw_q   = INIT_WAIT_CYCLES - 1;
    reg          init_done_q = 1'b0;
    assign init_done = init_done_q;

    localparam [IW-1:0] IW_TRP  = TRP_C - 1;
    localparam [IW-1:0] IW_TRFC = TRFC_C - 1;
    localparam [IW-1:0] IW_TMRD = MRD_C - 1;
    localparam [IW-1:0] IW_WAIT = INIT_WAIT_CYCLES - 1;

    reg  [2:0]    n_is;
    reg  [IW-1:0] n_iw;
    wire          fin_ev = (is_q == I_FIN) && (iw_q == {IW{1'b0}});
    wire          n_init_done = init_done_q | ((CALIBRATE != 0) ? cal_end : fin_ev);
    reg           i_v;                 // internal command from the init sequencer
    reg  [2:0]    i_cmd;
    reg           i_mrs, i_emrs;
    always_comb begin
        n_is = is_q; n_iw = iw_q; i_v = 1'b0; i_cmd = CMD_NOP; i_mrs = 1'b0; i_emrs = 1'b0;
        if (!init_done_q) begin
            if (iw_q != {IW{1'b0}}) n_iw = iw_q - 1'b1;
            else begin
                case (is_q)
                    I_WAIT: begin i_v = 1'b1; i_cmd = CMD_PREA; n_is = I_REF1; n_iw = IW_TRP;  end
                    I_REF1: begin i_v = 1'b1; i_cmd = CMD_REF;  n_is = I_REF2; n_iw = IW_TRFC; end
                    I_REF2: begin i_v = 1'b1; i_cmd = CMD_REF;  n_is = I_MRS;  n_iw = IW_TRFC; end
                    I_MRS:  begin i_v = 1'b1; i_mrs = 1'b1;                    n_is = I_EMRS; n_iw = IW_TMRD; end
                    I_EMRS: begin i_v = 1'b1; i_mrs = 1'b1; i_emrs = 1'b1;     n_is = I_FIN;  n_iw = IW_TMRD; end
                    default: ;
                endcase
            end
        end
    end
    always @(posedge clk) begin
        if (rst) begin
            is_q <= I_WAIT; iw_q <= IW_WAIT; init_done_q <= 1'b0;
        end else begin
            is_q <= n_is; iw_q <= n_iw; init_done_q <= n_init_done;
        end
    end

    // ---------------------------------------------------------------------------------------------------------
    // forced-refresh sequencer (defined below, section "refresh timer, preemption, aged rows")
    // ---------------------------------------------------------------------------------------------------------
    wire       busy_seq;               // the PHY owns the bus (forced BST / PREA / REF)
    wire       s_v;
    wire [2:0] s_cmd;
    wire       n_busy_seq;
    wire       f_force;                // the forced sequence starts (a fault)
    wire       f_force_age;            // ... because a row reached the force age (else the refresh debt)

    // ---------------------------------------------------------------------------------------------------------
    // interlock: the offered command against the registered permits
    // ---------------------------------------------------------------------------------------------------------
    reg [3:0] ok_act_q = 4'd0, ok_rd_q = 4'd0, ok_wr_q = 4'd0, ok_pre_q = 4'd0;
    reg       ok_prea_q = 1'b0, ok_ref_q = 1'b0, ok_bst_q = 1'b0;
    assign ok_act = ok_act_q; assign ok_rd = ok_rd_q; assign ok_wr = ok_wr_q; assign ok_pre = ok_pre_q;
    assign ok_prea = ok_prea_q; assign ok_ref = ok_ref_q; assign ok_bst = ok_bst_q;

    wire busy = !init_done_q | busy_seq | fe_busy;
    wire a10  = cmd_a[10];
    reg  leg;
    always_comb begin
        case (cmd)
            CMD_ACT:  leg = ok_act_q[cmd_ba];
`ifdef MUT_A10
            CMD_RD:   leg = ok_rd_q[cmd_ba];
            CMD_WR:   leg = ok_wr_q[cmd_ba];
`else
            CMD_RD:   leg = ok_rd_q[cmd_ba] & ~a10;
            CMD_WR:   leg = ok_wr_q[cmd_ba] & ~a10;
`endif
            CMD_PRE:  leg = ok_pre_q[cmd_ba];
            CMD_PREA: leg = ok_prea_q;
            CMD_REF:  leg = ok_ref_q;
            CMD_BST:  leg = ok_bst_q;
            default:  leg = 1'b1;           // NOP
        endcase
    end
    wire silent_bst = (cmd == CMD_BST) & ~ok_bst_q & ~busy;      // BST with no burst open: NOP, no fault
    wire illegal    = ~leg & ~silent_bst;
    wire [2:0] o_cmd = leg ? cmd : CMD_NOP;

    // effective command before the wrap guard: the PHY's own command, else the (legal) offer
    wire       int_v   = i_v | s_v | fe_v;
    wire [2:0] e0_cmd  = i_v ? i_cmd : (s_v ? s_cmd : (fe_v ? fe_cmd : o_cmd));
    wire       e0_mrs  = i_v & i_mrs;
    wire       e0_emrs = i_v & i_emrs;
    wire [1:0] e0_ba   = (i_v | s_v) ? 2'b00 : (fe_v ? fe_ba : cmd_ba);
    wire [12:0] e0_a   = (i_v | s_v) ? 13'd0 : (fe_v ? fe_a : cmd_a);

    // ---------------------------------------------------------------------------------------------------------
    // burst state and the column wrap guard (a burst must not run past column 1023: forced BST, fault)
    // ---------------------------------------------------------------------------------------------------------
    reg        rd_open_q = 1'b0, wr_open_q = 1'b0;
    reg [1:0]  rd_bank_q = 2'b00, wr_bank_q = 2'b00;
    reg [10:0] bcol_q = 11'd0;                   // column of the word that this cycle would carry
    wire       burst_open = rd_open_q | wr_open_q;
    wire [1:0] burst_bank = rd_open_q ? rd_bank_q : wr_bank_q;
    wire e0_term = (e0_cmd == CMD_BST) | (e0_cmd == CMD_PREA) | (e0_cmd == CMD_RD) | (e0_cmd == CMD_WR) |
                   ((e0_cmd == CMD_PRE) & (e0_ba == burst_bank));
`ifdef MUT_NOWRAP
    wire wrap_viol = 1'b0;
`else
    wire wrap_viol = burst_open & bcol_q[10] & ~e0_term & ~e0_mrs;
`endif

    wire [2:0]  e_cmd = wrap_viol ? CMD_BST : e0_cmd;
    wire        e_mrs = e0_mrs & ~wrap_viol;
    wire        e_emrs = e0_emrs & ~wrap_viol;
    wire [1:0]  e_ba  = wrap_viol ? 2'b00 : e0_ba;
    wire [12:0] e_a   = wrap_viol ? 13'd0 : e0_a;
    wire e_act  = (e_cmd == CMD_ACT)  & ~e_mrs;
    wire e_rd   = (e_cmd == CMD_RD)   & ~e_mrs;
    wire e_wr   = (e_cmd == CMD_WR)   & ~e_mrs;
    wire e_pre  = (e_cmd == CMD_PRE)  & ~e_mrs;
    wire e_prea = (e_cmd == CMD_PREA) & ~e_mrs;
    wire e_ref  = (e_cmd == CMD_REF)  & ~e_mrs;
    wire e_bst  = (e_cmd == CMD_BST)  & ~e_mrs;

    wire rd_open_n = e_rd | (rd_open_q & ~(e_bst | e_prea | e_wr | (e_pre & (e_ba == rd_bank_q))));
    wire wr_open_n = e_wr | (wr_open_q & ~(e_bst | e_prea | e_rd | (e_pre & (e_ba == wr_bank_q))));
    wire [1:0] rd_bank_n = e_rd ? e_ba : rd_bank_q;
    wire [1:0] wr_bank_n = e_wr ? e_ba : wr_bank_q;
    wire [10:0] bcol_n = (e_rd | e_wr) ? ({1'b0, e_a[9:0]} + 11'd1) : (bcol_q[10] ? bcol_q : bcol_q + 11'd1);

    // DQM / DQ output enable
    wire wv_i = fe_busy ? fe_wv : wvalid;
    wire [15:0] wd_i = fe_busy ? fe_wd : wdata;
    wire [1:0] be_i = fe_busy ? fe_be : be;
    wire dqm_force = (!init_done_q & ~fe_busy) | busy_seq | illegal;
    wire beat      = wr_open_n & wv_i & (be_i != 2'b00) & ~dqm_force;      // an unmasked word is written this cycle
    reg  [1:0] dqm_n;
    always_comb begin
        if (dqm_force)                          dqm_n = 2'b11;
        else if (rd_open_q | rd_open_n)         dqm_n = 2'b00;             // read: low through the terminating cycle
`ifdef MUT_DQM
        else if (wr_open_n)                     dqm_n = ~be_i;             // wvalid ignored
`else
        else if (wr_open_n & wv_i)              dqm_n = ~be_i;
`endif
        else                                    dqm_n = 2'b11;
    end
`ifdef MUT_OE_EARLY
    wire oe_n = (wr_open_n | wr_open_q | rd_open_q) & ~dqm_force;
`else
    wire oe_n = wr_open_n & ~dqm_force;
`endif

    // ---------------------------------------------------------------------------------------------------------
    // timing state
    // ---------------------------------------------------------------------------------------------------------
    localparam [CW-1:0] M_RCD = TRCD_C - 1, M_RP = TRP_C - 1, M_RAS = TRAS_C - 1, M_RC = TRC_C - 1,
                        M_WRP = WRP_C - 1, M_DAL = DAL_C - 1, M_RRD = RRD_C - 1, M_RFC = TRFC_C - 1, M_RW = RW_C - 1;
    function [CW-1:0] dec(input [CW-1:0] x);
        begin dec = (x == {CW{1'b0}}) ? {CW{1'b0}} : x - 1'b1; end
    endfunction

    reg [CW-1:0] rrd_q = {CW{1'b0}}, rfc_q = {CW{1'b0}}, rw_q = {CW{1'b0}};
    wire [CW-1:0] n_rrd = e_act ? M_RRD : dec(rrd_q);
    wire [CW-1:0] n_rfc = e_ref ? M_RFC : dec(rfc_q);
    wire [CW-1:0] n_rw  = (rd_open_q & ~rd_open_n) ? M_RW : dec(rw_q);
    always @(posedge clk) begin
        if (rst) begin rrd_q <= {CW{1'b0}}; rfc_q <= {CW{1'b0}}; rw_q <= {CW{1'b0}}; end
        else begin rrd_q <= n_rrd; rfc_q <= n_rfc; rw_q <= n_rw; end
    end

    wire [3:0] n_open, r_act_b, r_rd_b, r_wr_b, r_pre_b, prea_b, ref_b;
    wire [3:0] age_req, age_force;
    genvar b;
    generate
        for (b = 0; b < 4; b = b + 1) begin : gb
            reg          open_q = 1'b0;
            reg [CW-1:0] rcd_q = {CW{1'b0}}, ras_q = {CW{1'b0}}, rc_q = {CW{1'b0}}, rp_q = {CW{1'b0}},
                         wrp_q = {CW{1'b0}}, dal_q = {CW{1'b0}};
            reg [AW-1:0] age_q = {AW{1'b0}};
            wire act_b  = e_act & (e_ba == b);
            wire pre_b  = e_prea | (e_pre & (e_ba == b));
            wire beat_b = beat & (wr_bank_n == b);
            wire n_o    = act_b | (open_q & ~pre_b);
            wire [CW-1:0] n_rcd = act_b ? M_RCD : dec(rcd_q);
            wire [CW-1:0] n_ras = act_b ? M_RAS : dec(ras_q);
            wire [CW-1:0] n_rc  = act_b ? M_RC  : dec(rc_q);
            wire [CW-1:0] n_rp  = pre_b ? M_RP  : dec(rp_q);
            wire [CW-1:0] n_wrp = act_b ? {CW{1'b0}} : (beat_b ? M_WRP : dec(wrp_q));
            wire [CW-1:0] n_dal = act_b ? {CW{1'b0}} : (beat_b ? M_DAL : dec(dal_q));
            wire [AW-1:0] n_age = act_b ? {AW{1'b0}} : (n_o ? ((age_q == {AW{1'b1}}) ? age_q : age_q + 1'b1) : {AW{1'b0}});
            always @(posedge clk) begin
                if (rst) begin
                    open_q <= 1'b0; rcd_q <= {CW{1'b0}}; ras_q <= {CW{1'b0}}; rc_q <= {CW{1'b0}}; rp_q <= {CW{1'b0}};
                    wrp_q <= {CW{1'b0}}; dal_q <= {CW{1'b0}}; age_q <= {AW{1'b0}};
                end else begin
                    open_q <= n_o; rcd_q <= n_rcd; ras_q <= n_ras; rc_q <= n_rc; rp_q <= n_rp; wrp_q <= n_wrp;
                    dal_q <= n_dal; age_q <= n_age;
                end
            end
            // permits for the next cycle's offer, from the next state
`ifdef MUT_TRCD
            wire rcd_ok = 1'b1;
`else
            wire rcd_ok = (n_rcd == {CW{1'b0}});
`endif
`ifdef MUT_TRAS
            wire ras_ok = 1'b1;
`else
            wire ras_ok = (n_ras == {CW{1'b0}});
`endif
`ifdef MUT_TRP
            wire rp_ok = 1'b1;
`else
            wire rp_ok = (n_rp == {CW{1'b0}});
`endif
`ifdef MUT_TRC
            wire rc_ok = 1'b1;
`else
            wire rc_ok = (n_rc == {CW{1'b0}});
`endif
`ifdef MUT_TDPL
            wire wrp_ok = 1'b1;
`else
            wire wrp_ok = (n_wrp == {CW{1'b0}});
`endif
`ifdef MUT_TDAL
            wire dal_ok = 1'b1;
`else
            wire dal_ok = (n_dal == {CW{1'b0}});
`endif
`ifdef MUT_ACT_OPEN
            assign r_act_b[b] = rp_ok & rc_ok & dal_ok;
`else
            assign r_act_b[b] = ~n_o & rp_ok & rc_ok & dal_ok;
`endif
`ifdef MUT_COL_CLOSED
            assign r_rd_b[b]  = rcd_ok;
            assign r_wr_b[b]  = rcd_ok;
`else
            assign r_rd_b[b]  = n_o & rcd_ok;
            assign r_wr_b[b]  = n_o & rcd_ok;
`endif
            assign r_pre_b[b] = n_o & ras_ok & wrp_ok;
            assign prea_b[b]  = ~n_o | (ras_ok & wrp_ok);
`ifdef MUT_REF_OPEN
            assign ref_b[b]   = rp_ok & rc_ok;
`else
            assign ref_b[b]   = ~n_o & rp_ok & rc_ok;
`endif
            assign n_open[b]  = n_o;
            assign tpend[b]   = (rcd_q != {CW{1'b0}}) | (rp_q != {CW{1'b0}}) | (rc_q != {CW{1'b0}});
            assign age_req[b]   = open_q & (age_q >= AGE_REQ_C);
            assign age_force[b] = open_q & (age_q >= AGE_FORCE_C);
        end
    endgenerate

`ifdef MUT_TRRD
    wire rrd_ok = 1'b1;
`else
    wire rrd_ok = (n_rrd == {CW{1'b0}});
`endif
`ifdef MUT_TRFC
    wire rfc_ok = 1'b1;
`else
    wire rfc_ok = (n_rfc == {CW{1'b0}});
`endif
`ifdef MUT_TURN
    wire rw_ok = 1'b1;
`else
    wire rw_ok = (n_rw == {CW{1'b0}});
`endif
`ifdef MUT_WR_IN_READ
    wire no_rd = 1'b1;
`else
    wire no_rd = ~rd_open_n;
`endif
    wire n_burst = rd_open_n | wr_open_n;
    wire [3:0] r_act = r_act_b & {4{rrd_ok & rfc_ok}};
    wire [3:0] r_rd  = r_rd_b;
    wire [3:0] r_wr  = r_wr_b & {4{no_rd & rw_ok}};
    wire [3:0] r_pre = r_pre_b;
    wire       r_prea = (&prea_b) & rfc_ok;
    wire       r_ref = (&ref_b) & rfc_ok & ~n_burst;
    wire       r_bst = n_burst;

    wire n_busy = ~n_init_done | n_busy_seq | n_fe_busy;
    // unmasked (by the PHY's own ownership) permits, used by the forced-refresh sequencer
    reg rp_prea_q = 1'b0, rp_ref_q = 1'b0;
    always @(posedge clk) begin
        if (rst) begin
            ok_act_q <= 4'd0; ok_rd_q <= 4'd0; ok_wr_q <= 4'd0; ok_pre_q <= 4'd0; ok_prea_q <= 1'b0;
            ok_ref_q <= 1'b0; ok_bst_q <= 1'b0; rp_prea_q <= 1'b0; rp_ref_q <= 1'b0;
        end else begin
            rp_prea_q <= r_prea & n_init_done;
            rp_ref_q  <= r_ref & n_init_done;
            ok_act_q  <= r_act & {4{~n_busy}};
            ok_rd_q   <= r_rd  & {4{~n_busy}};
            ok_wr_q   <= r_wr  & {4{~n_busy}};
            ok_pre_q  <= r_pre & {4{~n_busy}};
            ok_prea_q <= r_prea & ~n_busy;
            ok_ref_q  <= r_ref & ~n_busy;
            ok_bst_q  <= r_bst & ~n_busy;
        end
    end

    always @(posedge clk) begin
        if (rst) begin
            rd_open_q <= 1'b0; wr_open_q <= 1'b0; rd_bank_q <= 2'b00; wr_bank_q <= 2'b00; bcol_q <= 11'd0;
        end else begin
            rd_open_q <= rd_open_n; wr_open_q <= wr_open_n; rd_bank_q <= rd_bank_n; wr_bank_q <= wr_bank_n; bcol_q <= bcol_n;
        end
    end

    // ---------------------------------------------------------------------------------------------------------
    // faults and counters
    // ---------------------------------------------------------------------------------------------------------
    reg        fault_q = 1'b0;
    reg [15:0] fcnt_q  = 16'd0;
    reg [3:0]  fcause_q = 4'd0;
    reg [31:0] refs_q  = 32'd0;
    reg [15:0] dqc_q   = 16'd0;
    wire       f_ev   = illegal | wrap_viol | f_force | f_cal;
    wire [3:0] f_code = illegal ? (busy ? 4'd12 : (((cmd == CMD_RD) | (cmd == CMD_WR)) & a10) ? 4'd11 : {1'b0, cmd}) :
                        wrap_viol ? 4'd8 : (f_cal ? 4'd13 : (f_force_age ? 4'd10 : 4'd9));
    wire       conflict_d = oe_n & ((rw_q != {CW{1'b0}}) | rd_open_q);
    always @(posedge clk) begin
        if (rst) begin
            fault_q <= 1'b0; fcnt_q <= 16'd0; fcause_q <= 4'd0; refs_q <= 32'd0; dqc_q <= 16'd0;
        end else begin
            if (f_ev) begin
                fault_q <= 1'b1;
                if (fcnt_q != 16'hffff) fcnt_q <= fcnt_q + 1'b1;
                if (!fault_q) fcause_q <= f_code;
            end
            if (e_ref) refs_q <= refs_q + 1'b1;
            if (conflict_d && dqc_q != 16'hffff) dqc_q <= dqc_q + 1'b1;
        end
    end
    assign phy_fault = fault_q; assign fault_count = fcnt_q; assign fault_cause = fcause_q;
    assign refreshes = refs_q; assign dq_conflicts = dqc_q;

    // ---------------------------------------------------------------------------------------------------------
    // pins
    // ---------------------------------------------------------------------------------------------------------
    localparam [12:0] MRS_VAL = (CL == 3) ? 13'h037 : 13'h027;
    reg [2:0]  pc;
    reg [1:0]  pba;
    reg [12:0] pa;
    always_comb begin
        pc = 3'b111; pba = 2'b00; pa = 13'd0;
        if (e_mrs) begin
            pc = 3'b000; pba = e_emrs ? 2'b10 : 2'b00; pa = e_emrs ? 13'h000 : MRS_VAL;
        end else begin
            case (e_cmd)
                CMD_ACT:  begin pc = 3'b011; pba = e_ba; pa = e_a; end
                CMD_RD:   begin pc = 3'b101; pba = e_ba; pa = {3'b000, e_a[9:0]}; end
                CMD_WR:   begin pc = 3'b100; pba = e_ba; pa = {3'b000, e_a[9:0]}; end
                CMD_PRE:  begin pc = 3'b010; pba = e_ba; end
                CMD_PREA: begin pc = 3'b010; pa = 13'h0400; end
                CMD_REF:  begin pc = 3'b001; end
                CMD_BST:  begin pc = 3'b110; end
                default: ;
            endcase
        end
    end

    // pin registers: each register drives only its pin (Quartus: FAST_OUTPUT_REGISTER / FAST_OUTPUT_ENABLE_REGISTER,
    // see tau_sdram_phy_assignments.txt). The per-bit DQ and OE registers are duplicated on purpose (PRESERVE_REGISTER).
    reg        cke_q   = 1'b1;
    reg        ras_q   = 1'b1, cas_q = 1'b1, we_q = 1'b1;
    reg [1:0]  dqm_q   = 2'b11;
    reg [1:0]  ba_q    = 2'b00;
    reg [12:0] a_q     = 13'd0;
    (* preserve *) reg [15:0] dq_q  = 16'd0;
    (* preserve *) reg [15:0] oe_q  = 16'd0;
    always @(posedge clk) begin
        if (rst) begin
            cke_q <= 1'b1; ras_q <= 1'b1; cas_q <= 1'b1; we_q <= 1'b1; dqm_q <= 2'b11; ba_q <= 2'b00; a_q <= 13'd0;
            dq_q <= 16'd0; oe_q <= 16'd0;
        end else begin
            cke_q <= 1'b1;
            {ras_q, cas_q, we_q} <= pc;
            dqm_q <= dqm_n;
            ba_q  <= pba;
            a_q   <= pa;
            dq_q  <= wd_i;
            oe_q  <= {16{oe_n}};
        end
    end
    assign pin_cke = cke_q;
    assign pin_ras_n = ras_q; assign pin_cas_n = cas_q; assign pin_we_n = we_q;
    assign pin_dqm = dqm_q; assign pin_ba = ba_q; assign pin_a = a_q;
    assign dq_o = dq_q; assign dq_oe = oe_q;

    // ---------------------------------------------------------------------------------------------------------
    // read path: capture registers (one per DQ bit), RD_EXTRA delay stages, rvalid delay line.
    // Word k of a READ offered in cycle a is on rdata in cycle a + RD_LAT; rvalid marks the cycles of a burst
    // (cycle a through the cycle before the terminating command), delayed by the same amount. No backpressure.
    // ---------------------------------------------------------------------------------------------------------
    wire [15:0] cap_q;
    reg [15:0] rx1_q = 16'd0, rx2_q = 16'd0;
    reg [7:0] vp_q = 8'd0;
    generate
        if (CALIBRATE == 0) begin : g_rd_fixed
            tau_sdram_dq_cap #(.CAP_MODE(CAP_MODE)) u_cap (.clk(clk), .d(dq_i), .q(cap_q));
            assign rdata = (RD_EXTRA == 0) ? cap_q : ((RD_EXTRA == 1) ? rx1_q : rx2_q);
            assign rvalid = vp_q[RD_LAT - 1];
            assign rd_lat = RD_LAT[4:0];
        end else begin : g_rd_sel
            // run-time selectable capture edge and stage count (CALIBRATE = 1)
            tau_sdram_dq_cap_dual u_cap (.clk(clk), .sel_rise(sel_rise_q), .d(dq_i), .q(cap_q));
            assign rdata = (ext_q == 2'd0) ? cap_q : ((ext_q == 2'd1) ? rx1_q : rx2_q);
            wire [4:0] lat = (sel_rise_q ? CL + 1 : CL + 2) + {3'd0, ext_q};
            assign rvalid = vp_q[lat - 1'b1];
            assign rd_lat = lat;
        end
    endgenerate
    always @(posedge clk) begin
        rx1_q <= cap_q;
        rx2_q <= rx1_q;
    end
    always @(posedge clk) begin
        if (rst) begin sel_rise_q <= (CAP_MODE == 0); ext_q <= RD_EXTRA; end
        else if (cal_set_v) begin sel_rise_q <= cal_set_rise; ext_q <= cal_set_ext; end
    end
    always @(posedge clk) begin
        if (rst) vp_q <= 8'd0;
        else     vp_q <= {vp_q[6:0], rd_open_n};
    end

    // ---------------------------------------------------------------------------------------------------------
    // refresh timer, preemption, aged rows
    //   The timer adds one to ref_debt every REFRESH_INTERVAL (780 clocks at 100 MHz); every REF (the scheduler's or the
    //   PHY's) takes one away. When the debt reaches REFRESH_DEBT_MAX, or a row reaches ROW_FORCE_NS, the PHY raises
    //   `preempt` and waits for preempt_ack (a cycle in which the scheduler has ended its burst) or PREEMPT_TIMEOUT
    //   clocks; then it issues BST (if a burst is open, DQM 11), PREA (when permitted), REF (when permitted) and pulses
    //   banks_closed. The event is a fault (cause 9 refresh debt, 10 aged row). row_aged[b] (open longer than
    //   OPEN_ROW_MAX_NS) is a request to the scheduler only.
    // ---------------------------------------------------------------------------------------------------------
    localparam integer TW = 16;
    localparam [TW-1:0] TMR_RELOAD = REFI_C - 1;
    localparam [3:0]    DEBT_MAX_V = REFRESH_DEBT_MAX;
    localparam [11:0]   TMO_V = PREEMPT_TIMEOUT;
    reg [TW-1:0] tmr_q = TMR_RELOAD;
    reg [3:0]    debt_q = 4'd0;
    reg          ref_due_q = 1'b0;
    wire tick   = init_done_q & (tmr_q == {TW{1'b0}});
    wire e_refc = e_ref & init_done_q;
`ifdef MUT_DEBT
    wire tick_eff = 1'b0;
`else
    wire tick_eff = tick;
`endif
    reg [3:0] debt_n;
    always_comb begin
        debt_n = debt_q;
        if (tick_eff & ~e_refc) begin
            if (debt_q != 4'd15) debt_n = debt_q + 4'd1;
        end else if (~tick_eff & e_refc & (debt_q != 4'd0)) debt_n = debt_q - 4'd1;
    end
    always @(posedge clk) begin
        if (rst) begin
            tmr_q <= TMR_RELOAD; debt_q <= 4'd0; ref_due_q <= 1'b0;
        end else begin
            tmr_q <= (!init_done_q || tick) ? TMR_RELOAD : tmr_q - 1'b1;
            debt_q <= debt_n;
            ref_due_q <= (debt_n != 4'd0);
        end
    end
    assign ref_due = ref_due_q;
    assign ref_debt = debt_q;

    localparam [2:0] Q_IDLE = 3'd0, Q_WAIT = 3'd1, Q_BST = 3'd2, Q_PREA = 3'd3, Q_REF = 3'd4;
    reg [2:0]  qs_q  = Q_IDLE;
    reg [11:0] tmo_q = TMO_V;
    reg        preempt_q = 1'b0, bclosed_q = 1'b0;
    reg [3:0]  aged_q = 4'd0;
`ifdef MUT_NOPREEMPT
    wire trig_debt = 1'b0;
`else
    wire trig_debt = (debt_q >= DEBT_MAX_V);
`endif
`ifdef MUT_NOAGE
    wire trig_age = 1'b0;
`else
    wire trig_age  = |age_force;
`endif
    wire trig      = init_done_q & ~fe_busy & (trig_debt | trig_age);
    reg [2:0]  n_qs;
    reg [11:0] n_tmo;
    reg        sq_v, sq_bc, sq_force;
    reg [2:0]  sq_cmd;
    always_comb begin
        n_qs = qs_q; n_tmo = tmo_q; sq_v = 1'b0; sq_cmd = CMD_NOP; sq_bc = 1'b0; sq_force = 1'b0;
        case (qs_q)
            Q_IDLE: if (trig) begin n_qs = Q_WAIT; n_tmo = TMO_V; end
            Q_WAIT: begin
                if (!trig) n_qs = Q_IDLE;
                else if (preempt_ack || tmo_q == 12'd0) begin n_qs = Q_BST; sq_force = 1'b1; end
                else n_tmo = tmo_q - 1'b1;
            end
            Q_BST: begin
                if (burst_open) begin sq_v = 1'b1; sq_cmd = CMD_BST; end
                n_qs = Q_PREA;
            end
            Q_PREA: begin
`ifdef MUT_PREA_EARLY
                if (1'b1) begin
`else
                if (rp_prea_q) begin
`endif
                    sq_v = 1'b1; sq_cmd = CMD_PREA; n_qs = Q_REF;
                end
            end
            Q_REF: if (rp_ref_q) begin sq_v = 1'b1; sq_cmd = CMD_REF; n_qs = Q_IDLE; sq_bc = 1'b1; end
            default: n_qs = Q_IDLE;
        endcase
    end
    assign busy_seq    = (qs_q == Q_BST) | (qs_q == Q_PREA) | (qs_q == Q_REF);
    assign n_busy_seq  = (n_qs == Q_BST) | (n_qs == Q_PREA) | (n_qs == Q_REF);
    assign s_v         = sq_v;
    assign s_cmd       = sq_cmd;
    assign f_force     = sq_force;
    assign f_force_age = trig_age;
    always @(posedge clk) begin
        if (rst) begin
            qs_q <= Q_IDLE; tmo_q <= TMO_V; preempt_q <= 1'b0; bclosed_q <= 1'b0; aged_q <= 4'd0;
        end else begin
            qs_q <= n_qs; tmo_q <= n_tmo; preempt_q <= (n_qs == Q_WAIT);
`ifdef MUT_NOPULSE
            bclosed_q <= 1'b0;
`else
            bclosed_q <= sq_bc;
`endif
            aged_q <= age_req;
        end
    end
    assign preempt = preempt_q;
    assign banks_closed = bclosed_q;
    assign row_aged = aged_q;

    // ---------------------------------------------------------------------------------------------------------
    // optional features (contract 3.7b), each a parameter, default OFF (then everything below is constant 0 and the
    // PHY behaves exactly as without it).
    //   Command bus ownership: CALIBRATE runs inside the init (between the EMRS and init_done) and owns the bus then.
    //   BIST runs only when the scheduler says it is quiet: a rising edge of `bist_start` is remembered, and the run
    //   starts in the first cycle in which `idle_ok` = 1, every bank is closed and past its timing, the scheduler
    //   offers NOP and the forced-refresh sequencer is idle. From then until `bist_busy` falls the PHY owns the bus:
    //   all permits are 0 and any non-NOP offer is replaced by NOP + fault (cause 12). `idle_ok` is sampled at the
    //   start only. The PHY's own refresh duty is kept by the engine (one REF before every page when ref_due).
    //   PERF only watches the commands that reach the chip after init_done.
    // ---------------------------------------------------------------------------------------------------------
    // ---- CALIBRATE ----
    wire        cal_v;  wire [2:0] cal_cmd; wire [1:0] cal_ba; wire [12:0] cal_a; wire cal_wv; wire [15:0] cal_wd;
    wire        cal_done_w, cal_fault_w;
    wire [7:0]  cal_window_w;
    wire [3:0]  cal_best_w;
    generate
        if (CALIBRATE != 0) begin : g_cal
            localparam [3:0] CS_OFF = 4'd0, CS_ACT = 4'd1, CS_ACTW = 4'd2, CS_WR = 4'd3, CS_WRB = 4'd4, CS_BST = 4'd5,
                             CS_GAP = 4'd6, CS_SET = 4'd7, CS_SETW = 4'd8, CS_RD = 4'd9, CS_RDW = 4'd10, CS_RBST = 4'd11,
                             CS_DR = 4'd12, CS_PRE = 4'd13, CS_PREW = 4'd14, CS_END = 4'd15;
            reg [3:0]  cs_q = CS_OFF;
            reg [10:0] cc_q = 11'd0;
            reg [2:0]  ck_q = 3'd0;
            reg [5:0]  pass_q = 6'd0;
            reg [10:0] nchk_q = 11'd0;
            reg        err_q = 1'b0, done_q = 1'b0, cfault_q = 1'b0;
            reg [3:0]  best_q = 4'd0;
            reg [3:0]  n_cs;
            reg [10:0] n_cc;
            reg [2:0]  n_ck;
            reg        fv, fwv;
            reg [2:0]  fcmd;
            reg [12:0] fa;
            reg [15:0] fwd;
            function [15:0] calpat(input [9:0] i);
                begin calpat = 16'hA5C3 ^ (i * 16'h9E37); end
            endfunction
            localparam [10:0] LEN_M1 = CAL_LEN - 1;
            always_comb begin
                n_cs = cs_q; n_cc = cc_q; n_ck = ck_q; fv = 1'b0; fcmd = CMD_NOP; fa = 13'd0; fwv = 1'b0; fwd = 16'd0;
                case (cs_q)
                    CS_OFF:  if (fin_ev && !done_q) n_cs = CS_ACT;
                    CS_ACT:  begin fv = 1'b1; fcmd = CMD_ACT; fa = CAL_ROW; n_cs = CS_ACTW; n_cc = TRCD_C + 1; end
                    CS_ACTW: if (cc_q == 11'd0) n_cs = CS_WR; else n_cc = cc_q - 1'b1;
                    CS_WR:   begin fv = 1'b1; fcmd = CMD_WR; fwv = 1'b1; fwd = calpat(10'd0); n_cc = 11'd1; n_cs = CS_WRB; end
                    CS_WRB:  begin
                        fwv = 1'b1; fwd = calpat(cc_q[9:0]);
                        if (cc_q == LEN_M1) n_cs = CS_BST; else n_cc = cc_q + 1'b1;
                    end
                    CS_BST:  begin fv = 1'b1; fcmd = CMD_BST; n_cs = CS_GAP; n_cc = WRP_C + 2; end
                    CS_GAP:  if (cc_q == 11'd0) n_cs = CS_SET; else n_cc = cc_q - 1'b1;
                    CS_SET:  begin n_cs = CS_SETW; n_cc = 11'd10; end
                    CS_SETW: if (cc_q == 11'd0) n_cs = CS_RD; else n_cc = cc_q - 1'b1;
                    CS_RD:   begin fv = 1'b1; fcmd = CMD_RD; n_cs = CS_RDW; n_cc = LEN_M1; end
                    CS_RDW:  if (cc_q <= 11'd1) n_cs = CS_RBST; else n_cc = cc_q - 1'b1;
                    CS_RBST: begin fv = 1'b1; fcmd = CMD_BST; n_cs = CS_DR; n_cc = 11'd14; end
                    CS_DR:   begin
                        if (cc_q == 11'd0) begin
                            if (ck_q == 3'd5) n_cs = CS_PRE; else begin n_ck = ck_q + 1'b1; n_cs = CS_SET; end
                        end else n_cc = cc_q - 1'b1;
                    end
                    CS_PRE:  begin fv = 1'b1; fcmd = CMD_PRE; n_cs = CS_PREW; n_cc = TRP_C + 2; end
                    CS_PREW: if (cc_q == 11'd0) n_cs = CS_END; else n_cc = cc_q - 1'b1;
                    CS_END:  n_cs = CS_OFF;
                    default: n_cs = CS_OFF;
                endcase
            end
            // best setting: the edge with more passing settings (ties: the configured CAP_MODE), the middle of its range
            wire [2:0] fm = pass_q[2:0];
            wire [2:0] rm = pass_q[5:3];
            wire [1:0] pf = fm[0] + fm[1] + fm[2];
            wire [1:0] pr = rm[0] + rm[1] + rm[2];
            wire       pick_rise = (pr > pf) || ((pr == pf) && (CAP_MODE == 0));
            wire [2:0] m  = pick_rise ? rm : fm;
            wire [1:0] lo = m[0] ? 2'd0 : (m[1] ? 2'd1 : 2'd2);
            wire [1:0] hi = m[2] ? 2'd2 : (m[1] ? 2'd1 : 2'd0);
            wire [1:0] mid = (lo + hi) >> 1;
            wire [1:0] bext = m[mid] ? mid : lo;
            wire       none = (m == 3'd0);
            assign cal_set_v    = (cs_q == CS_SET) | ((cs_q == CS_END) & ~none);
            assign cal_set_rise = (cs_q == CS_SET) ? (ck_q >= 3'd3) : pick_rise;
            assign cal_set_ext  = (cs_q == CS_SET) ? ((ck_q >= 3'd3) ? ck_q[1:0] - 2'd3 : ck_q[1:0]) : bext;
            assign cal_v = fv; assign cal_cmd = fcmd; assign cal_ba = CAL_BANK; assign cal_a = fa; assign cal_wv = fwv; assign cal_wd = fwd;
            assign cal_end = (cs_q == CS_END);
            assign cal_busy_w = (cs_q != CS_OFF);
            assign n_cal_busy_w = (n_cs != CS_OFF);
            assign f_cal = (cs_q == CS_END) & none;
            always @(posedge clk) begin
                if (rst) begin
                    cs_q <= CS_OFF; cc_q <= 11'd0; ck_q <= 3'd0; pass_q <= 6'd0; nchk_q <= 11'd0; err_q <= 1'b0;
                    done_q <= 1'b0; cfault_q <= 1'b0; best_q <= 4'd0;
                end else begin
                    cs_q <= n_cs; cc_q <= n_cc; ck_q <= n_ck;
                    if (cs_q == CS_SET) begin err_q <= 1'b0; nchk_q <= 11'd0; end
                    if (cs_q >= CS_RD && cs_q <= CS_DR && rvalid) begin
                        if (rdata !== calpat(nchk_q[9:0])) err_q <= 1'b1;
                        if (nchk_q != 11'h7ff) nchk_q <= nchk_q + 1'b1;
                    end
                    if (cs_q == CS_DR && cc_q == 11'd0) begin
                        if (!err_q && nchk_q == CAL_LEN[10:0]) pass_q[ck_q] <= 1'b1;
                    end
                    if (cs_q == CS_END) begin
                        done_q <= 1'b1; cfault_q <= none;
                        best_q <= none ? 4'd0 : (pick_rise ? (4'd3 + {2'b00, bext}) : {2'b00, bext});
                    end
                end
            end
            assign cal_done_w = done_q; assign cal_fault_w = cfault_q;
            assign cal_window_w = {2'b00, pass_q}; assign cal_best_w = best_q;
        end else begin : g_nocal
            assign cal_set_v = 1'b0; assign cal_set_rise = 1'b0; assign cal_set_ext = 2'd0;
            assign cal_v = 1'b0; assign cal_cmd = CMD_NOP; assign cal_ba = 2'd0; assign cal_a = 13'd0; assign cal_wv = 1'b0; assign cal_wd = 16'd0;
            assign cal_end = 1'b0; assign cal_busy_w = 1'b0; assign n_cal_busy_w = 1'b0; assign f_cal = 1'b0;
            assign cal_done_w = 1'b0; assign cal_fault_w = 1'b0; assign cal_window_w = 8'd0; assign cal_best_w = 4'd0;
        end
    endgenerate
    assign cal_window = cal_window_w; assign cal_best = cal_best_w; assign cal_done = cal_done_w; assign cal_fault = cal_fault_w;

    // ---- BIST: march over pages (a page = one row of one bank = 1,024 words; page index = {row[12:0], bank[1:0]}) ----
    //   pass 0 writes a hash of the address, pass 1 reads and checks it, pass 2 writes its complement, pass 3 reads and
    //   checks. bist_errors counts mismatching words (a missing or extra word counts as one error);
    //   bist_first_addr = {bank[1:0], row[12:0], col[9:0]} of the first mismatch.
    wire        bist_v;  wire [2:0] bist_cmd; wire [1:0] bist_ba; wire [12:0] bist_a; wire bist_wv; wire [15:0] bist_wd;
    wire        bist_done_w;
    wire [31:0] bist_errors_w;
    wire [24:0] bist_first_w;
    generate
        if (BIST != 0) begin : g_bist
            localparam [3:0] BS_OFF = 4'd0, BS_CHK = 4'd1, BS_REF = 4'd2, BS_ACT = 4'd3, BS_ACTW = 4'd4, BS_COL = 4'd5,
                             BS_DATA = 4'd6, BS_BST = 4'd7, BS_DR = 4'd8, BS_PRE = 4'd9, BS_NEXT = 4'd10, BS_END = 4'd11;
            localparam [14:0] PG_LO = BIST_PAGE_LO, PG_HI = BIST_PAGE_HI;
            reg [3:0]  bs_q = BS_OFF;
            reg [10:0] bc_q = 11'd0, bn_q = 11'd0;
            reg [1:0]  bp_q = 2'd0;
            reg [14:0] bpg_q = 15'd0;
            reg [31:0] berr_q = 32'd0;
            reg [24:0] bfirst_q = 25'd0;
            reg        bfirst_v = 1'b0, bdone_q = 1'b0, bpend_q = 1'b0, bsd_q = 1'b0;
            reg [3:0]  n_bs;
            reg [10:0] n_bc;
            reg [1:0]  n_bp;
            reg [14:0] n_bpg;
            reg        fv, fwv;
            reg [2:0]  fcmd;
            reg [12:0] fa;
            reg [15:0] fwd;
            wire [1:0]  bank = bpg_q[1:0];
            wire [12:0] row  = bpg_q[14:2];
            wire        is_wr = ~bp_q[0];
            wire        inv   = bp_q[1];
            function [15:0] bdat(input [24:0] a, input i);
                reg [15:0] x;
                begin x = a[15:0] ^ {1'b0, a[24:10]} ^ 16'h3C5A; bdat = i ? ~x : x; end
            endfunction
            wire start_ok = (bs_q == BS_OFF) & bpend_q & idle_ok & init_done_q & rp_ref_q & (qs_q == Q_IDLE) & (cmd == CMD_NOP) &
                            ~cal_busy_w;
            always_comb begin
                n_bs = bs_q; n_bc = bc_q; n_bp = bp_q; n_bpg = bpg_q; fv = 1'b0; fcmd = CMD_NOP; fa = 13'd0; fwv = 1'b0; fwd = 16'd0;
                case (bs_q)
                    BS_OFF:  if (start_ok) begin n_bs = BS_CHK; n_bp = 2'd0; n_bpg = PG_LO; end
                    BS_CHK:  if (rp_ref_q) n_bs = (debt_q != 4'd0) ? BS_REF : BS_ACT;
                    BS_REF:  begin fv = 1'b1; fcmd = CMD_REF; n_bs = BS_CHK; end
                    BS_ACT:  begin fv = 1'b1; fcmd = CMD_ACT; fa = row; n_bs = BS_ACTW; n_bc = TRCD_C + 1; end
                    BS_ACTW: if (bc_q == 11'd0) n_bs = BS_COL; else n_bc = bc_q - 1'b1;
                    BS_COL:  begin
                        fv = 1'b1; fcmd = is_wr ? CMD_WR : CMD_RD; fa = 13'd0;
                        if (is_wr) begin fwv = 1'b1; fwd = bdat({bank, row, 10'd0}, inv); end
                        n_bc = 11'd1; n_bs = BS_DATA;
                    end
                    BS_DATA: begin
                        if (is_wr) begin fwv = 1'b1; fwd = bdat({bank, row, bc_q[9:0]}, inv); end
                        if (bc_q == 11'd1023) n_bs = BS_BST; else n_bc = bc_q + 1'b1;
                    end
                    BS_BST:  begin fv = 1'b1; fcmd = CMD_BST; n_bs = BS_DR; n_bc = 11'd14; end
                    BS_DR:   if (bc_q == 11'd0) n_bs = BS_PRE; else n_bc = bc_q - 1'b1;
                    BS_PRE:  begin fv = 1'b1; fcmd = CMD_PRE; n_bs = BS_NEXT; end
                    BS_NEXT: begin
                        n_bs = BS_CHK;
                        if (bpg_q == PG_HI) begin
                            n_bpg = PG_LO; n_bp = bp_q + 1'b1;
                            if (bp_q == 2'd3) n_bs = BS_END;
                        end else n_bpg = bpg_q + 1'b1;
                    end
                    BS_END:  n_bs = BS_OFF;
                    default: n_bs = BS_OFF;
                endcase
            end
            assign bist_v = fv; assign bist_cmd = fcmd; assign bist_ba = bank; assign bist_a = fa; assign bist_wv = fwv; assign bist_wd = fwd;
            assign bist_busy_w = (bs_q != BS_OFF);
            assign n_bist_busy_w = (n_bs != BS_OFF);
            wire [24:0] raddr = {bank, row, bn_q[9:0]};
            wire        rd_phase = ~is_wr & (bs_q >= BS_COL) & (bs_q <= BS_DR);
            wire        mism = rd_phase & rvalid & ((rdata !== bdat(raddr, inv)) | (bn_q >= 11'd1024));
            wire        cnt_bad = ~is_wr & (bs_q == BS_DR) & (bc_q == 11'd0) & (bn_q != 11'd1024);
            always @(posedge clk) begin
                if (rst) begin
                    bs_q <= BS_OFF; bc_q <= 11'd0; bn_q <= 11'd0; bp_q <= 2'd0; bpg_q <= 15'd0; berr_q <= 32'd0;
                    bfirst_q <= 25'd0; bfirst_v <= 1'b0; bdone_q <= 1'b0; bpend_q <= 1'b0; bsd_q <= 1'b0;
                end else begin
                    bsd_q <= bist_start;
                    if (bist_start & ~bsd_q) bpend_q <= 1'b1;
                    if (start_ok) begin bpend_q <= 1'b0; berr_q <= 32'd0; bfirst_v <= 1'b0; bdone_q <= 1'b0; end
                    bs_q <= n_bs; bc_q <= n_bc; bp_q <= n_bp; bpg_q <= n_bpg;
                    if (bs_q == BS_ACT) bn_q <= 11'd0;
                    if (rd_phase & rvalid) bn_q <= bn_q + 1'b1;
                    if (mism | cnt_bad) begin
                        if (berr_q != 32'hffffffff) berr_q <= berr_q + 1'b1;
                        if (!bfirst_v) begin bfirst_v <= 1'b1; bfirst_q <= mism ? raddr : {bank, row, 10'd0}; end
                    end
                    if (bs_q == BS_END) bdone_q <= 1'b1;
                end
            end
            assign bist_done_w = bdone_q; assign bist_errors_w = berr_q; assign bist_first_w = bfirst_q;
        end else begin : g_nobist
            assign bist_v = 1'b0; assign bist_cmd = CMD_NOP; assign bist_ba = 2'd0; assign bist_a = 13'd0; assign bist_wv = 1'b0; assign bist_wd = 16'd0;
            assign bist_busy_w = 1'b0; assign n_bist_busy_w = 1'b0; assign bist_done_w = 1'b0; assign bist_errors_w = 32'd0; assign bist_first_w = 25'd0;
        end
    endgenerate
    assign bist_busy = bist_busy_w; assign bist_done = bist_done_w; assign bist_errors = bist_errors_w; assign bist_first_addr = bist_first_w;

    // one source at a time
    assign fe_v   = cal_busy_w ? cal_v : bist_v;
    assign fe_cmd = cal_busy_w ? cal_cmd : bist_cmd;
    assign fe_ba  = cal_busy_w ? cal_ba : bist_ba;
    assign fe_a   = cal_busy_w ? cal_a : bist_a;
    assign fe_wv  = cal_busy_w ? cal_wv : bist_wv;
    assign fe_wd  = cal_busy_w ? cal_wd : bist_wd;
    assign fe_be  = 2'b11;

    // ---- PERF ----
    //   perf_snapshot (read after a perf_snap strobe), 16 words of 32 bits, word i = perf_snapshot[32*i +: 32]:
    //   0 ACT, 1 PRE (PRE and PREA), 2 column-command row hits (a column command to a row that already had one since
    //   its ACT), 3 row misses (the first column command after an ACT), 4 read-to-write turnarounds (a WR after the
    //   previous column command was a RD), 5 write-to-read turnarounds, 6 bank-conflict stalls (NOP cycles while a
    //   bank still waits for tRCD, tRP or tRC), 7 bus-idle cycles (NOP cycles after init_done), 8..15 histogram of the
    //   number of NOP cycles before each command: 0, 1, 2, 3, 4-7, 8-15, 16-63, 64 or more. perf_clear zeroes the
    //   counters; the counters wrap at 2^32.
    generate
        if (PERF != 0) begin : g_perf
            reg [31:0] pc_q [0:15];
            reg [511:0] snap_q = 512'd0;
            reg [3:0]  used_q = 4'd0;
            reg [1:0]  last_q = 2'd0;
            reg [7:0]  gp_q = 8'd0;
            wire en     = init_done_q;
            wire e_nop  = ~e_mrs & (e_cmd == CMD_NOP);
            wire e_col  = e_rd | e_wr;
            wire used_c = used_q[e_ba];
            wire any_pend = |tpend;
            reg [2:0] bin;
            always_comb begin
                if (gp_q == 8'd0) bin = 3'd0;
                else if (gp_q == 8'd1) bin = 3'd1;
                else if (gp_q == 8'd2) bin = 3'd2;
                else if (gp_q == 8'd3) bin = 3'd3;
                else if (gp_q < 8'd8) bin = 3'd4;
                else if (gp_q < 8'd16) bin = 3'd5;
                else if (gp_q < 8'd64) bin = 3'd6;
                else bin = 3'd7;
            end
            integer k;
            always @(posedge clk) begin
                if (rst || perf_clear) begin
                    for (k = 0; k < 16; k = k + 1) pc_q[k] <= 32'd0;
                    used_q <= 4'd0; last_q <= 2'd0; gp_q <= 8'd0;
                end else if (en) begin
                    if (e_act) pc_q[0] <= pc_q[0] + 1'b1;
                    if (e_pre | e_prea) pc_q[1] <= pc_q[1] + 1'b1;
                    if (e_col & used_c)  pc_q[2] <= pc_q[2] + 1'b1;
                    if (e_col & ~used_c) pc_q[3] <= pc_q[3] + 1'b1;
                    if (e_wr & (last_q == 2'd1)) pc_q[4] <= pc_q[4] + 1'b1;
                    if (e_rd & (last_q == 2'd2)) pc_q[5] <= pc_q[5] + 1'b1;
                    if (e_nop & any_pend) pc_q[6] <= pc_q[6] + 1'b1;
                    if (e_nop) pc_q[7] <= pc_q[7] + 1'b1;
                    if (!e_nop) begin pc_q[8 + bin] <= pc_q[8 + bin] + 1'b1; gp_q <= 8'd0; end
                    else if (gp_q != 8'hff) gp_q <= gp_q + 1'b1;
                    if (e_rd) last_q <= 2'd1;
                    if (e_wr) last_q <= 2'd2;
                    if (e_prea) used_q <= 4'd0;
                    else begin
                        if (e_act) used_q[e_ba] <= 1'b0;
                        if (e_pre) used_q[e_ba] <= 1'b0;
                        if (e_col) used_q[e_ba] <= 1'b1;
                    end
                end
                if (perf_snap) begin
                    for (k = 0; k < 16; k = k + 1) snap_q[32*k +: 32] <= pc_q[k];
                end
                if (rst || perf_clear) snap_q <= 512'd0;
            end
            assign perf_snapshot = snap_q;
        end else begin : g_noperf
            assign perf_snapshot = 512'd0;
        end
    endgenerate

    wire unused_ok = &{1'b0, n_open, fe_be, idle_ok, bist_start, perf_snap, perf_clear, tpend, sel_rise_q, ext_q, int_v};
endmodule
`default_nettype wire
