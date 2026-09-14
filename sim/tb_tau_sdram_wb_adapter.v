// Verify bounded, classic Wishbone adaptation independently of SDRAM timing.
`timescale 1ns/1ps
`default_nettype none

module tb_tau_sdram_wb_adapter;
    reg clk = 0, rst = 1;
    always #5 clk = ~clk;
    reg cyc = 0, stb = 0, we = 0;
    reg [2:0] cti = 3'b000;
    reg [24:0] addr = 0;
    reg [31:0] wdata = 0;
    reg [3:0] sel = 0;
    wire [31:0] rdata;
    wire ack, unsupported;
    wire start, bwrite;
    wire [24:0] baddr;
    wire [31:0] bwdata;
    wire [3:0] bsel;
    reg bbusy = 0, bdone = 0;
    reg [31:0] brdata = 0;
    integer starts = 0, errors = 0;

    tau_sdram_wb_adapter dut (
        .clk(clk), .rst(rst), .wb_cyc(cyc), .wb_stb(stb), .wb_we(we), .wb_cti(cti),
        .wb_sdram_addr(addr), .wb_wdata(wdata), .wb_sel(sel), .wb_rdata(rdata),
        .wb_ack(ack), .wb_unsupported(unsupported), .bridge_start(start),
        .bridge_write(bwrite), .bridge_addr(baddr), .bridge_wdata(bwdata),
        .bridge_byte_en(bsel), .bridge_busy(bbusy), .bridge_done(bdone), .bridge_rdata(brdata)
    );

    always @(posedge clk) if (start) starts = starts + 1;
    task chk(input cond, input [511:0] what);
        begin
            if (!cond) begin $display("FAIL: %0s", what); errors = errors + 1; end
            else       $display("ok:   %0s", what);
        end
    endtask
    task finish_bridge(input [31:0] value);
        begin
            @(negedge clk); bbusy = 1; brdata = value;
            @(negedge clk); bbusy = 0; bdone = 1;
            @(negedge clk); bdone = 0;
        end
    endtask

    initial begin
        repeat (3) @(posedge clk); rst = 0;

        // A held classic read launches once, preserves address, and returns
        // exactly one response/ACK even if the master waits a cycle to drop it.
        @(negedge clk);
        cyc = 1; stb = 1; we = 0; cti = 3'b000; addr = 25'h0080000; sel = 4'hF;
        @(posedge clk); #1;
        chk(start && !bwrite && baddr == 25'h0080000 && bsel == 4'hF,
            "classic read launches one bridge request with translated address");
        finish_bridge(32'hCAFEBEEF);
        #1; chk(ack && rdata == 32'hCAFEBEEF, "bridge read completion produces one ACK and data");
        @(posedge clk); #1;
        chk(!ack && starts == 1, "held request does not duplicate after ACK");
        @(negedge clk); cyc = 0; stb = 0;
        @(posedge clk);

        // Back-to-back classic write after release retains every lane/data bit.
        @(negedge clk);
        cyc = 1; stb = 1; we = 1; cti = 3'b000; addr = 25'h0080010;
        wdata = 32'h11223344; sel = 4'b0101;
        @(posedge clk); #1;
        chk(start && bwrite && baddr == 25'h0080010 && bwdata == 32'h11223344 && bsel == 4'b0101,
            "classic write preserves address, data, and all byte lanes");
        finish_bridge(32'd0);
        #1; chk(ack && starts == 2, "write completion produces one additional ACK only");
        @(negedge clk); cyc = 0; stb = 0;
        @(posedge clk);

        // A bridge already occupied by diagnostic traffic must defer, not drop.
        @(negedge clk); bbusy = 1; cyc = 1; stb = 1; we = 0; addr = 25'h0080020;
        @(posedge clk); #1; chk(!start && starts == 2, "busy bridge defers request without a false launch");
        @(negedge clk); bbusy = 0;
        @(posedge clk); #1; chk(start, "deferred request launches once bridge becomes idle");
        finish_bridge(32'h01234567);
        @(negedge clk); cyc = 0; stb = 0;

        // Cache/burst cycles are expressly not accepted by this Phase 2a path.
        @(negedge clk); cyc = 1; stb = 1; cti = 3'b010;
        @(posedge clk); #1; chk(unsupported && !start && starts == 3,
            "incrementing burst is flagged and never reaches narrow bridge");
        @(negedge clk); cyc = 0; stb = 0; cti = 3'b000;
        @(posedge clk);

        // Reset during a wait clears state and suppresses an old completion.
        @(negedge clk); cyc = 1; stb = 1; addr = 25'h0080030;
        @(posedge clk); #1; chk(start, "post-test request launches before reset");
        @(negedge clk); rst = 1; bdone = 1;
        @(posedge clk); #1; chk(!ack && !start, "reset suppresses stale completion and start pulse");
        @(negedge clk); rst = 0; bdone = 0; cyc = 0; stb = 0;
        @(posedge clk); #1; chk(!ack && starts == 4, "reset leaves no duplicate request state");

        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end
endmodule

`default_nettype wire
