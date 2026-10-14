// =============================================================================
// tau_pcm_ring.sv -- hardware reader of the decoded-PCM ring (gapless, B-741). Design: docs/features/PCM_RING_HW_DESIGN.md.
//
// The CPU WRITES decoded stereo pairs (one 32-bit word each, {right, left}) into a ring in the PSRAM through its uncached window and moves a WRITE INDEX; this unit READS
// them back into the audio FIFO on its own, so no firmware stall can underrun the audio until the ring itself is empty. It is a third client of the single PSRAM controller
// port (arbitrated in mp3_soc.v: this client wins a tie, a granted transaction is never interrupted, the arbiter's two-cycle gap applies).
//
// Indices, not counts: `wptr` and `ridx` are both in [0, size). The ring is empty when they are equal; the CPU keeps it from filling completely (capacity size - 1), as in
// fw/pcm_ring.h. `cnt` counts every pair pushed (free-running, for the heard-position arithmetic). A flush (the firmware's pcm_flush) drops whatever is unread: ridx <= wptr.
//
// One PSRAM read per pair (32 cycles measured, KB-040) while the FIFO holds fewer than THR pairs: the FIFO is topped up, never overfilled (a push into a full pcm_fifo is silently
// dropped, so the level check is the safety property, tested). The read data is captured the cycle `ctl_done` pulses, pushed to the FIFO the next cycle.
// `hold` (B-745): the player's pause. Without it the hardware would keep draining the ring into the FIFO and the DAC after the CPU stopped decoding, so a paused track would play on for the ring's depth.
// BUG (test hook, 0 = correct): 1 no wrap of ridx, 2 flush ignored, 3 ignores the FIFO level, 4 drops a pair (advances twice), 5 ignores `en`, 6 ignores `hold`.
// =============================================================================
`default_nettype none
module tau_pcm_ring #(
    parameter THR = 1536,          // push while the FIFO holds fewer pairs than this (the FIFO is 2048 deep)
    parameter BUG = 0
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        en,                 // sticky enable (MMIO); while 0 the unit is idle and ridx follows wptr
    input  wire        flush,              // one-cycle pulse: drop the unread pairs
    input  wire        hold,               // sticky (MMIO): no new read starts while 1 (pause): the unread pairs are kept, a read in flight completes and is pushed
    input  wire [22:0] base,               // PSRAM word address of ring[0]
    input  wire [22:0] size,               // pairs in the ring (2 .. 2^23)
    input  wire [22:0] wptr,               // CPU write index
    input  wire [11:0] fifo_level,         // pcm_fifo.level (pairs queued)

    output reg  [22:0] ridx,               // read index
    output reg  [31:0] cnt,                // pairs pushed since reset
    output reg         push,               // one-cycle FIFO push
    output reg  [31:0] pdata,
    output wire        busy,               // a PSRAM read is in flight

    output wire        ctl_req,            // level held until ctl_done (same contract as the other PSRAM clients)
    output wire        ctl_we,
    output wire [22:0] ctl_word,
    output wire [31:0] ctl_wdata,
    output wire [3:0]  ctl_be,
    input  wire        ctl_done,
    input  wire [31:0] ctl_rdata,
    input  wire        ctl_guard
);
    localparam [1:0] S_IDLE = 2'd0, S_REQ = 2'd1, S_PUSH = 2'd2;
    reg [1:0] state;
    reg       guard_seen;
    wire      nonempty = (ridx != wptr);
    wire      room     = ((BUG == 3) ? 1'b1 : (fifo_level < THR)) && ((BUG == 6) ? 1'b1 : !hold);
    wire      active   = (BUG == 5) ? 1'b1 : en;
    wire [22:0] nxt    = (ridx + 23'd1 == size) ? 23'd0 : ridx + 23'd1;
    wire [22:0] nxt2   = (nxt  + 23'd1 == size) ? 23'd0 : nxt  + 23'd1;
    wire [22:0] adv    = (BUG == 1) ? ridx + 23'd1 : (BUG == 4 ? nxt2 : nxt);

    assign busy      = (state == S_REQ);
    assign ctl_req   = (state == S_REQ);
    assign ctl_we    = 1'b0;
    assign ctl_word  = base + ridx;
    assign ctl_wdata = 32'd0;
    assign ctl_be    = 4'b1111;

    reg drop;                                         // a flush arrived while a read was in flight: the word is discarded when it completes (the transaction itself cannot be aborted)
    always @(posedge clk) begin
        push <= 1'b0;
        if (rst) begin
            state <= S_IDLE; ridx <= 23'd0; cnt <= 32'd0; pdata <= 32'd0; guard_seen <= 1'b0; drop <= 1'b0;
        end else begin
            if (ctl_guard && state == S_REQ) guard_seen <= 1'b1;
            if (flush && BUG != 2) begin
                ridx <= wptr;                             // drop everything unread
                if (state == S_REQ) drop <= 1'b1;         // keep ctl_req until ctl_done: the arbiter's transaction runs to its end
                else state <= S_IDLE;
            end else case (state)
                S_IDLE: begin
                    if (!active) ridx <= wptr;            // disabled: nothing is owed
                    else if (nonempty && room) state <= S_REQ;
                end
                S_REQ: if (ctl_done) begin
                    if (drop) begin drop <= 1'b0; state <= S_IDLE; end
                    else begin pdata <= ctl_rdata; state <= S_PUSH; end
                end
                S_PUSH: begin
                    push <= 1'b1; cnt <= cnt + 32'd1;
                    ridx <= adv;
                    state <= S_IDLE;
                end
                default: state <= S_IDLE;
            endcase
            // a done that arrives in the same cycle as a flush (state S_REQ, drop just set) is handled next cycle by the drop flag; if ctl_done and flush coincide:
            if (flush && BUG != 2 && state == S_REQ && ctl_done) begin drop <= 1'b0; state <= S_IDLE; end
        end
    end
endmodule
`default_nettype wire
