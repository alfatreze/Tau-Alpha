// tb_sound_i2s_full16.v -- B-602 (Cymo C3, F2): the Pocket I2S slot word for full16 = 0 and 1.
// full16 = 0 must be the ORIGINAL mapping {sign, audio[15:1]} (the input shifted right by one) for every input; full16 = 1 must carry the whole
// 16-bit word. Checked on the combinational word audgen_sampdata (left in [15:0], right in [31:16]).
`timescale 1ns/1ps
module dcfifo (
    input  wire [31:0] data, input wire rdclk, input wire rdreq, input wire wrclk, input wire wrreq,
    output reg  [31:0] q, output wire rdempty, input wire aclr, output wire eccstatus, output wire rdfull, output wire [1:0] rdusedw,
    output wire wrempty, output wire wrfull, output wire [1:0] wrusedw
);
    parameter intended_device_family = "", lpm_numwords = 4, lpm_showahead = "OFF", lpm_type = "", lpm_width = 32, lpm_widthu = 2, overflow_checking = "ON",
              rdsync_delaypipe = 5, underflow_checking = "ON", use_eab = "ON", wrsync_delaypipe = 5, lpm_hint = "";
    initial q = 0;
    assign rdempty = 1'b1, eccstatus = 0, rdfull = 0, rdusedw = 0, wrempty = 0, wrfull = 0, wrusedw = 0;
endmodule

module tb_sound_i2s_full16;
    reg clk = 0, clk_mclk = 0;
    always #7.5 clk = ~clk;
    always #40.69 clk_mclk = ~clk_mclk;
    reg signed [15:0] l = 0, r = 0;
    reg full16 = 0;
    wire mclk, lrck, dac;
    sound_i2s #(.CHANNEL_WIDTH(16), .SIGNED_INPUT(1)) u_i2s (.clk_mclk(clk_mclk), .clk_audio(clk), .audio_l(l), .audio_r(r), .full16(full16),
                                                           .audio_mclk(mclk), .audio_lrck(lrck), .audio_dac(dac));
    integer i, bad = 0, n = 0;
    reg [15:0] want_l, want_r;
    task check(input [15:0] a, input [15:0] b, input f);
        begin
            l = a; r = b; full16 = f; #1;
            if (f) begin want_l = a; want_r = b; end
            else   begin want_l = {a[15], a[15:1]}; want_r = {b[15], b[15:1]}; end
            n = n + 1;
            if (u_i2s.audgen_sampdata[15:0] !== want_l || u_i2s.audgen_sampdata[31:16] !== want_r) begin
                bad = bad + 1;
                if (bad < 6) $display("FAIL full16=%0d in %h %h got %h %h want %h %h", f, a, b, u_i2s.audgen_sampdata[15:0], u_i2s.audgen_sampdata[31:16], want_l, want_r);
            end
        end
    endtask
    initial begin
        for (i = 0; i < 4000; i = i + 1) begin
            check($random, $random, 1'b0);
            check($random, $random, 1'b1);
        end
        check(16'h8000, 16'h7FFF, 1'b0); check(16'h8000, 16'h7FFF, 1'b1);
        check(16'h0001, 16'hFFFF, 1'b0); check(16'h0001, 16'hFFFF, 1'b1);
        $display("%s: %0d cases, %0d failures", bad ? "FAIL" : "PASS", n, bad);
        $finish;
    end
endmodule
