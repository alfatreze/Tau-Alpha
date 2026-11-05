// Pack watchdog (docs/features/PACK_WATCHDOG_PLAN.md, D-T15). MIT licence.
//
// A cycle-counting watchdog for the CPU. The firmware arms it with a limit in clk cycles just before it calls a pack's draw tick and disarms it right after;
// if the limit passes first, `irq` goes high and stays high until the firmware acknowledges, and the CPU takes it as its timer interrupt.
//
// Contract (every cycle count is in clk cycles; "edge E" is the clock edge on which arm_wr is sampled):
//   arm_wr, arm_val != 0 : armed <= 1, the counter is loaded with arm_val at edge E. irq rises at edge E + arm_val (arm_val cycles later), if not disarmed or re-armed before.
//                          arm_val = 1 expires on the very next edge.
//   arm_wr, arm_val == 0 : armed <= 0 at edge E; the counter stops. A pending (expired) interrupt is NOT cleared: only ack_wr does that, so an expiry is never lost.
//   re-arm while armed   : the counter is reloaded (a fresh window).
//   arm_wr in the same cycle as the count reaches its end: the write wins (no expiry from the old window).
//   expiry               : expired <= 1 (sticky), armed <= 0, expiries counter + 1. irq = expired.
//   ack_wr               : expired <= 0 (irq drops). An arm_wr in the same cycle as ack_wr still arms.
//   rst                  : everything cleared, including the two event counters.
// The status word: [31] present, [1] expired, [0] armed. The counts word: [31:16] arms (wraps), [15:0] expiries (wraps).
`default_nettype none
module tau_wdog (
    input  wire        clk,
    input  wire        rst,
    input  wire        arm_wr,
    input  wire [31:0] arm_val,
    input  wire        ack_wr,
    output wire        irq,
    output wire [31:0] status,
    output wire [31:0] counts
);
    reg        armed, expired;
    reg [31:0] cnt;
    reg [15:0] n_arms, n_exp;

    wire end_of_count = armed && (cnt == 32'd1);

    always @(posedge clk) begin
        if (rst) begin
            armed <= 1'b0; expired <= 1'b0; cnt <= 32'd0; n_arms <= 16'd0; n_exp <= 16'd0;
        end else begin
            if (ack_wr
`ifdef MUT_ACK_NOOP
                && 1'b0
`endif
               ) expired <= 1'b0;
            if (armed) begin
                cnt <= cnt - 32'd1;
                if (end_of_count
`ifndef MUT_ARM_LOSES_RACE
                    && !arm_wr
`endif
                   ) begin
                    armed <= 1'b0; expired <= 1'b1; n_exp <= n_exp + 16'd1;
                end
            end
            if (arm_wr) begin
                if (arm_val != 32'd0) begin
`ifdef MUT_REARM_NOLOAD
                    if (!armed) cnt <= arm_val;
`else
                    cnt <= arm_val;
`endif
                    armed <= 1'b1; n_arms <= n_arms + 16'd1;
                end else begin
`ifdef MUT_DISARM_KEEP
                    armed <= armed;
`else
                    armed <= 1'b0;
`endif
                    cnt <= 32'd0;
`ifdef MUT_DISARM_CLEARS_EXPIRED
                    expired <= 1'b0;
`endif
                end
            end
        end
    end

`ifdef MUT_IRQ_PULSE
    reg expired_q;
    always @(posedge clk) expired_q <= expired;
    assign irq = expired & ~expired_q;
`else
    assign irq = expired;
`endif
    assign status = {1'b1, 29'd0, expired, armed};
    assign counts = {n_arms, n_exp};
endmodule
`default_nettype wire
