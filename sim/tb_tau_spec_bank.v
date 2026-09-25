// tb_tau_spec_bank.v -- tau_spec_bank against a behavioural model of the firmware's octave cascade
// (fw/player.c meters_feed, literally: nested loop, C shift semantics), on a pseudo-random stereo stream.
// Checks every band mean of several windows, win_ctr, and that ticks arriving at the engine's worst-case spacing
// are all processed.
`timescale 1ns/1ps
module tb_tau_spec_bank;
    reg clk = 0; always #8.333 clk = ~clk;
    reg rst = 1, tick = 0;
    reg signed [15:0] in_l = 0, in_r = 0;
    reg [3:0] rd_idx = 0;
    wire [19:0] rd_mean;
    wire [15:0] win_ctr;
    tau_spec_bank #(.WIN_LOG2(10)) dut (.clk(clk), .rst(rst), .tick(tick), .in_l(in_l), .in_r(in_r),
                                        .rd_idx(rd_idx), .rd_mean(rd_mean), .win_ctr(win_ctr));

    // ---- reference model (the firmware algorithm) ----
    integer m_lp [0:7], m_slp [0:7], m_cnt [0:7];
    integer m_acc [0:15];
    integer m_mean [0:15];
    integer m_n;
    integer o, b, x, hp, sl, sh, fails, win, k, gotv;
    task model_sample(input integer l, input integer r);
        begin
            x = (l + r) >>> 1;
            for (o = 0; o < 8; o = o + 1) begin
                m_lp[o] = m_lp[o] + ((x - m_lp[o]) >>> 1);
                hp = x - m_lp[o];
                m_slp[o] = m_slp[o] + ((hp - m_slp[o]) >>> 2);
                sl = m_slp[o];
                sh = hp - sl;
                m_acc[o*2]   = m_acc[o*2]   + (sh < 0 ? -sh : sh);
                m_acc[o*2+1] = m_acc[o*2+1] + (sl < 0 ? -sl : sl);
                m_cnt[o] = m_cnt[o] + 1;
                if (m_cnt[o] & 1) o = 99;        // break
                else x = m_lp[o];
            end
            m_n = m_n + 1;
        end
    endtask

    reg [31:0] lfsr = 32'hACE1_2468;
    function [15:0] rnd; input dummy; begin
        lfsr = {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        rnd = lfsr[15:0];
    end endfunction

    integer i, j;
    initial begin
        fails = 0; m_n = 0;
        for (i = 0; i < 8; i = i + 1) begin m_lp[i] = 0; m_slp[i] = 0; m_cnt[i] = 0; end
        for (i = 0; i < 16; i = i + 1) begin m_acc[i] = 0; m_mean[i] = 0; end
        #200 rst = 0; #200;
        for (win = 1; win <= 4; win = win + 1) begin
            for (j = 0; j < 1024; j = j + 1) begin
                // a slowly varying tone plus noise, so the low bands have real content too
                in_l = $signed(rnd(0)) >>> 2;  in_r = $signed(rnd(0)) >>> 2;
                in_l = in_l + ((j & 64) ? 16'sd6000 : -16'sd6000);
                @(negedge clk) tick = 1; @(negedge clk) tick = 0;      // drive off the sampling edge: exactly one posedge sees it
                model_sample(in_l, in_r);
                repeat (60) @(posedge clk);          // 60 clocks between samples: far tighter than the ~1250 real budget
            end
            repeat (60) @(posedge clk);              // let the latch finish
            for (b = 0; b < 16; b = b + 1) begin
                m_mean[b] = m_acc[b] >>> (10 - (b/2));
                m_acc[b] = 0;
                rd_idx = b; #40;
                gotv = rd_mean;
                if (gotv !== m_mean[b]) begin
                    $display("FAIL: window %0d band %0d: hw %0d, model %0d", win, b, gotv, m_mean[b]);
                    fails = fails + 1;
                end
            end
            if (win_ctr !== win) begin $display("FAIL: win_ctr %0d after window %0d", win_ctr, win); fails = fails + 1; end
            m_n = 0;
        end
        if (fails == 0) $display("PASSED"); else $display("FAILED (%0d)", fails);
        $finish;
    end
endmodule
