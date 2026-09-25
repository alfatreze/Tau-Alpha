// =============================================================================
// tau_cdc_gray_bus.sv -- a multi-bit VALUE that changes by at most one step at a time, carried across clock domains
// in Gray code (Helios beam position, B-267).
//
// Why Gray: a plain synchroniser on a multi-bit bus can capture some bits before a change and some after, producing a
// value that was never on the bus. If the source value only ever moves by +1 (a line counter), its Gray encoding changes
// in exactly ONE bit per step, so whatever the destination captures is either the old or the new value -- never garbage.
// The source is registered in Gray form in ITS domain (so the crossing wires are glitch-free flop outputs), synchronised
// bit by bit in the destination domain, and decoded back to binary there.
//
// CAVEAT (why this is only for counters that step by one): a jump of more than one step (e.g. a wrap 399 -> 0) changes
// several Gray bits at once, so the destination may read a transient wrong value for about one destination sample around
// that instant. Consumers must not depend on the value across a wrap; the video line counter wraps inside vertical blanking,
// where every draw is safe anyway.
// =============================================================================
module tau_cdc_gray_bus #(
    parameter W      = 9,
    parameter STAGES = 3
)(
    input  wire         clk_src,
    input  wire [W-1:0] d_src,
    input  wire         clk_dst,
    output wire [W-1:0] q_dst
);
    reg [W-1:0] gray_src = {W{1'b0}};
    always @(posedge clk_src) gray_src <= d_src ^ (d_src >> 1);

    reg [W-1:0] sync [0:STAGES-1];
    integer i;
    initial for (i = 0; i < STAGES; i = i + 1) sync[i] = {W{1'b0}};
    always @(posedge clk_dst) begin
        sync[0] <= gray_src;
        for (i = 1; i < STAGES; i = i + 1) sync[i] <= sync[i-1];
    end

    // Gray -> binary: b[W-1] = g[W-1]; b[k] = b[k+1] ^ g[k]
    wire [W-1:0] g = sync[STAGES-1];
    reg  [W-1:0] b;
    integer k;
    always @* begin
        b[W-1] = g[W-1];
        for (k = W-2; k >= 0; k = k - 1) b[k] = b[k+1] ^ g[k];
    end
    assign q_dst = b;
endmodule
