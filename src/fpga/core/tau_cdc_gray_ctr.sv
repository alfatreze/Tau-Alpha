// =============================================================================
// tau_cdc_gray_ctr.sv -- free-running counter, safely read from a different,
// unrelated clock domain.
//
// Same technique mp3_fb.sv already uses for its draw-command FIFO pointers
// (b2g/g2b + a two-flop synchroniser), generalised and reused here for
// PHASE_F_SPEC.md's B7 (SDRAM busy-cycle counter): the counter lives in
// clk_src (the SDRAM arbiter's 100 MHz clk_sdram), the read side is clk_dst
// (mp3_soc's 60 MHz clk_sys), and the two are not phase- or frequency-related.
//
// Why Gray code, not a plain double-flop on the binary value: a binary
// counter can have many bits change at once (e.g. 0111 -> 1000), and
// sampling mid-transition can catch an arbitrary combination of old and new
// bits -- not just stale, but WRONG. Gray-coding guarantees every increment
// changes exactly one bit, so a synchroniser that catches a value mid-flight
// always lands on some value the counter genuinely held (stale but valid),
// never a torn one. This holds at any clk_src/clk_dst frequency ratio and
// for any counter width, as long as the source only ever changes by +1 per
// update -- true here since `inc` adds exactly one busy cycle at a time.
// Same reasoning mp3_fb.sv's own b2g/g2b comment gives for the FIFO pointers.
//
// IMPORTANT, checked rather than assumed: this bug is real silicon-level
// metastability, which a zero-delay functional Verilog simulator cannot
// reproduce -- every register atomically captures its whole input at the
// clock edge in simulation, so a plain binary double-flop sync and this
// Gray-coded one are functionally indistinguishable under iverilog (tried
// it: a raw-binary variant passed the testbench below with 0 failures). The
// testbench therefore verifies convergence and monotonicity under a real
// clk_src/clk_dst frequency mismatch, which is real and worth having, but it
// is NOT proof the Gray coding is doing anything -- that claim rests on the
// same established, cited technique already load-bearing elsewhere in this
// design, not on simulation.
//
// No clear/reset-on-read: clearing a free-running counter across an
// asynchronous domain boundary is its own CDC hazard (the clear pulse would
// need its own synchroniser, and a race between "clear" and "increment"
// arriving in the same source cycle needs care). Matches the existing
// convention for CYCLES (mp3_soc 0x0C) -- also free-running, never cleared --
// firmware computes deltas by sampling before and after a measurement
// window, not by clearing.
// =============================================================================
`default_nettype none

module tau_cdc_gray_ctr #(
    parameter WIDTH = 32
) (
    input  wire             clk_src,
    input  wire             rst_src,
    input  wire             inc,             // pulse: +1 this clk_src cycle

    input  wire             clk_dst,
    input  wire             rst_dst,
    output reg  [WIDTH-1:0] count_dst        // binary, synchronised into clk_dst
);

    // ---- Source domain: binary counter + its Gray mirror -------------------
    reg [WIDTH-1:0] cnt_bin = 0;
    reg [WIDTH-1:0] cnt_gray = 0;

    function [WIDTH-1:0] b2g(input [WIDTH-1:0] b); b2g = b ^ (b >> 1); endfunction
    function [WIDTH-1:0] g2b(input [WIDTH-1:0] g);
        integer i;
        begin
            g2b[WIDTH-1] = g[WIDTH-1];
            for (i = WIDTH-2; i >= 0; i = i - 1) g2b[i] = g2b[i+1] ^ g[i];
        end
    endfunction

    always @(posedge clk_src) begin
        if (rst_src) begin
            cnt_bin  <= {WIDTH{1'b0}};
            cnt_gray <= {WIDTH{1'b0}};
        end else if (inc) begin
            cnt_bin  <= cnt_bin + {{WIDTH-1{1'b0}}, 1'b1};
            cnt_gray <= b2g(cnt_bin + {{WIDTH-1{1'b0}}, 1'b1});
        end
    end

    // ---- Destination domain: two-flop synchroniser + Gray-to-binary --------
    reg [WIDTH-1:0] gray_s1 = 0, gray_s2 = 0;
    always @(posedge clk_dst) begin
        if (rst_dst) begin
            gray_s1   <= {WIDTH{1'b0}};
            gray_s2   <= {WIDTH{1'b0}};
            count_dst <= {WIDTH{1'b0}};
        end else begin
            gray_s1   <= cnt_gray;
            gray_s2   <= gray_s1;
            count_dst <= g2b(gray_s2);
        end
    end

endmodule

`default_nettype wire
