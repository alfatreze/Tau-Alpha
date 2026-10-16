// tb_pcm_under.v -- Player 2 S1 (P6): the free-running underrun statistics of pcm_fifo.
// Checks: zero after reset; nothing counts before the FIFO has primed (a track start is not an underrun); once primed, each output tick that finds the FIFO empty is one tick and a
// run of them is ONE event, a real sample ends the run; a flush does not clear the record; the tick count saturates at 0xFFFF. UB selects a mutant (1 counts unprimed, 2 counts an
// event per tick, 3 clears on flush): each must fail this bench.
`timescale 1ns/1ps
module tb_pcm_under;
    parameter UB = 0;
    reg clk = 0, rst = 1, flush = 0, push = 0; reg [31:0] pdata = 0;
    wire full, empty, underrun; wire [11:0] level; wire signed [15:0] out_l, out_r; wire [14:0] ev; wire [15:0] tk;
    pcm_fifo #(.AW(11), .UBUG(UB)) dut (.clk(clk), .rst(rst), .flush(flush), .push(push), .pdata(pdata), .full(full), .empty(empty), .level(level),
        .rate_inc(32'h4000_0000), .out_l(out_l), .out_r(out_r), .underrun(underrun), .under_events(ev), .under_ticks(tk));       // a tick every 4 clocks
    always #10 clk = ~clk;
    integer errors = 0, k;
    task chk(input [255:0] name, input ok); begin if (!ok) begin errors = errors + 1; $display("FAIL %0s (ev=%0d tk=%0d)", name, ev, tk); end end endtask
    task push1; begin @(negedge clk); push = 1; pdata = 32'h1234_5678; @(negedge clk); push = 0; end endtask
    task ticks(input integer n); begin repeat (n * 4) @(negedge clk); end endtask
    integer ev0, tk0;
    initial begin
        repeat (4) @(negedge clk); rst = 0; @(negedge clk);
        chk("zero after reset", ev == 0 && tk == 0);
        ticks(50); chk("unprimed ticks do not count", ev == 0 && tk == 0);
        for (k = 0; k <= 1024; k = k + 1) push1;             // prime (half full)
        while (!empty) @(negedge clk);
        ticks(2); ev0 = ev; tk0 = tk;                           // the first empty ticks after the drain
        ticks(10);
        chk("a run of empty ticks is counted per tick", tk >= tk0 + 8 && tk <= tk0 + 12);
        chk("and as ONE event", ev == 1);
        push1; ticks(1); ticks(6);
        chk("a real sample ends the run, the next run is event 2", ev == 2);
        ev0 = ev; tk0 = tk;
        @(negedge clk); flush = 1; @(negedge clk); flush = 0; ticks(10);
        chk("a flush keeps the record", ev == ev0 && tk >= tk0);
        chk("after a flush (unprimed) nothing more counts", ev == ev0 && tk == tk0);
        for (k = 0; k <= 1024; k = k + 1) push1;
        while (!empty) @(negedge clk);
        ticks(70000);
        chk("ticks saturate at 0xFFFF", tk == 16'hFFFF);
        if (errors == 0) $display("PASSED"); else $display("FAILED %0d", errors);
        $finish;
    end
endmodule
