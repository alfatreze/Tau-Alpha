// tb_tau_mp3_poly.v -- tau_mp3_poly against the golden-model vectors (build/rtl/mp3_poly_vectors.txt, written by sim/test_mp3_poly_model.py from
// Helix's real FDCT32 + PolyphaseStereo): per slot 64 pushed words (channel 0's 32, then channel 1's) and 32 expected PCM words. Every word of
// every slot is compared. Parameter BUG selects a mutation of the DUT, which must FAIL this bench (see make test-rtl-mp3-poly-mutation).
`timescale 1ns/1ps
module tb_tau_mp3_poly;
    parameter integer BUG = 0;
    parameter integer NSLOT = 170;
    reg clk = 0; always #8.333 clk = ~clk;
    reg rst = 1, clear = 0, push_we = 0, go = 0, out_idx_we = 0;
    reg [31:0] push_data = 0;
    reg [4:0] out_idx = 0;
    wire busy; wire [31:0] rd_data; wire [15:0] slots_done;
    tau_mp3_poly #(.BUG(BUG)) dut (.clk(clk), .rst(rst), .clear(clear), .push_we(push_we), .push_data(push_data), .go(go), .busy(busy),
                                  .out_idx_we(out_idx_we), .out_idx(out_idx), .rd_data(rd_data), .slots_done(slots_done));
    reg [31:0] vec [0:NSLOT*96-1];
    integer s, k, fails, wrong_slots, cyc;
    initial begin
        $readmemh("build/rtl/mp3_poly_vectors.txt", vec);
        fails = 0; wrong_slots = 0;
        repeat (4) @(posedge clk); rst <= 0; repeat (4) @(posedge clk);
        clear <= 1; @(posedge clk); clear <= 0; @(posedge clk); while (busy) @(posedge clk);       // a new track: zero the history
        for (s = 0; s < NSLOT; s = s + 1) begin
            for (k = 0; k < 64; k = k + 1) begin
                push_data <= vec[s*96 + k]; push_we <= 1; @(posedge clk); push_we <= 0;
            end
            go <= 1; @(posedge clk); go <= 0; @(posedge clk);
            cyc = 0;
            while (busy && cyc < 20000) begin @(posedge clk); cyc = cyc + 1; end
            if (busy) begin fails = fails + 1; $display("FAIL slot %0d: never finished", s); end
            if (s == 0) $display("clocks per stereo slot: %0d", cyc);
            begin : cmp
                integer bad; bad = 0;
                for (k = 0; k < 32; k = k + 1) begin
                    out_idx <= k; out_idx_we <= 1; @(posedge clk); out_idx_we <= 0; @(posedge clk); @(posedge clk);
                    if (rd_data !== vec[s*96 + 64 + k]) begin
                        bad = bad + 1; fails = fails + 1;
                        if (fails < 8) $display("FAIL slot %0d word %0d: got %08x want %08x", s, k, rd_data, vec[s*96 + 64 + k]);
                    end
                end
                if (bad) wrong_slots = wrong_slots + 1;
            end
        end
        if (slots_done !== NSLOT[15:0]) begin fails = fails + 1; $display("FAIL slots_done %0d", slots_done); end
        if (fails == 0) $display("PASSED (0 failures)"); else $display("FAILED (%0d words wrong in %0d slots)", fails, wrong_slots);
        $finish;
    end
endmodule
