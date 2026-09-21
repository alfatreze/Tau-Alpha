// Verify the Phase 2 map before enabling a mapped CPU transaction.
`timescale 1ns/1ps
`default_nettype none

module tb_tau_sdram_addr_decode;
    reg [29:0] a;
    wire bram_c, bram_u, mmio, sdram_c, sdram_u;
    wire [24:0] sdram_a;
    wire psram_u;
    wire [22:0] psram_w;
    integer errors = 0;

    tau_sdram_addr_decode dut (
        .dadr(a), .bram_cached(bram_c), .bram_uncached(bram_u), .mmio(mmio),
        .sdram_cached(sdram_c), .sdram_uncached(sdram_u), .sdram_addr(sdram_a),
        .psram_uncached(psram_u), .psram_word(psram_w)
    );

    task chk(input cond, input [511:0] what);
        begin
            if (!cond) begin $display("FAIL: %0s", what); errors = errors + 1; end
            else       $display("ok:   %0s", what);
        end
    endtask

    task one(input [29:0] value);
        begin #1; a = value; #1; end
    endtask

    initial begin
        one(30'h00000000);
        chk(bram_c && !bram_u && !mmio && !sdram_c && !sdram_u,
            "cached BRAM base is exclusive");
        one(30'h0000FFFF);
        chk(bram_c, "cached BRAM final word is included");
        one(30'h30000000);
        chk(bram_u && !bram_c && !mmio && !sdram_c && !sdram_u,
            "uncached BRAM alias is exclusive");
        one(30'h20000000);
        chk(mmio && !bram_c && !bram_u && !sdram_c && !sdram_u,
            "MMIO page base is exclusive");
        one(30'h200003FF);
        chk(mmio, "MMIO final word is included");
        one(30'h20000400);
        chk(!mmio && !bram_c && !bram_u && !sdram_c && !sdram_u,
            "address after narrow MMIO page is unmapped");

        one(30'h10000000);
        chk(!bram_c && !sdram_c && !sdram_u,
            "cached SDRAM framebuffer guard cannot alias BRAM");
        one(30'h10040000);
        chk(sdram_c && !bram_c && !bram_u && !mmio && !sdram_u && sdram_a == 25'h0080000,
            "cached SDRAM owned base maps to physical 1 MiB boundary");
        one(30'h10FFFFFF);
        chk(sdram_c && sdram_a == 25'h1FFFFFE,
            "cached SDRAM final CPU word maps inside controller range");
        one(30'h11000000);
        chk(!sdram_c && !bram_c, "cached SDRAM upper limit is exclusive");

        one(30'h28040000);
        chk(sdram_u && !bram_c && !bram_u && !mmio && !sdram_c && sdram_a == 25'h0080000,
            "uncached alias shares physical owned base");
        one(30'h28FFFFFF);
        chk(sdram_u && sdram_a == 25'h1FFFFFE,
            "uncached alias final word maps inside controller range");
        one(30'h29000000);
        chk(!sdram_u && !mmio, "uncached SDRAM upper limit is exclusive");

        // PSRAM window (B-016): 0xA400_0000..A5FF_FFFF, adjacent to the SDRAM window
        one(30'h29000000);
        chk(psram_u && !sdram_u && !sdram_c && !bram_c && !bram_u && !mmio && psram_w == 23'h000000,
            "PSRAM window base is exclusive and starts at word offset 0");
        one(30'h297FFFFF);
        chk(psram_u && psram_w == 23'h7FFFFF && !sdram_u && !mmio,
            "PSRAM window final word maps to the last CPU word (chip 1, die 1)");
        one(30'h29800000);
        chk(!psram_u && !sdram_u && !sdram_c && !bram_c && !bram_u && !mmio,
            "PSRAM upper limit is exclusive and unmapped");
        one(30'h28FFFFFF);
        chk(sdram_u && !psram_u, "last SDRAM window word is not PSRAM (no overlap)");
        one(30'h29200000);
        chk(psram_u && psram_w == 23'h200000, "die 1 of chip 0 starts at CPU-word offset 0x200000");
        one(30'h29400000);
        chk(psram_u && psram_w == 23'h400000, "chip 1 starts at CPU-word offset 0x400000");
        // no cached counterpart and no aliasing through other windows
        one(30'h11000000);
        chk(!psram_u, "cached SDRAM upper limit is not PSRAM");
        one(30'h09000000);
        chk(!psram_u && !bram_c && !sdram_c, "0x2400_0000 (would-be cached PSRAM) is unmapped");
        one(30'h29000000 | 30'h10000000);
        chk(!psram_u, "PSRAM offset with bit 28 set does not alias into the window");
        one(30'h20000000 | 30'h000000A0);
        chk(!psram_u && mmio, "MMIO page never matches the PSRAM window");

        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end
endmodule

`default_nettype wire
