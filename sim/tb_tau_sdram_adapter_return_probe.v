`timescale 1ns/1ps
`default_nettype none

// A-077: retain adapter response data for the target fifth CPU read before
// mp3_soc selects it onto the CPU-facing Wishbone return bus.
module tb_tau_sdram_adapter_return_probe;
    reg clk = 0;
    always #5 clk = ~clk;
    reg rst = 1, cpu_req = 0, cpu_we = 0, adapter_ack = 0;
    reg [31:0] cpu_wdata = 0, adapter_rdata = 0;
    reg [2:0] cpu_cti = 0;
    reg [3:0] cpu_sel = 4'hf;
    wire [48:0] bits;

    tau_sdram_cpu_window_probe #(.RETURN_PATH_MODE(2)) dut (
        .clk(clk), .rst(rst), .cpu_req(cpu_req), .cpu_we(cpu_we),
        .cpu_wdata(cpu_wdata), .cpu_cti(cpu_cti), .cpu_sel(cpu_sel),
        .adapter_req(1'b0), .mux_accept(1'b0), .mux_start(1'b0),
        .bridge_busy(1'b0), .bridge_done(1'b0), .adapter_done(1'b0),
        .wb_ack(adapter_ack), .adapter_rdata(adapter_rdata),
        .unsupported(1'b0), .adapter_write(1'b0), .adapter_wdata(32'd0),
        .adapter_be(4'd0), .mux_wb_start(1'b0), .mux_write(1'b0),
        .mux_wdata(32'd0), .mux_be(4'd0), .bridge_write_seen(1'b0),
        .bridge_op_write(1'b0), .bridge_wdata_all_ones(1'b0),
        .bridge_be_all_enabled(1'b0), .bridge_lo_write_accepted(1'b0),
        .bridge_hi_write_accepted(1'b0), .ctrl_write_latched(1'b0),
        .ctrl_write_data_all_ones(1'b0), .ctrl_write_be_all_enabled(1'b0),
        .ctrl_write_command(1'b0), .ctrl_write_dq_enabled(1'b0),
        .ctrl_write_dq_all_ones(1'b0), .ctrl_write_dqm_unmasked(1'b0),
        .ctrl_follow_read_seen(1'b0), .ctrl_follow_read_data_seen(1'b0),
        .ctrl_follow_read_data_zero(1'b0), .ctrl_follow_read_data_all_ones(1'b0),
        .bridge_follow_read_seen(1'b0), .bridge_follow_read_data_zero(1'b0),
        .bridge_follow_read_data_all_ones(1'b0), .cpu_ack(1'b0),
        .cpu_rdata(32'd0), .bits(bits)
    );

    task tick; begin @(posedge clk); #1; end endtask
    task request(input write, input [31:0] data);
        begin cpu_we = write; cpu_wdata = data; cpu_req = 1; tick; cpu_req = 0; tick; end
    endtask

    initial begin
        tick; rst = 0;
        request(0, 32'd0); request(1, 32'd0); request(0, 32'd0);
        request(1, 32'hFFFFFFFF); request(0, 32'd0);
        adapter_rdata = 32'hFFFFFFFF; adapter_ack = 1; tick; adapter_ack = 0;
        if (bits[48:46] !== 3'b101)
            $fatal(1, "adapter return probe did not capture all-ones data: %h", bits);
        $display("PASS: adapter return probe captures all-ones data at ACK");
        $finish;
    end
endmodule

`default_nettype wire
