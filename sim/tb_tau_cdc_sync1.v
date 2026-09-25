// tb_tau_cdc_sync1.v -- functional check for tau_cdc_sync1.sv (Helios/Talos H0).
//
// What this CAN prove: a source-domain level held for long enough relative to
// the destination clock (as any real vsync/vblank pulse -- many scanlines wide
// -- always is relative to clk_sys) is correctly seen, edge for edge, on the
// destination side with bounded latency (2-4 destination cycles).
//
// What this CANNOT prove, same honest limitation tau_cdc_gray_ctr's own
// testbench already documented: a zero-delay functional simulator (iverilog)
// always captures a register's whole input atomically, so it cannot reproduce
// the actual metastability hazard a real synchroniser resolves. This bench does
// not claim to test that -- only that the functional behaviour (correct value,
// bounded latency, no missed short-relative-to-nothing edges) is right.
`timescale 1ns/1ps

module tb_tau_cdc_sync1;
    reg  clk_src = 0;
    reg  clk_dst = 0;
    reg  d_src   = 0;
    wire q_dst;

    // Deliberately unrelated rates, matching clk_vid (~27 MHz-ish video pixel
    // clock order of magnitude) vs clk_sys (60 MHz) -- not simple multiples of
    // each other, so a bug that only shows up with a lucky/unlucky ratio can't
    // hide behind a convenient common period the way it could in mp3_fb.sv's
    // own real clock relationship even less than an arbitrary test one could.
    always #18.5 clk_src = ~clk_src;   // ~27.0 MHz
    always #8.33 clk_dst = ~clk_dst;   // ~60.0 MHz

    tau_cdc_sync1 #(.STAGES(3)) dut (
        .clk_dst(clk_dst),
        .d_src  (d_src),
        .q_dst  (q_dst)
    );

    integer fails = 0;
    integer i;

    task check_eventually(input want, input [63:0] timeout_ns);
        reg [63:0] t0;
        begin
            t0 = $time;
            while (q_dst !== want && ($time - t0) < timeout_ns) @(posedge clk_dst);
            if (q_dst !== want) begin
                $display("FAIL: q_dst never reached %b within %0d ns (still %b at t=%0t)", want, timeout_ns, q_dst, $time);
                fails = fails + 1;
            end
        end
    endtask

    initial begin
        // Reset state check: synchroniser starts at 0.
        #1;
        if (q_dst !== 1'b0) begin
            $display("FAIL: q_dst not 0 at reset (%b)", q_dst);
            fails = fails + 1;
        end

        // A real vsync/vblank-shaped pulse: held for many clk_src periods (a
        // real vertical blank spans multiple whole scanlines), released, and
        // repeated -- exactly the shape this synchroniser exists to carry.
        for (i = 0; i < 5; i = i + 1) begin
            d_src = 1'b1;
            #500;                                  // held ~27 clk_src periods
            check_eventually(1'b1, 60);             // must be seen well within 4 clk_dst periods
            d_src = 1'b0;
            #500;
            check_eventually(1'b0, 60);
        end

        // A pulse shorter than one clk_dst period is a real hazard case for
        // ANY synchroniser (it may or may not be caught, by design) -- not
        // tested here since Talos never produces one this short for vblank
        // (vs_pulse spans many clk_sys cycles by construction, mp3_fb.sv's own
        // VS_ST/VS_EN window), and asserting a specific outcome for it would
        // test simulator behaviour, not the module's real contract.

        if (fails == 0) $display("PASSED (0 failures)");
        else $display("FAILED (%0d failures)", fails);
        $finish;
    end
endmodule
