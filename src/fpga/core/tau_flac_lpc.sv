// =============================================================================
// tau_flac_lpc.sv -- FLAC LPC/FIXED reconstruction in hardware. B-364..B-368; design:
// docs/research/FLAC_LPC_KERNEL_DESIGN.md. NOT YET WIRED INTO mp3_soc.v -- built and testbenched in
// isolation first, matching tau_mp3_poly.sv's own build order (B-292 before its mp3_soc.v wiring).
//
// UNLIKE tau_mp3_poly.sv, this unit has no fixed coefficient ROM: FLAC's predictor order (1-32),
// right-shift (0-31) and coefficients are per-subframe and streamed in by firmware before each
// subframe's samples are reconstructed. One unit serves BOTH fw/flac.c's channel-0 path (subframe(),
// two separately-timed passes) and its channel-1 path (subframe_stream(), which fuses bit-reading,
// reconstruction and stereo decorrelation into one loop that cannot be split in software) -- this
// unit only replaces the reconstruction MAC itself, which is the identical shape in both, so it does
// not need firmware's two call sites to be split any further than they already are (B-361's own
// finding motivated this: channel 1 costs MORE than channel 0, not the same, so this unit's real value
// is exactly in being usable from both).
//
// ARITHMETIC: signed coefficient (<=15-bit, FLAC's own `prec` cap) x signed sample history (<=25-bit:
// 24-bit audio + 1 for the wider side channel of a decorrelated pair) into a 48-bit accumulate (B-365:
// 45 bits is PROVEN sufficient across FLAC's entire legal parameter space -- 20,000 random legal-range
// trials, 27 explicit worst-case corners, and 15.7 million real captured samples from 7 real files, all
// exact, both in Python (sim/test_flac_lpc_symmetry.py) and independently in C (sim/flac_lpc_model.c) --
// 48 gives a little headroom over that proven minimum without meaningfully costing more), then one
// arithmetic right-shift (0-31), matching fw/flac.c's own `p += (int64_t)coef[j]*hist[j]; out[i] +=
// p >> shift` exactly, no rounding, no clip (FLAC's own spec does neither at this step).
//
// TIMING STYLE (the project's rule, B-109/B-111/B-114/B-150/B-157/B-211): one small operation per
// clock in a sequenced machine, registered between every stage -- the tap MAC is ONE multiply-add per
// cycle (up to 32 cycles per sample, comfortably inside the audio sample period even at 96kHz against
// a 60-66MHz fabric clock), and the shift/add-residual/history-push steps that follow are each their
// own registered cycle, never chained combinationally with the MAC or with each other.
//
// REGISTER CONTRACT (docs/research/FLAC_LPC_KERNEL_DESIGN.md section 5, MMIO 0x120+ once wired):
// firmware writes CFG (order, shift) once per subframe, then loads `order` coefficients and `order`
// warm-up/history samples via sticky auto-incrementing index+data pairs (same convention as
// R_BLT_IDX/DATA and R_CLUT_IDX/DATA) -- index 0 in BOTH pairs is the tap/position that pairs with the
// MOST RECENT sample (coef[0] in fw/flac.c's own `p += coef[j] * hist[i-1-j]` convention), not oldest-
// first. Then, per sample: write RESIDUAL to start a computation, poll STATUS for done, read SAMPLE.
// =============================================================================
module tau_flac_lpc #(
    parameter integer ACC_WIDTH  = 48,   // B-365: 45 bits is the proven minimum; a little headroom, still one DSP block
    parameter integer MAX_ORDER  = 32,
    parameter integer BUG        = 0     // mutation hooks for the testbench: 1 tap order reversed, 2 no shift, 3 coef sign-extension dropped, 4 order off by one, 5 no history push
)(
    input  wire        clk,
    input  wire        rst,

    input  wire        cfg_we,
    input  wire [5:0]  cfg_order,        // 1..32
    input  wire [4:0]  cfg_shift,        // 0..31

    input  wire        coef_idx_we,
    input  wire [4:0]  coef_idx_d,
    input  wire        coef_data_we,
    input  wire signed [15:0] coef_data_d,

    input  wire        warm_idx_we,
    input  wire [4:0]  warm_idx_d,
    input  wire        warm_data_we,
    input  wire signed [31:0] warm_data_d,

    input  wire        residual_we,
    input  wire signed [31:0] residual_d,

    input  wire        sample_rd,

    output reg  signed [31:0] sample,    // the last computed reconstructed sample
    output wire        busy,
    output reg         done              // latches until `sample` is read (sample_rd pulses it low)
);

    // ---- per-subframe state: order, shift, coefficients, sliding history window --------------------
    reg  [5:0] order_r;
    reg  [4:0] shift_r;
    reg  [4:0] coef_idx_r, warm_idx_r;
    reg  signed [15:0] coef_mem [0:MAX_ORDER-1];
    reg  signed [31:0] hist_mem [0:MAX_ORDER-1];   // hist_mem[0] = most recent sample, paired with coef_mem[0]

    // hist_mem's actual array WRITE lives in the state-machine always block below (S_PUSH also writes
    // it) -- Verilog forbids two separate always blocks driving the same reg, and while Icarus tolerated
    // it in simulation (this testbench never happened to fire both in the same cycle), Quartus correctly
    // refused it at synthesis ("Can't resolve multiple constant drivers for net hist_mem[0][31]",
    // B-371's own fit log). warm_idx_r's own increment has no such conflict and stays here.
    always @(posedge clk) begin
        if (rst) begin
            order_r <= 6'd1; shift_r <= 5'd0; coef_idx_r <= 5'd0; warm_idx_r <= 5'd0;
        end else begin
            if (cfg_we)  begin order_r <= cfg_order; shift_r <= cfg_shift; coef_idx_r <= 5'd0; warm_idx_r <= 5'd0; end
            if (coef_idx_we)  coef_idx_r <= coef_idx_d;
            if (warm_idx_we)  warm_idx_r <= warm_idx_d;
            if (coef_data_we) begin coef_mem[coef_idx_r] <= coef_data_d; coef_idx_r <= coef_idx_r + 5'd1; end
            if (warm_data_we) warm_idx_r <= warm_idx_r + 5'd1;
        end
    end

    // ---- the sequenced MAC/shift/add/push state machine ---------------------------------------------
    localparam S_IDLE = 3'd0, S_MAC = 3'd1, S_SHIFT = 3'd2, S_ADD = 3'd3, S_PUSH = 3'd4, S_DONE = 3'd5;
    reg [2:0] state;
    reg [5:0] tap;
    reg signed [ACC_WIDTH-1:0] acc;
    reg signed [ACC_WIDTH-1:0] shifted;
    reg signed [31:0] residual_r;

    assign busy = (state != S_IDLE) && (state != S_DONE);

    // Coefficient/history indices for the current tap. BUG=1 reverses ONLY the history side, so
    // coefficient j is multiplied by the WRONG history sample -- a real functional mismatch. (Reversing
    // BOTH sides together would be undetectable: it still sums every (coef[k], hist[k]) pair exactly
    // once, and addition is commutative, so that "mutation" would silently pass every vector -- caught
    // by first trying exactly that and finding it didn't fail, not assumed safe.)
    wire [5:0] coef_addr = tap;
    wire [5:0] hist_addr = (BUG == 1) ? (order_r - 6'd1 - tap) : tap;

    always @(posedge clk) begin
        if (rst) begin
            state <= S_IDLE; tap <= 6'd0; acc <= {ACC_WIDTH{1'b0}}; done <= 1'b0; sample <= 32'sd0;
        end else begin
            // hist_mem's sole write-owning block (see the register-load block above): a warm-up load and
            // S_PUSH structurally never coincide (firmware only asserts warm_data_we while state==S_IDLE,
            // well before any residual write can drive the state machine into S_PUSH), but both being
            // plain statements in ONE always block is legal either way -- unlike two separate blocks,
            // Quartus resolves same-block writes by ordinary in-block priority, no synthesis error.
            if (warm_data_we) hist_mem[warm_idx_r] <= warm_data_d;
            case (state)
                S_IDLE: begin
                    if (sample_rd) done <= 1'b0;
                    if (residual_we) begin
                        residual_r <= residual_d;
                        tap <= 6'd0;
                        acc <= {ACC_WIDTH{1'b0}};
                        state <= (order_r == 6'd0) ? S_ADD : S_MAC;   // order 0 (should not happen; guarded anyway): skip straight to the residual
                    end
                end
                S_MAC: begin
                    // ONE multiply-add per cycle, registered -- never chained with the shift/add below.
                    // BUG=3: zero-extend the coefficient instead of sign-extending it (a negative
                    // coefficient becomes a large positive one). BUG=4: skip accumulating the LAST
                    // tap's product entirely -- same loop bound/indexing as the correct path (always
                    // terminates, never reads out of the array, for any legal order 1-32), just drops
                    // one real term from the sum, a genuine "one tap short" bug.
                    if (!(BUG == 4 && (tap + 6'd1 == order_r)))
                        acc <= acc + ($signed({{(ACC_WIDTH-16){(BUG == 3) ? 1'b0 : coef_mem[coef_addr[4:0]][15]}}, coef_mem[coef_addr[4:0]]})
                                      * $signed(hist_mem[hist_addr[4:0]]));
                    if (tap + 6'd1 == order_r) state <= S_SHIFT;
                    else tap <= tap + 6'd1;
                end
                S_SHIFT: begin
                    // Arithmetic right shift by a registered 5-bit amount -- a standard barrel-shift
                    // pattern, its own cycle, not combined with the MAC or the add below.
                    shifted <= (BUG == 2) ? acc : (acc >>> shift_r);
                    state <= S_ADD;
                end
                S_ADD: begin
                    sample <= shifted[31:0] + residual_r;
                    state <= (BUG == 5) ? S_DONE : S_PUSH;   // BUG=5: skip the history push entirely
                end
                S_PUSH: begin
                    // Push the new sample in at position 0, dropping the oldest (position order_r-1).
                    // A full parallel shift, one cycle -- cheap in an FPGA, and it means S_MAC never has
                    // to reason about a moving base address the way a true ring buffer would.
                    hist_mem[0] <= sample;
                    for (integer k = 1; k < MAX_ORDER; k = k + 1)
                        if (k < order_r) hist_mem[k] <= hist_mem[k-1];
                    state <= S_DONE;
                end
                S_DONE: begin
                    done <= 1'b1;
                    if (sample_rd) begin done <= 1'b0; state <= S_IDLE; end
                end
                default: state <= S_IDLE;
            endcase
        end
    end

endmodule
