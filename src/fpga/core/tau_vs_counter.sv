// =============================================================================
// tau_vs_counter.sv -- vsync level plus a free-running frame counter, in the CPU's clock domain
// (Helios/Talos H0, B-260).
//
// Why a counter and not just the level: the vsync pulse is only 4 video lines (~167 us at this core's
// timing) wide. The CPU polls it once per main-loop pass, which is milliseconds apart, so it almost never
// sees an edge -- on the Pocket the Info page's VBLANK row read 0/S for exactly that reason. A counter that
// counts every rising edge in hardware cannot be missed; the firmware just reads it twice and subtracts.
//
// The vsync level crosses clk_vid -> clk_dst through tau_cdc_sync1 (a single bit, so a plain synchroniser is
// the right tool). The pulse is thousands of clk_dst cycles wide, so the synchronised level is clean and the
// edge detect and counter live entirely in clk_dst -- no multi-bit value ever crosses a clock domain.
//
// q = {count[W-1:0], level}: bit 0 is the synchronised level (as before), bits [W:1] the frame count.
// Free-running from reset, never cleared; the firmware diffs two reads (wrap-safe modulo 2^W).
// =============================================================================
module tau_vs_counter #(
    parameter STAGES = 3,
    parameter W      = 16
)(
    input  wire         clk_dst,
    input  wire         d_src,
    output wire [W:0]   q
);
    wire lvl;
    tau_cdc_sync1 #(.STAGES(STAGES)) u_sync (.clk_dst(clk_dst), .d_src(d_src), .q_dst(lvl));
    reg           prev = 1'b0;
    reg [W-1:0]   cnt  = {W{1'b0}};
    always @(posedge clk_dst) begin
        prev <= lvl;
        if (lvl && !prev) cnt <= cnt + 1'b1;
    end
    assign q = {cnt, lvl};
endmodule
