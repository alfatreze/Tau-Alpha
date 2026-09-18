`timescale 1ns/1ps
`default_nettype none

// End-to-end RTL check for the Phase 2 uncached CPU path:
// address decoder -> held-request adapter -> shared bridge-owner mux ->
// stand-in existing bridge -> response/ACK back to the CPU-side adapter.
module tb_tau_sdram_phase2_path;
    reg clk = 0, rst = 1;
    always #5 clk = ~clk;

    reg [29:0] dadr = 0;
    wire bram_c, bram_u, mmio, sdram_c, sdram_u;
    wire [24:0] decoded_addr;
    tau_sdram_addr_decode decode (
        .dadr(dadr), .bram_cached(bram_c), .bram_uncached(bram_u), .mmio(mmio),
        .sdram_cached(sdram_c), .sdram_uncached(sdram_u), .sdram_addr(decoded_addr)
    );

    reg cyc = 0, stb = 0, we = 0;
    reg [2:0] cti = 0;
    reg [31:0] wdata = 0;
    reg [3:0] sel = 4'hF;
    wire [31:0] rdata;
    wire ack, unsupported;
    wire wb_req, wb_write, wb_accept, wb_done;
    wire [24:0] wb_addr;
    wire [31:0] wb_wdata, wb_rdata;
    wire [3:0] wb_be;
    tau_sdram_wb_adapter adapter (
        .clk(clk), .rst(rst), .wb_cyc(cyc & sdram_u), .wb_stb(stb), .wb_we(we),
        .wb_cti(cti), .wb_sdram_addr(decoded_addr), .wb_wdata(wdata), .wb_sel(sel),
        .wb_rdata(rdata), .wb_ack(ack), .wb_unsupported(unsupported),
        .bridge_req(wb_req), .bridge_write(wb_write), .bridge_addr(wb_addr),
        .bridge_wdata(wb_wdata), .bridge_byte_en(wb_be), .bridge_accept(wb_accept),
        .bridge_done(wb_done), .bridge_rdata(wb_rdata)
    );

    wire diag_done, bridge_start, bridge_write, bridge_busy, bridge_done;
    wire [31:0] diag_rdata, bridge_wdata, bridge_rdata;
    wire [24:0] bridge_addr;
    wire [3:0] bridge_be;
    reg model_busy = 0, model_done = 0, pending = 0;
    reg [2:0] delay_count = 0;
    reg [31:0] model_rdata = 32'h89ABCDEF;
    reg [31:0] captured_wdata = 0;
    reg [24:0] captured_addr = 0;
    reg [3:0] captured_be = 0;
    reg captured_write = 0;
    integer errors = 0;

    tau_sdram_bridge_mux mux (
        .clk(clk), .rst(rst),
        .diag_req(1'b0), .diag_write(1'b0), .diag_addr(25'd0),
        .diag_wdata(32'd0), .diag_be(4'd0),
        .wb_req(wb_req), .wb_write(wb_write), .wb_addr(wb_addr),
        .wb_wdata(wb_wdata), .wb_be(wb_be), .wb_accept(wb_accept),
        .diag_done(diag_done), .wb_done(wb_done), .diag_rdata(diag_rdata),
        .wb_rdata(wb_rdata), .busy(), .bridge_start(bridge_start),
        .bridge_write(bridge_write), .bridge_addr(bridge_addr),
        .bridge_wdata(bridge_wdata), .bridge_be(bridge_be),
        .bridge_busy(model_busy), .bridge_done(model_done), .bridge_rdata(model_rdata)
    );

    // Deterministic stand-in for the already-tested two-halfword bridge.
    always @(posedge clk) begin
        if (rst) begin
            model_busy <= 0;
            model_done <= 0;
            pending <= 0;
            delay_count <= 0;
            captured_wdata <= 0;
            captured_addr <= 0;
            captured_be <= 0;
            captured_write <= 0;
        end else begin
            model_done <= 0;
            if (bridge_start && !pending) begin
                captured_wdata <= bridge_wdata;
                captured_addr <= bridge_addr;
                captured_be <= bridge_be;
                captured_write <= bridge_write;
                pending <= 1;
                delay_count <= 3;
            end else if (pending) begin
                model_busy <= 1;
                if (delay_count == 0) begin
                    model_busy <= 0;
                    model_done <= 1;
                    pending <= 0;
                end else delay_count <= delay_count - 1'b1;
            end
        end
    end

    task check(input cond, input [511:0] message);
        begin
            if (!cond) begin $display("FAIL: %0s", message); errors = errors + 1; end
            else       $display("ok:   %0s", message);
        end
    endtask

    initial begin
        repeat (3) @(posedge clk);
        @(negedge clk); rst = 0;

        // BYTE 0xA0100000 is the uncached alias of physical SDRAM + 1 MiB.
        @(negedge clk);
        dadr = 30'h28040000;
        cyc = 1; stb = 1; we = 0; cti = 3'b000; sel = 4'hF;
        #1;
        check(sdram_u && decoded_addr == 25'h0080000,
              "uncached aperture decodes to physical SDRAM word address");
        wait (ack === 1'b1); #1;
        check(rdata == 32'h89ABCDEF && captured_addr == 25'h0080000,
              "read traverses mux and returns bridge data with one ACK");
        @(negedge clk); cyc = 0; stb = 0;
        repeat (2) @(posedge clk);

        // A write must preserve byte lanes and payload through every boundary.
        @(negedge clk);
        // dadr is a 32-bit CPU word address: +8 bytes is +2 words.
        dadr = 30'h28040002;
        cyc = 1; stb = 1; we = 1; cti = 3'b000;
        sel = 4'b0101; wdata = 32'h13579BDF;
        wait (ack === 1'b1); #1;
        check(captured_write && captured_addr == 25'h0080004 &&
              captured_wdata == 32'h13579BDF && captured_be == 4'b0101,
              "write payload, physical address, and byte enables are preserved");
        @(negedge clk); cyc = 0; stb = 0;
        repeat (2) @(posedge clk);

        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end

    initial begin
        #20000;
        $display("FAIL: timed out waiting for end-to-end SDRAM response");
        $finish;
    end
endmodule

`default_nettype wire
