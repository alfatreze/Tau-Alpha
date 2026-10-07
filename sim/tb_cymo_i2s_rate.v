// tb_cymo_i2s_rate.v -- does the real pcm_fifo -> sound_i2s hand-off reproduce the 44.1 kHz error seen on hardware?
//
// Drives the REAL pcm_fifo (44.1 kHz drain) into the REAL sound_i2s, with a behavioural model of the Altera dcfifo
// megafunction (4 words, showahead off, rdsync_delaypipe 5, underflow ignored). With the EQ off the output is a plain mux
// (audio_l = fifo_l), so it is left out. Records, at every LRCK reload, the 32-bit word that is about to be serialised,
// and writes one "left right" line per 48 kHz output slot. sim/test_cymo_i2s_rate.py compares that to the ideal hold.
//
// LIMIT: the dcfifo model's flag latencies are my reading of the datasheet behaviour, not the silicon's. A clean result
// here rules out the RTL logic around the FIFO, not a difference in the real megafunction's timing.
`timescale 1ns/1ps
`ifndef USE_ALTERA_MF   // define it (and add Quartus' eda/sim_lib/altera_mf.v to the build) to use Intel's own dcfifo model
module dcfifo (
    input  wire [31:0] data, input wire rdclk, input wire rdreq, input wire wrclk, input wire wrreq,
    output reg  [31:0] q, output wire rdempty,
    input wire aclr, output wire eccstatus, output wire rdfull, output wire [1:0] rdusedw,
    output wire wrempty, output wire wrfull, output wire [1:0] wrusedw
);
    parameter intended_device_family = "", lpm_numwords = 4, lpm_showahead = "OFF", lpm_type = "", lpm_width = 32,
              lpm_widthu = 2, overflow_checking = "ON", rdsync_delaypipe = 5, underflow_checking = "ON",
              use_eab = "ON", wrsync_delaypipe = 5, lpm_hint = "";
    reg [31:0] mem [0:3];
    reg [2:0] wptr = 0, rptr = 0;
    reg [2:0] w_s [0:7];
    integer i;
    initial for (i = 0; i < 8; i = i + 1) w_s[i] = 0;
    always @(posedge wrclk) if (wrreq && (wptr - w_s[7]) < 4) begin mem[wptr[1:0]] <= data; wptr <= wptr + 1; end
    always @(posedge rdclk) begin
        w_s[0] <= wptr;
        for (i = 1; i < 8; i = i + 1) w_s[i] <= w_s[i-1];
    end
    wire [2:0] w_seen = w_s[rdsync_delaypipe - 1];
    assign rdempty = (w_seen == rptr);
    always @(posedge rdclk) if (rdreq && !rdempty) begin q <= mem[rptr[1:0]]; rptr <= rptr + 1; end
    assign eccstatus = 0, rdfull = 0, rdusedw = 0, wrempty = 0, wrfull = 0, wrusedw = 0;
endmodule
`endif

module tb_cymo_i2s_rate;
    parameter integer SRC_HZ = 44100;
    parameter integer N_OUT  = 9000 ;                 // 48 kHz output slots to record
    localparam real   CLK_SYS_HZ = 66666667.0;
    reg clk = 0, clk_mclk = 0, rst = 1, flush = 0;
    always #7.5 clk = ~clk;                           // 66.667 MHz
    always #40.690 clk_mclk = ~clk_mclk;               // 12.288 MHz (B-457: PLL-synthesised, ideal in sim)

    // ---- source: 1 kHz, -6 dBFS, SRC_HZ, pushed in bursts like the firmware --------------------------------------
    integer n_src = 0;
    reg push = 0; reg [31:0] pdata = 0;
    wire full, empty, underrun, sample_tick; wire [11:0] level;
    wire signed [15:0] out_l, out_r;
    real ph;
    reg signed [15:0] s16;
    always @(posedge clk) begin
        push <= 0;
        if (!rst && level < 1500 && !full) begin
            ph = 2.0 * 3.14159265358979 * 1000.0 * n_src / SRC_HZ;
            s16 = $rtoi($floor(16383.5 * $sin(ph) + 0.5));
            pdata <= {s16, s16}; push <= 1; n_src <= n_src + 1;
        end
    end
    wire [31:0] rate_inc = $rtoi($floor(SRC_HZ * 4294967296.0 / CLK_SYS_HZ + 0.5));
    pcm_fifo #(.AW(11)) u_pcm (.clk(clk), .rst(rst), .flush(flush), .push(push), .pdata(pdata), .full(full),
        .empty(empty), .level(level), .rate_inc(rate_inc), .out_l(out_l), .out_r(out_r), .underrun(underrun),
        .sample_tick(sample_tick));

    wire mclk, lrck, dac;
    sound_i2s #(.CHANNEL_WIDTH(16), .SIGNED_INPUT(1)) u_i2s (.clk_mclk(clk_mclk), .clk_audio(clk),
        .audio_l(out_l), .audio_r(out_r), .full16(1'b0), .audio_mclk(mclk), .audio_lrck(lrck), .audio_dac(dac));

    // word about to be serialised: captured at the same edge the serialiser reloads. B-457: the
    // serializer now runs entirely inside clk_mclk, gated on sclk_div==3 (the cycle right before
    // SCLK's own bit falls 3->0 -- the same instant the old prev_audgen_sclk/audgen_sclk edge-detect
    // fired), not a separate clk74-domain edge-detect of a jittery accumulator toggle.
    integer fd, n_rec = 0;
    always @(posedge clk_mclk) begin
        if (u_i2s.sclk_div == 2'd3 && u_i2s.audio_lrck_cnt == 31 && ~u_i2s.audio_lrck) begin
            if (n_rec < N_OUT) begin
                $fdisplay(fd, "%0d %0d", $signed(u_i2s.audgen_sampdata_s[15:0]), $signed(u_i2s.audgen_sampdata_s[31:16]));
                n_rec <= n_rec + 1;
            end
        end
    end
    // Power-up state: Quartus registers start at 0, Icarus starts them at X and ~X stays X, so the serialiser never ran.
    // audio_mclk is now a continuous assign (= clk_mclk directly, B-457), not a register -- nothing to force there.
    initial begin u_i2s.audio_lrck = 0; u_i2s.audio_dac = 0; end
    initial begin
        fd = $fopen("build/rtl/cymo_i2s_rate.txt", "w");
        #200 rst = 0;
        wait (n_rec >= N_OUT);
        $fclose(fd);
        $display("recorded %0d slots, underrun=%0d", n_rec, underrun);
        $finish;
    end
    parameter integer TIMEOUT_NS = 400_000_000;
    initial begin #TIMEOUT_NS; $display("TIMEOUT"); $finish; end
endmodule
