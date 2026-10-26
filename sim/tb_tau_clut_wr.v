// tb_tau_clut_wr.v -- tau_clut_wr stores each R_CLUT_DATA write in the slot the index named when it was written (B-575).
// Drives IDX/DATA strobes with random gaps (including consecutive-cycle writes), applies the outputs to a 256-entry RAM exactly as
// mp3_fb.sv does (`if (wr) clut[waddr] <= wdata`) and compares it with a model of the contract: entry n of a load that starts at s
// lands in slot (s + n) mod 256. BUG=1 (mutation): the old behaviour, must be caught.
`timescale 1ns/1ps
module tb_tau_clut_wr #(parameter BUG = 0);
    reg clk = 0;
    always #5 clk = ~clk;
    reg idx_we = 0, data_we = 0;
    reg [7:0] idx_d = 0;
    reg [15:0] data_d = 0;
    wire wr; wire [7:0] waddr; wire [15:0] wdata;
    tau_clut_wr #(.BUG(BUG)) dut (.clk(clk), .idx_we(idx_we), .idx_d(idx_d), .data_we(data_we), .data_d(data_d),
                                  .wr(wr), .waddr(waddr), .wdata(wdata));
    reg [15:0] ram [0:255];
    reg [15:0] exp [0:255];
    integer pulses = 0, writes = 0, fails = 0, i, k, s, n, g, seed = 1234;
    always @(posedge clk) if (wr) begin ram[waddr] <= wdata; pulses <= pulses + 1; end
    task gap(input integer m); integer j; begin for (j = 0; j < m; j = j + 1) @(posedge clk); end endtask
    task set_idx(input [7:0] v); begin @(posedge clk); idx_we <= 1; idx_d <= v; @(posedge clk); idx_we <= 0; end endtask
    task put(input [15:0] v, input [7:0] slot); begin
        data_we <= 1; data_d <= v; exp[slot] = v; writes = writes + 1; @(posedge clk); data_we <= 0; end endtask
    task load(input [7:0] start, input integer count, input integer maxgap); integer q; begin
        set_idx(start);
        gap($urandom(seed) % (maxgap + 1));
        for (q = 0; q < count; q = q + 1) begin
            put(16'h1000 + ($urandom(seed) % 16'h0F00) + q, start + q);
            if (maxgap > 0) gap($urandom(seed) % (maxgap + 1));
        end
    end endtask
    task check(input [639:0] what); integer j, bad; begin
        gap(4); bad = 0;
        for (j = 0; j < 256; j = j + 1) if (ram[j] !== exp[j]) bad = bad + 1;
        if (bad) begin $display("FAIL: %0s: %0d slots differ", what, bad); fails = fails + 1; end
        else $display("ok   %0s", what);
    end endtask
    initial begin
        for (i = 0; i < 256; i = i + 1) begin ram[i] = 16'd0; exp[i] = 16'd0; end
        gap(4);
        load(8'd0, 256, 0);   check("256 entries from 0, consecutive-cycle writes");
        load(8'd255, 8, 0);   check("8 entries from 255 (wraps to 0..6), consecutive writes");
        load(8'd0, 8, 2);     check("8 entries from 0 with gaps");
        load(8'd100, 20, 3);  check("20 entries from 100 with gaps");
        load(8'd250, 12, 1);  check("12 entries from 250 crossing the wrap");
        set_idx(8'd40); gap(2); put(16'hAAAA, 8'd40); put(16'hBBBB, 8'd41); set_idx(8'd7); put(16'hCCCC, 8'd7); put(16'hDDDD, 8'd8);
        check("a re-seated index mid-load");
        for (k = 0; k < 200; k = k + 1) begin
            s = $urandom(seed) % 256; n = 1 + $urandom(seed) % 40; g = $urandom(seed) % 4;
            load(s[7:0], n, g);
        end
        check("200 random loads");
        if (pulses !== writes) begin $display("FAIL: %0d write pulses for %0d data writes", pulses, writes); fails = fails + 1; end
        else $display("ok   exactly one pulse per data write (%0d)", pulses);
        if (fails == 0) $display("PASSED"); else $display("FAILED (%0d)", fails);
        $finish;
    end
endmodule
