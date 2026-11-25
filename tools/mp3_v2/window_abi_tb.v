// window_abi_tb.v -- drives tau_mp3_poly with an arbitrary word stream (for tools/mp3_v2/window_abi_check.py): the RTL, not only the C model, against the ABI words.
// Plusargs: +words=<hex file, 64 lines per slot (channel 0's P0..P31 then channel 1's)>  +n=<slots>  +out=<file: 32 lines per slot, {R sample, L sample} hex>  [+mono]
// Prints "CLEAR_CLOCKS <n>" (busy clocks of one clear), "SLOT_CLOCKS <n>" (busy clocks of each slot, one line per slot: the same every time) and "DONE".
`timescale 1ns/1ps
module window_abi_tb;
    reg clk = 0; always #7.5 clk = ~clk;
    reg rst = 1, clear = 0, push_we = 0, go = 0, out_idx_we = 0;
    reg [31:0] push_data = 0;
    reg [4:0] out_idx = 0;
    wire busy; wire [31:0] rd_data; wire [15:0] slots_done;
    tau_mp3_poly dut (.clk(clk), .rst(rst), .clear(clear), .push_we(push_we), .push_data(push_data), .go(go), .busy(busy),
                      .out_idx_we(out_idx_we), .out_idx(out_idx), .rd_data(rd_data), .slots_done(slots_done));
    reg [31:0] vec [0:64*4096-1];
    reg [1023:0] wpath, opath;
    integer n, s, k, fo, cyc, mono;
    initial begin
        if (!$value$plusargs("words=%s", wpath)) begin $display("no +words"); $finish; end
        if (!$value$plusargs("out=%s", opath)) begin $display("no +out"); $finish; end
        if (!$value$plusargs("n=%d", n)) n = 1;
        mono = $test$plusargs("mono");
        $readmemh(wpath, vec);
        fo = $fopen(opath, "w");
        repeat (4) @(posedge clk); rst <= 0; repeat (4) @(posedge clk);
        clear <= 1; @(posedge clk); clear <= 0; cyc = 1; @(posedge clk);
        while (busy) begin @(posedge clk); cyc = cyc + 1; end
        $display("CLEAR_CLOCKS %0d", cyc);
        for (s = 0; s < n; s = s + 1) begin
            for (k = 0; k < 64; k = k + 1) begin
                push_data <= (mono && k >= 32) ? vec[s*64 + k - 32] : vec[s*64 + k]; push_we <= 1; @(posedge clk); push_we <= 0;
            end
            go <= 1; @(posedge clk); go <= 0; cyc = 0; @(posedge clk);
            while (busy && cyc < 20000) begin @(posedge clk); cyc = cyc + 1; end
            $display("SLOT_CLOCKS %0d", cyc + 1);
            for (k = 0; k < 32; k = k + 1) begin
                out_idx <= k; out_idx_we <= 1; @(posedge clk); out_idx_we <= 0; @(posedge clk); @(posedge clk);
                $fdisplay(fo, "%08x", rd_data);
            end
        end
        $fclose(fo);
        $display("DONE");
        $finish;
    end
endmodule
