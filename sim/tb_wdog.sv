// tau_wdog testbench: directed cases that pin the contract in src/fpga/core/tau_wdog.sv, then 200,000 random cycles against an independent reference model.
// Prints PASSED or FAIL lines; run with the mutant defines (MUT_*) to prove each one is killed (Makefile test-rtl-wdog).
`timescale 1ns/1ps
module tb_wdog;
    reg clk = 0, rst = 1;
    always #5 clk = ~clk;
    reg arm_wr = 0, ack_wr = 0; reg [31:0] arm_val = 0;
    wire irq; wire [31:0] status, counts;
    tau_wdog dut (.clk(clk), .rst(rst), .arm_wr(arm_wr), .arm_val(arm_val), .ack_wr(ack_wr), .irq(irq), .status(status), .counts(counts));

    integer fails = 0;
    task check(input ok, input [511:0] what);
        begin if (!ok) begin fails = fails + 1; $display("FAIL %0s (t=%0t)", what, $time); end end
    endtask

    // one clock with the given strobes
    task cyc(input a, input [31:0] v, input k);
        begin arm_wr = a; arm_val = v; ack_wr = k; @(posedge clk); #1; arm_wr = 0; ack_wr = 0; arm_val = 0; end
    endtask
    task idle(input integer n); integer i; begin for (i = 0; i < n; i = i + 1) cyc(0, 0, 0); end endtask

    integer i, t;
    // ---- reference model (blocking, written separately from the RTL) ----
    reg m_armed, m_exp; reg [31:0] m_cnt; reg [15:0] m_arms, m_expn;
    task m_step(input a, input [31:0] v, input k);
        reg fire;
        begin
            fire = m_armed && (m_cnt == 32'd1) && !a;
            if (k) m_exp = 0;
            if (m_armed) begin m_cnt = m_cnt - 1; if (fire) begin m_armed = 0; m_exp = 1; m_expn = m_expn + 1; end end
            if (a) begin
                if (v != 0) begin m_cnt = v; m_armed = 1; m_arms = m_arms + 1; end
                else begin m_armed = 0; m_cnt = 0; end
            end
        end
    endtask

    initial begin
        m_armed = 0; m_exp = 0; m_cnt = 0; m_arms = 0; m_expn = 0;
        repeat (3) @(posedge clk); #1; rst = 0;
        check(status === 32'h8000_0000 && counts === 32'd0 && !irq, "after reset: present, idle, no counts");

        // 1. exact timing: limit N gives irq after exactly N clocks
        cyc(1, 5, 0);                                  // edge E
        for (i = 1; i <= 5; i = i + 1) begin check(!irq, "limit 5: not yet"); check(status[0] === 1'b1, "limit 5: armed while counting"); cyc(0, 0, 0); end
        check(irq === 1'b1, "limit 5: irq exactly 5 clocks after the arm"); check(status[1:0] === 2'b10, "limit 5: expired, no longer armed");
        // 2. sticky until ack, for any number of clocks
        idle(7); check(irq === 1'b1, "irq stays up until acknowledged"); check(counts[15:0] === 16'd1 && counts[31:16] === 16'd1, "one arm, one expiry");
        cyc(0, 0, 1); check(irq === 1'b0 && status[1] === 1'b0, "ack drops irq");
        // 3. limit 1 expires on the next edge
        cyc(1, 1, 0); check(!irq, "limit 1: not at the arm edge"); cyc(0, 0, 0); check(irq === 1'b1, "limit 1: expired one clock after the arm"); cyc(0, 0, 1);
        // 4. disarm before expiry: no interrupt, ever
        cyc(1, 6, 0); idle(2); cyc(1, 0, 0); check(status[0] === 1'b0, "disarm clears armed"); idle(20); check(!irq, "disarmed: no interrupt");
        // 5. a disarm after the expiry does not clear it (only ack does)
        cyc(1, 2, 0); idle(2); check(irq, "expired for case 5"); cyc(1, 0, 0); check(irq === 1'b1, "disarm does not clear a pending expiry"); cyc(0, 0, 1); check(!irq, "ack clears it");
        // 6. re-arm while counting reloads the window
        cyc(1, 10, 0); idle(5); cyc(1, 10, 0); idle(9); check(!irq, "re-arm: still counting after the old window"); idle(1); check(irq === 1'b1, "re-arm: expiry 10 clocks after the second arm"); cyc(0, 0, 1);
        // 7. an arm landing in the same cycle as the end of the count wins: no expiry from the old window
        cyc(1, 4, 0); idle(3);                          // edges E+1..E+3; the count ends at edge E+4
        cyc(1, 9, 0);                                   // this write is sampled on edge E+4, the end-of-count edge
        check(!irq, "arm in the end-of-count cycle: no expiry from the old window"); idle(8); check(!irq, "new window still counting"); idle(1); check(irq === 1'b1, "new window expires 9 clocks after the write"); cyc(0, 0, 1);
        // 8. ack and arm in one cycle: both take effect
        cyc(1, 2, 0); idle(2); check(irq, "expired for case 8"); cyc(1, 3, 1); check(!irq && status[0] === 1'b1, "ack + arm together: cleared and armed"); idle(2); check(!irq, "new window counting"); idle(1); check(irq === 1'b1, "the new window ran 3 clocks"); cyc(0, 0, 1);
        // 9. a long limit (needs more than 16 bits)
        cyc(1, 70000, 0); repeat (69998) begin @(posedge clk); end #1; check(!irq && status[0], "limit 70000: still armed late"); @(posedge clk); #1; @(posedge clk); #1; check(irq === 1'b1, "limit 70000 expired");
        cyc(0, 0, 1);
        // 10. reset clears everything, counts included
        cyc(1, 3, 0); rst = 1; @(posedge clk); #1; rst = 0; check(status === 32'h8000_0000 && counts === 32'd0 && !irq, "reset clears state and counts");
        // sync the model after the directed part
        m_armed = 0; m_exp = 0; m_cnt = 0; m_arms = 0; m_expn = 0;

        // random vs the model
        for (t = 0; t < 200000; t = t + 1) begin
            reg a, k; reg [31:0] v;
            a = ($urandom % 24) == 0; k = ($urandom % 16) == 0;
            case ($urandom % 6) 0: v = 0; 1: v = 1; 2: v = 2; 3: v = $urandom % 8; 4: v = $urandom % 40; default: v = $urandom % 300; endcase
            m_step(a, v, k);
            cyc(a, v, k);
            if (irq !== m_exp || status[1:0] !== {m_exp, m_armed} || counts !== {m_arms, m_expn}) begin
                fails = fails + 1; if (fails < 40) $display("FAIL model mismatch at cycle %0d: irq %b/%b status %h counts %h vs armed %b exp %b arms %0d expn %0d", t, irq, m_exp, status, counts, m_armed, m_exp, m_arms, m_expn);
            end
        end
        if (fails == 0) $display("PASSED tau_wdog: directed cases and 200000 random cycles");
        else $display("FAIL tau_wdog: %0d failures", fails);
        $finish;
    end
endmodule
