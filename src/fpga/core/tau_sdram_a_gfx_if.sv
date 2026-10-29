// Tau Alpha SDRAM scheduler path: client adapter for the graphics master (tau_gfx / tau_talos). MIT licence.
// Written from docs/features/SDRAM_CONTROLLER_CONTRACT.md section 4 and section 6, docs/features/TALOS_T3_SPEC.md and the
// Talos source (the client's own protocol). No code of the old controller (tau_sdram_ctrl.sv) was used.
//
// The graphics master speaks the player's p0 protocol: ONE request at a time (a one-cycle wr_req / rd_req pulse with the
// address, length and data on the p0_* registers), a read burst ended by the client (p0_end_burst_req), a streamed write
// whose words the controller pulls from wsrc_addr / wsrc_q (one cycle read latency), p0_available while idle, p0_ready at
// the end of a request. This module turns that into two scheduler-protocol ports (section 4):
//   port 0 = scan-out FILL (a read the master marks with p0_is_fill; the scheduler's urgent port),
//   port 1 = the draw engine (every other read and every write).
// Because the master keeps at most one request outstanding, one set of latched fields serves both ports and only the
// valid / port select differs. A read's length comes from the sideband p0_rd_len (a scheduler needs it up front; the
// master consumes exactly that many words and then raises p0_end_burst_req, which is therefore ignored here).
//
// Request path: in the cycle of the pulse the live p0_* fields go straight to the port (no register in between: one cycle
// of latency saved on every short engine row); if the port is not ready the fields are latched and held.
// Write data: w_idx of port 1 drives wsrc_addr; wsrc_q (valid the next cycle) is registered once more so that w_data
// is presented exactly W_LEAD = 2 cycles after w_next (contract section 4). A constant write holds p0_data.
`default_nettype none
module tau_sdram_a_gfx_if (
    input  wire         clk,
    input  wire         rst,
    input  wire         init_done,
    // the master's p0 protocol
    input  wire [24:0]  p0_addr,
    input  wire [15:0]  p0_data,
    input  wire [1:0]   p0_byte_en,
    input  wire [10:0]  p0_wr_len,
    input  wire         p0_wr_stream,
    input  wire         p0_wr_req,
    input  wire         p0_rd_req,
    input  wire         p0_end_burst_req,   // ignored (the length is known up front)
    input  wire         p0_is_fill,
    input  wire [9:0]   p0_rd_len,
    output wire [15:0]  p0_q,
    output wire         p0_available,
    output wire         p0_ready,
    output wire         p0_data_available,
    output wire [10:0]  wsrc_addr,
    input  wire [15:0]  wsrc_q,
    // scheduler ports 0 (fill) and 1 (engine); the request fields are shared (one request at a time)
    output wire [1:0]   req_valid,
    input  wire [1:0]   req_ready,
    output wire         req_we,
    output wire [24:0]  req_addr,
    output wire [9:0]   req_len,
    input  wire [1:0]   w_next,
    input  wire [19:0]  w_idx,
    output wire [15:0]  w_data,
    output wire [1:0]   w_be,
    input  wire [1:0]   r_valid,
    input  wire [31:0]  r_data,
    input  wire [1:0]   r_last,
    input  wire [1:0]   done
);
    reg         pend, outst;
    reg         l_we, l_fill, l_stream, o_port;
    reg  [24:0] l_addr;
    reg  [9:0]  l_len;
    reg  [15:0] l_data;
    reg  [1:0]  l_be;

    wire        pulse    = (p0_wr_req | p0_rd_req) && !pend && !outst;
    // a write longer than the scheduler's 512 words is made a rejected request (len 0: it completes at once, counted bad)
    wire [9:0]  live_wlen = (p0_wr_len[10] || p0_wr_len[9:0] > 10'd512) ? 10'd0 : p0_wr_len[9:0];
    wire        live_we   = p0_wr_req;
`ifdef MUT_A_RDLEN
    wire [9:0]  live_len  = live_we ? live_wlen : (p0_rd_len > 10'd1 ? p0_rd_len - 10'd1 : p0_rd_len);
`else
    wire [9:0]  live_len  = live_we ? live_wlen : p0_rd_len;
`endif
`ifdef MUT_A_NOFILLPORT
    wire        live_fill = 1'b0;
`else
    wire        live_fill = p0_rd_req && p0_is_fill;
`endif

    wire        c_fill = pend ? l_fill : live_fill;
    assign req_we   = pend ? l_we   : live_we;
    assign req_addr = pend ? l_addr : p0_addr;
    assign req_len  = pend ? l_len  : live_len;
    wire        go = pulse | pend;
    assign req_valid = {go & ~c_fill, go & c_fill};
    wire        c_ready = c_fill ? req_ready[0] : req_ready[1];

    always @(posedge clk) begin
        if (rst) begin
            pend <= 1'b0; outst <= 1'b0; o_port <= 1'b0;
            l_we <= 1'b0; l_fill <= 1'b0; l_stream <= 1'b0; l_addr <= 25'd0; l_len <= 10'd0; l_data <= 16'd0; l_be <= 2'b11;
        end else begin
            if (pulse) begin
                l_we <= live_we; l_fill <= live_fill; l_stream <= p0_wr_stream && live_we; l_addr <= p0_addr; l_len <= live_len;
                l_data <= p0_data; l_be <= p0_byte_en;
            end
            if (go) begin
                if (c_ready) begin pend <= 1'b0; outst <= 1'b1; o_port <= ~c_fill; end
                else pend <= 1'b1;
            end
            if (outst && (|done)) outst <= 1'b0;
        end
    end

    // read return and completion
    assign p0_q              = o_port ? r_data[31:16] : r_data[15:0];
    assign p0_data_available = outst & (o_port ? r_valid[1] : r_valid[0]);
    assign p0_ready          = outst & (|done);
`ifdef MUT_A_AVAIL
    assign p0_available      = init_done & ~pend & ~rst;
`else
    assign p0_available      = init_done & ~pend & ~outst & ~rst;
`endif

    // write data: wsrc_addr follows the pull of the engine port, the data is registered once more (W_LEAD 2)
    assign wsrc_addr = {1'b0, w_idx[19:10]};
    reg [15:0] wd_q;
`ifdef MUT_A_WLEAD
    assign w_data = l_stream ? wsrc_q : l_data;       // one cycle late relative to W_LEAD: the data of the previous word
`else
    always @(posedge clk) wd_q <= l_stream ? wsrc_q : l_data;
    assign w_data = wd_q;
`endif
    assign w_be   = l_be;

    wire _unused = &{1'b0, p0_end_burst_req, w_next, w_idx[9:0], r_last};
endmodule
`default_nettype wire
