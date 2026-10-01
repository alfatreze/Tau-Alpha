// tb_tau_cymo_resamp.v -- tau_cymo_resamp against the golden-model vectors
// (build/rtl/cymo_resamp_vectors.txt, written by sim/test_cymo_resamp_model.py from
// sim/cymo_resamp_model.c, cross-checked there against an independent Python re-implementation).
// Replays the ENTIRE sequence as the real hardware protocol would: reset, then for each output request
// `start`, wait `done`, compare out_l/out_r against the vector's own expected values, and if the vector's
// `pop` flag is set, push the next unconsumed input sample (held_l/held_r latch) before the next `start`
// -- exercising the real push/start/pop_req handshake end to end, not just the MAC math in isolation.
// Parameter BUG selects a mutation of the DUT, which must FAIL this bench (make test-rtl-cymo-resamp-mutation).
`timescale 1ns/1ps
module tb_tau_cymo_resamp;
    parameter integer BUG = 0;

    reg clk = 0; always #7.5 clk = ~clk;   // ~66.7 MHz, matches clk_sys's own CLK66 rate (irrelevant to correctness, just realistic)
    reg rst = 1;
    reg clear = 0;
    reg push_we = 0;
    reg signed [15:0] push_l = 0, push_r = 0;
    reg start = 0;
    reg out_rd = 0;
    wire busy, done, pop_req;
    wire signed [15:0] out_l, out_r;

    tau_cymo_resamp #(.BUG(BUG)) dut (
        .clk(clk), .rst(rst), .clear(clear),
        .push_we(push_we), .push_l(push_l), .push_r(push_r),
        .start(start), .out_rd(out_rd), .busy(busy), .done(done), .pop_req(pop_req),
        .out_l(out_l), .out_r(out_r)
    );

    integer nin, nout;
    reg [31:0] vin [0:7999];     // NIN*2 words (NIN=4000 in the golden model)
    reg [31:0] vout [0:13061];   // NOUT*3 words (NOUT~4354)
    integer i, k, consumed, fails, cyc;
    reg signed [15:0] exp_l, exp_r;
    reg exp_pop;

    initial begin
        // The vectors file is one flat stream: 2 header words (NIN, NOUT), then NIN*2 input words, then
        // NOUT*3 output words. Read it into a FLAT array sized to the exact known word count (not an
        // oversized placeholder) so $readmemh's own word-count check stays a real sanity check, not noise.
        begin : load
            reg [31:0] flat [0:21063];   // 2 + 4000*2 + 4354*3, matches sim/cymo_resamp_model.c's own NIN=4000
            $readmemh("build/rtl/cymo_resamp_vectors.txt", flat);
            nin  = flat[0];
            nout = flat[1];
            for (i = 0; i < nin*2; i = i + 1) vin[i] = flat[2+i];
            for (i = 0; i < nout*3; i = i + 1) vout[i] = flat[2+nin*2+i];
        end

        fails = 0;
        consumed = 0;
        repeat (4) @(posedge clk); rst <= 0; repeat (4) @(posedge clk);
        // The golden model's initial state is all-zero history (sim/cymo_resamp_model.c's `hist[2][TAPS]
        // = {{0}}`) -- real hardware memory has no defined power-on value, so `clear` must run once before
        // the first output, same as a real track-start would do.
        // Fixed, generous wait rather than polling `busy` right after the pulse: a `busy` read in the
        // SAME active region as the posedge that triggers S_CLR can race the DUT's own NBA update to
        // `st` and observe the pre-transition value, exiting the poll loop one cycle too early. S_CLR
        // takes exactly TAPS=32 cycles; wait well past that.
        clear <= 1; @(posedge clk); clear <= 0;
        repeat (40) @(posedge clk);

        for (k = 0; k < nout; k = k + 1) begin
            exp_l   = vout[3*k][15:0];
            exp_r   = vout[3*k+1][15:0];
            exp_pop = vout[3*k+2][0];

            start <= 1; @(posedge clk); start <= 0;
            cyc = 0;
            while (!done && cyc < 300) begin @(posedge clk); cyc = cyc + 1; end
            if (!done) begin
                fails = fails + 1;
                if (fails < 8) $display("FAIL output %0d: never finished", k);
            end else begin
                if (out_l !== exp_l || out_r !== exp_r) begin
                    fails = fails + 1;
                    if (fails < 8) $display("FAIL output %0d: got (%0d,%0d) want (%0d,%0d)", k, out_l, out_r, exp_l, exp_r);
                end
                if (pop_req !== exp_pop) begin
                    fails = fails + 1;
                    if (fails < 8) $display("FAIL output %0d: pop_req got %0d want %0d", k, pop_req, exp_pop);
                end
                // read-is-ack (same convention as tau_flac_lpc.sv's sample_rd): without this, `done`
                // stays held forever and the NEXT iteration's `while(!done...)` would exit immediately
                // on a stale flag instead of waiting for the real next computation.
                out_rd <= 1; @(posedge clk); out_rd <= 0;
            end
            @(posedge clk);   // let pop_req settle one more cycle before acting on it

            if (exp_pop) begin
                push_l <= vin[2*consumed][15:0];
                push_r <= vin[2*consumed+1][15:0];
                push_we <= 1; @(posedge clk); push_we <= 0;
                consumed = consumed + 1;
            end
        end

        if (consumed !== nin) begin
            fails = fails + 1;
            $display("FAIL: consumed %0d inputs, expected all %0d", consumed, nin);
        end

        if (fails == 0) $display("PASSED (0 failures, %0d outputs, %0d inputs consumed)", nout, consumed);
        else $display("FAILED (%0d failures)", fails);
        $finish;
    end
endmodule
