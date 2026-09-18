`timescale 1ns/1ps
`default_nettype none

// Focused simulation of the controller-side provenance recorder used by the
// A-066 Pocket probe. This is deliberately not an SDRAM functional model: it
// proves that the recorder sees the registered WRITE/DQ/DQM cycle and samples
// a deliberately-zero first READ_OUTPUT halfword, so the two evidence classes
// cannot be confused by a future refactor.
module tb_sdram_fb_controller_probe;
    reg clk = 0;
    always #5 clk = ~clk;

    reg reset = 1;
    wire init_complete;
    reg [24:0] p0_addr = 25'h001000;
    reg [15:0] p0_data = 16'hFFFF;
    reg [1:0] p0_byte_en = 2'b11;
    reg [10:0] p0_wr_len = 11'd1;
    wire [15:0] p0_q;
    reg p0_wr_req = 0, p0_rd_req = 0, p0_end_burst_req = 0;
    wire p0_available, p0_ready, p0_data_available;
    wire [10:0] wsrc_addr;
    wire [15:0] dq;
    wire [12:0] a;
    wire [1:0] dqm, ba;
    wire ncs, nwe, nras, ncas, cke, sdram_clk;

    wire dbg_latched, dbg_data_all_ones, dbg_be_all_enabled;
    wire dbg_write_command, dbg_dq_enabled, dbg_dq_all_ones, dbg_dqm_unmasked;
    wire dbg_read_seen, dbg_read_data_seen, dbg_read_data_zero, dbg_read_data_allones;

    reg model_drives_read = 0;
    assign dq = model_drives_read ? 16'h0000 : 16'hZZZZ;
    wire [3:0] command = {ncs, nras, ncas, nwe};
    integer write_commands = 0;
    reg observed_bus_write = 0;
    always @(negedge clk) begin
        if (command == 4'b0101)
            model_drives_read <= 1'b1;
        if (command == 4'b0010)
            model_drives_read <= 1'b0;
        if (command == 4'b0100) begin
            write_commands <= write_commands + 1;
            if (dq === 16'hFFFF && dqm == 2'b00)
                observed_bus_write <= 1'b1;
        end
    end

    sdram_fb #(.CLOCK_SPEED_MHZ(100), .BURST_TYPE(0), .CAS_LATENCY(2), .WRITE_BURST(1)) dut (
        .clk(clk), .reset(reset), .init_complete(init_complete),
        .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(p0_byte_en),
        .p0_wr_len(p0_wr_len), .p0_q(p0_q), .p0_wr_stream(1'b0),
        .wsrc_addr(wsrc_addr), .wsrc_q(16'h0000),
        .p0_wr_req(p0_wr_req), .p0_rd_req(p0_rd_req), .p0_end_burst_req(p0_end_burst_req),
        .debug_p0_cpu_selected(1'b1),
        .debug_cpu_allones_write_latched(dbg_latched),
        .debug_cpu_allones_data_latched(dbg_data_all_ones),
        .debug_cpu_allones_be_latched(dbg_be_all_enabled),
        .debug_cpu_allones_write_command(dbg_write_command),
        .debug_cpu_allones_dq_enabled(dbg_dq_enabled),
        .debug_cpu_allones_dq_allones(dbg_dq_all_ones),
        .debug_cpu_allones_dqm_unmasked(dbg_dqm_unmasked),
        .debug_cpu_follow_read_seen(dbg_read_seen),
        .debug_cpu_follow_read_data_seen(dbg_read_data_seen),
        .debug_cpu_follow_read_data_zero(dbg_read_data_zero),
        .debug_cpu_follow_read_data_allones(dbg_read_data_allones),
        .p0_available(p0_available), .p0_ready(p0_ready), .p0_data_available(p0_data_available),
        .SDRAM_DQ(dq), .SDRAM_A(a), .SDRAM_DQM(dqm), .SDRAM_BA(ba),
        .SDRAM_nCS(ncs), .SDRAM_nWE(nwe), .SDRAM_nRAS(nras), .SDRAM_nCAS(ncas),
        .SDRAM_CKE(cke), .SDRAM_CLK(sdram_clk)
    );

    task tick;
        begin @(posedge clk); #1; end
    endtask

    task wait_for_ready;
        integer limit;
        begin
            limit = 0;
            while (!p0_ready && limit < 200) begin tick; limit = limit + 1; end
            if (!p0_ready) $fatal(1, "controller never completed request");
        end
    endtask

    initial begin
        repeat (4) tick;
        reset = 0;
        while (!init_complete) tick;

        // One CPU-owned 16-bit FFFF write. The external model independently
        // observes the command/data bus; the DUT's retained bits must agree.
        @(negedge clk); p0_wr_req = 1;
        @(negedge clk); p0_wr_req = 0;
        wait_for_ready;
        if (!observed_bus_write || write_commands == 0)
            $fatal(1, "model did not observe FFFF WRITE with DQM clear");
        if ({dbg_latched, dbg_data_all_ones, dbg_be_all_enabled,
             dbg_write_command, dbg_dq_enabled, dbg_dq_all_ones,
             dbg_dqm_unmasked} !== 7'b1111111)
            $fatal(1, "write provenance incomplete: %b%b%b%b%b%b%b",
                   dbg_latched, dbg_data_all_ones, dbg_be_all_enabled,
                   dbg_write_command, dbg_dq_enabled, dbg_dq_all_ones,
                   dbg_dqm_unmasked);

        // Read the same CPU-owned slot. The model intentionally supplies zero;
        // this validates that readback observation is independent from the
        // previously latched all-ones write facts.
        @(negedge clk); p0_rd_req = 1;
        @(negedge clk); p0_rd_req = 0; p0_end_burst_req = 1;
        wait_for_ready;
        p0_end_burst_req = 0;
        if ({dbg_read_seen, dbg_read_data_seen, dbg_read_data_zero,
             dbg_read_data_allones} !== 4'b1110)
            $fatal(1, "read provenance incorrect: %b%b%b%b",
                   dbg_read_seen, dbg_read_data_seen, dbg_read_data_zero,
                   dbg_read_data_allones);

        $display("PASS: sdram_fb provenance records registered WRITE pins and zero READ_OUTPUT");
        $finish;
    end

    initial begin
        #500000;
        $fatal(1, "timeout waiting for sdram_fb controller probe");
    end
endmodule

`default_nettype wire
