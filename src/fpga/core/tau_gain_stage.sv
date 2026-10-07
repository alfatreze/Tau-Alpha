// =============================================================================
// tau_gain_stage.sv -- the hardware gain stage at the pcm_fifo output (Cymo C3 slice 2, B-615; docs/features/CYMO_GAIN_STAGE_DESIGN.md).
//
// It applies, per output sample pair, exactly what fw/pcm_push.h's pcm_gain_apply() applies in firmware today (the host model tools/lab/cymo_gain_model.py is proved
// equal to that function sample for sample, and this module is replayed against the model):
//   1. ramp   cur moves toward min(target, 32768) by at most `ramp` per tick
//   2. volume y = (x * cur + 16384) >>> 15                (round to nearest; unity is exact)
//   3. fade   while fade_left != 0:  g = (total - fade_left) >> shift ;  y = (y * g + 128) >>> 8 ;  fade_left decrements only on a tick that delivered a real
//             sample (`adv`: the FIFO was primed and not empty), so priming and underrun gaps do not use up the fade
// It sits where the FIFO's own register output is (position B): a volume change, a mute or a fade acts within one sample instead of waiting out the up to 46 ms the
// FIFO holds. A flush (track change, seek) or `fade_now` (the firmware's other discontinuities) restarts the fade, so firmware no longer touches samples at all.
//
// ONE multiplier, time-shared (4 products per tick: L and R volume, L and R fade): a tick is >= 1,388 clocks apart, a sequence is 12. Both channels are published in
// the SAME clock edge, so a reader (the Cymo feed, the EQ's own 48 kHz tick) can never see a torn pair. The multiplier has registered operands and a registered product
// so Quartus packs it into a DSP block with its registers (no sync reset or enable on those registers: KB-036).
//
// The fade length is 256 << shift samples (shift 3 = 2048, the firmware's FADE_SAMPLES); change it only while no fade runs (g would otherwise jump).
//
// BUG selects a deliberate mutation (sim/tb_tau_gain_stage.v must FAIL each one): 1 no ramp (cur jumps), 2 volume rounding floors, 3 fade counts ticks without a sample,
// 4 fade never applied, 5 target floor of 1 (mute not exact), 6 snap ignored, 7 shift ignored (fixed 3), 8 R published a clock after L (torn pair), 9 fade rounding floors.
// =============================================================================
module tau_gain_stage #(
    parameter integer BUG = 0
)(
    input  wire        clk,
    input  wire        rst,
    input  wire        tick,                 // pcm_fifo.sample_tick: in_l/in_r take a new value at the END of this clock
    input  wire        adv,                  // this tick delivered a real sample (tick & primed & !empty & !flush)
    input  wire        flush,                // restarts the fade (and nothing else)
    input  wire        fade_now,             // pulse: restart the fade
    input  wire        snap,                 // pulse: cur = target now
    input  wire [15:0] target,               // wanted gain, Q15 (32768 = unity); values above unity are capped
    input  wire [15:0] ramp,                 // maximum change of cur per tick
    input  wire [1:0]  shift,                // fade length 256 << shift samples
    input  wire signed [15:0] in_l,
    input  wire signed [15:0] in_r,
    output reg  signed [15:0] out_l,
    output reg  signed [15:0] out_r,
    output wire [15:0] cur_o,
    output wire        fading
);
    wire [15:0] tgt_raw = (target > 16'd32768) ? 16'd32768 : target;
    wire [15:0] tgt = (BUG == 5 && tgt_raw == 16'd0) ? 16'd1 : tgt_raw;
    wire [1:0]  shf = (BUG == 7) ? 2'd3 : shift;
    wire [11:0] total = 12'd256 << shf;      // 256..2048

    reg [15:0] cur = 16'd32768;
    reg [11:0] fade_left = 12'd0;
    reg [8:0]  g_r = 9'd0;                   // fade gain for THIS tick's sample (from fade_left BEFORE this tick's decrement)
    reg        fa_r = 1'b0;                  // a fade applies to this tick's sample
    assign cur_o  = cur;
    assign fading = (fade_left != 12'd0);

    wire [15:0] dn = cur - tgt;
    wire [15:0] up = tgt - cur;
    always @(posedge clk) begin
        if (rst) begin
            cur <= 16'd32768; fade_left <= 12'd0; g_r <= 9'd0; fa_r <= 1'b0;
        end else begin
            if (snap && BUG != 6) cur <= tgt;
            else if (tick) begin
                if (BUG == 1)                cur <= tgt;
                else if (cur > tgt)          cur <= (dn > ramp) ? cur - ramp : tgt;
                else if (cur < tgt)          cur <= (up > ramp) ? cur + ramp : tgt;
            end
            if (flush || fade_now) fade_left <= total;
            else if (tick) begin
                g_r  <= (total - fade_left) >> shf;
                fa_r <= (fade_left != 12'd0) && (BUG != 4);
                if (fade_left != 12'd0 && (adv || BUG == 3)) fade_left <= fade_left - 12'd1;
            end
        end
    end

    // ---- the sequence: starts the clock AFTER a tick, when in_l/in_r/cur/g_r hold this sample's values ----
    reg        tick_d = 1'b0;
    reg  [3:0] st = 4'd0;
    reg signed [15:0] xl, xr, yl, yr;
    reg signed [17:0] ma, mb;
    reg signed [35:0] mp;
    reg        fa_h;
    reg [8:0]  g_h;
    wire signed [35:0] rv = mp + 36'sd16384;
    wire signed [35:0] rf = mp + 36'sd128;
    wire signed [15:0] vol_y = (BUG == 2) ? mp[30:15] : rv[30:15];     // product of 16 x <=32768 stays inside 31 bits; the round-to-nearest constant is the only difference
    wire signed [15:0] fad_y = (BUG == 9) ? mp[23:8]  : rf[23:8];
    always @(posedge clk) begin
        tick_d <= rst ? 1'b0 : tick;
        if (rst) begin
            st <= 4'd0; out_l <= 16'sd0; out_r <= 16'sd0;
        end else begin
            case (st)
            4'd0: if (tick_d) begin xl <= in_l; xr <= in_r; fa_h <= fa_r; g_h <= g_r; st <= 4'd1; end
            4'd1: begin ma <= {{2{xl[15]}}, xl}; mb <= {2'b00, cur}; st <= 4'd2; end
            4'd2: begin mp <= ma * mb; ma <= {{2{xr[15]}}, xr}; st <= 4'd3; end
            4'd3: begin mp <= ma * mb; yl <= vol_y; st <= 4'd4; end
            4'd4: begin yr <= vol_y; st <= 4'd5; end
            4'd5: begin
                if (!fa_h) begin out_l <= yl; out_r <= (BUG == 8) ? out_r : yr; st <= (BUG == 8) ? 4'd11 : 4'd0; end
                else begin ma <= {{2{yl[15]}}, yl}; mb <= {9'd0, g_h}; st <= 4'd6; end
            end
            4'd6: begin mp <= ma * mb; ma <= {{2{yr[15]}}, yr}; st <= 4'd7; end
            4'd7: begin mp <= ma * mb; yl <= fad_y; st <= 4'd8; end
            4'd8: begin yr <= fad_y; st <= 4'd9; end
            4'd9: begin out_l <= yl; out_r <= (BUG == 8) ? out_r : yr; st <= (BUG == 8) ? 4'd11 : 4'd0; end
            4'd11: begin out_r <= yr; st <= 4'd0; end         // BUG 8 only: the R half arrives a clock late
            default: st <= 4'd0;
            endcase
        end
    end
endmodule
