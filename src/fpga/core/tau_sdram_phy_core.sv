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
    // init sequencer (also after every reset)
    // ---------------------------------------------------------------------------------------------------------
    localparam [2:0] I_WAIT = 3'd0, I_REF1 = 3'd1, I_REF2 = 3'd2, I_MRS = 3'd3, I_EMRS = 3'd4, I_FIN = 3'd5;
    localparam integer IW = 15;
    reg [2:0]    is_q   = I_WAIT;
    reg [IW-1:0] iw_q   = INIT_WAIT_CYCLES - 1;
    reg          init_done_q = 1'b0;
    assign init_done = init_done_q;

    reg  [2:0]    n_is;
    reg  [IW-1:0] n_iw;
    wire          n_init_done = init_done_q | ((is_q == I_FIN) && (iw_q == 0));
    // internal command (init)
    reg           i_v;
    reg  [2:0]    i_cmd;
    reg           i_mrs, i_emrs;
    always_comb begin
        n_is = is_q; n_iw = iw_q; i_v = 1'b0; i_cmd = CMD_NOP; i_mrs = 1'b0; i_emrs = 1'b0;
        if (!init_done_q) begin
            if (iw_q != 0) n_iw = iw_q - 1'b1;
            else begin
                case (is_q)
                    I_WAIT: begin i_v = 1'b1; i_cmd = CMD_PREA; n_is = I_REF1; n_iw = TRP_C - 1; end
                    I_REF1: begin i_v = 1'b1; i_cmd = CMD_REF;  n_is = I_REF2; n_iw = TRFC_C - 1; end
                    I_REF2: begin i_v = 1'b1; i_cmd = CMD_REF;  n_is = I_MRS;  n_iw = TRFC_C - 1; end
                    I_MRS:  begin i_v = 1'b1; i_mrs = 1'b1;                    n_is = I_EMRS; n_iw = MRD_C - 1; end
                    I_EMRS: begin i_v = 1'b1; i_mrs = 1'b1; i_emrs = 1'b1;     n_is = I_FIN;  n_iw = MRD_C - 1; end
                    default: ;
                endcase
            end
        end
    end

    // ---------------------------------------------------------------------------------------------------------
    // effective command (stage 1: the init sequencer only; the scheduler path is added in the next stage)
    // ---------------------------------------------------------------------------------------------------------
    wire e_mrs  = i_v & i_mrs;
    wire e_emrs = i_v & i_emrs;
    wire [2:0] e_cmd = i_v ? i_cmd : CMD_NOP;
    wire [1:0] e_ba  = 2'b00;
    wire [12:0] e_a  = 13'd0;
    wire dqm_force = !init_done_q;

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

    // ---------------------------------------------------------------------------------------------------------
    // pin registers: each register drives only its pin (Quartus: FAST_OUTPUT_REGISTER / FAST_OUTPUT_ENABLE_REGISTER,
    // see tau_sdram_phy_assignments.txt). The duplicate per-bit OE registers carry PRESERVE_REGISTER.
    // ---------------------------------------------------------------------------------------------------------
    reg        cke_q   = 1'b1;
    reg        ras_q   = 1'b1, cas_q = 1'b1, we_q = 1'b1;
    reg [1:0]  dqm_q   = 2'b11;
    reg [1:0]  ba_q    = 2'b00;
    reg [12:0] a_q     = 13'd0;
    (* preserve *) reg [15:0] dq_q  = 16'd0;
    (* preserve *) reg [15:0] oe_q  = 16'd0;
    wire       oe_n = 1'b0;
    wire [1:0] dqm_n = 2'b11;
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
    // state update (stage 1: init only)
    // ---------------------------------------------------------------------------------------------------------
    always @(posedge clk) begin
        if (rst) begin
            is_q <= I_WAIT; iw_q <= INIT_WAIT_CYCLES - 1; init_done_q <= 1'b0;
        end else begin
            is_q <= n_is; iw_q <= n_iw; init_done_q <= n_init_done;
        end
    end

    // stage-1 stubs for the not yet implemented outputs
    assign ok_act = 4'd0; assign ok_rd = 4'd0; assign ok_wr = 4'd0; assign ok_pre = 4'd0;
    assign ok_prea = 1'b0; assign ok_ref = 1'b0; assign ok_bst = 1'b0;
    assign rvalid = 1'b0; assign rdata = 16'd0;
    assign phy_fault = 1'b0; assign fault_count = 16'd0; assign fault_cause = 4'd0; assign row_aged = 4'd0;
    assign preempt = 1'b0; assign banks_closed = 1'b0; assign ref_due = 1'b0; assign ref_debt = 4'd0;
    assign refreshes = 32'd0; assign dq_conflicts = 16'd0;

    wire unused_ok = &{1'b0, cmd, cmd_ba, cmd_a, wvalid, be, preempt_ack, dq_i};
endmodule
`default_nettype wire
