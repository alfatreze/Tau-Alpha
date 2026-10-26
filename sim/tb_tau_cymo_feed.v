// tb_tau_cymo_feed.v -- the Cymo hand-off against the REAL resampler at the real clock ratio (B-527).
// Two free-running fractional accumulators exactly as in mp3_soc.v: the 48 kHz output tick (CYMO_TICK_RATE_INC) starts the resampler, a track-rate tick
// (TRACK_INC, 44.1 kHz) supplies one new input sample per tick (a counting ramp, so a dropped or repeated sample is visible as a break in the pushed sequence).
// Checks, over SIM_MS of simulated audio: no stale consumes, no dropped ticks, and the sequence pushed into the resampler is the ramp 1,2,3,... with nothing
// skipped or repeated. Parameter BUG=1 (the old tick-gated hand-off) must FAIL (make test-rtl-cymo-feed-mutation).
`timescale 1ns/1ps
module tb_tau_cymo_feed;
    parameter integer BUG = 0;
    parameter integer SIM_MS = 40;
    parameter [31:0] TRACK_INC = 32'd2841063;     // 44100 * 2^32 / 66,666,667
    parameter [31:0] CYMO_INC  = 32'd3092376;     // 48000 * 2^32 / 66,666,667 (mp3_soc.v CYMO_TICK_RATE_INC)

    reg clk = 0; always #7.5 clk = ~clk;
    reg rst = 1, clear = 0;
    reg [31:0] acc_t = 0, acc_c = 0;
    wire tick_t = ({1'b0, acc_t} + {1'b0, TRACK_INC}) >> 32;
    wire tick_c = ({1'b0, acc_c} + {1'b0, CYMO_INC}) >> 32;
    always @(posedge clk) if (!rst) begin acc_t <= acc_t + TRACK_INC; acc_c <= acc_c + CYMO_INC; end

    reg signed [15:0] ramp = 16'sd0;
    always @(posedge clk) if (rst) ramp <= 16'sd0; else if (tick_t) ramp <= ramp + 16'sd1;   // sample k (k >= 1) is on in_l during tick k-1... see below
    wire signed [15:0] in_l = ramp + 16'sd1, in_r = -(ramp + 16'sd1);

    wire pop_req, push_we, busy, done;
    wire signed [15:0] push_l, push_r, out_l, out_r;
    wire [15:0] stale_cnt, drop_cnt;
    wire [2:0] level;
    reg start = 0;
    always @(posedge clk) start <= tick_c & ~rst;
    reg live = 0;

    tau_cymo_feed #(.BUG(BUG)) feed (.clk(clk), .rst(rst), .clear(clear), .live(live), .tick(tick_t), .in_l(in_l), .in_r(in_r), .pop_req(pop_req),
        .push_we(push_we), .push_l(push_l), .push_r(push_r), .stale_cnt(stale_cnt), .drop_cnt(drop_cnt), .level(level));
    tau_cymo_resamp rs (.clk(clk), .rst(rst), .clear(clear), .push_we(push_we), .push_l(push_l), .push_r(push_r), .start(start), .out_rd(1'b0),
        .busy(busy), .done(done), .pop_req(pop_req), .out_l(out_l), .out_r(out_r));

    integer pushes = 0, breaks = 0, k;
    reg signed [15:0] last_l = 0;
    always @(posedge clk) if (push_we) begin
        pushes <= pushes + 1;
        if (pushes >= 2) begin                                  // the first PREFILL pushes are the zero prefill
            if (pushes == 2) begin if (push_l != 16'sd1) breaks <= breaks + 1; end
            else if (push_l != last_l + 16'sd1) breaks <= breaks + 1;
            last_l <= push_l;
        end
    end

    integer fails = 0;
    initial begin
        repeat (8) @(posedge clk); rst = 0;
        repeat (4) @(posedge clk);
        clear = 1; @(posedge clk); clear = 0;               // engage: prime the queue and clear the resampler
        repeat (80) @(posedge clk);
        live = 1;
        for (k = 0; k < SIM_MS; k = k + 1) repeat (66667) @(posedge clk);
        $display("pushes=%0d breaks=%0d stale=%0d drop=%0d level=%0d", pushes, breaks, stale_cnt, drop_cnt, level);
        if (pushes < SIM_MS * 40) begin $display("FAIL too few pushes"); fails = fails + 1; end
        if (stale_cnt != 0) begin $display("FAIL stale consumes: %0d", stale_cnt); fails = fails + 1; end
        if (drop_cnt != 0)  begin $display("FAIL dropped ticks: %0d", drop_cnt); fails = fails + 1; end
        if (breaks != 0)    begin $display("FAIL the pushed sequence skipped or repeated a sample %0d times", breaks); fails = fails + 1; end
        if (fails == 0) $display("PASSED"); else $display("FAILED %0d", fails);
        $finish;
    end
endmodule
