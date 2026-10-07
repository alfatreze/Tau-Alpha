// =============================================================================
// tau_halcyon.sv -- the Halcyon EQ engine (docs/features/CYMO_HALCYON_SPEC.md).
//
// Rewrite of eq_biquad.v for the writable-coefficient design. eq_biquad.v is
// untouched and remains the shipped engine; this module is not wired in yet.
//
//  * NST biquad stages per channel (parameter, 17 = 10 correction + 6 tone + 1
//    infrasonic), `nact` of them active at run time, then one preamp multiply.
//  * Coefficients Q2.22 (24-bit), written through a sticky port into the SHADOW
//    bank; `commit` swaps the banks (and latches nact) at the next sample
//    boundary, so a coefficient set is never torn. Never write the live bank.
//  * Filter state lives in a plain RAM with ONE registered read and ONE write
//    port and NO reset: a clearing sweep (on reset and on `clr`) replaces the
//    reset loop that kept eq_biquad.v's state in registers (B-627).
//  * Wide in/out: WI and WO bits, of which IFB / OFB are fractional bits below the 16-bit LSB and the rest above 16 are headroom at the same LSB; the output rounds to nearest
//    and clamps (headroom bits at the input let a hot signal through when the preamp has attenuated it by the output).
//  * One multiplier, pipelined: five MACs issue one per clock, 12 clocks per
//    stage; the output rounds to nearest and CLAMPS (never wraps).
//  * nact == 0 or bypass = true bypass (a mux); the engine state is kept warm.
//
// Arithmetic is integer and exactly mirrored by tools/lab/halcyon_engine_model.py
// (sim/tb_tau_halcyon.v compares sample for sample).
// BUG selects deliberate defects for the mutation test; 0 = the real design.
// =============================================================================
module tau_halcyon #(
    parameter integer CLK_HZ  = 60_000_000,
    parameter integer RATE_HZ = 48_000,
    parameter integer NST     = 17,
    parameter integer WI      = 16,   // input width in bits
    parameter integer IFB     = 0,    // of which fractional bits below the 16-bit LSB (a 24-bit path: WI 24, IFB 8); the rest above 16 are HEADROOM bits at the same LSB (the Cymo resampler's overshoot: WI 18, IFB 0)
    parameter integer WO      = 16,   // output width in bits (16 = the I2S path today)
    parameter integer OFB     = 0,    // output fractional bits below the 16-bit LSB
    parameter integer BUG     = 0
) (
    input  wire                 clk,
    input  wire                 rst,
    input  wire signed [WI-1:0] in_l,
    input  wire signed [WI-1:0] in_r,
    input  wire                 bypass,
    // coefficient store (shadow bank): idx 0..NST*5-1 = stage coefficients
    // b0 b1 b2 a1 a2 per stage, idx NST*5 = preamp
    input  wire                 wr_we,
    input  wire [7:0]           wr_idx,
    input  wire signed [23:0]   wr_data,
    input  wire [5:0]           nact_in,     // sampled with commit
    input  wire                 commit,      // pulse: swap banks at next sample boundary
    input  wire                 clr,         // pulse: clear filter state
    output reg  signed [WO-1:0] out_l,
    output reg  signed [WO-1:0] out_r,
    output wire                 busy
);
    localparam integer CW = 24, FC = 22, SW = 36, FS = 16, AW = 64;
    localparam integer SHI = 16 - IFB;              // input -> state (16 fraction bits): left shift
    localparam integer SH  = 16 - OFB;              // state -> output: right shift (rounded)
    localparam integer DIV = CLK_HZ / RATE_HZ;
    localparam integer SDEPTH = 2 * NST * 4;
    localparam integer PRE = NST * 5;
    localparam signed [SW-1:0] MAXV = (36'sd1 <<< (WO - 1)) - 36'sd1;
    localparam signed [SW-1:0] MINV = -(36'sd1 <<< (WO - 1));

    // ---- memories ---------------------------------------------------------
    (* ramstyle = "MLAB, no_rw_check" *) reg signed [SW-1:0] st [0:SDEPTH-1];
    (* ramstyle = "MLAB, no_rw_check" *) reg signed [CW-1:0] cm [0:255];
    reg signed [SW-1:0] st_rd;
    reg signed [CW-1:0] cf_rd;
    reg                 st_we;
    reg [8:0]           st_wa;
    reg signed [SW-1:0] st_wd;
    reg [8:0]           st_ra;
    reg [8:0]           cf_ra;
    reg                 cm_we;
    reg [8:0]           cm_wa;
    reg signed [CW-1:0] cm_wd;
    reg                 bank;                       // live bank

    always @(posedge clk) begin
        if (st_we) st[st_wa] <= st_wd;
        st_rd <= st[st_ra];
        if (cm_we) cm[cm_wa[7:0]] <= cm_wd;         // 128 words per bank, bank = bit 7
        cf_rd <= cm[cf_ra[7:0]];
    end

    // ---- sample tick ------------------------------------------------------
    reg [11:0] divctr;
    wire tick = (divctr == DIV[11:0] - 12'd1);
    always @(posedge clk) begin
        if (rst) divctr <= 12'd0;
        else     divctr <= tick ? 12'd0 : divctr + 12'd1;
    end

    // ---- commit / clear request -------------------------------------------
    reg        pend;
    reg [5:0]  pend_nact, nact;
    reg        commit_r;
    reg [5:0]  nact_r;
    reg        clr_r;

    // ---- engine -----------------------------------------------------------
    localparam [2:0] S_IDLE = 3'd0, S_ENTER = 3'd1, S_RUN = 3'd2, S_OUT = 3'd3, S_SWEEP = 3'd4;
    reg [2:0]  state;
    reg        ch;
    reg [5:0]  stg;
    reg [3:0]  t;
    reg [8:0]  sbase;
    reg [7:0]  cbase;
    reg [8:0]  swc;
    reg signed [SW-1:0] smp;
    reg signed [WI-1:0] r_hold;
    reg signed [SW-1:0] x1_r, y1_r, y_r;
    reg signed [AW-1:0] p_reg, acc;
    reg signed [WO-1:0] eq_l, eq_r;

    assign busy = (state != S_IDLE);

    wire        pre   = (stg == nact);
    wire [3:0]  lastk = pre ? 4'd0 : 4'd4;
    wire [3:0]  mulk  = t - 4'd2;
    wire [3:0]  acck  = t - 4'd3;
    wire        mul_v = (t >= 4'd2) && (mulk <= lastk);
    wire        acc_v = (t >= 4'd3) && (acck <= lastk);

    function signed [SW-1:0] rnd;                    // round to nearest, Q.FC -> Q.0 of SW bits
        input signed [AW-1:0] v;
        reg   signed [AW-1:0] u;
        begin
            u   = v + (64'sd1 <<< (FC - 1));
            if (BUG == 7) u = v;                     // mutant: truncate
            rnd = u[FC + SW - 1 : FC];
        end
    endfunction

    function signed [WO-1:0] clampw;
        input signed [SW-1:0] v;
        reg   signed [SW-1:0] u, w;
        begin
            u = v + (36'sd1 <<< (SH - 1));
            w = u >>> SH;
            if (BUG == 4)       clampw = w[WO-1:0];
            else if (w > MAXV)  clampw = MAXV[WO-1:0];
            else if (w < MINV)  clampw = MINV[WO-1:0];
            else                clampw = w[WO-1:0];
        end
    endfunction

    // BUG 10 (mutant): the headroom bits of a wide input are thrown away (clamped to 16 bits) before the engine sees them
    function signed [WI-1:0] hin;
        input signed [WI-1:0] v;
        begin
            if (BUG == 10 && WI > 16 && v > 32767) hin = 32767;
            else if (BUG == 10 && WI > 16 && v < -32768) hin = -32768;
            else hin = v;
        end
    endfunction

    wire signed [WI-1:0] in_l_h = hin(in_l), in_r_h = hin(in_r);
    wire signed [SW-1:0] opnd = (mulk == 4'd0) ? smp : st_rd;

    always @(posedge clk) begin
        st_we <= 1'b0;
        cm_we <= 1'b0;
        commit_r <= commit;
        nact_r   <= nact_in;
        clr_r    <= clr;

        // coefficient write port: shadow bank only (BUG 2 writes the live bank)
        if (wr_we) begin
            cm_we <= 1'b1;
            cm_wa <= {1'b0, (BUG == 2) ? bank : ~bank, wr_idx[6:0]};
            cm_wd <= wr_data;
        end
        if (commit_r) begin
            if (BUG == 1) begin bank <= ~bank; nact <= nact_r; end
            else begin pend <= 1'b1; pend_nact <= nact_r; end
        end

        if (rst) begin
            state <= S_SWEEP; swc <= 9'd0; pend <= 1'b0; bank <= 1'b0;
            nact <= 6'd0; pend_nact <= 6'd0;
            eq_l <= {WO{1'b0}}; eq_r <= {WO{1'b0}};
        end else begin
            case (state)
            S_IDLE: begin
                if (clr_r) begin state <= S_SWEEP; swc <= 9'd0; end
                else if (tick) begin
                    if (pend) begin bank <= ~bank; nact <= pend_nact; pend <= 1'b0; end
                    r_hold <= in_r_h;
                    smp    <= $signed({{(SW-WI){in_l_h[WI-1]}}, in_l_h}) <<< SHI;
                    ch <= 1'b0; stg <= 6'd0; t <= 4'd0; sbase <= 9'd0; cbase <= 8'd0;
                    state <= S_ENTER;
                end
            end
            S_ENTER: begin
                if (nact == 6'd0) state <= S_IDLE;
                else              state <= S_RUN;
            end
            S_RUN: begin
                if (clr_r) begin state <= S_SWEEP; swc <= 9'd0; end
                else begin
                    t <= t + 4'd1;
                    // issue
                    if (t <= lastk) begin
                        st_ra <= sbase + {5'd0, (t - 4'd1) & 4'd3};
                        cf_ra <= {1'b0, bank, 7'd0} | (pre ? PRE[8:0] : ({1'b0, cbase} + {5'd0, t}));
                    end
                    // multiply
                    if (mul_v) begin
                        p_reg <= $signed(cf_rd) * $signed(opnd);
                        if (!pre && mulk == 4'd1) x1_r <= st_rd;
                        if (!pre && mulk == 4'd3) y1_r <= st_rd;
                    end
                    // accumulate
                    if (acc_v) begin
                        if (acck == 4'd0)               acc <= p_reg;
                        else if (acck <= 4'd2)          acc <= acc + p_reg;
                        else                            acc <= acc - p_reg;
                    end
                    if (pre) begin
                        if (t == 4'd4) begin
                            smp   <= (BUG == 5) ? smp : rnd(acc);
                            state <= S_OUT;
                        end
                    end else begin
                        case (t)
                        4'd8: begin
                            y_r   <= rnd(acc);
                            st_we <= 1'b1; st_wa <= sbase + 9'd1; st_wd <= x1_r;     // x2 <= x1
                        end
                        4'd9: begin
                            st_we <= 1'b1; st_wa <= sbase + 9'd0; st_wd <= smp;      // x1 <= input
                            smp   <= y_r;
                            if (BUG == 6) begin st_wa <= sbase + 9'd1; end           // mutant: x1 into x2 slot
                        end
                        4'd10: begin
                            st_we <= 1'b1; st_wa <= sbase + 9'd3; st_wd <= y1_r;     // y2 <= y1
                        end
                        4'd11: begin
                            st_we <= 1'b1; st_wa <= sbase + 9'd2; st_wd <= y_r;      // y1 <= y
                            stg <= stg + 6'd1; sbase <= sbase + 9'd4; cbase <= cbase + 8'd5;
                            t <= 4'd0;
                        end
                        default: ;
                        endcase
                    end
                end
            end
            S_OUT: begin
                if (!ch) begin
                    eq_l <= clampw(smp);
                    ch <= 1'b1; stg <= 6'd0; t <= 4'd0; sbase <= NST * 4; cbase <= 8'd0;
                    smp <= $signed({{(SW-WI){r_hold[WI-1]}}, r_hold}) <<< SHI;
                    state <= S_RUN;
                end else begin
                    eq_r <= clampw(smp);
                    state <= S_IDLE;
                end
            end
            S_SWEEP: begin
                st_we <= 1'b1; st_wa <= swc; st_wd <= {SW{1'b0}};
                swc <= swc + 9'd1;
                if (swc == ((BUG == 3) ? SDEPTH - 2 : SDEPTH - 1)) begin
                    state <= S_IDLE;
                end
            end
            default: state <= S_IDLE;
            endcase
        end

        if (!rst) begin
            out_l <= ((bypass && BUG != 9) || nact == 6'd0) ? clampw($signed({{(SW-WI){in_l[WI-1]}}, in_l}) <<< SHI) : eq_l;     // the bypass converts the width by the same rounding and clamp
            out_r <= ((bypass && BUG != 9) || nact == 6'd0) ? clampw($signed({{(SW-WI){in_r[WI-1]}}, in_r}) <<< SHI) : eq_r;
        end
    end
endmodule
