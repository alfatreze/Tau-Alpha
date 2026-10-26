// =============================================================================
// tau_cymo_feed.sv -- the elastic hand-off between pcm_fifo's track-rate sample tick and the Cymo resampler (B-527).
//
// WHY THIS EXISTS. The first live wiring (B-476, with B-492/B-498's gating) let a sample into the resampler only when pcm_sample_tick (every 1/44100 s) happened
// to land while pop_req was high. pop_req is high for ~1240 of the ~1389 clocks between two 48 kHz starts and the tick comes every 1512 clocks, so a window with
// no tick in it is common: the resampler then consumed the PREVIOUS sample again (a repeat), and the tick that arrived while pop_req was low was thrown away (a
// drop). A cycle-level model (see docs/AUDIT_TRAIL.md B-527) predicts 9-29% of samples repeated and as many dropped depending on the compute time (11.5% at 160
// clocks), and the hardware's own STALE counter read 7,580 after about a second of Cymo being on -- the same range. That sample slip is a plausible cause of
// the owner's reports (a tone whose pitch differs each time Cymo is switched on, a constant low-level noise), and it is structural: two free-running
// accumulators with a one-slot, tick-gated hand-off. B-484/B-488/B-492/B-498 each fixed a real bug in it without removing the cause.
//
// WHAT IT DOES. Every tick pushes the incoming sample into a small queue (DEPTH entries, dropped if full). The resampler's owed sample is taken from the queue
// head as soon as pop_req asks for it, instead of waiting for a tick. The queue absorbs the phase mismatch between the 44.1 kHz supply and the pattern in which
// the resampler asks (147 of every 160 outputs); the average rates are equal. It is primed with PREFILL zero samples on clear so it starts mid-way: room both
// to run empty and to fill. Counters: STALE = a consume found nothing pushed since the previous one (the resampler repeated a sample), DROP = a tick arrived
// with the queue full (an input sample was lost). Both saturate; with the queue they should stay at 0 apart from the very rare slip when the two clocks'
// fractional accumulators drift apart by one sample.
//
// BUG: 1 = the old tick-gated hand-off (push only when a tick coincides with pop_req, no queue): the mutation that must FAIL tb_tau_cymo_feed.
// =============================================================================
module tau_cymo_feed #(
    parameter integer DEPTH   = 4,      // queue entries (power of two)
    parameter integer PREFILL = 2,      // entries (zero samples) present right after clear
    parameter integer BUG     = 0
)(
    input  wire        clk,
    input  wire        rst,
    input  wire        clear,                         // live engage / flush / explicit clear: empty and re-prime the queue, zero the counters
    input  wire        live,
    input  wire        tick,                          // pcm_fifo's sample tick: one new input sample available on in_l/in_r
    input  wire signed [15:0] in_l,
    input  wire signed [15:0] in_r,
    input  wire        pop_req,                       // the resampler owes a new input sample (LEVEL)
    output reg         push_we,                       // one-clock pulse: push_l/push_r go into the resampler
    output reg  signed [15:0] push_l,
    output reg  signed [15:0] push_r,
    output reg  [15:0] stale_cnt,
    output reg  [15:0] drop_cnt,
    output wire [2:0]  level
);
    localparam integer AW = (DEPTH <= 2) ? 1 : (DEPTH <= 4) ? 2 : 3;
    reg [31:0] mem [0:DEPTH-1];
    reg [AW-1:0] wr, rd;
    reg [3:0]    cnt;
    reg          fresh;                               // a push has landed since the last consume
    reg          pop_req_d;
    assign level = cnt[2:0];

    wire consume  = pop_req_d & ~pop_req;             // falling edge: the resampler took the held sample (S_SHIFTHIST)
    // BUG 1 reproduces the old design: a sample only reaches the resampler on a tick that coincides with pop_req.
    wire can_pop  = (BUG == 1) ? 1'b0 : (cnt != 4'd0);
    wire do_pop   = live & pop_req & ~fresh & can_pop;
    wire do_tick  = live & tick;
    wire legacy_push = (BUG == 1) & live & pop_req & ~fresh & tick;
    wire [3:0] eff_cnt = cnt - {3'd0, do_pop};
    wire room     = (BUG == 1) ? 1'b0 : (eff_cnt < DEPTH);

    integer i;
    always @(posedge clk) begin
        push_we   <= 1'b0;
        pop_req_d <= rst ? 1'b0 : pop_req;
        if (rst || clear) begin
            for (i = 0; i < DEPTH; i = i + 1) mem[i] <= 32'd0;
            wr <= PREFILL[AW-1:0]; rd <= {AW{1'b0}}; cnt <= PREFILL[3:0];
            fresh <= 1'b0; stale_cnt <= 16'd0; drop_cnt <= 16'd0;
        end else begin
            if (do_pop) begin
                push_we <= 1'b1; push_l <= mem[rd][31:16]; push_r <= mem[rd][15:0];
                rd <= rd + 1'b1; fresh <= 1'b1;
            end
            if (legacy_push) begin push_we <= 1'b1; push_l <= in_l; push_r <= in_r; fresh <= 1'b1; end
            if (do_tick) begin
                if (room) begin mem[wr] <= {in_l, in_r}; wr <= wr + 1'b1; end
                else if (BUG != 1 && drop_cnt != 16'hFFFF) drop_cnt <= drop_cnt + 16'd1;
            end
            cnt <= cnt + {3'd0, (do_tick & room)} - {3'd0, do_pop};
            if (consume) begin
                fresh <= 1'b0;
                if (!fresh && stale_cnt != 16'hFFFF) stale_cnt <= stale_cnt + 16'd1;
            end
        end
    end
endmodule
