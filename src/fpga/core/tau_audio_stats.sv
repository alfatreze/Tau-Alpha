// =============================================================================
// tau_audio_stats.sv -- audio statistics for the meters, in hardware.
//
// RMS and stereo correlation need running sums over every sample; the CPU cannot afford to see them. This block accumulates, from the PCM FIFO's
// sample strobe (the same tap as the spectrum bank and the wave meter):
//     N    samples in the window
//     SLL  sum L*L      SRR  sum R*R      SLR  sum L*R  (signed)
// Firmware latches once per picture (CTL bit 0): the sums are copied to a read-out register set and a new window starts, so no sample is lost or counted
// twice (a sample whose product is still in the pipeline at the latch lands in the closing window). Firmware turns the sums into rms = sqrt(S/N) and
// corr = SLR / sqrt(SLL * SRR) (fw2/cores/audio_stats_core.h).
//
// Width: 16 x 16 products are 31 bits plus sign; the sums are 48 bits, exact for 131,072 samples (2.7 s at 48 kHz). A window that grows past that sets the
// sticky `ovf` bit (cleared by the next latch); firmware latches every picture, a few hundred samples apart.
// Timing style: three registered 16x16 multipliers (three DSP blocks), one 48-bit add a clock; nothing chained.
//
// Read-out words (IDX 0..7): 0 status, 1 N, 2 SLL[31:0], 3 SLL[47:32], 4 SRR[31:0], 5 SRR[47:32], 6 SLR[31:0], 7 SLR[47:32] sign-extended.
// status = {16'h5354 ('ST'), seq[7:0] (latches so far), 6'd0, ovf, 1 (present)}.
// =============================================================================
module tau_audio_stats #(
    parameter BUG = 0                          // mutants for sim/tb_tau_audio_stats.v: 1 products unsigned, 2 sums not cleared at a latch, 3 in-flight sample dropped at a latch, 4 overflow flag never set
)(
    input  wire               clk,
    input  wire               rst,
    input  wire               tick,              // one pulse per input sample
    input  wire signed [15:0] in_l,
    input  wire signed [15:0] in_r,
    input  wire               ctl_we,
    input  wire        [1:0]  ctl_data,          // bit 0: latch and start a new window; bit 1: clear everything
    input  wire               idx_we,
    input  wire        [2:0]  idx_data,
    output wire        [31:0] rd_data,
    output wire        [31:0] status
);
    reg signed [15:0] a_l, a_r;                // stage 1: the sample
    reg               v1;
    reg signed [31:0] p_ll, p_rr, p_lr;        // stage 2: the products
    reg               v2;
    reg        [17:0] acc_n;
    reg signed [47:0] acc_ll, acc_rr, acc_lr;
    reg               acc_ovf;
    reg        [31:0] sh_n;
    reg signed [47:0] sh_ll, sh_rr, sh_lr;
    reg               sh_ovf;
    reg        [7:0]  seq;
    reg        [2:0]  idx;

    wire do_latch = ctl_we && ctl_data[0];
    wire do_clear = ctl_we && ctl_data[1];

    always @(posedge clk) begin
        if (rst) begin
            v1 <= 1'b0; v2 <= 1'b0; acc_n <= 18'd0; acc_ll <= 48'sd0; acc_rr <= 48'sd0; acc_lr <= 48'sd0; acc_ovf <= 1'b0;
            sh_n <= 32'd0; sh_ll <= 48'sd0; sh_rr <= 48'sd0; sh_lr <= 48'sd0; sh_ovf <= 1'b0; seq <= 8'd0; idx <= 3'd0;
            a_l <= 16'sd0; a_r <= 16'sd0; p_ll <= 32'sd0; p_rr <= 32'sd0; p_lr <= 32'sd0;
        end else begin
            if (idx_we) idx <= idx_data;
            v1 <= tick; if (tick) begin a_l <= in_l; a_r <= in_r; end
            v2 <= v1;
            if (v1) begin
                if (BUG == 1) begin
                    p_ll <= $signed({1'b0, a_l[14:0]}) * $signed({1'b0, a_l[14:0]}); p_rr <= $signed({1'b0, a_r[14:0]}) * $signed({1'b0, a_r[14:0]});
                    p_lr <= $signed({1'b0, a_l[14:0]}) * $signed({1'b0, a_r[14:0]});      // drops the sign bit
                end else begin
                    p_ll <= a_l * a_l; p_rr <= a_r * a_r; p_lr <= a_l * a_r;
                end
            end
            if (do_clear) begin
                acc_n <= 18'd0; acc_ll <= 48'sd0; acc_rr <= 48'sd0; acc_lr <= 48'sd0; acc_ovf <= 1'b0; seq <= 8'd0;
                sh_n <= 32'd0; sh_ll <= 48'sd0; sh_rr <= 48'sd0; sh_lr <= 48'sd0; sh_ovf <= 1'b0;
            end else if (do_latch) begin
                sh_n   <= {14'd0, acc_n} + ((v2 && BUG != 3) ? 32'd1 : 32'd0);
                sh_ll  <= acc_ll + ((v2 && BUG != 3) ? p_ll : 48'sd0);
                sh_rr  <= acc_rr + ((v2 && BUG != 3) ? p_rr : 48'sd0);
                sh_lr  <= acc_lr + ((v2 && BUG != 3) ? p_lr : 48'sd0);
                sh_ovf <= acc_ovf;
                seq    <= seq + 8'd1;
                if (BUG != 2) begin acc_n <= 18'd0; acc_ll <= 48'sd0; acc_rr <= 48'sd0; acc_lr <= 48'sd0; end
                acc_ovf <= 1'b0;
            end else if (v2) begin
                acc_n  <= acc_n + 18'd1;
                acc_ll <= acc_ll + p_ll; acc_rr <= acc_rr + p_rr; acc_lr <= acc_lr + p_lr;
                if (acc_n >= 18'd131071 && BUG != 4) acc_ovf <= 1'b1;
            end
        end
    end

    reg [31:0] rd_q;
    always @* begin
        case (idx)
            3'd0:    rd_q = status;
            3'd1:    rd_q = sh_n;
            3'd2:    rd_q = sh_ll[31:0];
            3'd3:    rd_q = {16'd0, sh_ll[47:32]};
            3'd4:    rd_q = sh_rr[31:0];
            3'd5:    rd_q = {16'd0, sh_rr[47:32]};
            3'd6:    rd_q = sh_lr[31:0];
            default: rd_q = {{16{sh_lr[47]}}, sh_lr[47:32]};
        endcase
    end
    assign rd_data = rd_q;
    assign status  = {16'h5354, seq, 6'd0, sh_ovf, 1'b1};
endmodule
