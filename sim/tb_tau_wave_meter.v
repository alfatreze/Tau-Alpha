// tb_tau_wave_meter.v -- tau_wave_meter against an independent behavioural model: level peaks, rising-zero-crossing trigger,
// timeout capture, 256 min/max columns at several spans. Every column and both peaks are compared.
`timescale 1ns/1ps
module tb_tau_wave_meter;
    reg clk = 0; always #8.333 clk = ~clk;
    reg rst = 1, tick = 0;
    reg signed [15:0] in_l = 0, in_r = 0;
    reg ctl_we = 0; reg [11:0] ctl_data = 0;
    reg idx_we = 0; reg [7:0] idx_data = 0;
    wire [31:0] rd_data, status;
    wire [15:0] pk_l, pk_r;
    tau_wave_meter #(.COLS(256), .TRIG_MAX(2048)) dut (.clk(clk), .rst(rst), .tick(tick), .in_l(in_l), .in_r(in_r),
        .ctl_we(ctl_we), .ctl_data(ctl_data), .idx_we(idx_we), .idx_data(idx_data), .rd_data(rd_data),
        .pk_l(pk_l), .pk_r(pk_r), .status(status));

    integer mids [0:8191];
    integer n, fails, mode, span, trig, i, c, k, mn, mx, prevneg, want_pkl, want_pkr, e, w, timeout_exp, sc;

    task put(input integer l, input integer r);
        begin
            in_l <= l; in_r <= r; tick <= 1; @(posedge clk); tick <= 0;
            repeat (7) @(posedge clk);
            mids[n] = (l + r) >>> 1; n = n + 1;
            if (l < 0 ? -l > want_pkl : l > want_pkl) want_pkl = (l < 0) ? -l : l;
            if (r < 0 ? -r > want_pkr : r > want_pkr) want_pkr = (r < 0) ? -r : r;
        end
    endtask

    function integer sig(input integer sc_mode, input integer t);
        integer ph;
        begin
            ph = t % 97;
            if (sc_mode == 0) sig = (ph < 48) ? (ph * 130 - 3000) : (3000 - (ph - 48) * 130);   // triangle, crosses zero
            else              sig = 5000 + (t % 7) * 100;                                          // DC + ripple: never crosses
        end
    endfunction

    task run(input integer sc_mode, input integer sp);
        begin
            n = 0; want_pkl = 0; want_pkr = 0;
            @(posedge clk); ctl_data <= 12'h003 | ((sp - 1) << 8); ctl_we <= 1; @(posedge clk); ctl_we <= 0;   // clear peaks + arm
            repeat (3) @(posedge clk);
            i = 0;
            while (status[1] && n < 8000) begin put(sig(sc_mode, i), sig(sc_mode, i) / 2 + ((i % 5) - 2) * 50); i = i + 1; end
            repeat (4) @(posedge clk);
            if (status[1]) begin fails = fails + 1; $display("FAIL mode %0d span %0d: capture never finished", sc_mode, sp); end
            // model: first rising crossing (armed with prev_neg = 0) or the TRIG_MAX-th sample
            prevneg = 0; trig = -1;
            for (k = 0; k < n && trig < 0; k = k + 1) begin
                if ((prevneg && mids[k] >= 0) || k == 2047) trig = k;
                prevneg = (mids[k] < 0);
            end
            timeout_exp = (trig == 2047) && !(trig > 0 && mids[trig-1] < 0 && mids[trig] >= 0);
            if (status[2] !== timeout_exp[0]) begin fails = fails + 1; $display("FAIL mode %0d: timeout flag %b expected %0d", sc_mode, status[2], timeout_exp); end
            for (c = 0; c < 256; c = c + 1) begin
                mn = 32767; mx = -32768;
                for (k = 0; k < sp; k = k + 1) begin
                    e = mids[trig + c * sp + k];
                    if (e < mn) mn = e; if (e > mx) mx = e;
                end
                idx_data <= c; idx_we <= 1; @(posedge clk); idx_we <= 0; repeat (3) @(posedge clk);
                if ($signed(rd_data[31:16]) !== mn || $signed(rd_data[15:0]) !== mx) begin
                    fails = fails + 1;
                    if (fails < 12) $display("FAIL mode %0d span %0d col %0d: got min %0d max %0d, want %0d %0d", sc_mode, sp, c,
                                             $signed(rd_data[31:16]), $signed(rd_data[15:0]), mn, mx);
                end
            end
        end
    endtask

    initial begin
        fails = 0;
        repeat (4) @(posedge clk); rst = 0; repeat (4) @(posedge clk);
        run(0, 4);
        if (pk_l !== want_pkl[15:0] || pk_r !== want_pkr[15:0]) begin fails = fails + 1; $display("FAIL peaks got %0d/%0d want %0d/%0d", pk_l, pk_r, want_pkl, want_pkr); end
        run(0, 1);
        run(0, 16);
        run(1, 4);
        // clearing the peaks
        @(posedge clk); ctl_data <= 12'h001; ctl_we <= 1; @(posedge clk); ctl_we <= 0; @(posedge clk);
        if (pk_l !== 16'd0 || pk_r !== 16'd0) begin fails = fails + 1; $display("FAIL peaks not cleared"); end
        // an extreme sample: |-32768| must read 32768
        put(-32768, 32767);
        if (pk_l !== 16'd32768 || pk_r !== 16'd32767) begin fails = fails + 1; $display("FAIL extreme peaks %0d/%0d", pk_l, pk_r); end
        if (fails == 0) $display("PASSED (0 failures)"); else $display("FAILED (%0d)", fails);
        $finish;
    end
endmodule
