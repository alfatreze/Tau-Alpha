// tb_tau_flac_lpc.v -- tau_flac_lpc against the golden-model vectors (build/rtl/flac_lpc_vectors.txt,
// written by sim/test_flac_lpc_model.py from sim/flac_lpc_model.c, itself independently re-proving
// sim/test_flac_lpc_symmetry.py's own result, docs/research/FLAC_LPC_KERNEL_DESIGN.md). Each vector is
// 67 fixed-width hex words: order, shift, 32 coefficient slots (zero-padded past `order`), 32 history
// slots (same padding), and the expected PREDICTION (fw/flac.c's own `acc >> shift`, no residual added --
// the golden model never adds one, matching sim/test_flac_lpc_symmetry.py's own software_predict()). For
// every vector: load CFG, load `order` coefficients and `order` warm-up samples (both index 0 = most-
// recent-paired, per the module's own register contract), write a real NON-ZERO RESIDUAL (a simple
// per-vector value, not zero -- zero would leave the DUT's own add-residual step (S_ADD) untested, since
// adding zero cannot distinguish a correct adder from a broken one) and compare SAMPLE against the
// vector's own prediction plus that same residual, computed here with Verilog's own signed add. Parameter
// BUG selects a mutation of the DUT, which must FAIL this bench (see make test-rtl-flac-lpc-mutation).
`timescale 1ns/1ps
module tb_tau_flac_lpc;
    parameter integer BUG = 0;
    parameter integer NVEC = 20000;
    localparam integer STRIDE = 67;

    reg clk = 0; always #8.333 clk = ~clk;
    reg rst = 1;
    reg        cfg_we = 0;
    reg [5:0]  cfg_order = 0;
    reg [4:0]  cfg_shift = 0;
    reg        coef_idx_we = 0;
    reg [4:0]  coef_idx_d = 0;
    reg        coef_data_we = 0;
    reg signed [15:0] coef_data_d = 0;
    reg        warm_idx_we = 0;
    reg [4:0]  warm_idx_d = 0;
    reg        warm_data_we = 0;
    reg signed [31:0] warm_data_d = 0;
    reg        residual_we = 0;
    reg signed [31:0] residual_d = 0;
    reg        sample_rd = 0;
    wire signed [31:0] sample;
    wire busy, done;

    tau_flac_lpc #(.BUG(BUG)) dut (
        .clk(clk), .rst(rst),
        .cfg_we(cfg_we), .cfg_order(cfg_order), .cfg_shift(cfg_shift),
        .coef_idx_we(coef_idx_we), .coef_idx_d(coef_idx_d), .coef_data_we(coef_data_we), .coef_data_d(coef_data_d),
        .warm_idx_we(warm_idx_we), .warm_idx_d(warm_idx_d), .warm_data_we(warm_data_we), .warm_data_d(warm_data_d),
        .residual_we(residual_we), .residual_d(residual_d),
        .sample_rd(sample_rd), .sample(sample), .busy(busy), .done(done)
    );

    reg [31:0] vec [0:NVEC*STRIDE-1];
    integer v, j, fails, cyc, base;
    reg [31:0] order, shift_v, expect_out;

    initial begin
        $readmemh("build/rtl/flac_lpc_vectors.txt", vec);
        fails = 0;
        repeat (4) @(posedge clk); rst <= 0; repeat (4) @(posedge clk);

        for (v = 0; v < NVEC; v = v + 1) begin
            base = v * STRIDE;
            order = vec[base]; shift_v = vec[base+1]; expect_out = vec[base+2+64];

            cfg_order <= order[5:0]; cfg_shift <= shift_v[4:0]; cfg_we <= 1; @(posedge clk); cfg_we <= 0; @(posedge clk);

            for (j = 0; j < order; j = j + 1) begin
                coef_idx_d <= j[4:0]; coef_idx_we <= 1; @(posedge clk); coef_idx_we <= 0;
                coef_data_d <= vec[base+2+j][15:0]; coef_data_we <= 1; @(posedge clk); coef_data_we <= 0;
            end
            for (j = 0; j < order; j = j + 1) begin
                warm_idx_d <= j[4:0]; warm_idx_we <= 1; @(posedge clk); warm_idx_we <= 0;
                warm_data_d <= vec[base+2+32+j]; warm_data_we <= 1; @(posedge clk); warm_data_we <= 0;
            end

            // A real, non-zero, varying residual -- exercises S_ADD properly (see the file header note).
            residual_d <= (v * 32'sd7919) ^ 32'sd12345;
            residual_we <= 1; @(posedge clk); residual_we <= 0;
            cyc = 0;
            while (!done && cyc < 200) begin @(posedge clk); cyc = cyc + 1; end
            if (!done) begin
                fails = fails + 1;
                if (fails < 8) $display("FAIL vector %0d: never finished (order %0d)", v, order);
            end else begin
                if (sample !== ($signed(expect_out) + residual_d)) begin
                    fails = fails + 1;
                    if (fails < 8) $display("FAIL vector %0d: order %0d shift %0d got %0d want %0d",
                                             v, order, shift_v, sample, $signed(expect_out) + residual_d);
                end
                if (v == 0) $display("clocks for order-%0d prediction: %0d", order, cyc);
            end
            sample_rd <= 1; @(posedge clk); sample_rd <= 0; @(posedge clk);
        end

        // ---- sequential prediction test: exercises the history push/shift (S_PUSH) -------------------
        // The main loop above reloads warm-up history fresh for every vector, so it never actually
        // exercises push/shift across successive predictions within one subframe -- the real usage
        // pattern (many residuals per subframe, each updating history for the next). Hand-computed here
        // with small values so the expected sequence is easy to verify by eye, not just by construction:
        // order 2, coef = {3, -2}, warm = {10, 5} (index 0 = most recent, per the module's own
        // contract). sample1 = 3*10 + (-2)*5 + r1 = 30 - 10 + r1 = 20 + r1. After the push, history
        // becomes {sample1, 10} (the old index-0 value shifts to index 1, dropping the old index-1
        // value). sample2 = 3*sample1 + (-2)*10 + r2 = 3*sample1 - 20 + r2 -- a value that can ONLY be
        // right if S_PUSH actually updated history; BUG=5 (no push) would instead reuse {10, 5} again
        // and produce 20 + r2, a different, distinguishable number.
        begin : seq_test
            reg signed [31:0] s1, s2, want1, want2;
            cfg_order <= 6'd2; cfg_shift <= 5'd0; cfg_we <= 1; @(posedge clk); cfg_we <= 0; @(posedge clk);
            coef_idx_d <= 5'd0; coef_idx_we <= 1; @(posedge clk); coef_idx_we <= 0;
            coef_data_d <= 16'sd3; coef_data_we <= 1; @(posedge clk); coef_data_we <= 0;
            coef_idx_d <= 5'd1; coef_idx_we <= 1; @(posedge clk); coef_idx_we <= 0;
            coef_data_d <= -16'sd2; coef_data_we <= 1; @(posedge clk); coef_data_we <= 0;
            warm_idx_d <= 5'd0; warm_idx_we <= 1; @(posedge clk); warm_idx_we <= 0;
            warm_data_d <= 32'sd10; warm_data_we <= 1; @(posedge clk); warm_data_we <= 0;
            warm_idx_d <= 5'd1; warm_idx_we <= 1; @(posedge clk); warm_idx_we <= 0;
            warm_data_d <= 32'sd5; warm_data_we <= 1; @(posedge clk); warm_data_we <= 0;

            residual_d <= 32'sd7; residual_we <= 1; @(posedge clk); residual_we <= 0;
            cyc = 0; while (!done && cyc < 200) begin @(posedge clk); cyc = cyc + 1; end
            s1 = sample; want1 = 32'sd20 + 32'sd7;
            sample_rd <= 1; @(posedge clk); sample_rd <= 0; @(posedge clk);

            residual_d <= 32'sd11; residual_we <= 1; @(posedge clk); residual_we <= 0;
            cyc = 0; while (!done && cyc < 200) begin @(posedge clk); cyc = cyc + 1; end
            s2 = sample; want2 = 3 * s1 - 32'sd20 + 32'sd11;
            sample_rd <= 1; @(posedge clk); sample_rd <= 0; @(posedge clk);

            if (s1 !== want1) begin fails = fails + 1; $display("FAIL seq_test sample1: got %0d want %0d", s1, want1); end
            if (s2 !== want2) begin fails = fails + 1; $display("FAIL seq_test sample2 (history push): got %0d want %0d", s2, want2); end
            else $display("ok   seq_test: sample1=%0d sample2=%0d (history push verified)", s1, s2);
        end

        if (fails == 0) $display("PASSED (0 failures, %0d vectors + sequential push test)", NVEC);
        else $display("FAILED (%0d failures)", fails);
        $finish;
    end
endmodule
