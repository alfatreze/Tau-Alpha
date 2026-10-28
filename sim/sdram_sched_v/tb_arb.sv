// Unit test of the scheduler's arbiter (urgent first, same-row continuation, weighted round robin per piece).
`timescale 1ns/1ps
module tb_arb;
    reg clk = 0; always #5 clk = ~clk;
    reg rst = 1;
    reg [5:0] cand = 0, urgent = 0;
    reg cont_ok = 0; reg [2:0] last_port = 0;
    reg grant = 0; reg [2:0] grant_port = 0; reg grant_wrr = 0;
    wire sel_v, sel_wrr; wire [2:0] sel_p;
    // weights: port 0 is the least significant nibble: p0=4 p1=4 p2=4 p3=2 p4=1 p5=1
    tau_sdram_sv_arb #(.NP(6), .WEIGHT({4'd1,4'd1,4'd1,4'd1,4'd2,4'd4,4'd4,4'd4}) ) dut (.clk(clk), .rst(rst), .cand(cand), .urgent(urgent),
        .cont_ok(cont_ok), .last_port(last_port), .grant(grant), .grant_port(grant_port), .grant_wrr(grant_wrr),
        .sel_v(sel_v), .sel_p(sel_p), .sel_wrr(sel_wrr));
    integer fail = 0, i;
    integer n0, n1, n2, n3, n4, n5;
    task chk(input c, input [255:0] m); begin if (!c) begin $display("FAIL: %0s", m); fail = fail + 1; end end endtask
    task do_grant; begin grant = 1; grant_port = sel_p; grant_wrr = sel_wrr; @(posedge clk); #1; grant = 0; end endtask
    task count_sel; begin
        case (sel_p) 0: n0 = n0 + 1; 1: n1 = n1 + 1; 2: n2 = n2 + 1; 3: n3 = n3 + 1; 4: n4 = n4 + 1; default: n5 = n5 + 1; endcase
    end endtask
    initial begin
        repeat (3) @(posedge clk); #1; rst = 0;
        cand = 6'b000000; #1; chk(!sel_v, "no candidate, no selection");
        cand = 6'b100000; #1; chk(sel_v && sel_p == 5, "single candidate");
        cand = 6'b111111; urgent = 6'b001000; #1; chk(sel_v && sel_p == 3 && !sel_wrr, "urgent first");
        urgent = 6'b101000; #1; chk(sel_p == 3, "lowest urgent index");
        urgent = 6'b100000; cand = 6'b011111; #1; chk(sel_p != 5, "urgent port without a candidate is not chosen");
        urgent = 0; cont_ok = 1; last_port = 4; cand = 6'b111111; #1; chk(sel_p == 4 && !sel_wrr, "continuation");
        urgent = 6'b000001; #1; chk(sel_p == 0, "urgent beats continuation");
        cont_ok = 0; urgent = 0;
        // weighted round robin, all candidates: shares 4:4:4:2:1:1
        cand = 6'b111111; n0 = 0; n1 = 0; n2 = 0; n3 = 0; n4 = 0; n5 = 0;
        for (i = 0; i < 160; i = i + 1) begin #1; count_sel; do_grant; end
        $display("shares %0d %0d %0d %0d %0d %0d", n0, n1, n2, n3, n4, n5);
        chk(n0 == 40 && n1 == 40 && n2 == 40 && n3 == 20 && n4 == 10 && n5 == 10, "WRR shares 4:4:4:2:1:1");
        // a port without a candidate is skipped; the others keep their ratio
        cand = 6'b110011; n0 = 0; n1 = 0; n2 = 0; n3 = 0; n4 = 0; n5 = 0;
        for (i = 0; i < 100; i = i + 1) begin #1; count_sel; do_grant; end
        $display("shares %0d %0d %0d %0d %0d %0d", n0, n1, n2, n3, n4, n5);
        chk(n2 == 0 && n3 == 0 && n0 == 40 && n1 == 40 && n4 == 10 && n5 == 10, "WRR skips idle ports");
        // an urgent grant does not disturb the round robin state
        cand = 6'b000011; urgent = 6'b000001; #1; chk(sel_p == 0 && !sel_wrr, "urgent");
        if (fail == 0) $display("TEST arb PASS"); else $display("TEST arb FAIL (%0d)", fail);
        $finish;
    end
endmodule
