// =============================================================================
// tau_main_ram.sv -- the CPU's main RAM, one shared read/write port, backing
// store optionally split into two independently power-of-two-sized regions.
//
// Extracted out of mp3_soc.v's own inline array (Phase G, RAM-shrink track,
// docs/PHASE_F_SPEC.md section 4/4.1) so the region-select logic has its own
// name and its own focused testbench (sim/tb_tau_main_ram.v) instead of being
// one more thing buried in mp3_soc.v's already-large always block.
//
// WHY TWO REGIONS: RAM_WORDS **must be a power of two** -- a single
// 49,152-word array (192 KB / 4 lanes) once exploded the Fitter into LUTs
// instead of M10K ("Fitter requires 3621 LABs ... device contains only
// 1848", docs/PHASE_F_SPEC.md section 4). Splitting 49,152 into 32,768
// (WORDS_A) + 16,384 (WORDS_B) keeps each individual array a clean power of
// two, sidestepping the irregular-depth problem entirely.
//
// WORDS_B = 0 collapses this to a single region, bit-identical in structure
// to mp3_soc.v's original array (same one clocked always block, same single
// computed address, same registered read) -- this is what every build uses
// today; WORDS_B > 0 is the opt-in RAM-shrink path (TAU_RAM_192K in
// mp3_soc.v), not yet enabled in any shipped or tested bitstream.
//
// A REAL, DOCUMENTED RISK, not a theoretical one: mp3_soc.v's own comment on
// the array this replaces says plainly that a *different* restructuring of
// this exact memory (splitting the loader onto a second always block) once
// made Quartus fail to infer RAM megafunctions AT ALL for the whole 256 KB,
// falling back to registers ("Cannot convert all sets of registers into RAM
// megafunctions"). This module keeps the same "one always block, one
// computed address per region" shape specifically to minimize that risk, but
// whether Quartus still infers M10K/MLAB for BOTH regions here, rather than
// falling back to logic for one or both, is a synthesis-stage question this
// file's own testbench cannot answer -- a synthesis-only check (matching the
// project's own B-100 precedent: ~5 minutes, no fit) is the required next
// step before any Quartus fit is attempted, and a real fit before any
// hardware install, per the project's standing "measure, don't assume"
// discipline for anything touching M10K inference.
// =============================================================================
module tau_main_ram #(
    parameter WORDS_A = 65536,   // must be a power of two
    parameter WORDS_B = 0,       // must be a power of two, or 0 to disable region B.
                                  // Precondition (not enforced by synthesis, see the
                                  // testbench): WORDS_B <= WORDS_A, which is what makes
                                  // a single high address bit (addr[AW_A]) a correct,
                                  // subtraction-free region selector -- see g_split below.
    parameter AW      = 16,      // must cover WORDS_A+WORDS_B-1
    parameter BUG_FORCE_SEL_A = 0  // test-only: forces every access into region A,
                                    // regardless of address, so sim/tb_tau_main_ram.v's
                                    // mutation check has a real fault to catch. Always 0
                                    // in any real build.
)(
    input  wire            clk,
    input  wire [AW-1:0]   addr,
    input  wire [31:0]     wdata,
    input  wire [3:0]      be,
    input  wire            we,
    output reg  [31:0]     rdata
);
    localparam AW_A = $clog2(WORDS_A);

    reg [7:0] a0 [0:WORDS_A-1];
    reg [7:0] a1 [0:WORDS_A-1];
    reg [7:0] a2 [0:WORDS_A-1];
    reg [7:0] a3 [0:WORDS_A-1];

    generate
    if (WORDS_B == 0) begin : g_single
        // Bit-for-bit the original mp3_soc.v array: one always block, one
        // address, four byte lanes, a registered read. B-225: indexes the
        // port signal directly (no intermediate address wire) -- an earlier
        // version routed through a local `idx` wire and Quartus reported
        // EVERY array here as "uninferred due to asynchronous read logic"
        // even in this WORDS_B=0 case, which is structurally identical to
        // the original working array apart from that indirection. Testing
        // whether removing it alone restores inference.
        always @(posedge clk) begin
            if (we & be[0]) a0[addr[AW_A-1:0]] <= wdata[7:0];
            if (we & be[1]) a1[addr[AW_A-1:0]] <= wdata[15:8];
            if (we & be[2]) a2[addr[AW_A-1:0]] <= wdata[23:16];
            if (we & be[3]) a3[addr[AW_A-1:0]] <= wdata[31:24];
            rdata <= {a3[addr[AW_A-1:0]], a2[addr[AW_A-1:0]], a1[addr[AW_A-1:0]], a0[addr[AW_A-1:0]]};
        end
    end else begin : g_split
        localparam AW_B = $clog2(WORDS_B);

        reg [7:0] b0 [0:WORDS_B-1];
        reg [7:0] b1 [0:WORDS_B-1];
        reg [7:0] b2 [0:WORDS_B-1];
        reg [7:0] b3 [0:WORDS_B-1];

        // addr < WORDS_A+WORDS_B <= 2*WORDS_A (the WORDS_B<=WORDS_A precondition)
        // means addr fits in AW_A+1 bits, so bit AW_A alone tells region A from
        // region B apart, and clearing it (addr[AW_A-1:0]) is exactly addr-WORDS_A
        // when that bit is set -- no subtractor needed, just a wire slice.
        wire sel_b = BUG_FORCE_SEL_A ? 1'b0 : addr[AW_A];

        // B-226/B-227: the real bug. Two earlier versions (a local idx wire,
        // then a nested if/else write with a ternary-of-two-multi-array-reads
        // for the register update) BOTH made Quartus report every array here
        // -- including region A's, whose own shape is otherwise identical to
        // the original working array -- as "uninferred due to asynchronous
        // read logic", falling back to registers and blowing the device's
        // register budget. A parallel isolation (B-227) ruled out both the
        // module-port boundary and the `generate` block as the cause: the
        // SAME failure reproduced even with the split written inline in
        // mp3_soc.v, no module, no generate. What's left, and what this
        // version fixes: EVERY OTHER inferred RAM in this codebase (the
        // original array included) reads with a single, unconditional,
        // directly-addressed `reg <= array[addr];` -- nothing else on the
        // right-hand side. The failing versions read through a ternary
        // selecting between two DIFFERENT multi-array concatenations in one
        // statement, which is not that shape. Fix: give region A and region B
        // each their own plain registered read (qa/qb below), matching the
        // canonical template exactly, then mux the ALREADY-REGISTERED byte
        // values together afterward -- a trivial 8:1 byte mux with no RAM-
        // inference implications at all, since by that point nothing being
        // selected between is an array reference any more. Writes are
        // similarly flattened to one condition per array (`we & be[n] &
        // ~sel_b` / `& sel_b`) instead of a nested if/else, for the same
        // "match the simple per-array template" reason.
        reg [7:0] qa0, qa1, qa2, qa3;
        reg [7:0] qb0, qb1, qb2, qb3;
        reg       q_sel_b;

        always @(posedge clk) begin
            if (we & be[0] & ~sel_b) a0[addr[AW_A-1:0]] <= wdata[7:0];
            if (we & be[1] & ~sel_b) a1[addr[AW_A-1:0]] <= wdata[15:8];
            if (we & be[2] & ~sel_b) a2[addr[AW_A-1:0]] <= wdata[23:16];
            if (we & be[3] & ~sel_b) a3[addr[AW_A-1:0]] <= wdata[31:24];
            if (we & be[0] &  sel_b) b0[addr[AW_B-1:0]] <= wdata[7:0];
            if (we & be[1] &  sel_b) b1[addr[AW_B-1:0]] <= wdata[15:8];
            if (we & be[2] &  sel_b) b2[addr[AW_B-1:0]] <= wdata[23:16];
            if (we & be[3] &  sel_b) b3[addr[AW_B-1:0]] <= wdata[31:24];

            qa0 <= a0[addr[AW_A-1:0]];
            qa1 <= a1[addr[AW_A-1:0]];
            qa2 <= a2[addr[AW_A-1:0]];
            qa3 <= a3[addr[AW_A-1:0]];
            qb0 <= b0[addr[AW_B-1:0]];
            qb1 <= b1[addr[AW_B-1:0]];
            qb2 <= b2[addr[AW_B-1:0]];
            qb3 <= b3[addr[AW_B-1:0]];
            q_sel_b <= sel_b;
        end

        // Combinational, and deliberately NOT inside the clocked block above:
        // rdata is already one cycle behind addr via qa/qb, so this mux must
        // not add a second cycle of latency.
        always @(*) rdata = q_sel_b ? {qb3, qb2, qb1, qb0} : {qa3, qa2, qa1, qa0};
    end
    endgenerate
endmodule
