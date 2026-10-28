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
    parameter integer SIM_SHORT_INIT      = 0             // 1 allows INIT_WAIT_CYCLES below 100 us (simulation only)
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
    assign rd_lat = RD_LAT[4:0];

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
        if (PREEMPT_TIMEOUT < 1 || PREEMPT_TIMEOUT > 4095) begin : g_chk_tmo
            initial $error("tau_sdram_phy: PREEMPT_TIMEOUT must be 1..4095");
        end
    endgenerate
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
    wire          n_init_done = init_done_q | ((is_q == I_FIN) && (iw_q == {IW{1'b0}}));
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

    wire busy = !init_done_q | busy_seq;
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
    wire       int_v   = i_v | s_v;
    wire [2:0] e0_cmd  = i_v ? i_cmd : (s_v ? s_cmd : o_cmd);
    wire       e0_mrs  = i_v & i_mrs;
    wire       e0_emrs = i_v & i_emrs;
    wire [1:0] e0_ba   = int_v ? 2'b00 : cmd_ba;
    wire [12:0] e0_a   = int_v ? 13'd0 : cmd_a;

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
    wire dqm_force = !init_done_q | busy_seq | illegal;
    wire beat      = wr_open_n & wvalid & (be != 2'b00) & ~dqm_force;      // an unmasked word is written this cycle
    reg  [1:0] dqm_n;
    always_comb begin
        if (dqm_force)                          dqm_n = 2'b11;
        else if (rd_open_q | rd_open_n)         dqm_n = 2'b00;             // read: low through the terminating cycle
`ifdef MUT_DQM
        else if (wr_open_n)                     dqm_n = ~be;               // wvalid ignored
`else
        else if (wr_open_n & wvalid)            dqm_n = ~be;
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

    wire n_busy = ~n_init_done | n_busy_seq;
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
    wire       f_ev   = illegal | wrap_viol | f_force;
    wire [3:0] f_code = illegal ? (busy ? 4'd12 : (((cmd == CMD_RD) | (cmd == CMD_WR)) & a10) ? 4'd11 : {1'b0, cmd}) :
                        wrap_viol ? 4'd8 : (f_force_age ? 4'd10 : 4'd9);
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
            dq_q  <= wdata;
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
    tau_sdram_dq_cap #(.CAP_MODE(CAP_MODE)) u_cap (.clk(clk), .d(dq_i), .q(cap_q));
    reg [15:0] rx1_q = 16'd0, rx2_q = 16'd0;
    always @(posedge clk) begin
        rx1_q <= cap_q;
        rx2_q <= rx1_q;
    end
    assign rdata = (RD_EXTRA == 0) ? cap_q : ((RD_EXTRA == 1) ? rx1_q : rx2_q);
    reg [7:0] vp_q = 8'd0;
    always @(posedge clk) begin
        if (rst) vp_q <= 8'd0;
        else     vp_q <= {vp_q[6:0], rd_open_n};
    end
    assign rvalid = vp_q[RD_LAT - 1];

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
    wire trig      = init_done_q & (trig_debt | trig_age);
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

    wire unused_ok = &{1'b0, n_open};
endmodule
`default_nettype wire
