// =============================================================================
// tau_cymo_resamp.sv -- a real polyphase FIR resampler, 44100:48000 ONLY (exact ratio 160:147),
// evolving pcm_fifo.v's own rate_inc fractional-accumulator idea (B-467+; design:
// docs/features/CYMO_AUDIO_ENGINE.md section 15, following the neoge/pocket-mp3 architecture review
// there) into real interpolation instead of a zero-order hold. NOT YET WIRED into pcm_fifo.v -- this is a
// standalone, independently-verified unit; the pcm_fifo.v integration (replacing the hold with a request
// to this module at the FIXED 48 kHz output tick, with pcm_fifo's own priming/flush/underrun-glide logic
// still owning the input-sample FIFO itself) is the next step, deliberately kept separate so each piece is
// verified on its own, matching this project's own incremental-build discipline.
//
// ALGORITHM (sim/cymo_resamp_model.c is the golden model; tools/gen_cymo_resamp_rom.py generates the ROM):
// 160 phase banks of a 32-tap FIR, Kaiser-windowed, each bank independently normalised to unity DC gain,
// coefficients quantised to signed 16-bit Q1.15 (tools/gen_cymo_resamp_rom.py's own choice of a clean
// power-of-two scale, see that file's header for why it differs slightly from the lab model's 32767).
// A phase accumulator (0..159) advances by Q=147 every OUTPUT request; when it reaches/exceeds P=160 it
// wraps and ALSO signals (`pop_req`) that the caller must push one new 44.1 kHz input sample pair into
// this module's history before the next output request -- exactly matching the model's own loop order
// (compute output from CURRENT history, THEN advance phase, THEN consume input on wrap).
//
// TAP CONVENTION: tap 0 pairs with the MOST RECENT input sample (same hist[0]-is-newest convention as
// tau_flac_lpc.sv's own register contract). History is a 32-deep newest-first shift register, one copy
// per channel (L, R) -- genuinely shifted every push (not a ring with a moving head like tau_flac_lpc's
// B-377/B-378 ring, since a push here always happens in lockstep for BOTH channels and only at the phase-
// wrap rate (~44.1kHz, not every clock), so the shift's extra register-to-register copies cost nothing
// that matters at this rate -- unlike tau_flac_lpc's own per-TAP ring optimisation, which existed because
// that unit pushes once per RECONSTRUCTED SAMPLE, a much hotter path).
//
// PUSH LATCHING: push_we/push_l/push_r are latched into held_l/held_r the instant push_we fires (a plain
// always-on register, independent of the main state machine), decoupling the CALLER's own timing (it may
// push any time after a pop_req, well before the next start) from the INTERNAL moment the ring is actually
// shifted (S_PUSH, mid-computation of the following output) -- the shift always consumes the latest held
// value, never the raw push_l/push_r wires directly.
//
// TIMING STYLE (the project's rule, B-109/B-111/B-114/B-150/B-157/B-211/B-369): one small operation per
// clock in a sequenced machine, registered between every stage. The coefficient ROM is M10K-styled
// (tau_cymo_resamp_rom.svh), so its read is registered (S_MAC/S_MAC2 split, same shape as
// tau_flac_lpc.sv's own M10K-read split after B-378) -- never read and used combinationally in one cycle.
// Both channels (L then R) share ONE time-multiplexed MAC datapath (one multiplier, matching the design
// doc's own "1 DSP" resource estimate) -- same discipline as tau_mp3_poly.sv's own channel-0-then-
// channel-1 loop, not two parallel multipliers. 2 channels x 32 taps x 2 cycles/tap (address, then MAC)
// + a few cycles for shift/clip/push: comfortably inside the ~1,389-cycle budget per 48kHz output sample
// at clk_sys ~66.7MHz (cycle budget was never the constraint here, M10K for the ROM is -- section 15).
//
// REGISTER CONTRACT (not yet wired into mp3_soc.v -- this module's own ports ARE the register contract
// once it is): `clear` resets history to all-zero and phase to 0 (new track / flush, mirrors pcm_fifo.v's
// own flush semantics); caller pushes one input sample pair via push_we/push_l/push_r whenever `pop_req`
// asked for one (any other time push_we is a caller bug, not guarded against here -- same trust-the-
// caller convention as every other MMIO-facing unit in this codebase); `start` requests the next output;
// `busy`/`done` bracket the computation; `pop_req` is a one-cycle pulse alongside `done`, high only on
// outputs where phase actually wrapped.
// =============================================================================
module tau_cymo_resamp #(
    parameter integer Q_STEP = 147,   // exact 44100:48000 = 160:147 -- 44.1 kHz input ONLY (section 9/15's "start with one ratio" decision)
    parameter integer ACC_WIDTH = 40, // sim/cymo_resamp_model.c: proven sufficient (max product ~2^30 x 32 taps ~2^35, headroom to 2^39)
    parameter integer OUT_W = 16,     // B-634: output word width, 16 (shipped) or 18: with 18 the filter's overshoot on hot material (+2 dB true peaks are routine) is carried to the next stage instead of being hard-clipped at +-32767
    parameter integer BUG = 0         // mutation hooks (6: clip at 16 bits whatever OUT_W is): 1 tap/history indexing reversed, 2 no final shift, 3 coef sign-extension dropped, 4 wrong phase step, 5 pop_req never asserted (history never advances)
)(
    input  wire        clk,
    input  wire        rst,
    input  wire        clear,              // new track / flush: history -> 0, phase -> 0 (takes TAPS clocks, see S_CLR)

    input  wire        push_we,            // caller must push exactly once per asserted pop_req, before the next `start`
    input  wire signed [15:0] push_l,
    input  wire signed [15:0] push_r,

    input  wire        start,              // request the next output sample; ignored while busy
    input  wire        out_rd,             // read-acknowledges `done` (same read-is-ack convention as
                                            // tau_flac_lpc.sv's own sample_rd) -- without this, a firmware
                                            // polling loop running vastly slower than clk_sys could poll
                                            // right past a one-cycle pulse and never see it at all
    output wire        busy,
    output reg         done,               // HELD (not a pulse) from the end of computation until out_rd
    output wire        pop_req,            // LEVEL, not a pulse: mirrors `pending_pop` directly, so a
                                            // polling read can never land in a race window and miss it
                                            // (the same reasoning that makes `done` a held flag, not a pulse)
    output reg  signed [OUT_W-1:0] out_l,
    output reg  signed [OUT_W-1:0] out_r
);
`include "tau_cymo_resamp_rom.svh"
    localparam integer TAPS = CYMO_RESAMP_TAPS;     // 32
    localparam integer P    = CYMO_RESAMP_BANKS;    // 160

    // ---- latched push data: decouples caller timing from the internal S_PUSH moment ------------------
    reg signed [15:0] held_l, held_r;
    always @(posedge clk) begin
        if (push_we) begin held_l <= push_l; held_r <= push_r; end
    end

    // ---- per-channel history: newest-first shift register, one copy per channel -------------------
    reg signed [15:0] hist0 [0:TAPS-1];
    reg signed [15:0] hist1 [0:TAPS-1];
    reg [8:0] phase;   // 0..159

    // ---- the sequenced MAC/shift/clip state machine ------------------------------------------------
    // S_MAC/S_MAC2 split: the ROM read (coef_q below) is registered (M10K-styled), so the value it reads
    // for THIS tap is only valid one cycle after the address is presented -- same shape as
    // tau_flac_lpc.sv's own B-378 split, never chained combinationally with the MAC.
    localparam [3:0] S_IDLE = 0, S_SHIFTHIST = 1, S_MAC_ENTRY = 2, S_MAC = 3, S_MAC2 = 4, S_NEXTCH = 5, S_SHIFT0 = 6, S_SHIFT1 = 7, S_PHASE = 8, S_DONE = 9, S_CLR = 10;
    reg [3:0] st;
    reg       ch;                              // 0 = L (first pass), 1 = R (second pass)
    reg [5:0] tap;
    reg signed [ACC_WIDTH-1:0] acc, acc0_r;
    reg signed [ACC_WIDTH-1:0] shifted0, shifted1;
    // output clip limits: +-(2^(OUT_W-1)); BUG 6 keeps the 16-bit limits whatever the width (a mutant the 18-bit vectors must catch)
    localparam integer CLIPW = (BUG == 6) ? 16 : OUT_W;
    localparam signed [ACC_WIDTH-1:0] OMAX = (64'sd1 <<< (CLIPW - 1)) - 64'sd1;
    localparam signed [ACC_WIDTH-1:0] OMIN = -(64'sd1 <<< (CLIPW - 1));
    reg [5:0] clr_i;
    // Set at the end of one output's S_PHASE when the phase wrapped (a new input sample is owed), CLEARED
    // by actually performing the shift at the START of the NEXT start -- see S_SHIFTHIST below. This
    // defers consuming held_l/held_r by exactly one `start` call: pop_req tells the caller "push before
    // your NEXT start", and the module only actually reads held_l/held_r at that next start, giving the
    // caller the whole done..next-start window to supply it (the obvious-looking alternative -- shifting
    // immediately inside the SAME S_PHASE that raises pop_req -- was tried first and is wrong: at that
    // point the caller has not even SEEN pop_req yet, so held_l/held_r would still hold whatever was
    // pushed for some earlier, unrelated wrap, or X at power-up; found by the testbench, not by
    // inspection).
    reg pending_pop;

    assign busy = (st != S_IDLE) && (st != S_DONE);
    assign pop_req = pending_pop;

    // Coefficient ROM address for the current tap. BUG=1 reverses which HISTORY sample pairs with tap --
    // reversing the coefficient side too would be undetectable (every (coef,hist) pair still appears
    // exactly once, and addition is commutative -- the same lesson tau_flac_lpc.sv's own BUG=1 comment
    // already recorded), so only the history side flips here.
    wire [12:0] coef_addr = {phase[7:0], tap[4:0]};
    // TAPS (32) needs 6 bits to represent, not 5 (5'b100000 truncates to 0) -- compute the reversal in
    // 6-bit arithmetic, the result (0..31) then fits the 5-bit index.
    wire [5:0] hist_idx6 = TAPS[5:0] - 6'd1 - tap;
    wire [4:0] hist_idx  = (BUG == 1) ? hist_idx6[4:0] : tap[4:0];

    reg signed [15:0] coef_q;
    reg signed [15:0] h_q;
    always @(posedge clk) begin
        coef_q <= coef_rom[coef_addr];
        h_q    <= ch ? hist1[hist_idx] : hist0[hist_idx];
    end

    // hist0/hist1's one write-owning block: the clear-zeroing sweep (S_CLR) and the push shift (S_PUSH)
    // both live in the SAME always block below, the same single-owner discipline tau_flac_lpc.sv's own
    // B-371 comment explains Quartus requires (two separate always blocks driving one array fails
    // synthesis even when Icarus tolerates it).
    always @(posedge clk) begin
        if (rst) begin
            st <= S_IDLE; phase <= 9'd0; done <= 1'b0; out_l <= {OUT_W{1'b0}}; out_r <= {OUT_W{1'b0}};
            ch <= 1'b0; tap <= 6'd0; clr_i <= 6'd0; pending_pop <= 1'b0;
        end else begin
            if (clear && st == S_IDLE) begin
                phase <= 9'd0; clr_i <= 6'd0; st <= S_CLR; pending_pop <= 1'b0; done <= 1'b0;
            end
            if (out_rd) done <= 1'b0;    // read-is-ack, same convention as tau_flac_lpc.sv's sample_rd
            case (st)
                S_CLR: begin
                    hist0[clr_i[4:0]] <= 16'sd0;
                    hist1[clr_i[4:0]] <= 16'sd0;
                    if (clr_i == TAPS[5:0] - 6'd1) st <= S_IDLE;
                    else clr_i <= clr_i + 6'd1;
                end
                S_IDLE: begin
                    if (start) st <= pending_pop ? S_SHIFTHIST : S_MAC_ENTRY;
                end
                S_SHIFTHIST: begin
                    // The shift owed from the PREVIOUS output's wrap, now actually performed (see the
                    // pending_pop comment above) -- held_l/held_r have had the whole done..this-start
                    // window to become valid. BUG=5: never shift in, so history silently stops advancing.
                    if (BUG != 5) begin
                        for (integer t = TAPS - 1; t > 0; t = t - 1) begin
                            hist0[t] <= hist0[t-1];
                            hist1[t] <= hist1[t-1];
                        end
                        hist0[0] <= held_l;
                        hist1[0] <= held_r;
                    end
                    pending_pop <= 1'b0;
                    st <= S_MAC_ENTRY;
                end
                S_MAC_ENTRY: begin
                    ch <= 1'b0; tap <= 6'd0; acc <= {ACC_WIDTH{1'b0}};
                    st <= S_MAC;
                end
                S_MAC: st <= S_MAC2;    // wait one cycle for the registered ROM/history read to land
                S_MAC2: begin
                    // ONE multiply-add per cycle, registered -- never chained with the shift below.
                    // BUG=3: zero-extend the coefficient instead of sign-extending it (a negative tap
                    // weight becomes a large positive one -- same mutation shape as tau_flac_lpc.sv's own
                    // BUG=3, which found that an un-sign-extended coefficient is a real, distinguishable
                    // functional mismatch, not an edge case).
                    acc <= acc + ($signed({{(ACC_WIDTH-16){(BUG == 3) ? 1'b0 : coef_q[15]}}, coef_q}) * $signed(h_q));
                    if (tap == TAPS[5:0] - 6'd1) st <= (ch == 1'b0) ? S_NEXTCH : S_SHIFT0;
                    else begin tap <= tap + 6'd1; st <= S_MAC; end
                end
                S_NEXTCH: begin
                    // Channel 0's 32-tap sum is complete (the S_MAC2 cycle above already folded the LAST
                    // tap's product into acc) -- stash it and restart the same loop for channel 1,
                    // matching tau_mp3_poly.sv's own channel-0-then-channel-1 discipline (one shared
                    // multiplier, not two).
                    acc0_r <= acc;
                    ch <= 1'b1; tap <= 6'd0; acc <= {ACC_WIDTH{1'b0}};
                    st <= S_MAC;
                end
                S_SHIFT0: begin
                    // Arithmetic right shift by 15 (Q1.15 coefficients) -- its own cycle, not combined
                    // with the MAC above or the clip below. BUG=2: skip the shift (raw accumulator value
                    // truncated straight to 16 bits -- wildly wrong on any non-trivial coefficient).
                    shifted0 <= (BUG == 2) ? acc0_r : (acc0_r >>> 15);
                    shifted1 <= (BUG == 2) ? acc : (acc >>> 15);
                    st <= S_SHIFT1;
                end
                S_SHIFT1: begin
                    // 16-bit clip, its own cycle (same split-out-the-last-small-step discipline, same
                    // clip-literal style as tau_mp3_poly.sv's own clip16()).
                    out_l <= (shifted0 > OMAX) ? OMAX[OUT_W-1:0] : (shifted0 < OMIN) ? OMIN[OUT_W-1:0] : shifted0[OUT_W-1:0];
                    out_r <= (shifted1 > OMAX) ? OMAX[OUT_W-1:0] : (shifted1 < OMIN) ? OMIN[OUT_W-1:0] : shifted1[OUT_W-1:0];
                    st <= S_PHASE;
                end
                S_PHASE: begin
                    // Phase advance happens here, AFTER the output has been computed from the CURRENT
                    // history -- exactly sim/cymo_resamp_model.c's own loop order. The actual history
                    // shift is DEFERRED to S_SHIFTHIST at the start of the next `start` (see pending_pop's
                    // own comment above) -- only the phase and the pending flag are touched here.
                    // BUG=4: advance by P instead of Q (wrong ratio entirely -- every bank selected would
                    // be bank 0, since phase would wrap every single step).
                    if ({1'b0, phase} + {1'b0, (BUG == 4 ? P[8:0] : Q_STEP[8:0])} >= {1'b0, P[8:0]}) begin
                        phase <= phase + (BUG == 4 ? P[8:0] : Q_STEP[8:0]) - P[8:0];
                        pending_pop <= 1'b1;
                    end else begin
                        phase <= phase + (BUG == 4 ? P[8:0] : Q_STEP[8:0]);
                    end
                    st <= S_DONE;
                end
                S_DONE: begin
                    done <= 1'b1;
                    st <= S_IDLE;
                end
                default: st <= S_IDLE;
            endcase
        end
    end

endmodule
