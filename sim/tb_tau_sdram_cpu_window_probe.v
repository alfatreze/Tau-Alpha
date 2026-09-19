`timescale 1ns/1ps
`default_nettype none

module tb_tau_sdram_cpu_window_probe;
    reg clk = 0;
    always #5 clk = ~clk;

    reg rst = 1;
    reg cpu_req = 0;
    reg cpu_we = 0;
    reg [31:0] cpu_wdata = 0;
    reg [2:0] cpu_cti = 0;
    reg [3:0] cpu_sel = 0;
    reg adapter_req = 0, mux_accept = 0, mux_start = 0, bridge_busy = 0;
    reg bridge_done = 0, adapter_done = 0, wb_ack = 0, unsupported = 0;
    reg adapter_write = 0, mux_wb_start = 0, mux_write = 0;
    reg [31:0] adapter_wdata = 0, mux_wdata = 0;
    reg [3:0] adapter_be = 0, mux_be = 0;
    reg bridge_write_seen = 0, bridge_op_write = 0;
    reg bridge_wdata_all_ones = 0, bridge_be_all_enabled = 0;
    reg bridge_lo_write_accepted = 0, bridge_hi_write_accepted = 0;
    reg ctrl_write_latched = 0, ctrl_write_data_all_ones = 0;
    reg ctrl_write_be_all_enabled = 0, ctrl_write_command = 0;
    reg ctrl_write_dq_enabled = 0, ctrl_write_dq_all_ones = 0;
    reg ctrl_write_dqm_unmasked = 0, ctrl_follow_read_seen = 0;
    reg ctrl_follow_read_data_seen = 0, ctrl_follow_read_data_zero = 0;
    reg ctrl_follow_read_data_allones = 0;
    reg bridge_follow_read_seen = 0, bridge_follow_read_data_zero = 0;
    reg bridge_follow_read_data_allones = 0;
    wire [48:0] bits;

    tau_sdram_cpu_window_probe dut (
        .clk(clk), .rst(rst), .cpu_req(cpu_req), .cpu_we(cpu_we), .cpu_wdata(cpu_wdata),
        .cpu_cti(cpu_cti), .cpu_sel(cpu_sel),
        .adapter_req(adapter_req), .mux_accept(mux_accept), .mux_start(mux_start),
        .bridge_busy(bridge_busy), .bridge_done(bridge_done), .adapter_done(adapter_done),
        .mux_done(1'b0), .mux_rdata(32'd0),
        .wb_ack(wb_ack), .adapter_rdata(32'd0), .unsupported(unsupported), .adapter_write(adapter_write),
        .adapter_wdata(adapter_wdata), .adapter_be(adapter_be),
        .mux_wb_start(mux_wb_start), .mux_write(mux_write),
        .mux_wdata(mux_wdata), .mux_be(mux_be),
        .bridge_write_seen(bridge_write_seen), .bridge_op_write(bridge_op_write),
        .bridge_wdata_all_ones(bridge_wdata_all_ones),
        .bridge_be_all_enabled(bridge_be_all_enabled),
        .bridge_lo_write_accepted(bridge_lo_write_accepted),
        .bridge_hi_write_accepted(bridge_hi_write_accepted),
        .ctrl_write_latched(ctrl_write_latched),
        .ctrl_write_data_all_ones(ctrl_write_data_all_ones),
        .ctrl_write_be_all_enabled(ctrl_write_be_all_enabled),
        .ctrl_write_command(ctrl_write_command),
        .ctrl_write_dq_enabled(ctrl_write_dq_enabled),
        .ctrl_write_dq_all_ones(ctrl_write_dq_all_ones),
        .ctrl_write_dqm_unmasked(ctrl_write_dqm_unmasked),
        .ctrl_follow_read_seen(ctrl_follow_read_seen),
        .ctrl_follow_read_data_seen(ctrl_follow_read_data_seen),
        .ctrl_follow_read_data_zero(ctrl_follow_read_data_zero),
        .ctrl_follow_read_data_all_ones(ctrl_follow_read_data_allones),
        .bridge_follow_read_seen(bridge_follow_read_seen),
        .bridge_follow_read_data_zero(bridge_follow_read_data_zero),
        .bridge_follow_read_data_all_ones(bridge_follow_read_data_allones),
        .bits(bits)
    );

    task tick;
        begin @(posedge clk); #1; end
    endtask

    initial begin
        tick; rst = 0;
        // First request records its bus shape permanently.
        cpu_cti = 3'b000; cpu_sel = 4'b1111; cpu_we = 0; cpu_req = 1; tick; cpu_req = 0;
        if (bits[0] !== 1'b1 || bits[11:9] !== 3'b000 || bits[15:12] !== 4'b1111 ||
            bits[16] !== 1'b0)
            $fatal(1, "first CPU request metadata was not latched: %h", bits);

        adapter_req = 1; tick; adapter_req = 0;
        mux_accept = 1; tick; mux_accept = 0;
        mux_start = 1; bridge_busy = 1; tick; mux_start = 0; bridge_busy = 0;
        bridge_done = 1; adapter_done = 1; tick; bridge_done = 0; adapter_done = 0;
        wb_ack = 1; tick; wb_ack = 0;
        if (bits[7:1] !== 7'b1111111 || bits[8] !== 1'b0)
            $fatal(1, "milestones not retained: %h", bits);

        // The A-061 ROM's second and third mapped requests are the zero store
        // and its readback. They must not arm the A-065 all-ones payload
        // capture, which targets the fourth request.
        cpu_cti = 3'b000; cpu_sel = 4'b1111; cpu_wdata = 32'h00000000;
        cpu_we = 1; cpu_req = 1;
        tick; cpu_req = 0; unsupported = 0;
        tick;
        cpu_we = 0; cpu_req = 1;
        tick; cpu_req = 0;
        tick;
        if (bits[17] !== 1'b0)
            $fatal(1, "zero-store sequence armed all-ones payload capture: %h", bits);

        // The fourth request is the first all-ones matrix store. It must
        // preserve the first read metadata and retain its all-ones payload.
        cpu_cti = 3'b000; cpu_sel = 4'b1111; cpu_wdata = 32'hFFFFFFFF;
        cpu_we = 1; cpu_req = 1;
        tick; cpu_req = 0;
        if (bits[15:9] !== {4'b1111, 3'b000} || bits[8] !== 1'b0 ||
            bits[17] !== 1'b1 || bits[18] !== 1'b1)
            $fatal(1, "first request metadata was overwritten: %h", bits);

        adapter_req = 1; adapter_write = 1; adapter_wdata = 32'hFFFFFFFF; adapter_be = 4'hF;
        tick; adapter_req = 0;
        mux_wb_start = 1; mux_write = 1; mux_wdata = 32'hFFFFFFFF; mux_be = 4'hF;
        tick; mux_wb_start = 0;
        bridge_write_seen = 1; bridge_op_write = 1; bridge_wdata_all_ones = 1;
        bridge_be_all_enabled = 1; bridge_lo_write_accepted = 1; bridge_hi_write_accepted = 1;
        tick; tick; tick;
        if (bits[34:19] !== 16'hFFFF)
            $fatal(1, "store payload trace was incomplete: %h", bits);

        // The controller recorder crosses from clk_sdram into clk_sys through
        // two synchronizer stages. It must retain the direct controller facts
        // even when the returning halfword is deliberately zero.
        ctrl_write_latched = 1; ctrl_write_data_all_ones = 1;
        ctrl_write_be_all_enabled = 1; ctrl_write_command = 1;
        ctrl_write_dq_enabled = 1; ctrl_write_dq_all_ones = 1;
        ctrl_write_dqm_unmasked = 1; ctrl_follow_read_seen = 1;
        ctrl_follow_read_data_seen = 1; ctrl_follow_read_data_zero = 1;
        ctrl_follow_read_data_allones = 0;
        tick; tick; tick;
        if (bits[44:35] !== 10'b1111111111 || bits[45] !== 1'b0)
            $fatal(1, "controller trace did not retain write/zero-read evidence: %h", bits);

        bridge_follow_read_seen = 1; bridge_follow_read_data_zero = 0;
        bridge_follow_read_data_allones = 1;
        tick; tick; tick;
        if (bits[48:46] !== 3'b101)
            $fatal(1, "bridge response provenance was not retained: %h", bits);

        $display("PASS: CPU-window probe latches request shapes, bridge trace, and controller evidence");
        $finish;
    end
endmodule

`default_nettype wire
