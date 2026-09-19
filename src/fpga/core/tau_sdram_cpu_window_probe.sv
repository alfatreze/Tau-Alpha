// =============================================================================
// tau_sdram_cpu_window_probe.sv -- one-way Phase 2 request-path recorder.
//
// This module is diagnostic-only.  It latches milestones from the first CPU
// access to the uncached SDRAM window, retaining them even if that access then
// stalls the CPU.  The bit vector is exported to a small video overlay by
// core_game.vh; it is not part of a release build.
//
// Bits  0..8: CPU request, adapter request, mux accept, mux start,
//             bridge busy, bridge done, adapter done, Wishbone ACK,
//             unsupported CTI.
// Bits 11..9: first request CTI.  Bits 15..12: first request SEL.
// Bit 16: first request WE. Bits 17/18: fourth request observed / its WE.
// Bits 19..34: fourth-request data/byte-enable evidence across adapter, mux,
//              and SDRAM-domain bridge (expectation bits target FFFFFFFF/F).
// Bits 35..45: controller latch, external WRITE drive, and the first
//              following CPU-read halfword (zero/all-ones predicates).
// Bits 46..48: by default, bridge's assembled 32-bit response for that
//              following read (seen, zero, all ones), before mux/Wishbone
//              return handling. RETURN_PATH_MODE=1 uses CPU-facing ACK/data;
//              mode 2 uses adapter ACK/data (seen, zero, all ones).
// The A-061 ROM order is preflight read, zero store, zero read, all-ones
// store. Capturing request four therefore observes the write that produces the
// diagnostic's first failing FFFFFFFF readback, rather than the valid zero
// store that preceded it.
// =============================================================================
`default_nettype none

module tau_sdram_cpu_window_probe #(
    // 0 preserves A-074 bridge-response semantics. 1 reuses cells 46..48
    // for CPU-facing return data; 2 captures adapter data immediately before
    // mp3_soc's registered return selector.
    parameter RETURN_PATH_MODE = 0
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        cpu_req,
    input  wire        cpu_we,
    input  wire [31:0] cpu_wdata,
    input  wire [2:0]  cpu_cti,
    input  wire [3:0]  cpu_sel,
    input  wire        adapter_req,
    input  wire        mux_accept,
    input  wire        mux_start,
    input  wire        bridge_busy,
    input  wire        bridge_done,
    input  wire        adapter_done,
    input  wire        wb_ack,
    input  wire [31:0] adapter_rdata,
    input  wire        unsupported,
    input  wire        adapter_write,
    input  wire [31:0] adapter_wdata,
    input  wire [3:0]  adapter_be,
    input  wire        mux_wb_start,
    input  wire        mux_write,
    input  wire [31:0] mux_wdata,
    input  wire [3:0]  mux_be,
    input  wire        bridge_write_seen,
    input  wire        bridge_op_write,
    input  wire        bridge_wdata_all_ones,
    input  wire        bridge_be_all_enabled,
    input  wire        bridge_lo_write_accepted,
    input  wire        bridge_hi_write_accepted,
    input  wire        ctrl_write_latched,
    input  wire        ctrl_write_data_all_ones,
    input  wire        ctrl_write_be_all_enabled,
    input  wire        ctrl_write_command,
    input  wire        ctrl_write_dq_enabled,
    input  wire        ctrl_write_dq_all_ones,
    input  wire        ctrl_write_dqm_unmasked,
    input  wire        ctrl_follow_read_seen,
    input  wire        ctrl_follow_read_data_seen,
    input  wire        ctrl_follow_read_data_zero,
    input  wire        ctrl_follow_read_data_all_ones,
    input  wire        bridge_follow_read_seen,
    input  wire        bridge_follow_read_data_zero,
    input  wire        bridge_follow_read_data_all_ones,
    input  wire        cpu_ack,
    input  wire [31:0] cpu_rdata,
    output reg  [48:0] bits
);
    reg cpu_req_d;
    reg [2:0] cpu_request_count;
    reg cpu_follow_read_armed;
    reg [8:0] bridge_debug_sync_1, bridge_debug_sync_2;
    reg [10:0] ctrl_debug_sync_1, ctrl_debug_sync_2;
    always @(posedge clk) begin
        if (rst) begin
            bits <= 49'd0;
            cpu_req_d <= 1'b0;
            cpu_request_count <= 3'd0;
            cpu_follow_read_armed <= 1'b0;
            bridge_debug_sync_1 <= 9'd0;
            bridge_debug_sync_2 <= 9'd0;
            ctrl_debug_sync_1 <= 11'd0;
            ctrl_debug_sync_2 <= 11'd0;
        end else begin
            cpu_req_d <= cpu_req;
            bridge_debug_sync_1 <= {bridge_write_seen, bridge_op_write,
                bridge_wdata_all_ones, bridge_be_all_enabled,
                bridge_lo_write_accepted, bridge_hi_write_accepted,
                bridge_follow_read_seen, bridge_follow_read_data_zero,
                bridge_follow_read_data_all_ones};
            bridge_debug_sync_2 <= bridge_debug_sync_1;
            ctrl_debug_sync_1 <= {ctrl_write_latched,
                ctrl_write_data_all_ones, ctrl_write_be_all_enabled,
                ctrl_write_command, ctrl_write_dq_enabled,
                ctrl_write_dq_all_ones, ctrl_write_dqm_unmasked,
                ctrl_follow_read_seen, ctrl_follow_read_data_seen,
                ctrl_follow_read_data_zero, ctrl_follow_read_data_all_ones};
            ctrl_debug_sync_2 <= ctrl_debug_sync_1;
            if (cpu_req && !bits[0]) begin
                bits[0]    <= 1'b1;
                bits[11:9] <= cpu_cti;
                bits[15:12]<= cpu_sel;
            end
            if (cpu_req && !cpu_req_d) begin
                if (cpu_request_count == 3'd0) begin
                    bits[16] <= cpu_we;
                end else if (cpu_request_count == 3'd3) begin
                    bits[17] <= 1'b1;
                    bits[18] <= cpu_we;
                    bits[19] <= (cpu_wdata == 32'hFFFFFFFF);
                    bits[20] <= (cpu_sel == 4'b1111);
                end else if (cpu_request_count == 3'd4 && !cpu_we) begin
                    // Request five is the all-ones readback in the A-061 ROM.
                    // The successor return probe samples the CPU-facing bus
                    // only when this specific read is acknowledged.
                    cpu_follow_read_armed <= 1'b1;
                end
                if (cpu_request_count != 3'd7)
                    cpu_request_count <= cpu_request_count + 1'b1;
            end
            // The fourth CPU request is the A-061 ROM's first all-ones
            // matrix store. Later stages may occur many cycles later, so each
            // observed milestone is retained independently after that request.
            if (bits[17]) begin
                if (adapter_req) begin
                    bits[21] <= 1'b1;
                    bits[22] <= adapter_write;
                    bits[23] <= (adapter_wdata == 32'hFFFFFFFF);
                    bits[24] <= (adapter_be == 4'b1111);
                end
                if (mux_wb_start) begin
                    bits[25] <= 1'b1;
                    bits[26] <= mux_write;
                    bits[27] <= (mux_wdata == 32'hFFFFFFFF);
                    bits[28] <= (mux_be == 4'b1111);
                end
                if (bridge_debug_sync_2[8]) begin
                    bits[29] <= 1'b1;
                    bits[30] <= bridge_debug_sync_2[7];
                    bits[31] <= bridge_debug_sync_2[6];
                    bits[32] <= bridge_debug_sync_2[5];
                end
                if (bridge_debug_sync_2[4]) bits[33] <= 1'b1;
                if (bridge_debug_sync_2[3]) bits[34] <= 1'b1;
                if (ctrl_debug_sync_2[10]) bits[35] <= 1'b1;
                if (ctrl_debug_sync_2[9])  bits[36] <= 1'b1;
                if (ctrl_debug_sync_2[8])  bits[37] <= 1'b1;
                if (ctrl_debug_sync_2[7])  bits[38] <= 1'b1;
                if (ctrl_debug_sync_2[6])  bits[39] <= 1'b1;
                if (ctrl_debug_sync_2[5])  bits[40] <= 1'b1;
                if (ctrl_debug_sync_2[4])  bits[41] <= 1'b1;
                if (ctrl_debug_sync_2[3])  bits[42] <= 1'b1;
                if (ctrl_debug_sync_2[2])  bits[43] <= 1'b1;
                if (ctrl_debug_sync_2[1])  bits[44] <= 1'b1;
                if (ctrl_debug_sync_2[0])  bits[45] <= 1'b1;
                if (RETURN_PATH_MODE == 1) begin
                    if (cpu_follow_read_armed && cpu_ack && !bits[46]) begin
                        bits[46] <= 1'b1;
                        bits[47] <= (cpu_rdata == 32'h00000000);
                        bits[48] <= (cpu_rdata == 32'hFFFFFFFF);
                    end
                end else if (RETURN_PATH_MODE == 2) begin
                    if (cpu_follow_read_armed && wb_ack && !bits[46]) begin
                        bits[46] <= 1'b1;
                        bits[47] <= (adapter_rdata == 32'h00000000);
                        bits[48] <= (adapter_rdata == 32'hFFFFFFFF);
                    end
                end else begin
                    if (bridge_debug_sync_2[2]) bits[46] <= 1'b1;
                    if (bridge_debug_sync_2[1]) bits[47] <= 1'b1;
                    if (bridge_debug_sync_2[0]) bits[48] <= 1'b1;
                end
            end
            if (adapter_req) bits[1] <= 1'b1;
            if (mux_accept)  bits[2] <= 1'b1;
            if (mux_start)   bits[3] <= 1'b1;
            if (bridge_busy) bits[4] <= 1'b1;
            if (bridge_done) bits[5] <= 1'b1;
            if (adapter_done)bits[6] <= 1'b1;
            if (wb_ack)      bits[7] <= 1'b1;
            if (unsupported) bits[8] <= 1'b1;
        end
    end
endmodule

`default_nettype wire
