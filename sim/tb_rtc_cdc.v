// tb_rtc_cdc.v -- the wall clock (APF host command 0x0090) through the REAL core_bridge_cmd.v and tau_rtc_cdc.sv: the bridge side drives the host's register
// writes exactly as the APF does (parameter words at 0x20/0x24/0x28, then the command word 0x434D0090 at 0x00), the system side is a different clock. Checks:
//   * before the command: valid 0 and every word 0, for a long time;
//   * after it: valid 1 and epoch / BCD date / BCD time exactly the sent words (the day-of-week nibble of the time word included);
//   * at every system clock, valid implies the words are the sent ones (valid never leads the words) -- kills BUG=2;
//   * a second 0x0090 with different words does not change the latched ones (the host sends it once; first wins) -- kills BUG=1;
//   * the same for several system clock periods and phases (SYSP half period in ps, PHASE start delay in ps).
// BUG selects the mutant of tau_rtc_cdc (the bench must FAIL for 1 and 2).
`timescale 1ps/1ps
`default_nettype none
module tb_rtc_cdc;
    parameter BUG = 0;
    parameter SYSP = 8333;      // clk_sys half period, ps
    parameter PHASE = 0;        // clk_sys start delay, ps
    reg clk74 = 0, clk_sys = 0;
    always #6734 clk74 = ~clk74;                               // 74.25 MHz
    initial begin #(PHASE); forever #(SYSP) clk_sys = ~clk_sys; end

    reg  [31:0] b_addr = 0, b_wdata = 0; reg b_wr = 0;
    wire        rtc_valid; wire [31:0] rtc_epoch, rtc_date, rtc_time;
    wire        reset_n;
    core_bridge_cmd icb (
        .clk(clk74), .reset_n(reset_n), .bridge_endian_little(1'b0), .bridge_addr(b_addr), .bridge_rd(1'b0), .bridge_rd_data(), .bridge_wr(b_wr), .bridge_wr_data(b_wdata),
        .status_boot_done(1'b1), .status_setup_done(1'b0), .status_running(1'b0),
        .dataslot_requestread(), .dataslot_requestread_id(), .dataslot_requestread_ack(1'b0), .dataslot_requestread_ok(1'b0),
        .dataslot_requestwrite(), .dataslot_requestwrite_id(), .dataslot_requestwrite_size(), .dataslot_requestwrite_ack(1'b0), .dataslot_requestwrite_ok(1'b0),
        .dataslot_update(), .dataslot_update_id(), .dataslot_update_size(), .dataslot_allcomplete(),
        .rtc_epoch_seconds(rtc_epoch), .rtc_date_bcd(rtc_date), .rtc_time_bcd(rtc_time), .rtc_valid(rtc_valid),
        .savestate_supported(1'b0), .savestate_addr(32'd0), .savestate_size(32'd0), .savestate_maxloadsize(32'd0),
        .osnotify_inmenu(), .savestate_start(), .savestate_start_ack(1'b0), .savestate_start_busy(1'b0), .savestate_start_ok(1'b0), .savestate_start_err(1'b0),
        .savestate_load(), .savestate_load_ack(1'b0), .savestate_load_busy(1'b0), .savestate_load_ok(1'b0), .savestate_load_err(1'b0),
        .target_dataslot_read(1'b0), .target_dataslot_write(1'b0), .target_dataslot_flush(1'b0), .target_dataslot_getfile(1'b0), .target_dataslot_openfile(1'b0),
        .target_dataslot_ack(), .target_dataslot_done(), .target_dataslot_err(),
        .target_dataslot_id(16'd0), .target_dataslot_slotoffset(32'd0), .target_dataslot_bridgeaddr(32'd0), .target_dataslot_length(32'd0),
        .target_buffer_param_struct(32'd0), .target_buffer_resp_struct(32'd0),
        .datatable_addr(10'd0), .datatable_wren(1'b0), .datatable_data(32'd0), .datatable_q());
    wire v_s; wire [31:0] ep_s, da_s, ti_s;
    tau_rtc_cdc #(.BUG(BUG)) dut (.clk_74a(clk74), .rtc_valid(rtc_valid), .rtc_epoch_seconds(rtc_epoch), .rtc_date_bcd(rtc_date), .rtc_time_bcd(rtc_time),
                                  .clk_sys(clk_sys), .valid_sys(v_s), .epoch_sys(ep_s), .date_sys(da_s), .time_sys(ti_s));

    task bw(input [31:0] a, input [31:0] d); begin @(posedge clk74); b_addr <= a; b_wdata <= d; b_wr <= 1; @(posedge clk74); b_wr <= 0; end endtask
    task host_rtc(input [31:0] ep, input [31:0] da, input [31:0] ti);
        begin bw(32'hF8000020, ep); bw(32'hF8000024, da); bw(32'hF8000028, ti); bw(32'hF8000000, 32'h434D0090); repeat (40) @(posedge clk74); end
    endtask

    localparam [31:0] EP1 = 32'h6724_B3C5, DA1 = 32'h2026_1031, TI1 = 32'h0612_3456;   // the time word carries a day-of-week nibble (6)
    localparam [31:0] EP2 = 32'h6725_0000, DA2 = 32'h2026_1101, TI2 = 32'h0100_0102;
    reg sent = 0;                                    // the first command has been handed to the bridge registers
    integer errors = 0, k;
    always @(posedge clk_sys) begin
        if (!sent && v_s) errors = errors + 1;
        if (v_s && (ep_s !== EP1 || da_s !== DA1 || ti_s !== TI1)) errors = errors + 1;                    // valid with anything but the sent words: valid led the words, or they moved
        if (!v_s && (ep_s !== 32'd0 || da_s !== 32'd0 || ti_s !== 32'd0)) errors = errors + 1;           // words appeared without valid
    end
    initial begin
        icb.hstate = 4'd0; icb.host_cmd_start = 1'b0;               // the APF file relies on the FPGA's power-up zero for these two (Icarus starts them at x)
        repeat (200) @(posedge clk74);
        if (v_s !== 1'b0 || ep_s !== 32'd0 || da_s !== 32'd0 || ti_s !== 32'd0) begin $display("FAIL: not idle (zero, invalid) before the command"); errors = errors + 1; end
        sent = 1; host_rtc(EP1, DA1, TI1);
        repeat (60) @(posedge clk_sys);
        if (v_s !== 1'b1) begin $display("FAIL: valid did not rise"); errors = errors + 1; end
        if (ep_s !== EP1 || da_s !== DA1 || ti_s !== TI1) begin $display("FAIL: words %h %h %h", ep_s, da_s, ti_s); errors = errors + 1; end
        for (k = 0; k < 3; k = k + 1) begin host_rtc(EP2, DA2, TI2); repeat (30) @(posedge clk_sys); end   // the host does not do this; if it did, the first words stay
        if (ep_s !== EP1 || da_s !== DA1 || ti_s !== TI1) begin $display("FAIL: a later 0x0090 changed the latched words: %h %h %h", ep_s, da_s, ti_s); errors = errors + 1; end
        if (errors == 0) $display("PASSED: tb_rtc_cdc (BUG=%0d SYSP=%0d PHASE=%0d)", BUG, SYSP, PHASE); else $display("FAILED: tb_rtc_cdc %0d errors (BUG=%0d)", errors, BUG);
        $finish;
    end
    initial begin #20_000_000_000; $display("FAIL: timeout"); $finish; end
endmodule
