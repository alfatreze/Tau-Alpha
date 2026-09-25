// =============================================================================
// tau_spec_bank.sv -- the spectrum meter's half-octave filter bank, in hardware (PHASE_F_SPEC.md section 7, B-263).
//
// This is the firmware's octave cascade (fw/player.c, meters_feed) moved into RTL, bit for bit:
//
//     for each octave o = 0..7, on every 2^o-th sample (each stage runs at half the rate of the one before):
//         lp[o]  += (x - lp[o]) >> 1            one-pole low-pass: splits the octave
//         hp      = x - lp[o]
//         slp[o] += (hp - slp[o]) >> 2          splits the octave in two halves
//         sh      = hp - slp[o]
//         acc[2o] += |sh|      acc[2o+1] += |slp[o]|
//         if this stage's phase bit was 0: stop (the next stage only sees every second sample)
//         else x = lp[o] and go on to stage o+1
//
// No multiplier anywhere: subtract, arithmetic shift, add. The CPU spent about 1.5% of its time on this and could
// only afford it while a spectrum meter was on screen; here it runs continuously at zero CPU cost.
//
// WHAT IT PUBLISHES: every 1024 input samples (a fixed window, ~23 ms at 44.1 kHz) the accumulators are turned into
// per-band MEANS and latched: stage o saw 1024 >> o samples in the window, so mean = acc >> (10 - o) (a shift, no
// divider). The firmware keeps its own gain table, logarithm and ballistics and only swaps "acc / count" for a read of
// these means, so the display behaves exactly as before. `win_ctr` increments once per window so the firmware can tell
// a fresh set from a stale one.
//
// TIMING STYLE: one small operation per clock in a sequential FSM (about 6 clocks per stage, at most 8 stages, so
// under 50 clocks per sample against ~1250 available at 48 kHz / 60 MHz). Deliberately no long combinational chain: the
// stage-to-stage dependency (x = lp[o]) is what a parallel version would have to chain, and this project's timing history
// (B-109, B-111, B-114) says not to build those.
//
// State is logic registers, not RAM (the arrays are tiny and this must not consume an M10K block).
// =============================================================================
module tau_spec_bank #(
    parameter WIN_LOG2 = 10                      // window = 2^WIN_LOG2 input samples (must be >= 7 so stage 7 runs)
)(
    input  wire               clk,
    input  wire               rst,
    input  wire               tick,              // one pulse per input sample
    input  wire signed [15:0] in_l,
    input  wire signed [15:0] in_r,
    input  wire        [3:0]  rd_idx,            // which band's mean to present
    output wire        [19:0] rd_mean,
    output reg         [15:0] win_ctr            // increments once per completed window
);
    localparam integer OCT = 8;

    (* ramstyle = "logic" *) reg signed [17:0] lp  [0:OCT-1];
    (* ramstyle = "logic" *) reg signed [17:0] slp [0:OCT-1];
    (* ramstyle = "logic" *) reg        [29:0] acc [0:2*OCT-1];
    (* ramstyle = "logic" *) reg        [19:0] mean[0:2*OCT-1];
    reg [OCT-1:0]            ph;                 // per-stage phase bit == firmware's spec_cnt[o] & 1
    reg [WIN_LOG2-1:0]       wn;                 // samples seen in this window

    reg signed [17:0] x, t1, hp, t2, sl, sh, lpn;
    reg [2:0]  o;
    reg [3:0]  lb;                               // latch index
    reg [3:0]  st;
    localparam [3:0] S_IDLE=0, S_A=1, S_B=2, S_C=3, S_D=4, S_E=5, S_F=6, S_LATCH=7;

    assign rd_mean = mean[rd_idx];

    wire signed [17:0] shs = sh;
    wire        [17:0] abs_sh = shs[17] ? (~shs + 18'd1) : shs;
    wire signed [17:0] sls = sl;
    wire        [17:0] abs_sl = sls[17] ? (~sls + 18'd1) : sls;

    integer i;
    initial begin
        for (i = 0; i < OCT; i = i + 1) begin lp[i] = 0; slp[i] = 0; end
        for (i = 0; i < 2*OCT; i = i + 1) begin acc[i] = 0; mean[i] = 0; end
    end

    always @(posedge clk) begin
        if (rst) begin
            st <= S_IDLE; ph <= 0; wn <= 0; win_ctr <= 0; o <= 0; lb <= 0;
            for (i = 0; i < OCT; i = i + 1) begin lp[i] <= 0; slp[i] <= 0; end
            for (i = 0; i < 2*OCT; i = i + 1) begin acc[i] <= 0; mean[i] <= 0; end
        end else begin
            case (st)
            S_IDLE: if (tick) begin
                // mono = (L + R) >> 1, exactly the firmware's ((int32)L + (int32)R) >> 1
                x  <= ($signed({in_l[15], in_l[15], in_l}) + $signed({in_r[15], in_r[15], in_r})) >>> 1;
                o  <= 0;
                wn <= wn + 1'b1;
                st <= S_A;
            end
            S_A: begin t1 <= (x - lp[o]) >>> 1;          st <= S_B; end     // lp += (x - lp) >> 1, first half
            S_B: begin lpn <= lp[o] + t1; lp[o] <= lp[o] + t1; hp <= x - (lp[o] + t1); st <= S_C; end
            S_C: begin t2 <= (hp - slp[o]) >>> 2;        st <= S_D; end     // slp += (hp - slp) >> 2, first half
            S_D: begin sl <= slp[o] + t2; slp[o] <= slp[o] + t2; sh <= hp - (slp[o] + t2); st <= S_E; end
            S_E: begin acc[{o, 1'b0}] <= acc[{o, 1'b0}] + abs_sh; st <= S_F; end
            S_F: begin
                acc[{o, 1'b1}] <= acc[{o, 1'b1}] + abs_sl;
                ph[o] <= ~ph[o];
                if (ph[o] && o != OCT-1) begin           // firmware: ++cnt even -> continue to the next stage
                    x <= lpn; o <= o + 1'b1; st <= S_A;
                end else if (wn == 0) begin              // window complete (wn wrapped to 0)
                    lb <= 0; st <= S_LATCH;
                end else st <= S_IDLE;
            end
            S_LATCH: begin                               // mean = acc >> (WIN_LOG2 - stage), then clear
                mean[lb] <= acc[lb] >> (WIN_LOG2 - (lb >> 1));
                acc[lb]  <= 0;
                lb <= lb + 1'b1;
                if (lb == 4'd15) begin win_ctr <= win_ctr + 1'b1; st <= S_IDLE; end
            end
            default: st <= S_IDLE;
            endcase
        end
    end
endmodule
