// Replays the golden vectors from sim/test_cymo_gain_model.py --vectors against tau_gain_stage (B-615). Events: T target, F fade_now, N snap, H shift, S x y adv exp_l exp_r.
`timescale 1ns/1ps
module tb_tau_gain_stage;
    reg clk = 0; always #5 clk = ~clk;
    reg rst = 1, tick = 0, adv = 0, flush = 0, fade_now = 0, snap = 0;
    reg [15:0] target = 16'd32768; reg [1:0] shift = 2'd3;
    reg signed [15:0] in_l = 0, in_r = 0;
    wire signed [15:0] out_l, out_r; wire [15:0] cur_o; wire fading;
    tau_gain_stage #(.BUG(`BUGV)) dut (.clk(clk), .rst(rst), .tick(tick), .adv(adv), .flush(flush), .fade_now(fade_now), .snap(snap),
        .target(target), .ramp(16'd149), .shift(shift), .in_l(in_l), .in_r(in_r), .out_l(out_l), .out_r(out_r), .cur_o(cur_o), .fading(fading));
    // torn-pair monitor: one channel changes on an edge and the other on the very next edge
    reg signed [15:0] pl = 0, pr = 0; reg lc1 = 0, rc1 = 0; integer torn = 0;
    always @(posedge clk) begin
        if ((lc1 && !rc1 && (out_r !== pr) && (out_l === pl)) || (rc1 && !lc1 && (out_l !== pl) && (out_r === pr))) torn = torn + 1;
        lc1 <= (out_l !== pl); rc1 <= (out_r !== pr); pl <= out_l; pr <= out_r;
    end
    integer fd, rc, n, bad, i; reg [8*8-1:0] op; integer x, y, a, el, er, v;
    reg [255:0] line;
    initial begin
        fd = $fopen("build/rtl/gain_vectors.txt", "r");
        if (fd == 0) begin $display("FAIL: no vectors"); $finish; end
        n = 0; bad = 0;
        repeat (4) @(posedge clk); rst <= 0; @(posedge clk);
        while (!$feof(fd)) begin
            rc = $fscanf(fd, "%s", op);
            if (rc == 1) begin
                if (op == "T") begin rc = $fscanf(fd, "%d", v); target <= v[15:0]; repeat (3) @(posedge clk); end
                else if (op == "F") begin @(posedge clk); fade_now <= 1; @(posedge clk); fade_now <= 0; repeat (3) @(posedge clk); end
                else if (op == "N") begin @(posedge clk); snap <= 1; @(posedge clk); snap <= 0; repeat (3) @(posedge clk); end
                else if (op == "H") begin rc = $fscanf(fd, "%d", v); shift <= v[1:0]; repeat (3) @(posedge clk); end
                else if (op == "S") begin
                    rc = $fscanf(fd, "%d %d %d %d %d", x, y, a, el, er);
                    @(posedge clk); tick <= 1; adv <= a[0]; in_l <= x[15:0]; in_r <= y[15:0];
                    @(posedge clk); tick <= 0; adv <= 0;
                    repeat (24) @(posedge clk);
                    n = n + 1;
                    if (out_l !== el[15:0] || out_r !== er[15:0]) begin
                        bad = bad + 1;
                        if (bad <= 5) $display("MISMATCH #%0d x=%0d y=%0d adv=%0d: got %0d,%0d want %0d,%0d (cur=%0d)", n, x, y, a, out_l, out_r, el, er, cur_o);
                    end
                end
            end
        end
        if (bad == 0 && torn == 0) $display("PASS tb_tau_gain_stage: %0d sample pairs match the model, no torn pair", n);
        else $display("FAIL tb_tau_gain_stage: %0d mismatches, %0d torn pairs of %0d", bad, torn, n);
        $finish;
    end
endmodule
