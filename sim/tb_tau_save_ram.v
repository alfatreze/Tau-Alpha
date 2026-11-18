// tb_tau_save_ram.v -- the meter-settings save RAM (src/fpga/core/tau_save_ram.sv) against the bus cycles it will see on a Pocket.
//   BRIDGE side, modelled on src/fpga/apf/io_bridge_peripheral.v: a write is the address and data held, then a one-cycle pmp_wr; a read is the address
//   held, the data SAMPLED four clk_74a edges after the address changed (ST_READ_0, read_cnt == 3), then a one-cycle pmp_rd.
//   CPU side, modelled on tau_soc.v's MMIO: a request pulse at cycle t, the bus samples the read data in cycle t+1, the next request can come at t+2;
//   a write is registered and arrives at the RAM one cycle after the request.
// Phases: (1) APF LOADs the file (4-byte words, big-endian on the bus) with junk writes around it (other regions, past the end) that must change nothing;
// (2) the CPU reads it back at full bus speed and checks the little-endian view; (3) the CPU writes changes through (seek, auto-increment, back to back);
// (4) the CPU is held in reset and APF FLUSHes: every word read at the shell's timing must equal the model of the file; (5) the CPU and the bridge work on
// different addresses at the same time; (6) the status word. BUG selects a mutant of the RAM (the bench must FAIL). SYSP / BRP are the clock periods in ps.
`timescale 1ps/1ps
`default_nettype none
module tb_tau_save_ram;
    parameter BUG  = 0;
    parameter SYSP = 15000;
    parameter BRP  = 13468;
    parameter AW   = 11;
    parameter WORDS = 1 << AW;
    reg clk_sys = 0, clk_74a = 0;
    always #(SYSP/2) clk_sys = ~clk_sys;
    always #(BRP/2)  clk_74a = ~clk_74a;

    reg  [31:0] bridge_addr = 0, bridge_wr_data = 0; reg bridge_wr = 0;
    wire        bridge_hit; wire [31:0] bridge_rdata;
    reg         rst_sys = 1, idx_we = 0, data_we = 0, data_req = 0; reg [31:0] idx_wd = 0, data_wd = 0;
    wire [31:0] idx_rd, data_rd, status;
    tau_save_ram #(.AW(AW), .BUG(BUG)) dut (.clk_74a(clk_74a), .bridge_addr(bridge_addr), .bridge_wr(bridge_wr), .bridge_wr_data(bridge_wr_data), .bridge_hit(bridge_hit), .bridge_rdata(bridge_rdata),
        .clk_sys(clk_sys), .rst_sys(rst_sys), .idx_we(idx_we), .idx_wd(idx_wd), .data_we(data_we), .data_wd(data_wd), .data_req(data_req), .idx_rd(idx_rd), .data_rd(data_rd), .status(status));

    // ---- the model of the FILE (bytes), and of what the bridge must see ----
    reg [7:0] file [0:WORDS*4-1];
    integer errors = 0, i, k;
    reg [31:0] lf = 32'hACE1_2345;
    function [31:0] rnd(input integer dummy); begin lf = {lf[30:0], lf[31] ^ lf[21] ^ lf[1] ^ lf[0]}; lf = lf * 32'd1664525 + 32'd1013904223; rnd = lf; end endfunction
    function [31:0] be_word(input integer w); be_word = {file[4*w], file[4*w+1], file[4*w+2], file[4*w+3]}; endfunction   // the bus word: the first file byte in [31:24]
    function [31:0] le_word(input integer w); le_word = {file[4*w+3], file[4*w+2], file[4*w+1], file[4*w]}; endfunction   // what a little-endian CPU must read
    task set_be(input integer w, input [31:0] v); begin {file[4*w], file[4*w+1], file[4*w+2], file[4*w+3]} = v; end endtask
    task fail(input [8*60-1:0] what, input [31:0] got, input [31:0] want); begin errors = errors + 1; if (errors < 12) $display("FAIL %0s: got %08x want %08x (t=%0t)", what, got, want, $time); end endtask

    // ---- bridge BFM ----
    task b_write(input [31:0] a, input [31:0] d);        // the shell: address and data valid, then one pmp_wr cycle
        begin @(posedge clk_74a); bridge_addr <= a; bridge_wr_data <= d; repeat (2) @(posedge clk_74a); bridge_wr <= 1; @(posedge clk_74a); bridge_wr <= 0; repeat ($urandom_range(0, 6)) @(posedge clk_74a); end
    endtask
    reg [31:0] b_got;
    task b_read(input [31:0] a);                          // the shell: address changes at edge 0, the data is taken at edge 4 (the value before that edge)
        begin @(posedge clk_74a); bridge_addr <= a; repeat (4) @(posedge clk_74a); b_got = bridge_rdata; repeat ($urandom_range(0, 4)) @(posedge clk_74a); end
    endtask
    // ---- CPU BFM ----
    reg [31:0] c_got;
    task c_idx(input [31:0] v);  begin @(posedge clk_sys); idx_we <= 1; idx_wd <= v; @(posedge clk_sys); idx_we <= 0; end endtask      // request at t, the RAM sees the registered pulse at t+1
    task c_wr(input [31:0] v);   begin @(posedge clk_sys); data_we <= 1; data_wd <= v; @(posedge clk_sys); data_we <= 0; end endtask
    task c_rd;                    begin @(posedge clk_sys); data_req <= 1; @(posedge clk_sys); data_req <= 0; @(negedge clk_sys); c_got = data_rd; @(posedge clk_sys); end endtask    // request at t, sampled in the middle of t+1 (the bus takes it at the end of t+1)
    // back to back: a request every other cycle (the fastest the bus can ask)
    task c_rd_fast(output [31:0] v); begin data_req <= 1; @(posedge clk_sys); data_req <= 0; @(negedge clk_sys); v = data_rd; @(posedge clk_sys); end endtask
    task c_wr_fast(input [31:0] x); begin data_we <= 1; data_wd <= x; @(posedge clk_sys); data_we <= 0; @(posedge clk_sys); end endtask

    integer w, kk, ib, ic; reg [31:0] v, vc;
    initial begin
        for (i = 0; i < WORDS*4; i = i + 1) file[i] = 8'h00;
        repeat (6) @(posedge clk_sys); repeat (6) @(posedge clk_74a);
        // ---------------- (1) APF LOADs the file, the CPU is held in reset ----------------
        for (i = 0; i < WORDS; i = i + 1) begin
            v = rnd(0); set_be(i, v); b_write(32'h3000_0000 + 4*i, v);
            if (i % 97 == 0) begin b_write(32'h2000_0000 + 4*(i % 32), ~v); b_write(32'h0000_0000 + 4*i, ~v); b_write(32'h3000_0000 + 4*(WORDS + i % 5), ~v); b_write(32'h3001_0000, ~v); b_write(32'hF800_2000, ~v); end   // junk: other regions and past the end
        end
        // ---------------- (2) the CPU reads it back, full speed, the little-endian view ----------------
        @(posedge clk_sys); rst_sys <= 0; repeat (4) @(posedge clk_sys);
        c_idx(0); repeat (3) @(posedge clk_sys);
        for (i = 0; i < WORDS; i = i + 1) begin c_rd_fast(v); if (v !== le_word(i)) fail("CPU read after LOAD", v, le_word(i)); end
        repeat (3) @(posedge clk_sys); if (idx_rd !== 0) fail("index wraps after a full sweep", idx_rd, 0);
        c_idx(5); @(posedge clk_sys); @(posedge clk_sys); c_rd; if (c_got !== le_word(5)) fail("CPU read at a seek", c_got, le_word(5));
        c_rd; if (c_got !== le_word(6)) fail("CPU read after auto-increment", c_got, le_word(6));
        // ---------------- (3) the CPU writes changes through ----------------
        c_idx(0); repeat (2) @(posedge clk_sys);                            // a sparse record region: a run at the start, some single words, the last word
        for (i = 0; i < 40; i = i + 1) begin v = rnd(0); k = i; {file[4*k+3], file[4*k+2], file[4*k+1], file[4*k]} = v; c_wr_fast(v); end
        for (kk = 0; kk < 60; kk = kk + 1) begin w = rnd(0) % WORDS; v = rnd(0); c_idx(w); {file[4*w+3], file[4*w+2], file[4*w+1], file[4*w]} = v; c_wr(v); end
        c_idx(WORDS - 1); v = 32'hDEAD_BEEF; {file[4*(WORDS-1)+3], file[4*(WORDS-1)+2], file[4*(WORDS-1)+1], file[4*(WORDS-1)]} = v; c_wr(v);
        for (kk = 0; kk < 40; kk = kk + 1) begin w = rnd(0) % WORDS; c_idx(w); repeat (2) @(posedge clk_sys); c_rd; if (c_got !== le_word(w)) fail("CPU read-back after write", c_got, le_word(w)); end
        // ---------------- (5) CPU and bridge at once, different addresses ----------------
        fork
            begin for (ib = 0; ib < 300; ib = ib + 1) begin b_read(32'h3000_0000 + 4*ib); if (b_got !== be_word(ib)) fail("bridge read while the CPU writes elsewhere", b_got, be_word(ib)); end end
            begin c_idx(1500); for (ic = 0; ic < 200; ic = ic + 1) begin vc = rnd(0); {file[4*(1500+ic)+3], file[4*(1500+ic)+2], file[4*(1500+ic)+1], file[4*(1500+ic)]} = vc; c_wr_fast(vc); end end
        join
        // ---------------- (4) Quit: the CPU is held in reset, APF reads everything back ----------------
        @(posedge clk_sys); rst_sys <= 1; repeat (4) @(posedge clk_sys);
        for (i = 0; i < WORDS; i = i + 1) begin b_read(32'h3000_0000 + 4*i); if (b_got !== be_word(i)) fail("FLUSH word", b_got, be_word(i)); if (!bridge_hit) fail("FLUSH hit", bridge_hit, 1); end
        b_read(32'h3000_0000 + 4*WORDS); if (b_got !== 0) fail("read past the end is zero", b_got, 0);
        b_read(32'h2000_0000);           if (b_got !== 0 || bridge_hit) fail("another region is not this RAM's", b_got, 0);
        b_read(32'h0000_0010);           if (b_got !== 0 || bridge_hit) fail("the firmware region is not this RAM's", b_got, 0);
        // the whole file, as bytes, in order: the Mac sees exactly this
        for (i = 0; i < WORDS; i = i + 1) begin b_read(32'h3000_0000 + 4*i); if (b_got[31:24] !== file[4*i] || b_got[23:16] !== file[4*i+1] || b_got[15:8] !== file[4*i+2] || b_got[7:0] !== file[4*i+3]) fail("file byte order", b_got, be_word(i)); end
        // ---------------- (6) status ----------------
        repeat (8) @(posedge clk_sys);
        if (status[31] !== 1'b1) fail("status present", status, 32'h8000_0000);
        if (status[30] !== 1'b1) fail("status: the bridge has written", status, 32'hC000_0000);
        if (status[28:24] !== AW) fail("status AW", status[28:24], AW);
        if (status[15:0] !== WORDS[15:0]) fail("status: the count of bridge words written is the in-range writes only", status[15:0], WORDS[15:0]);
        if (errors == 0) $display("PASSED: tb_tau_save_ram (BUG %0d, SYSP %0d, BRP %0d)", BUG, SYSP, BRP); else $display("FAILED: %0d errors", errors);
        $finish;
    end
    initial begin #20_000_000_000; $display("FAIL: timeout"); $finish; end
endmodule
