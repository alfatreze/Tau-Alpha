// =============================================================================
// tau_ram_probe_a.sv -- EXPERIMENT, not a real feature. B-226's isolation test:
// is a byte-array memory, in its OWN module (module-port boundary), with NO
// generate block and NO split-region logic -- otherwise a literal, unmodified
// copy of mp3_soc.v's original working array shape -- still M10K-inferrable?
//
// If this infers cleanly, the module boundary alone is not the problem and
// B-225/B-226's `generate` block is the real suspect. If this ALSO fails,
// no array-in-its-own-module pattern works on this device/toolchain for this
// memory shape, independent of generate, and tau_main_ram.sv's whole
// extract-into-a-module approach needs to be abandoned, not just tweaked.
//
// Delete after the experiment answers the question either way -- this file
// is not wired into any real build path.
// =============================================================================
module tau_ram_probe_a #(
    parameter WORDS = 65536,
    parameter AW    = 16
)(
    input  wire            clk,
    input  wire [AW-1:0]   addr,
    input  wire [31:0]     wdata,
    input  wire [3:0]      be,
    input  wire            we,
    output reg  [31:0]     rdata
);
    reg [7:0] a0 [0:WORDS-1];
    reg [7:0] a1 [0:WORDS-1];
    reg [7:0] a2 [0:WORDS-1];
    reg [7:0] a3 [0:WORDS-1];

    always @(posedge clk) begin
        if (we & be[0]) a0[addr] <= wdata[7:0];
        if (we & be[1]) a1[addr] <= wdata[15:8];
        if (we & be[2]) a2[addr] <= wdata[23:16];
        if (we & be[3]) a3[addr] <= wdata[31:24];
        rdata <= {a3[addr], a2[addr], a1[addr], a0[addr]};
    end
endmodule
