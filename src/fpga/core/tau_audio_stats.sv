// =============================================================================
// tau_audio_stats.sv -- audio statistics in hardware: signal power, stereo correlation and clipping.
//
// Three measurements the meters want and the CPU should not spend decode time on: how loud the signal really is
// (RMS, not just its peak), whether the channels agree (stereo correlation) and whether it clips. They all come from
// the same PCM sample strobe the spectrum bank and the level/waveform block use, at zero CPU cost.
//
// WHAT IT ACCUMULATES, over a fixed window of 2^WIN_LOG2 samples (1024 = ~23 ms at 44.1 kHz, the spectrum bank's window):
//     LL = sum L*L      RR = sum R*R      LR = sum L*R        (48 bits each; 1024 * 2^30 needs 41, so no overflow at any window <= 2^17)
// and, free running since the firmware last cleared them, how many samples reached full scale on each channel (|s| >= 32767).
// At the end of a window the three sums are latched as a set, so the firmware always reads one consistent window.
//
// WHAT THE FIRMWARE DOES WITH IT (fw/meter_core.h): RMS = isqrt(LL / N); correlation = LR / sqrt(LL * RR); crest factor = peak / RMS.
// The square root and the divide stay in software, once per display frame; only the per-sample multiply-accumulate is here.
//
// TIMING STYLE (this project's rule): one small operation per clock, one multiplier shared by all three products (16x16, registered),
// no chain. About 8 clocks per sample against ~1250 available.
// READ-OUT: write IDX, read DATA. 0 LL[31:0], 1 LL[47:32], 2 RR[31:0], 3 RR[47:32], 4 LR[31:0], 5 LR[47:32] sign-extended,
// 6 {clipsR[15:0], clipsL[15:0]}. STATUS: bit 0 = built in, bits 31:16 = windows completed (read before and after, retry if it moved).
// CTL bit 0 clears both clip counters.
//
// BUG selects deliberate faults for the mutation test (sim/tb_tau_audio_stats.v); 0 is the real design.
// =============================================================================
module tau_audio_stats #(
    parameter WIN_LOG2 = 10,
    parameter BUG      = 0
)(
    input  wire               clk,
    input  wire               rst,
    input  wire               tick,              // one pulse per input sample
    input  wire signed [15:0] in_l,
    input  wire signed [15:0] in_r,
    input  wire               ctl_we,
    input  wire        [0:0]  ctl_data,          // bit 0: clear the clip counters
    input  wire               idx_we,
    input  wire        [2:0]  idx_data,
    output wire        [31:0] rd_data,
    output wire        [31:0] status
);
    reg signed [15:0] sl, sr, ma, mb;
    reg signed [31:0] prod;
    reg        [47:0] acc_ll, acc_rr, lat_ll, lat_rr;
    reg signed [47:0] acc_lr, lat_lr;
    reg [WIN_LOG2-1:0] wn;
    reg [15:0] win_ctr, cl_l, cl_r;
    reg [2:0]  idx;
    reg [3:0]  st;
    localparam [3:0] S_IDLE=0, S_M1=1, S_A1=2, S_M2=3, S_A2=4, S_M3=5, S_A3=6, S_WIN=7;
    localparam [WIN_LOG2-1:0] LAST = (BUG == 2) ? ({WIN_LOG2{1'b1}} - 1'b1) : {WIN_LOG2{1'b1}};

    wire clip_l = (BUG == 4) ? (in_l >  16'sd32767) || (in_l <= -16'sd32767) : (in_l >= 16'sd32767) || (in_l <= -16'sd32767);
    wire clip_r = (BUG == 4) ? (in_r >  16'sd32767) || (in_r <= -16'sd32767) : (in_r >= 16'sd32767) || (in_r <= -16'sd32767);

    assign status = {win_ctr, 15'd0, 1'b1};
    assign rd_data = (idx == 3'd0) ? lat_ll[31:0] :
                     (idx == 3'd1) ? {16'd0, lat_ll[47:32]} :
                     (idx == 3'd2) ? lat_rr[31:0] :
                     (idx == 3'd3) ? {16'd0, lat_rr[47:32]} :
                     (idx == 3'd4) ? lat_lr[31:0] :
                     (idx == 3'd5) ? {{16{lat_lr[47]}}, lat_lr[47:32]} :
                     (idx == 3'd6) ? {cl_r, cl_l} : 32'd0;

    always @(posedge clk) begin
        if (rst) begin
            st <= S_IDLE; wn <= 0; win_ctr <= 0; cl_l <= 0; cl_r <= 0; idx <= 0;
            acc_ll <= 0; acc_rr <= 0; acc_lr <= 0; lat_ll <= 0; lat_rr <= 0; lat_lr <= 0;
            sl <= 0; sr <= 0; ma <= 0; mb <= 0; prod <= 0;
        end else begin
            if (idx_we) idx <= idx_data;
            if (ctl_we && ctl_data[0]) begin cl_l <= 0; cl_r <= 0; end
            case (st)
                S_IDLE: if (tick) begin
                    sl <= in_l; sr <= in_r; ma <= in_l; mb <= in_l;
                    if (clip_l && cl_l != 16'hFFFF) cl_l <= cl_l + 1'b1;
                    if (clip_r && cl_r != 16'hFFFF) cl_r <= cl_r + 1'b1;
                    st <= S_M1;
                end
                S_M1: begin prod <= ma * mb; st <= S_A1; end
                S_A1: begin
                    acc_ll <= acc_ll + {16'd0, prod};
                    if (BUG != 5) begin ma <= sr; mb <= sr; end
                    st <= S_M2;
                end
                S_M2: begin prod <= ma * mb; st <= S_A2; end
                S_A2: begin
                    acc_rr <= acc_rr + {16'd0, prod};
                    ma <= sl; mb <= sr;
                    st <= S_M3;
                end
                S_M3: begin prod <= ma * mb; st <= S_A3; end
                S_A3: begin
                    acc_lr <= (BUG == 1) ? acc_lr - {{16{prod[31]}}, prod} : acc_lr + {{16{prod[31]}}, prod};
                    st <= S_WIN;
                end
                S_WIN: begin
                    if (wn == LAST) begin
                        lat_ll <= acc_ll; lat_rr <= acc_rr; lat_lr <= acc_lr;
                        if (BUG != 3) begin acc_ll <= 0; acc_rr <= 0; acc_lr <= 0; end
                        win_ctr <= win_ctr + 1'b1;
                        wn <= 0;
                    end else wn <= wn + 1'b1;
                    st <= S_IDLE;
                end
                default: st <= S_IDLE;
            endcase
        end
    end
endmodule
