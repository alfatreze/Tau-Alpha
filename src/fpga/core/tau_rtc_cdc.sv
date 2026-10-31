// =============================================================================
// tau_rtc_cdc.sv -- the Pocket's real-time clock (APF host command 0x0090) carried from the bridge domain (clk_74a)
// into the system domain, for the wall-clock MMIO registers (tau_soc.v 0x1C4-0x1D0, docs/MMIO_ALLOCATION.md).
//
// The source is core_bridge_cmd.v (it stays clocked directly from clk_74a and is not touched): on command 0x0090 it loads
// rtc_epoch_seconds / rtc_date_bcd / rtc_time_bcd and sets rtc_valid in the SAME clock edge, and rtc_valid then stays 1
// (the host sends the command once, at boot). So the payload words are stable from the edge that raises rtc_valid until a
// later 0x0090, which the host does not send within microseconds.
//
// The crossing is the stable-payload handshake this code base uses for dataslot_update (tau_soc.v) and tgt_cmd.v:
//   clk_74a : the RISING edge of rtc_valid flips a toggle (a one-cycle pulse could be lost; a level change cannot);
//   clk_sys : the toggle goes through a three-flop synchroniser; its edge (sync[2] ^ sync[1]) loads the three payload words
//             once, and raises `valid`. The payload is only sampled while it is stable (at least three destination clocks
//             after it was loaded), so no multi-bit word can be captured half old, half new. `valid` rises in the same
//             clock edge as the words, never before them.
// Only the FIRST command is taken (rtc_valid has no further rising edge); the words never change afterwards. Nothing here
// is reset by the system reset: the Pocket may send the command while the CPU is still held in reset (firmware loading),
// and the registers must still hold it when the CPU starts.
//
// BUG is a mutation hook for sim/tb_rtc_cdc.v (nonzero must make it fail): 1 = the words follow the bridge registers
// continuously (an ungated, unsynchronised capture), 2 = `valid` comes straight from the bridge domain (it can be seen before
// the words are latched).
// =============================================================================
`default_nettype none

module tau_rtc_cdc #(
    parameter BUG = 0
) (
    input  wire        clk_74a,
    input  wire        rtc_valid,          // clk_74a: core_bridge_cmd's rtc_valid (rises with the payload)
    input  wire [31:0] rtc_epoch_seconds,  // clk_74a
    input  wire [31:0] rtc_date_bcd,       // clk_74a
    input  wire [31:0] rtc_time_bcd,       // clk_74a

    input  wire        clk_sys,
    output wire        valid_sys,
    output wire [31:0] epoch_sys,
    output wire [31:0] date_sys,
    output wire [31:0] time_sys
);
    // ---- clk_74a: rising edge of rtc_valid -> toggle ---------------------------------------------------------------
    reg v_prev = 1'b0, tgl = 1'b0;
    always @(posedge clk_74a) begin
        v_prev <= rtc_valid;
        if (rtc_valid & ~v_prev) tgl <= ~tgl;
    end

    // ---- clk_sys: synchronise the toggle, load the payload on its edge ----------------------------------------------
    reg [2:0]  sync = 3'b000;
    reg        valid_q = 1'b0;
    reg [31:0] epoch_q = 32'd0, date_q = 32'd0, time_q = 32'd0;
    wire       edge_s = sync[2] ^ sync[1];
    always @(posedge clk_sys) begin
        sync <= {sync[1:0], tgl};
        if (BUG == 1 || (edge_s && !valid_q)) begin
            epoch_q <= rtc_epoch_seconds;
            date_q  <= rtc_date_bcd;
            time_q  <= rtc_time_bcd;
        end
        if (edge_s) valid_q <= 1'b1;
    end
    assign valid_sys = (BUG == 2) ? rtc_valid : valid_q;
    assign epoch_sys = epoch_q;
    assign date_sys  = date_q;
    assign time_sys  = time_q;
endmodule
