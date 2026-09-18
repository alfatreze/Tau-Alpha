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
    wire breq, bwrite;
    wire [24:0] baddr;
    wire [31:0] bwdata;
    wire [3:0] bsel;
    reg baccept = 0, bdone = 0;
    reg [31:0] brdata = 0;
    integer starts = 0, errors = 0;

    tau_sdram_wb_adapter dut (
        .clk(clk), .rst(rst), .wb_cyc(cyc), .wb_stb(stb), .wb_we(we), .wb_cti(cti),
        .wb_sdram_addr(addr), .wb_wdata(wdata), .wb_sel(sel), .wb_rdata(rdata),
        .wb_ack(ack), .wb_unsupported(unsupported), .bridge_req(breq),
        .bridge_write(bwrite), .bridge_addr(baddr), .bridge_wdata(bwdata),
        .bridge_byte_en(bsel), .bridge_accept(baccept), .bridge_done(bdone), .bridge_rdata(brdata)
    );

    always @(posedge clk) if (breq && baccept) starts = starts + 1;
    task chk(input cond, input [511:0] what);
        begin
            if (!cond) begin $display("FAIL: %0s", what); errors = errors + 1; end
            else       $display("ok:   %0s", what);
        end
    endtask
    task finish_bridge(input [31:0] value);
        begin
            @(negedge clk); brdata = value;
            @(negedge clk); bdone = 1;
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
        chk(breq && !bwrite && baddr == 25'h0080000 && bsel == 4'hF,
            "classic read holds one translated bridge request until accepted");
        @(negedge clk); baccept = 1;
        @(negedge clk); baccept = 0;
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
        chk(breq && bwrite && baddr == 25'h0080010 && bwdata == 32'h11223344 && bsel == 4'b0101,
            "classic write preserves address, data, and all byte lanes");
        @(negedge clk); baccept = 1;
        @(negedge clk); baccept = 0;
        finish_bridge(32'd0);
        #1; chk(ack && starts == 2, "write completion produces one additional ACK only");
        @(negedge clk); cyc = 0; stb = 0;
        @(posedge clk);

        // A shared bridge can defer by withholding acceptance without dropping.
        @(negedge clk); cyc = 1; stb = 1; we = 0; addr = 25'h0080020;
        @(posedge clk); #1; chk(breq && starts == 2, "unaccepted bridge request remains held");
        @(negedge clk); baccept = 1;
        @(negedge clk); baccept = 0;
        @(posedge clk); #1; chk(!breq && starts == 3, "request advances only after explicit acceptance");
        finish_bridge(32'h01234567);
        @(negedge clk); cyc = 0; stb = 0;

        // Legal classic Wishbone traffic may leave CYC/STB asserted while
        // advancing directly to the next beat after ACK. The adapter needs a
        // one-cycle turnaround, not an indefinite wait for a low level.
        @(negedge clk); cyc = 1; stb = 1; we = 1; addr = 25'h0080040;
        wdata = 32'hA0A1A2A3; sel = 4'hF;
        @(negedge clk); baccept = 1;
        @(negedge clk); baccept = 0;
        finish_bridge(32'd0);
        #1; chk(ack && starts == 4, "first back-to-back beat acknowledges once");
        @(negedge clk); addr = 25'h0080044; wdata = 32'hB0B1B2B3;
        @(posedge clk); #1;
        chk(!ack && starts == 4 && (!breq ||
            (baddr == 25'h0080044 && bwdata == 32'hB0B1B2B3)),
            "turnaround suppresses stale-beat duplication");
        @(posedge clk); #1;
        chk(breq && baddr == 25'h0080044 && bwdata == 32'hB0B1B2B3,
            "held CYC/STB admits the next classic beat");
        @(negedge clk); baccept = 1;
        @(negedge clk); baccept = 0;
        finish_bridge(32'd0);
        #1; chk(ack && starts == 5, "second back-to-back beat acknowledges once");
        @(negedge clk); cyc = 0; stb = 0;
        @(posedge clk);

        // Cache/burst cycles are expressly not accepted by this Phase 2a path.
        @(negedge clk); cyc = 1; stb = 1; cti = 3'b010;
        @(posedge clk); #1; chk(unsupported && !breq && starts == 5,
            "incrementing burst is flagged and never reaches narrow bridge");
        @(negedge clk); cyc = 0; stb = 0; cti = 3'b000;
        @(posedge clk);

        // Reset during a wait clears state and suppresses an old completion.
        @(negedge clk); cyc = 1; stb = 1; addr = 25'h0080030;
        @(posedge clk); #1; chk(breq, "post-test request holds before reset");
        @(negedge clk); rst = 1; bdone = 1;
        @(posedge clk); #1; chk(!ack && !breq, "reset suppresses stale completion and bridge request");
        @(negedge clk); rst = 0; bdone = 0; cyc = 0; stb = 0;
        @(posedge clk); #1; chk(!ack && starts == 5, "reset leaves no duplicate request state");

        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end
endmodule

`default_nettype wire
