// Behavioural model of the Altera dcfifo megafunction (4 words, showahead off, rdsync_delaypipe 5, underflow ignored), shared by the I2S testbenches (include with -I sim).
// LIMIT: the flag latencies are a reading of the datasheet, not the silicon's. Define USE_ALTERA_MF (and add Quartus' altera_mf.v) to use Intel's own model instead.
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
