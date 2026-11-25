`timescale 1ps/1ps
// Trace regression bench for tau_scanout (Talos 2 T5, contract section 7; D-T20).
// It replaces the differential bench that ran tau_scanout against the pre-rewrite oracle (deleted by D-T20 after the Pocket confirmation DEV 198).
// The stimulus is the same: random line-buffer writes, an engine model that answers the fill requests (missing, doubled and very late ones included),
// cmd_push, a reset, three unrelated clocks. Every output is folded into one 64-bit trace hash per clock domain (X and Z are part of the trace);
// the hash must equal the golden value in sim/golden/scanout_trace.txt, which was recorded from the oracle on the passing bench (same seeds, cycle for cycle).
// +seed=N picks the stimulus, +gvid=<hex> +gsys=<hex> the golden hashes; -Ptb_tau_scanout_trace.BUG=n turns on mutant n of tau_scanout (the bench must FAIL).
module tb_tau_scanout_trace;
    parameter BUG = 0;
    parameter NFRAMES = 4;       // frames simulated (one frame = 16.667 ms)

    // ---- clocks: 100 MHz, 49.0 MHz-ish, 12 MHz; periods in ps, none an integer multiple of another
    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0;
    always #5000  clk_sdram = ~clk_sdram;     // 10.000 ns
    always #10190 clk_sys   = ~clk_sys;       // 20.380 ns (49.07 MHz)
    always #41666 clk_vid   = ~clk_vid;       // 83.332 ns (12.0001 MHz)

    integer seed, seed0;
    function integer rnd(input integer n);   // 0 .. n-1
        integer r;
        begin r = $random(seed); if (r < 0) r = -r; if (r < 0) r = 0; rnd = r % n; end
    endfunction

    // ---- stimulus
    reg        reset = 0;
    reg        cmd_push = 0;
    reg        lb_we = 0;
    reg [9:0]  lb_waddr = 0;
    reg [15:0] lb_wdata = 0;
    reg        fill_done_inc = 0;
    reg [9:0]  fill_done_line = 0;
    reg        fill_seen = 0;

    // ---- the device
    wire [15:0] n_cnt;
    wire        n_rt, n_vt;
    wire [9:0]  n_fl;
    wire [23:0] n_rgb;
    wire        n_de, n_hs, n_vs;
    wire [8:0]  n_svc;

    tau_scanout #(
        .BUG_ACT_X(BUG==1), .BUG_HS(BUG==2), .BUG_LATENCY(BUG==3), .BUG_PARITY(BUG==4), .BUG_VBLANK(BUG==5),
        .BUG_NO_PAINT(BUG==6), .BUG_LATE_HC(BUG==7), .BUG_REQ_EARLY(BUG==8), .BUG_VS(BUG==9),
        .BUG_PAINT_SYN(BUG==10), .BUG_NO_NEXT(BUG==11), .BUG_EXPAND(BUG==12)
    ) n (.reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid), .cmd_push(cmd_push),
        .lb_we(lb_we), .lb_waddr(lb_waddr), .lb_wdata(lb_wdata),
        .fill_done_inc(fill_done_inc), .fill_done_line(fill_done_line), .fill_seen(fill_seen),
        .underflow_cnt(n_cnt), .fill_req_tgl(n_rt), .fill_line_req(n_fl), .vblank_tgl(n_vt),
        .video_rgb(n_rgb), .video_de(n_de), .video_hs(n_hs), .video_vs(n_vs), .scan_vc(n_svc));

    // ---- trace: 100 ps after each edge of the output's own clock (outputs have settled, nothing else moves yet)
    function [63:0] fold(input [63:0] h, input [47:0] p);   // FNV-style step; any X or Z in the sample is folded as one marker value (X-ness is part of the trace)
        begin fold = ((^p) === 1'bx) ? ((h ^ 64'hdeadbeefdeadbeef) * 64'h100000001b3) : ((h ^ {16'd0, p}) * 64'h100000001b3); end
    endfunction
    reg [63:0] hv = 64'hcbf29ce484222325, hs = 64'hcbf29ce484222325, gvid = 0, gsys = 0;
    integer cmp_vid = 0, cmp_sys = 0, have_golden = 0;
    // The first three vid edges flush the output pipeline (its registers start undefined in simulation) and are not part of the trace.
    always @(posedge clk_vid) begin
        #100;
        cmp_vid = cmp_vid + 1;
        if (cmp_vid > 3) hv = fold(hv, {n_rt,n_fl,n_vt,n_rgb,n_de,n_hs,n_vs,n_svc});
    end
    always @(posedge clk_sys) begin
        #100;
        cmp_sys = cmp_sys + 1;
        hs = fold(hs, {32'd0,n_cnt});
    end

    // ---- coverage (measured on the new device's outputs)
    integer tg_de = 0, tg_hs = 0, tg_vs = 0, tg_rgb = 0, tg_rt = 0, tg_vt = 0, frames = 0, vsyncs = 0;
    integer rgb_nonzero_before_paint = 0, rgb_nonzero_after_paint = 0;
    reg p_de = 0, p_hs = 0, p_vs = 0, p_rt = 0, p_vt = 0;
    reg [23:0] p_rgb = 0;
    reg painted_seen = 0;   // cmd_push has happened
    integer cnt_at_reset = 0, cnt_end = 0;
    always @(posedge clk_vid) begin
        #200;
        if (n_de !== p_de) tg_de = tg_de + 1;
        if (n_hs !== p_hs) tg_hs = tg_hs + 1;
        if (n_vs !== p_vs) tg_vs = tg_vs + 1;
        if (n_rgb !== p_rgb) tg_rgb = tg_rgb + 1;
        if (n_rt !== p_rt) tg_rt = tg_rt + 1;
        if (n_vt !== p_vt) tg_vt = tg_vt + 1;
        if (n_vs && !p_vs) vsyncs = vsyncs + 1;
        if (n_rgb !== 24'd0) begin
            if (painted_seen) rgb_nonzero_after_paint = rgb_nonzero_after_paint + 1;
            else              rgb_nonzero_before_paint = rgb_nonzero_before_paint + 1;
        end
        p_de = n_de; p_hs = n_hs; p_vs = n_vs; p_rgb = n_rgb; p_rt = n_rt; p_vt = n_vt;
    end

    // ---- engine model on clk_sdram: line-buffer write bursts and fill-done events
    reg rt_s1 = 0, rt_s2 = 0, rt_prev = 0;
    reg [9:0] pend_line [0:63];
    integer   pend_due  [0:63];
    integer   ph = 0, pt = 0;           // head, tail (counts, index mod 64)
    integer   now_ns;
    integer   burst_left = 0, burst_gap = 0;
    integer   t_seen_ns, t_push_ns, t_reset_ns;
    integer   n_req = 0, n_done = 0, n_dup = 0, n_skip = 0, n_late = 0, n_wr = 0;
    reg       reset_fired = 0, push_fired = 0, seen_armed = 0;

    task enqueue(input [9:0] line, input integer delay_ns);
        begin
            if (pt - ph < 60) begin
                pend_line[pt % 64] = line;
                pend_due[pt % 64]  = now_ns + delay_ns;
                pt = pt + 1;
            end
        end
    endtask

    always @(posedge clk_sdram) begin
        now_ns = $time / 1000;
        // line-buffer writes: bursts of 1..40 words, random gaps
        lb_we <= 1'b0;
        if (burst_left > 0) begin
            lb_we    <= (rnd(8) != 0);
            lb_waddr <= rnd(1024);
            lb_wdata <= rnd(65536);
            burst_left = burst_left - 1;
            n_wr = n_wr + 1;
        end else if (burst_gap > 0) burst_gap = burst_gap - 1;
        else begin burst_left = 1 + rnd(40); burst_gap = rnd(30); end

        // request observation (the engine samples the toggle through two flops)
        rt_s1 <= n_rt; rt_s2 <= rt_s1; rt_prev <= rt_s2;
        if (rt_s2 !== rt_prev) begin
            n_req = n_req + 1;
            case (rnd(12))
                0: n_skip = n_skip + 1;                                        // missing
                1: begin enqueue(n_fl, rnd(2000)); enqueue(n_fl, rnd(6000)); n_dup = n_dup + 1; end  // two for one line
                2: begin enqueue(n_fl, 42000 + rnd(30000)); n_late = n_late + 1; end                 // more than a line late
                3: begin enqueue(n_fl, 20000 + rnd(20000)); end
                default: enqueue(n_fl, rnd(12000));
            endcase
        end

        // deliver the oldest due event
        fill_done_inc <= 1'b0;
        if (pt > ph && now_ns >= pend_due[ph % 64]) begin
            fill_done_inc  <= 1'b1;
            fill_done_line <= pend_line[ph % 64];
            ph = ph + 1;
            n_done = n_done + 1;
            if (!fill_seen && now_ns >= t_seen_ns) fill_seen <= 1'b1;  // sticky: 0 until the first fill after t_seen
        end
    end

    // ---- cmd_push (clk_sys) and reset (clk_vid)
    always @(posedge clk_sys) begin
        cmd_push <= 1'b0;
        if (!push_fired && ($time / 1000) >= t_push_ns) begin
            cmd_push <= 1'b1; push_fired = 1; painted_seen = 1;
        end else if (push_fired && rnd(500) == 0) cmd_push <= 1'b1;   // later pushes are no-ops
    end

    integer rst_cycles = 0;
    always @(posedge clk_vid) begin
        if (rst_cycles > 0) begin rst_cycles = rst_cycles - 1; if (rst_cycles == 0) reset <= 1'b0; end
        else if (!reset_fired && ($time / 1000) >= t_reset_ns) begin
            reset_fired = 1; reset <= 1'b1; rst_cycles = 1 + rnd(9);
            cnt_at_reset = n_cnt;
        end
    end

    // ---- run
    integer end_ns, k;
    integer ok;
    initial begin
        if (!$value$plusargs("seed=%d", seed)) seed = 1;
        seed0 = seed;
        for (k = 0; k < 64; k = k + 1) begin pend_line[k] = 0; pend_due[k] = 0; end
        // events: push in frame 1, fill_seen shortly after, reset in frame 2
        t_push_ns  = 200000 + rnd(6000000);
        t_seen_ns  = t_push_ns + rnd(2000000);
        t_reset_ns = 10000000 + rnd(5000000);
        end_ns     = NFRAMES * 16667000;                 // at least three and a half full frames after the reset
        repeat (end_ns / 1000) #1000000;
        cnt_end = n_cnt;
        $display("seed=%0d BUG=%0d simulated=%0d ns  vid_edges=%0d sys_edges=%0d  vsyncs=%0d", seed0, BUG, end_ns, cmp_vid, cmp_sys, vsyncs);
        $display("coverage: de=%0d hs=%0d vs=%0d rgb=%0d fill_req_tgl=%0d vblank_tgl=%0d late_lines(underflow_cnt)=%0d",
                 tg_de, tg_hs, tg_vs, tg_rgb, tg_rt, tg_vt, cnt_end);
        $display("stimulus: writes=%0d requests=%0d done=%0d dup=%0d skipped=%0d very_late=%0d  push@%0d ns seen@%0d ns reset@%0d ns",
                 n_wr, n_req, n_done, n_dup, n_skip, n_late, t_push_ns, t_seen_ns, t_reset_ns);
        $display("painted: rgb nonzero before push=%0d after push=%0d (black before)", rgb_nonzero_before_paint, rgb_nonzero_after_paint);
        $display("TRACE seed=%0d BUG=%0d vid=%h sys=%h", seed0, BUG, hv, hs);
        ok = 1;
        if ($value$plusargs("gvid=%h", gvid) && $value$plusargs("gsys=%h", gsys)) begin
            have_golden = 1;
            if (hv !== gvid) begin ok = 0; $display("TRACE MISMATCH: video-side hash %h, golden %h", hv, gvid); end
            if (hs !== gsys) begin ok = 0; $display("TRACE MISMATCH: sys-side hash %h, golden %h", hs, gsys); end
        end else begin ok = 0; $display("no golden hash given (+gvid=... +gsys=...)"); end
        if (BUG == 0) begin
            if (!(tg_de > 0 && tg_hs > 0 && tg_vs > 0 && tg_rgb > 0 && tg_rt > 0 && tg_vt > 0)) begin ok = 0; $display("coverage hole: a video output or toggle never moved"); end
            if (!(cnt_end > 0))                     begin ok = 0; $display("coverage hole: no late line was counted"); end
            if (!(reset_fired && push_fired))       begin ok = 0; $display("coverage hole: reset or push never fired"); end
            if (!(rgb_nonzero_after_paint > 0))     begin ok = 0; $display("coverage hole: picture never showed after painted"); end
            if (!(vsyncs >= 3))                     begin ok = 0; $display("coverage hole: fewer than three frames"); end
            if (!(n_dup > 0 && n_skip > 0 && n_late > 0)) begin ok = 0; $display("coverage hole: stimulus classes (double/missing/late) not all hit"); end
        end
        if (ok) $display("PASSED"); else $display("FAILED");
        $finish;
    end
endmodule
