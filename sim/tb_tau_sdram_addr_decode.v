// Verify the Phase 2 map before enabling a mapped CPU transaction.
`timescale 1ns/1ps
`default_nettype none

module tb_tau_sdram_addr_decode;
    reg [29:0] a;
    wire bram_c, bram_u, mmio, sdram_c, sdram_u;
    wire [24:0] sdram_a;
    integer errors = 0;

    tau_sdram_addr_decode dut (
        .dadr(a), .bram_cached(bram_c), .bram_uncached(bram_u), .mmio(mmio),
        .sdram_cached(sdram_c), .sdram_uncached(sdram_u), .sdram_addr(sdram_a)
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

        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end
endmodule

`default_nettype wire
