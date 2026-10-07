// Replays the golden vectors of tools/lab/halcyon_engine_model.py against tau_halcyon (B-635).
// Ops: W idx val (shadow write), C nact (commit), Z (clear), S bypass xl xr el er, P nact bypass xl xr el er (commit pulse mid-sample).
// -DHW=16|24 picks the width and the vector file, -DHBUG=n the mutant.
`timescale 1ns/1ps
`ifndef HW
`define HW 16
`endif
`ifndef HBUG
`define HBUG 0
`endif
module tb_tau_halcyon;
    localparam integer W = `HW;
    reg clk = 0; always #8 clk = ~clk;
    reg rst = 1, bypass = 0, wr_we = 0, commit = 0, clr = 0;
    reg [7:0] wr_idx = 0; reg signed [23:0] wr_data = 0; reg [5:0] nact_in = 0;
    reg signed [W-1:0] in_l = 0, in_r = 0;
    wire signed [W-1:0] out_l, out_r; wire busy;
    tau_halcyon #(.W(W), .BUG(`HBUG)) dut (.clk(clk), .rst(rst), .in_l(in_l), .in_r(in_r), .bypass(bypass),
        .wr_we(wr_we), .wr_idx(wr_idx), .wr_data(wr_data), .nact_in(nact_in), .commit(commit), .clr(clr),
        .out_l(out_l), .out_r(out_r), .busy(busy));
    integer fd, rc, n, bad, maxbusy, cyc, v, idx, nv, byp, xl, xr, el, er;
    reg [8*4-1:0] op;
    task sample(input integer mid);
        begin
            in_l <= xl; in_r <= xr; bypass <= byp[0];
            @(posedge clk); while (!dut.tick) @(posedge clk);
            @(posedge clk); cyc = 0;
            if (mid >= 0) begin
                repeat (40) @(posedge clk);
                nact_in <= mid[5:0]; commit <= 1; @(posedge clk); commit <= 0; cyc = 41;
            end
            while (busy) begin @(posedge clk); cyc = cyc + 1; end
            if (cyc > maxbusy) maxbusy = cyc;
            repeat (3) @(posedge clk);
            n = n + 1;
            if (out_l !== el[W-1:0] || out_r !== er[W-1:0]) begin
                bad = bad + 1;
                if (bad <= 5) $display("MISMATCH #%0d: got %0d,%0d want %0d,%0d", n, out_l, out_r, el, er);
            end
        end
    endtask
    initial begin
        fd = $fopen(`HW == 24 ? "build/rtl/halcyon_vectors_w24.txt" : "build/rtl/halcyon_vectors_w16.txt", "r");
        if (fd == 0) begin $display("FAIL: no vectors"); $finish; end
        n = 0; bad = 0; maxbusy = 0;
        repeat (4) @(posedge clk); rst = 0;
        repeat (200) @(posedge clk);                         // the reset sweep clears the state
        while (!$feof(fd)) begin
            rc = $fscanf(fd, "%s", op);
            if (rc == 1) begin
                if (op == "W") begin rc = $fscanf(fd, "%d %d", idx, v); @(posedge clk); wr_idx <= idx[7:0]; wr_data <= v[23:0]; wr_we <= 1; @(posedge clk); wr_we <= 0; end
                else if (op == "C") begin rc = $fscanf(fd, "%d", nv); @(posedge clk); nact_in <= nv[5:0]; commit <= 1; @(posedge clk); commit <= 0; repeat (3) @(posedge clk); end
                else if (op == "Z") begin @(posedge clk); clr <= 1; @(posedge clk); clr <= 0; repeat (200) @(posedge clk); end
                else if (op == "S") begin rc = $fscanf(fd, "%d %d %d %d %d", byp, xl, xr, el, er); sample(-1); end
                else if (op == "P") begin rc = $fscanf(fd, "%d %d %d %d %d %d", nv, byp, xl, xr, el, er); sample(nv); end
            end
        end
        if (bad == 0) $display("PASS tb_tau_halcyon W=%0d: %0d samples match the model; busy at most %0d clocks (budget %0d)", W, n, maxbusy, dut.DIV);
        else $display("FAIL tb_tau_halcyon W=%0d: %0d mismatches of %0d", W, bad, n);
        $finish;
    end
endmodule
