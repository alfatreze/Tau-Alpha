// ============================================================================
// Proves tau_main_ram.sv's two properties:
// 1. WORDS_B=0 (every build today) behaves exactly like the original inline
//    array: one cycle of write latency, registered read one cycle later,
//    independent byte lanes.
// 2. WORDS_B>0 (the opt-in 192 KB RAM-shrink path, not yet enabled anywhere)
//    is a real two-region memory: region A and region B are independently
//    addressable and independently writable, including right at the seam
//    (the last word of A, the first word of B) and at both ends of B, with
//    NO corruption crossing between them -- the actual risk a subtly wrong
//    region-select bit would create.
//
// A synthesis-only Quartus check (M10K/MLAB inference for both regions, not
// a LUT-fallback repeat of the original 48 KB failure) is a SEPARATE, still-
// required step this testbench cannot answer -- see tau_main_ram.sv's own
// header comment.
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module tb_tau_main_ram;
    reg clk = 0;
    always #5 clk = ~clk;

    integer errors = 0;
    task chk(input cond, input [1023:0] what);
        begin
            if (!cond) begin $display("FAIL: %0s", what); errors = errors + 1; end
            else       $display("ok:   %0s", what);
        end
    endtask

    // ---------------- DUT #1: WORDS_B=0, the shape every shipped build uses today
    reg  [15:0] addr; reg [31:0] wdata; reg [3:0] be; reg we; wire [31:0] rdata;
    tau_main_ram #(.WORDS_A(65536), .WORDS_B(0), .AW(16)) dut_single (
        .clk(clk), .addr(addr), .wdata(wdata), .be(be), .we(we), .rdata(rdata)
    );

    // ---------------- DUT #2: WORDS_A=32768, WORDS_B=16384 (192 KB / 4 lanes) --
    // the actual proposed RAM-shrink configuration (docs/PHASE_F_SPEC.md section 4).
    reg  [15:0] addr2; reg [31:0] wdata2; reg [3:0] be2; reg we2; wire [31:0] rdata2;
    tau_main_ram #(.WORDS_A(32768), .WORDS_B(16384), .AW(16)) dut_split (
        .clk(clk), .addr(addr2), .wdata(wdata2), .be(be2), .we(we2), .rdata(rdata2)
    );

    // ---------------- DUT #3: same split config, but with the region-select bug
    // forced on -- must produce a wrong answer, proving the check above actually
    // exercises the selector rather than trivially passing regardless.
    reg  [15:0] addr3; reg [31:0] wdata3; reg [3:0] be3; reg we3; wire [31:0] rdata3;
    tau_main_ram #(.WORDS_A(32768), .WORDS_B(16384), .AW(16), .BUG_FORCE_SEL_A(1)) dut_bug (
        .clk(clk), .addr(addr3), .wdata(wdata3), .be(be3), .we(we3), .rdata(rdata3)
    );

    task do_write(input [31:0] a, input [31:0] d, input [3:0] b);
        begin
            addr = a; wdata = d; be = b; we = 1;
            @(posedge clk); #1;
            we = 0;
        end
    endtask

    task do_write3(input [31:0] a, input [31:0] d, input [3:0] b);
        begin
            addr3 = a; wdata3 = d; be3 = b; we3 = 1;
            @(posedge clk); #1;
            we3 = 0;
        end
    endtask

    task do_write2(input [31:0] a, input [31:0] d, input [3:0] b);
        begin
            addr2 = a; wdata2 = d; be2 = b; we2 = 1;
            @(posedge clk); #1;
            we2 = 0;
        end
    endtask

    initial begin
        we = 0; we2 = 0; we3 = 0;
        @(posedge clk);

        // ---- DUT #1: single-region regression -------------------------------
        do_write(16'd0,      32'hAABBCCDD, 4'b1111);
        do_write(16'd1,      32'h11223344, 4'b1111);
        do_write(16'd65535,  32'h99887766, 4'b1111);
        do_write(16'd5,      32'hFFFFFFFF, 4'b1111);
        do_write(16'd5,      32'h000000AA, 4'b0001);  // byte-enable: only lane 0 changes

        addr = 16'd0; @(posedge clk); #1;
        chk(rdata == 32'hAABBCCDD, "single: word 0 reads back what was written");
        addr = 16'd1; @(posedge clk); #1;
        chk(rdata == 32'h11223344, "single: word 1 independent of word 0");
        addr = 16'd65535; @(posedge clk); #1;
        chk(rdata == 32'h99887766, "single: last word (65535) reachable and correct");
        addr = 16'd5; @(posedge clk); #1;
        chk(rdata == 32'hFFFFFFAA, "single: partial byte-enable only touches lane 0");

        // ---- DUT #2: split-region correctness --------------------------------
        // Region A: words 0, 1, and the LAST word of A (32767 -- the seam).
        // last word of A
        do_write2(32768 - 1, 32'hA000A000, 4'b1111);
        // first word of B (right after the seam)
        do_write2(32768,     32'hB111B111, 4'b1111);
        // a low word of A
        do_write2(0,         32'hA0000000, 4'b1111);
        // a low word of B
        do_write2(32768 + 1, 32'hB0000001, 4'b1111);
        // the LAST word of B (49151 -- the far end of the whole 192 KB/lane range)
        do_write2(32768 + 16384 - 1, 32'hBFFFFFFF, 4'b1111);
        // partial byte-enable inside region B
        do_write2(32768 + 5, 32'hFFFFFFFF, 4'b1111);
        do_write2(32768 + 5, 32'h000000CC, 4'b0001);

        addr2 = 32768 - 1; @(posedge clk); #1;
        chk(rdata2 == 32'hA000A000, "split: last word of region A (32767) correct");
        addr2 = 32768; @(posedge clk); #1;
        chk(rdata2 == 32'hB111B111, "split: first word of region B (32768), right past the seam");
        addr2 = 0; @(posedge clk); #1;
        chk(rdata2 == 32'hA0000000, "split: region A's own word 0 undisturbed by region B writes");
        addr2 = 32768 + 1; @(posedge clk); #1;
        chk(rdata2 == 32'hB0000001, "split: region B word 1 independent of region B word 0");
        addr2 = 32768 + 16384 - 1; @(posedge clk); #1;
        chk(rdata2 == 32'hBFFFFFFF, "split: last word of the whole range (49151) reachable");
        addr2 = 32768 + 5; @(posedge clk); #1;
        chk(rdata2 == 32'hFFFFFFCC, "split: partial byte-enable inside region B touches only lane 0");

        // Re-check region A's seam word ONE MORE TIME after all the region-B
        // writes above -- the real risk being tested is A/B crosstalk, not
        // just "can each region be read once".
        addr2 = 32768 - 1; @(posedge clk); #1;
        chk(rdata2 == 32'hA000A000, "split: region A's seam word (32767) still undisturbed after heavy region-B traffic");

        // ---- DUT #3: region-select bug forced on -- must show real corruption,
        // not just "a nominal region-B address returns *something*". With
        // sel_b stuck at 0, address 32768 (binary 1000_0000_0000_0000) aliases
        // onto region A's own word 0 (idxA = addr[14:0] = 0) instead of a
        // separate region-B word -- so writing a second, DIFFERENT value
        // through the aliased "region B" address must corrupt the value
        // already sitting at region A's real word 0, which a correct
        // implementation (DUT #2 above) never does.
        do_write3(0,     32'hA0000000, 4'b1111);   // real region A word 0
        do_write3(32768, 32'hB1111111, 4'b1111);   // aliases onto the SAME cell under the bug
        addr3 = 0; @(posedge clk); #1;
        chk(rdata3 != 32'hA0000000,
            "mutant caught: BUG_FORCE_SEL_A aliases region B onto region A word 0, corrupting it (proves the check exercises sel_b)");

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
