// ============================================================================
// Prove Phase 1 SDRAM arbitration without an SDRAM device model:
// - a simultaneous framebuffer/CPU request selects framebuffer;
// - the owner remains locked until controller ready;
// - controller read data and completion are returned to that owner only;
// - CPU runs after the framebuffer completion, not before.
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module tb_tau_sdram_arbiter;
    reg clk = 0, rst = 1;
    always #5 clk = ~clk;

    reg [24:0] fb_addr = 0, cpu_addr = 0;
    reg [15:0] fb_data = 0, cpu_data = 0;
    reg [1:0] fb_be = 2'b11, cpu_be = 2'b11;
    reg [10:0] fb_len = 1, cpu_len = 1;
    reg fb_stream = 0, fb_wr = 0, fb_rd = 0, fb_end = 0;
    reg cpu_wr = 0, cpu_rd = 0, cpu_end = 0;
    wire [15:0] fb_q, cpu_q;
    wire fb_avail, fb_ready, fb_data_avail;
    wire cpu_avail, cpu_ready, cpu_data_avail, cpu_accepted;
    wire [10:0] fb_wsrc_addr;

    wire [24:0] p_addr;
    wire [15:0] p_data;
    wire [1:0] p_be;
    wire [10:0] p_len;
    wire p_stream, p_wr, p_rd, p_end;
    reg [15:0] p_q = 0;
    // sdram_fb deasserts available combinationally when p_wr/p_rd is present.
    // Start low to model that real controller behavior; a request must still
    // acquire arbiter ownership in this cycle.
    reg p_avail = 0, p_ready = 0, p_data_avail = 0;
    reg [10:0] p_wsrc_addr = 0;

    tau_sdram_arbiter dut (
        .clk(clk), .rst(rst),
        .fb_addr(fb_addr), .fb_data(fb_data), .fb_byte_en(fb_be), .fb_wr_len(fb_len),
        .fb_wr_stream(fb_stream), .fb_wr_req(fb_wr), .fb_rd_req(fb_rd), .fb_end_burst_req(fb_end),
        .fb_q(fb_q), .fb_available(fb_avail), .fb_ready(fb_ready), .fb_data_available(fb_data_avail),
        .fb_wsrc_q(16'hCAFE), .fb_wsrc_addr(fb_wsrc_addr),
        .cpu_addr(cpu_addr), .cpu_data(cpu_data), .cpu_byte_en(cpu_be), .cpu_wr_len(cpu_len),
        .cpu_wr_req(cpu_wr), .cpu_rd_req(cpu_rd), .cpu_end_burst_req(cpu_end),
        .cpu_q(cpu_q), .cpu_available(cpu_avail), .cpu_ready(cpu_ready),
        .cpu_data_available(cpu_data_avail), .cpu_accepted(cpu_accepted),
        .p0_addr(p_addr), .p0_data(p_data), .p0_byte_en(p_be), .p0_wr_len(p_len),
        .p0_wr_stream(p_stream), .p0_q(p_q), .p0_wr_req(p_wr), .p0_rd_req(p_rd),
        .p0_end_burst_req(p_end), .p0_available(p_avail), .p0_ready(p_ready),
        .p0_data_available(p_data_avail), .wsrc_addr(p_wsrc_addr), .wsrc_q()
    );

    integer errors = 0;
    task chk(input cond, input [511:0] what);
        begin
            if (!cond) begin $display("FAIL: %0s", what); errors = errors + 1; end
            else       $display("ok:   %0s", what);
        end
    endtask

    initial begin
        repeat (3) @(posedge clk);
        rst = 0;

        // Both masters request together while the real controller-style
        // p_available is already low because p_rd is asserted. Framebuffer
        // must still win and CPU must not see a false acceptance pulse.
        @(negedge clk);
        fb_addr = 25'h12; fb_rd = 1;
        cpu_addr = 25'h345; cpu_wr = 1; cpu_data = 16'hBEEF;
        #1;
        chk(p_rd && p_addr == 25'h12, "framebuffer wins simultaneous request");
        chk(!p_wr && !cpu_accepted, "CPU not accepted while framebuffer requests");
        @(posedge clk);
        @(negedge clk);
        fb_rd = 0; cpu_wr = 1;
        p_avail = 0; p_q = 16'h1234; p_data_avail = 1;
        #1;
        chk(fb_data_avail && !cpu_data_avail && fb_q == 16'h1234,
            "read data returns only to framebuffer owner");
        chk(!cpu_avail, "CPU remains blocked during framebuffer operation");

        // Finish the framebuffer operation.  The CPU request is deliberately
        // held waiting; it must not leak into the controller until next slot.
        @(posedge clk);
        @(negedge clk);
        p_data_avail = 0; p_ready = 1;
        #1;
        chk(fb_ready && !cpu_ready, "completion returns only to framebuffer owner");
        chk(!p_wr, "CPU request is not forwarded before owner release");
        @(posedge clk);
        @(negedge clk);
        p_ready = 0; p_avail = 1;
        #1;
        chk(cpu_avail && p_wr && p_addr == 25'h345 && p_data == 16'hBEEF,
            "waiting CPU write gets the next arbitration slot");
        chk(cpu_accepted, "CPU acceptance identifies the forwarded request");
        @(posedge clk);
        @(negedge clk);
        cpu_wr = 0; p_avail = 0;
        @(posedge clk);
        @(negedge clk);
        p_ready = 1;
        #1;
        chk(cpu_ready && !fb_ready, "CPU completion returns only to CPU owner");

        // Mirror of the first contention case: CPU owns first, then scanout
        // arrives. The accepted CPU operation must finish intact, but the
        // framebuffer must win the very next idle arbitration point.
        @(posedge clk);
        @(negedge clk);
        p_ready = 0; p_avail = 1;
        cpu_addr = 25'h456; cpu_data = 16'hFACE; cpu_wr = 1;
        #1;
        chk(p_wr && cpu_accepted, "CPU can own an uncontended operation");
        @(posedge clk);
        @(negedge clk);
        cpu_wr = 0; p_avail = 0;
        fb_addr = 25'h23; fb_rd = 1;
        #1;
        chk(!p_rd && p_addr == 25'h456 && !fb_avail,
            "framebuffer waits while an accepted CPU operation completes");
        @(posedge clk);
        @(negedge clk);
        p_ready = 1;
        #1;
        chk(cpu_ready && !fb_ready, "CPU retains completion ownership");
        @(posedge clk);
        @(negedge clk);
        p_ready = 0; p_avail = 1;
        #1;
        chk(p_rd && p_addr == 25'h23 && !cpu_accepted,
            "framebuffer wins the first idle slot after CPU completion");

        @(posedge clk);
        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end

    initial begin
        #10000;
        $display("TIMEOUT");
        $finish;
    end
endmodule

`default_nettype wire
