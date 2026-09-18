// ============================================================================
// Verify a 60 MHz-style requester crossing to a 100 MHz-style SDRAM port.
// The fake port accepts one request at a time, emits sdram_fb's deliberately
// early read-available notification with stale zero data, then presents the
// actual halfword on the next qualifying cycle. Sampling the first notification
// reproduces the A-066 Pocket all-zero readback failure.
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module tb_tau_sdram_cpu_bridge;
    reg clk_sys = 0, clk_sdram = 0, rst_sys = 1, rst_sdram = 1;
    always #8.333 clk_sys = ~clk_sys;
    always #5     clk_sdram = ~clk_sdram;

    reg start = 0, wb_start = 0, write_op = 0;
    reg [24:0] addr = 0;
    reg [31:0] wdata = 0;
    reg [3:0] be = 0;
    wire busy, done;
    wire [31:0] rdata;
    wire [24:0] m_addr;
    wire [15:0] m_data;
    wire [1:0] m_be;
    wire [10:0] m_len;
    wire m_wr, m_rd, m_end;
    reg [15:0] m_q = 0;
    wire m_accepted;
    reg m_ready = 0, m_data_avail = 0;
    wire debug_wb_write_seen, debug_op_write, debug_wdata_all_ones;
    wire debug_be_all_enabled, debug_lo_write_accepted, debug_hi_write_accepted;
    wire debug_follow_read_seen, debug_follow_read_data_zero, debug_follow_read_data_all_ones;

    tau_sdram_cpu_bridge dut (
        .clk_sys(clk_sys), .rst_sys(rst_sys), .sys_start(start), .sys_wb_start(wb_start),
        .sys_write(write_op),
        .sys_addr(addr), .sys_wdata(wdata), .sys_byte_en(be), .sys_busy(busy),
        .sys_done(done), .sys_rdata(rdata),
        .debug_wb_write_seen(debug_wb_write_seen), .debug_op_write(debug_op_write),
        .debug_wdata_all_ones(debug_wdata_all_ones), .debug_be_all_enabled(debug_be_all_enabled),
        .debug_lo_write_accepted(debug_lo_write_accepted),
        .debug_hi_write_accepted(debug_hi_write_accepted),
        .debug_follow_read_seen(debug_follow_read_seen),
        .debug_follow_read_data_zero(debug_follow_read_data_zero),
        .debug_follow_read_data_all_ones(debug_follow_read_data_all_ones),
        .clk_sdram(clk_sdram), .rst_sdram(rst_sdram),
        .m_addr(m_addr), .m_data(m_data), .m_byte_en(m_be), .m_wr_len(m_len),
        .m_wr_req(m_wr), .m_rd_req(m_rd), .m_end_burst_req(m_end), .m_q(m_q),
        .m_accepted(m_accepted), .m_ready(m_ready), .m_data_available(m_data_avail)
    );

    reg port_busy = 0, port_read = 0, got_end = 0;
    integer delay = 0, write_count = 0, read_count = 0;
    reg [24:0] write_addr [0:3];
    reg [15:0] write_data [0:3];
    reg [1:0] write_be [0:3];
    assign m_accepted = !port_busy && (m_wr || m_rd);

    always @(posedge clk_sdram) begin
        m_ready <= 0;
        m_data_avail <= 0;
        if (!port_busy && (m_wr || m_rd)) begin
            port_busy <= 1;
            port_read <= m_rd;
            delay <= 0;
            got_end <= 0;
            if (m_wr) begin
                write_addr[write_count] <= m_addr;
                write_data[write_count] <= m_data;
                write_be[write_count] <= m_be;
                write_count <= write_count + 1;
            end else begin
                read_count <= read_count + 1;
            end
        end else if (port_busy) begin
            delay <= delay + 1;
            // Matches sdram_fb's documented client contract: data-available
            // leads READ_OUTPUT so a synchronous consumer captures this word
            // and asserts end-burst for the following controller edge.
            if (port_read && delay == 1) begin
                m_q <= (m_addr == 25'h100) ? 16'hBEEF :
                       ((m_addr[24:1] == 24'h22) ? 16'hFFFF : 16'hCAFE);
                m_data_avail <= 1;
            end
            if (port_read && m_end) got_end <= 1;
            if ((!port_read && delay == 3) || (port_read && got_end && delay >= 3)) begin
                port_busy <= 0;
                m_ready <= 1;
            end
        end
    end

    integer errors = 0;
    task chk(input cond, input [511:0] what);
        begin
            if (!cond) begin $display("FAIL: %0s", what); errors = errors + 1; end
            else       $display("ok:   %0s", what);
        end
    endtask

    task issue_write;
        begin
            @(posedge clk_sys);
            write_op <= 1; addr <= 25'h40; wdata <= 32'h11223344; be <= 4'b1101; start <= 1; wb_start <= 1;
            @(posedge clk_sys); start <= 0; wb_start <= 0;
            wait (busy);
            wait (done);
            @(posedge clk_sys);
        end
    endtask
    task issue_all_ones_write;
        begin
            @(posedge clk_sys);
            write_op <= 1; addr <= 25'h44; wdata <= 32'hFFFFFFFF; be <= 4'b1111;
            start <= 1; wb_start <= 1;
            @(posedge clk_sys); start <= 0; wb_start <= 0;
            wait (busy);
            wait (done);
            @(posedge clk_sys);
        end
    endtask
    task issue_read;
        begin
            @(posedge clk_sys);
            write_op <= 0; addr <= 25'h100; wdata <= 0; be <= 4'hF; start <= 1; wb_start <= 1;
            @(posedge clk_sys); start <= 0; wb_start <= 0;
            wait (busy);
            wait (done);
            @(posedge clk_sys);
        end
    endtask

    task issue_follow_read;
        begin
            @(posedge clk_sys);
            write_op <= 0; addr <= 25'h44; wdata <= 0; be <= 4'hF; start <= 1; wb_start <= 1;
            @(posedge clk_sys); start <= 0; wb_start <= 0;
            wait (busy);
            wait (done);
            @(posedge clk_sys);
        end
    endtask

    integer reads_before_reset;
    initial begin
        // Reset deassertion is intentionally skewed: clk_sys comes out first.
        // The bridge must neither create a phantom request nor lose its first
        // real request when the SDRAM side starts later.
        repeat (5) @(posedge clk_sys);
        rst_sys = 0;
        repeat (5) @(posedge clk_sdram);
        rst_sdram = 0;
        repeat (2) @(posedge clk_sys);
        chk(!busy && !done && read_count == 0 && write_count == 0,
            "sys-first reset release creates no phantom transaction");
        issue_write();
        chk(write_count == 2, "32-bit write issues two bounded controller operations");
        chk(write_addr[0] == 25'h40 && write_data[0] == 16'h3344 && write_be[0] == 2'b01,
            "write lower half uses lower address, data, and byte enables");
        chk(write_addr[1] == 25'h41 && write_data[1] == 16'h1122 && write_be[1] == 2'b11,
            "write upper half uses next address, data, and byte enables");
        chk(!debug_wb_write_seen,
            "non-all-ones write does not arm the A-065 diagnostic provenance");
        issue_all_ones_write();
        chk(write_count == 4, "all-ones write issues two bounded controller operations");
        chk(debug_wb_write_seen && debug_op_write && debug_lo_write_accepted &&
            debug_hi_write_accepted && debug_wdata_all_ones && debug_be_all_enabled,
            "all-ones mapped-write provenance reaches both controller halfwords");
        issue_follow_read();
        chk(read_count == 2, "32-bit follow read issues two bounded controller operations");
        chk(rdata == 32'hFFFFFFFF, "follow read combines the all-ones halfwords");
        chk(debug_follow_read_seen && !debug_follow_read_data_zero &&
            debug_follow_read_data_all_ones,
            "bridge retains the assembled follow-read response provenance");

        // Repeat with the opposite skew. Both toggle comparators reset to zero,
        // but the proof is behavioural: no completion arrives before a request,
        // and the next request still crosses and completes normally.
        reads_before_reset = read_count;
        @(posedge clk_sys); rst_sys = 1;
        @(posedge clk_sdram); rst_sdram = 1;
        repeat (5) @(posedge clk_sdram);
        rst_sdram = 0;
        repeat (5) @(posedge clk_sys);
        rst_sys = 0;
        repeat (2) @(posedge clk_sys);
        chk(!busy && !done && read_count == reads_before_reset,
            "sdram-first reset release creates no phantom transaction");
        issue_read();
        chk(read_count == reads_before_reset + 2,
            "first post-reset request crosses after sdram-first release");
        chk(rdata == 32'hCAFEBEEF,
            "post-reset read response remains correctly assembled");
        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end
    initial begin
        #100000;
        $display("TIMEOUT");
        $finish;
    end
endmodule

`default_nettype wire
