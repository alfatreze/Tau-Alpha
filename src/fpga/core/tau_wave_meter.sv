// =============================================================================
// tau_wave_meter.sv -- level and waveform metering in hardware (B-283).
//
// The firmware's meters_feed() scanned every decoded sample for the level meters (max |L|, max |R|) and searched the
// PCM for the oscilloscope's zero-crossing trigger, then sampled 64 columns by hand. This block does both from the PCM
// FIFO's sample strobe, at zero CPU cost, and at a higher resolution than the CPU could afford.
//
//  LEVEL   pk_l / pk_r: running max |sample| since the firmware last cleared them (CTL bit 0). Free-running, so no
//          chunk boundary can be missed. 16 bits unsigned (|-32768| = 32768 fits).
//
//  SCOPE   On ARM (CTL bit 1) it waits for the first rising zero crossing of the mono mix (mid = (L+R)>>1), or gives up
//          after TRIG_MAX samples and captures anyway (flag `timeout`, so a DC or silent signal still draws a line), then
//          records COLS columns. Each column is the MIN and MAX of the mid over SPAN consecutive samples
//          (SPAN = CTL[11:8] + 1, 1..16), packed {min, max} into one 32-bit word of a small RAM. A min/max envelope, not a
//          point sample: point-sampling aliases and the trace jumps about (the same reason the firmware did it).
//          COLS = 256, so a 360 px box gets about one column per pixel; the old path had 64.
//
// Timing style (this project's rule): one comparison per clock, registered result, no chain. Memory: one 256 x 32 array,
// inferred as a single M10K block (a plain synchronous read of one array, the shape Quartus infers reliably here).
// =============================================================================
module tau_wave_meter #(
    parameter COLS     = 256,
    parameter TRIG_MAX = 2048                    // samples to wait for a rising zero crossing before capturing anyway
)(
    input  wire               clk,
    input  wire               rst,
    input  wire               tick,              // one pulse per input sample
    input  wire signed [15:0] in_l,
    input  wire signed [15:0] in_r,
    input  wire               ctl_we,
    input  wire        [11:0] ctl_data,          // bit 0: clear peaks; bit 1: arm scope; [11:8]: span - 1
    input  wire               idx_we,
    input  wire        [7:0]  idx_data,          // column to present on rd_data
    output wire        [31:0] rd_data,           // {min[15:0], max[15:0]} of the selected column
    output reg         [15:0] pk_l,
    output reg         [15:0] pk_r,
    output wire        [31:0] status             // [0] 1 (present), [1] busy, [2] timeout, [15:8] capture sequence number
);
    reg  [31:0] mem [0:COLS-1];
    reg  [7:0]  rd_addr;
    reg  [31:0] rd_q;
    assign rd_data = rd_q;

    localparam [1:0] S_IDLE = 2'd0, S_TRIG = 2'd1, S_CAP = 2'd2;
    reg [1:0]  st;
    reg [3:0]  span;
    reg [3:0]  sub;                              // sample within the column
    reg [8:0]  col;
    reg [11:0] tcnt;
    reg        prev_neg;
    reg        timeout;
    reg [7:0]  seq;
    reg signed [15:0] cmin, cmax;

    wire signed [16:0] sum   = in_l + in_r;
    wire signed [15:0] mid   = sum[16:1];
    wire        [15:0] abs_l = in_l[15] ? (16'd0 - in_l) : in_l;
    wire        [15:0] abs_r = in_r[15] ? (16'd0 - in_r) : in_r;

    wire trig_hit = (st == S_TRIG) && ((prev_neg && !mid[15]) || (tcnt == TRIG_MAX - 1));
    wire active   = (st == S_CAP) || trig_hit;
    wire first    = (st == S_TRIG) || (sub == 4'd0);          // first sample of a column
    wire signed [15:0] nmin = first ? mid : ((mid < cmin) ? mid : cmin);
    wire signed [15:0] nmax = first ? mid : ((mid > cmax) ? mid : cmax);
    wire [3:0] sub_now = (st == S_TRIG) ? 4'd0 : sub;
    wire col_end  = (sub_now == span);
    wire wr       = tick && active && col_end;
    wire [7:0] wr_addr = (st == S_TRIG) ? 8'd0 : col[7:0];

    always @(posedge clk) begin
        if (wr) mem[wr_addr] <= {nmin[15:0], nmax[15:0]};
        rd_q <= mem[rd_addr];
    end

    always @(posedge clk) begin
        if (rst) begin
            st <= S_IDLE; span <= 4'd3; sub <= 4'd0; col <= 9'd0; tcnt <= 12'd0; prev_neg <= 1'b0;
            timeout <= 1'b0; seq <= 8'd0; pk_l <= 16'd0; pk_r <= 16'd0; cmin <= 16'sd0; cmax <= 16'sd0; rd_addr <= 8'd0;
        end else begin
            if (idx_we) rd_addr <= idx_data;
            if (ctl_we && ctl_data[0]) begin pk_l <= 16'd0; pk_r <= 16'd0; end
            if (ctl_we && ctl_data[1]) begin
                st <= S_TRIG; span <= ctl_data[11:8]; tcnt <= 12'd0; prev_neg <= 1'b0; timeout <= 1'b0;
                sub <= 4'd0; col <= 9'd0;
            end
            if (tick) begin
                if (abs_l > pk_l) pk_l <= abs_l;
                if (abs_r > pk_r) pk_r <= abs_r;
                if (st == S_TRIG) begin
                    prev_neg <= mid[15];
                    tcnt     <= tcnt + 12'd1;
                end
                if (active) begin
                    cmin <= nmin; cmax <= nmax;
                    if (trig_hit && !(prev_neg && !mid[15])) timeout <= 1'b1;
                    if (col_end) begin
                        sub <= 4'd0;
                        if ((st == S_TRIG ? 9'd0 : col) == COLS - 1) begin
                            st <= S_IDLE; seq <= seq + 8'd1;
                        end else begin
                            col <= (st == S_TRIG ? 9'd0 : col) + 9'd1;
                            st  <= S_CAP;
                        end
                    end else begin
                        sub <= sub_now + 4'd1;
                        st  <= S_CAP;
                    end
                end
            end
        end
    end
    assign status = {16'd0, seq, 5'd0, timeout, (st != S_IDLE), 1'b1};
endmodule
