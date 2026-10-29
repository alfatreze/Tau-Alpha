// Unit test of the per-port queue (piece generation, page splits, 2D rows, bad requests) through tau_sdram_sv_pq.
`timescale 1ns/1ps
module tb_queue;
    reg clk = 0; always #5 clk = ~clk;
    reg rst = 1;
    reg req_valid = 0; reg req_we = 0; reg [24:0] req_addr = 0; reg [9:0] req_len = 0; reg [7:0] req_tag = 0;
    reg req_2d = 0; reg [5:0] req_w = 0, req_h = 0; reg [10:0] req_stride = 0; reg [7:0] sk_vld = 0; reg [23:0] sk_base = 0; reg [15:0] sk_val = 0; 
    wire [3:0] ent_v, ent_we; wire [4*13-1:0] ent_lo, ent_hi;
    reg adv = 0, pop_bad = 0;
    wire req_ready, hd_v, hd_bad, hd_we, hd_last, hd_2d, accepted, accepted_bad;
    wire [3:0] count; wire [1:0] hd_bank; wire [12:0] hd_row; wire [9:0] hd_col, hd_plen; wire [7:0] hd_tag;
    tau_sdram_sv_pq #(.DEPTH(4), .EN_2D(1), .BANK_HASH(2), .MAXG_RD(64), .MAXG_WR(32)) dut (.*);
    integer fail = 0;
    task chk(input c, input [255:0] m); begin if (!c) begin $display("FAIL t=%0t: %0s", $time, m); fail = fail + 1; end end endtask
    task push(input we, input [24:0] a, input [9:0] l, input [7:0] t);
        begin req_valid = 1; req_we = we; req_addr = a; req_len = l; req_tag = t; req_2d = 0;
              @(posedge clk); #1; req_valid = 0; end
    endtask
    task piece(input [9:0] len, input last, input [9:0] col);
        begin settle; chk(hd_v && hd_plen == len && hd_last == last && hd_col == col, "piece"); 
              if (!(hd_v && hd_plen == len && hd_last == last && hd_col == col))
                  $display("   got v=%b plen=%0d last=%b col=%0d, want len=%0d last=%b col=%0d", hd_v, hd_plen, hd_last, hd_col, len, last, col);
              adv = 1; @(posedge clk); #1; adv = 0; end
    endtask
    task settle; begin @(posedge clk); #1; end endtask   // the head piece is registered: one cycle behind the entry
    integer i;
    initial begin
        repeat (3) @(posedge clk); #1; rst = 0; @(posedge clk); #1;
        chk(req_ready, "ready after reset");
        // read of 200 words from col 1000: pieces 24 (page end), then 64, 64, 48
        push(0, 25'h0003E8, 10'd200, 8'd1);
        piece(24, 0, 1000); piece(64, 0, 0); piece(64, 0, 64); piece(48, 1, 128);
        chk(count == 0, "empty after the last piece");
        // write of 100 words (max 32): 32 32 32 4
        push(1, 25'h0000400, 10'd100, 8'd2);
        piece(32, 0, 0); piece(32, 0, 32); piece(32, 0, 64); piece(4, 1, 96);
        // length 512 read at column 0: 8 pieces of 64
        push(0, 25'h0000000, 10'd512, 8'd3);
        for (i = 0; i < 7; i = i + 1) piece(64, 0, i * 64);
        piece(64, 1, 448);
        // bad lengths
        push(0, 25'h0, 10'd0, 8'd4); #1; settle; chk(hd_bad && !hd_v, "len 0 bad");
        pop_bad = 1; @(posedge clk); #1; pop_bad = 0; chk(count == 0, "bad retired");
        push(0, 25'h0, 10'd513, 8'd5); #1; settle; chk(hd_bad, "len 513 bad");
        pop_bad = 1; @(posedge clk); #1; pop_bad = 0;
        push(0, 25'h0, 10'd1023, 8'd6); #1; settle; chk(hd_bad, "len 1023 bad");
        pop_bad = 1; @(posedge clk); #1; pop_bad = 0;
        // 2D: 10 words x 3 rows, stride 400, start col 1020: row 0 splits 4 + 6, rows 1 and 2 whole
        req_valid = 1; req_we = 0; req_addr = 25'd1020; req_2d = 1; req_w = 6'd10; req_h = 6'd3; req_stride = 11'd400; req_tag = 8'd7;
        @(posedge clk); #1; req_valid = 0; req_2d = 0;
        piece(4, 0, 1020); piece(6, 0, 0); piece(10, 0, (1020 + 400) & 1023); piece(10, 1, (1020 + 800) & 1023);
        // 2D rejects: 0 width, 0 height, area 513, 2D write
        req_valid = 1; req_2d = 1; req_w = 6'd0; req_h = 6'd3; @(posedge clk); #1; req_valid = 0; settle; chk(hd_bad, "w=0"); pop_bad = 1; @(posedge clk); #1; pop_bad = 0;
        req_valid = 1; req_2d = 1; req_w = 6'd4; req_h = 6'd0; @(posedge clk); #1; req_valid = 0; settle; chk(hd_bad, "h=0"); pop_bad = 1; @(posedge clk); #1; pop_bad = 0;
        req_valid = 1; req_2d = 1; req_w = 6'd33; req_h = 6'd16; @(posedge clk); #1; req_valid = 0; settle; chk(hd_bad, "area 528"); pop_bad = 1; @(posedge clk); #1; pop_bad = 0;
        req_valid = 1; req_2d = 1; req_addr = 0; req_w = 6'd32; req_h = 6'd16; req_stride = 11'd1024; @(posedge clk); #1; req_valid = 0; settle; chk(hd_v && !hd_bad, "area 512 ok");
        for (i = 0; i < 16; i = i + 1) piece(32, i == 15, 0);
        req_valid = 1; req_2d = 1; req_we = 1; req_w = 6'd4; req_h = 6'd2; @(posedge clk); #1; req_valid = 0; req_2d = 0; settle; chk(hd_bad, "2D write rejected"); pop_bad = 1; @(posedge clk); #1; pop_bad = 0;
        // fill the queue: ready drops after 4 entries
        for (i = 0; i < 4; i = i + 1) push(0, i * 16, 10'd8, i);
        chk(!req_ready && count == 4, "queue full, ready low");
        req_valid = 1; @(posedge clk); #1; req_valid = 0; chk(count == 4, "no push while not ready");
        piece(8, 1, 0); chk(req_ready, "ready after a pop");
        if (fail == 0) $display("TEST queue PASS"); else $display("TEST queue FAIL (%0d)", fail);
        $finish;
    end
endmodule
