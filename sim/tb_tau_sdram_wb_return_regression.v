// A-093 regression: production-timing Wishbone -> adapter -> mux -> CDC bridge
// -> arbiter -> functional memory, with an mp3_soc-style registered ACK
// (dACK <= adapter wb_ack) and a master that either keeps STB asserted into the
// next beat (gap 0) or idles (gap > 0).  Hardware (A-092) showed CPU reads
// returning the previous read's data and a load after a store returning stale
// data although the SDRAM contents were correct.  This test asserts the data
// AND that each Wishbone beat produces exactly one bridge request.
`timescale 1ns/1ps
`default_nettype none
module tb_tau_sdram_wb_return_regression;
    reg clk_sys = 0, clk_sdram = 0, rst_sys = 1, rst_sdram = 1;
    always #8.333 clk_sys = ~clk_sys;
    always #5 clk_sdram = ~clk_sdram;

    // ---- CPU-side Wishbone master + mp3_soc-style registered ACK ------------
    reg cyc = 0, stb = 0, we = 0;
    reg [24:0] addr = 0;
    reg [31:0] wdata = 0;
    wire [31:0] rdata;
    wire ack_pulse, unsupported;
    reg dACK = 0;
    always @(posedge clk_sys) dACK <= rst_sys ? 1'b0 : ack_pulse;

    wire wb_req, wb_write, wb_accept, wb_done;
    wire [24:0] wb_addr;
    wire [31:0] wb_wdata, wb_rdata;
    wire [3:0] wb_be;
    tau_sdram_wb_adapter adapter (
        .clk(clk_sys), .rst(rst_sys), .wb_cyc(cyc), .wb_stb(stb), .wb_we(we),
        .wb_cti(3'b000), .wb_sdram_addr(addr), .wb_wdata(wdata), .wb_sel(4'hf),
        .wb_rdata(rdata), .wb_ack(ack_pulse), .wb_unsupported(unsupported),
        .bridge_req(wb_req), .bridge_write(wb_write), .bridge_addr(wb_addr),
        .bridge_wdata(wb_wdata), .bridge_byte_en(wb_be), .bridge_accept(wb_accept),
        .bridge_done(wb_done), .bridge_rdata(wb_rdata)
    );

    wire bridge_start, bridge_wb_start, bridge_write, bridge_busy, bridge_done;
    wire [24:0] bridge_addr;
    wire [31:0] bridge_wdata, bridge_rdata;
    wire [3:0] bridge_be;
    tau_sdram_bridge_mux mux (
        .clk(clk_sys), .rst(rst_sys),
        .diag_req(1'b0), .diag_write(1'b0), .diag_addr(25'd0),
        .diag_wdata(32'd0), .diag_be(4'd0),
        .wb_req(wb_req), .wb_write(wb_write), .wb_addr(wb_addr),
        .wb_wdata(wb_wdata), .wb_be(wb_be), .wb_accept(wb_accept),
        .diag_done(), .wb_done(wb_done), .diag_rdata(), .wb_rdata(wb_rdata),
        .busy(), .bridge_start(bridge_start), .bridge_write(bridge_write),
        .bridge_addr(bridge_addr), .bridge_wdata(bridge_wdata), .bridge_be(bridge_be),
        .bridge_wb_start(bridge_wb_start),
        .bridge_busy(bridge_busy), .bridge_done(bridge_done), .bridge_rdata(bridge_rdata)
    );

    wire [24:0] cpu_addr;
    wire [15:0] cpu_data, cpu_q;
    wire [1:0] cpu_be;
    wire [10:0] cpu_len;
    wire cpu_wr, cpu_rd, cpu_end, cpu_accepted, cpu_ready, cpu_data_available;
    tau_sdram_cpu_bridge bridge (
        .clk_sys(clk_sys), .rst_sys(rst_sys), .sys_start(bridge_start), .sys_wb_start(bridge_wb_start),
        .sys_write(bridge_write), .sys_addr(bridge_addr), .sys_wdata(bridge_wdata),
        .sys_byte_en(bridge_be), .sys_busy(bridge_busy), .sys_done(bridge_done),
        .sys_rdata(bridge_rdata), .clk_sdram(clk_sdram), .rst_sdram(rst_sdram),
        .m_addr(cpu_addr), .m_data(cpu_data), .m_byte_en(cpu_be), .m_wr_len(cpu_len),
        .m_wr_req(cpu_wr), .m_rd_req(cpu_rd), .m_end_burst_req(cpu_end), .m_q(cpu_q),
        .m_accepted(cpu_accepted), .m_ready(cpu_ready), .m_data_available(cpu_data_available)
    );

    // ---- controller-contract memory model behind the arbiter ---------------
    reg fb_req = 0, fb_busy = 0, port_busy = 0, port_read = 0;
    reg [3:0] port_delay = 0;
    reg [15:0] mem [0:4095];
    reg [24:0] lat_addr = 0;
    wire [15:0] p0_q, p0_data, fb_q;
    wire [24:0] p0_addr;
    wire [1:0] p0_be;
    wire [10:0] p0_len;
    wire p0_wr, p0_rd, p0_end, p0_stream, p0_available, p0_ready, p0_data_available;
    wire fb_available, fb_ready, fb_data_available;
    assign p0_available = !port_busy;
    assign cpu_q = p0_q;
    assign p0_q = port_read ? mem[lat_addr[11:0]] : 16'h0000;

    tau_sdram_arbiter arbiter (
        .clk(clk_sdram), .rst(rst_sdram),
        .fb_addr(25'h20), .fb_data(16'hbeef), .fb_byte_en(2'b11), .fb_wr_len(11'd1),
        .fb_wr_stream(1'b0), .fb_wr_req(fb_req), .fb_rd_req(1'b0), .fb_end_burst_req(1'b0),
        .fb_q(fb_q), .fb_available(fb_available), .fb_ready(fb_ready),
        .fb_data_available(fb_data_available), .fb_wsrc_q(16'd0), .fb_wsrc_addr(),
        .cpu_addr(cpu_addr), .cpu_data(cpu_data), .cpu_byte_en(cpu_be), .cpu_wr_len(cpu_len),
        .cpu_wr_req(cpu_wr), .cpu_rd_req(cpu_rd), .cpu_end_burst_req(cpu_end), .cpu_q(cpu_q),
        .cpu_available(), .cpu_ready(cpu_ready), .cpu_data_available(cpu_data_available),
        .cpu_accepted(cpu_accepted), .p0_cpu_selected(), .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(p0_be),
        .p0_wr_len(p0_len), .p0_wr_stream(p0_stream), .p0_q(p0_q), .p0_wr_req(p0_wr),
        .p0_rd_req(p0_rd), .p0_end_burst_req(p0_end), .p0_available(p0_available),
        .p0_ready(p0_ready), .p0_data_available(p0_data_available), .wsrc_addr(), .wsrc_q()
    );

    reg p0_ready_r = 0, p0_data_r = 0;
    assign p0_ready = p0_ready_r;
    assign p0_data_available = p0_data_r;
    reg [4:0] fb_tick = 0;
    always @(posedge clk_sdram) begin
        p0_ready_r <= 0; p0_data_r <= 0;
        fb_tick <= fb_tick + 1'b1;
        if (!fb_req && !fb_busy && fb_tick == 5'd4) fb_req <= 1;
        if (fb_req && !fb_busy && fb_available) begin fb_req <= 0; fb_busy <= 1; end
        if (!port_busy && (p0_wr || p0_rd)) begin
            port_busy <= 1; port_read <= p0_rd; port_delay <= 0;
            lat_addr <= p0_addr;
            if (p0_wr && p0_addr >= 25'h100) begin
                if (p0_be[0]) mem[p0_addr[11:0]][7:0]  <= p0_data[7:0];
                if (p0_be[1]) mem[p0_addr[11:0]][15:8] <= p0_data[15:8];
            end
        end else if (port_busy) begin
            port_delay <= port_delay + 1'b1;
            if (port_read && port_delay == 1) p0_data_r <= 1;
            if ((!port_read && port_delay == 3) || (port_read && p0_end && port_delay >= 3)) begin
                port_busy <= 0; p0_ready_r <= 1; fb_busy <= 0;
            end
        end
    end

    // ---- beat driver ---------------------------------------------------------
    integer errors = 0, beats = 0, starts = 0;
    always @(posedge clk_sys) if (bridge_wb_start) starts <= starts + 1;

    reg [31:0] got;
    // Drive one beat starting at the current time (nonblocking so it is seen at
    // the next edge, like a registered CPU output); return when dACK is sampled.
    task beat(input w, input [24:0] a, input [31:0] d, input integer gap);
        integer n;
        begin
            cyc <= 1; stb <= 1; we <= w; addr <= a; wdata <= d;
            beats = beats + 1;
            @(posedge clk_sys);
            n = 0;
            while (!dACK) begin @(posedge clk_sys); n = n + 1; if (n > 2000) begin
                $display("FAIL: beat timeout"); errors = errors + 1; $finish; end end
            got = rdata;                    // value at the ACK cycle
            if (gap > 0) begin
                cyc <= 0; stb <= 0;
                repeat (gap) @(posedge clk_sys);
            end
        end
    endtask

    task check(input [31:0] actual, input [31:0] expected, input [255:0] name); begin
        if (actual !== expected) begin
            $display("FAIL: %0s: got %08h expected %08h", name, actual, expected);
            errors = errors + 1;
        end else $display("ok:   %0s = %08h", name, actual);
    end endtask

    integer gapv, i;
    initial begin
        for (i = 0; i < 4096; i = i + 1) mem[i] = 16'h0000;
        mem[12'h200] = 16'h5678; mem[12'h201] = 16'h1234;      // 12345678
        mem[12'h202] = 16'hF00D; mem[12'h203] = 16'h0BAD;      // 0BADF00D
        mem[12'h204] = 16'hACED; mem[12'h205] = 16'h0DEF;      // 0DEFACED
        repeat (5) @(posedge clk_sys); rst_sys <= 0;
        repeat (5) @(posedge clk_sdram); rst_sdram <= 0;
        repeat (10) @(posedge clk_sys);
        for (gapv = 0; gapv <= 3; gapv = gapv + 3) begin
            $display("--- gap %0d ---", gapv);
            beat(0, 25'h200, 32'd0, gapv); check(got, 32'h12345678, "load A");
            beat(0, 25'h202, 32'd0, gapv); check(got, 32'h0BADF00D, "load B (back-to-back)");
            beat(0, 25'h204, 32'd0, gapv); check(got, 32'h0DEFACED, "load C (back-to-back)");
            beat(1, 25'h210, 32'hFFFFFFFF, gapv);
            beat(0, 25'h210, 32'd0, gapv); check(got, 32'hFFFFFFFF, "load after all-ones store");
            beat(1, 25'h210, 32'h00000000, gapv);
            beat(0, 25'h210, 32'd0, gapv); check(got, 32'h00000000, "load after zero store");
            beat(1, 25'h220, 32'hA5C33C5A, gapv);
            beat(0, 25'h220, 32'd0, gapv); check(got, 32'hA5C33C5A, "load after pattern store");
        end
        cyc <= 0; stb <= 0;
        repeat (400) @(posedge clk_sys);
        if (starts != beats) begin
            $display("FAIL: %0d Wishbone beats produced %0d bridge requests", beats, starts);
            errors = errors + 1;
        end else $display("ok:   one bridge request per beat (%0d)", beats);
        $display("\n%0s (%0d failures)", errors ? "FAILED" : "PASSED", errors);
        $finish;
    end
    initial begin #4000000; $display("FAIL: regression timeout"); $finish; end
endmodule
`default_nettype wire
