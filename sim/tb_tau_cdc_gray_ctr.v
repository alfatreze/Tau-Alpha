// ============================================================================
// Prove tau_cdc_gray_ctr's cross-domain guarantee without an SDRAM model:
// - clk_src (100 MHz-equivalent) and clk_dst (60 MHz-equivalent) are
//   unrelated (different period, no fixed phase);
// - count_dst never jumps backward and never overshoots the source's true
//   running total (the failure mode a plain binary double-flop would have);
// - after the source quiesces, count_dst converges to the exact true total.
//
// NOT covered here: the actual metastability/bit-tearing hazard Gray coding
// exists to prevent is a physical phenomenon a zero-delay functional
// simulator cannot produce (checked: a raw-binary-sync variant passes this
// same bench with 0 failures, since iverilog always captures a register's
// whole input atomically at the clock edge). See tau_cdc_gray_ctr.sv's own
// header for what this bench does and does not prove.
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module tb_tau_cdc_gray_ctr;
    localparam WIDTH = 32;

    reg clk_src = 0, clk_dst = 0;
    always #5  clk_src = ~clk_src;   // 100 MHz-equivalent, 10 ns period
    always #8  clk_dst = ~clk_dst;   // 60 MHz-equivalent-ish, 16 ns period, unrelated phase

    reg rst_src = 1, rst_dst = 1;
    reg inc = 0;
    wire [WIDTH-1:0] count_dst;

    // True reference total, kept in the testbench's own always block on clk_src.
    reg [WIDTH-1:0] true_total = 0;
    always @(posedge clk_src) begin
        if (rst_src) true_total <= 0;
        else if (inc) true_total <= true_total + 1;
    end

    tau_cdc_gray_ctr #(.WIDTH(WIDTH)) dut (
        .clk_src(clk_src), .rst_src(rst_src), .inc(inc),
        .clk_dst(clk_dst), .rst_dst(rst_dst), .count_dst(count_dst)
    );

    integer errors = 0;
    task chk(input cond, input [511:0] what);
        begin
            if (!cond) begin $display("FAIL: %0s", what); errors = errors + 1; end
            else       $display("ok:   %0s", what);
        end
    endtask

    // Monotonicity watch: sample count_dst every clk_dst edge and require it
    // never decreases and never exceeds the (asynchronously sampled, so
    // itself slightly stale, but only ever growing) true_total.
    reg [WIDTH-1:0] prev_dst = 0;
    integer mono_fail = 0;
    integer overshoot_fail = 0;
    always @(posedge clk_dst) begin
        if (!rst_dst) begin
            if (count_dst < prev_dst) mono_fail = mono_fail + 1;
            if (count_dst > true_total) overshoot_fail = overshoot_fail + 1;
            prev_dst <= count_dst;
        end
    end

    integer i;
    initial begin
        repeat (5) @(posedge clk_src);
        rst_src = 0; rst_dst = 0;

        // Drive a pseudo-random, bursty increment pattern for a while -- the
        // kind of traffic a real SDRAM arbiter's busy signal produces
        // (back-to-back during a burst, idle between).
        for (i = 0; i < 5000; i = i + 1) begin
            @(posedge clk_src);
            inc = ($random % 3) != 0;   // busy ~2/3 of cycles, bursty via seed correlation
        end
        inc = 0;

        // Let the destination domain fully settle (well past the 2-flop
        // synchroniser's worst-case latency) before checking convergence.
        repeat (20) @(posedge clk_dst);

        chk(mono_fail == 0, "count_dst never decreases between samples");
        chk(overshoot_fail == 0, "count_dst never exceeds the true running total");
        chk(count_dst == true_total,
            "count_dst converges to the exact true total once the source quiesces");
        chk(true_total > 0 && true_total < 5000, "sanity: some but not all cycles incremented");

        @(posedge clk_dst);
        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end

    initial begin
        #200000;
        $display("TIMEOUT");
        $finish;
    end
endmodule

`default_nettype wire
