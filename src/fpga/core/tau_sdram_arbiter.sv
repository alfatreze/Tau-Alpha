// =============================================================================
// tau_sdram_arbiter.sv -- framebuffer-priority owner lock for sdram_fb port 0.
//
// The Pocket SDRAM controller is intentionally single-port.  This module does
// not modify its command/state machine: it chooses one caller while the
// controller is idle and holds that choice until p0_ready completes the
// operation.  The framebuffer wins only at a true arbitration point; a CPU
// operation already accepted by the controller is never interrupted.
//
// Phase 1 uses the CPU side for single halfword transfers only.  The bridge
// turns a CPU 32-bit access into two separate requests, restoring a framebuffer
// arbitration point between them.  CPU writes never use sdram_fb's streaming
// source, which remains exclusive to mp3_fb.
// =============================================================================
`default_nettype none

module tau_sdram_arbiter (
    input  wire        clk,
    input  wire        rst,

    // Framebuffer master -----------------------------------------------------
    input  wire [24:0] fb_addr,
    input  wire [15:0] fb_data,
    input  wire [1:0]  fb_byte_en,
    input  wire [10:0] fb_wr_len,
    input  wire        fb_wr_stream,
    input  wire        fb_wr_req,
    input  wire        fb_rd_req,
    input  wire        fb_end_burst_req,
    output wire [15:0] fb_q,
    output wire        fb_available,
    output wire        fb_ready,
    output wire        fb_data_available,
    input  wire [15:0] fb_wsrc_q,
    output wire [10:0] fb_wsrc_addr,

    // CPU bridge master ------------------------------------------------------
    input  wire [24:0] cpu_addr,
    input  wire [15:0] cpu_data,
    input  wire [1:0]  cpu_byte_en,
    input  wire [10:0] cpu_wr_len,
    input  wire        cpu_wr_req,
    input  wire        cpu_rd_req,
    input  wire        cpu_end_burst_req,
    output wire [15:0] cpu_q,
    output wire        cpu_available,
    output wire        cpu_ready,
    output wire        cpu_data_available,
    output wire        cpu_accepted,

    // Diagnostic provenance for the sole controller port. This is asserted
    // combinationally with a CPU-owned request, including its first accepted
    // cycle before `owner` becomes registered.
    output wire        p0_cpu_selected,

    // Sole controller port ---------------------------------------------------
    output wire [24:0] p0_addr,
    output wire [15:0] p0_data,
    output wire [1:0]  p0_byte_en,
    output wire [10:0] p0_wr_len,
    output wire        p0_wr_stream,
    input  wire [15:0] p0_q,
    output wire        p0_wr_req,
    output wire        p0_rd_req,
    output wire        p0_end_burst_req,
    input  wire        p0_available,
    input  wire        p0_ready,
    input  wire        p0_data_available,
    input  wire [10:0] wsrc_addr,
    output wire [15:0] wsrc_q
);

    localparam [1:0] OWNER_NONE = 2'd0, OWNER_FB = 2'd1, OWNER_CPU = 2'd2;
    reg [1:0] owner;

    wire fb_req  = fb_wr_req  | fb_rd_req;
    wire cpu_req = cpu_wr_req | cpu_rd_req;

    // At idle, an incoming framebuffer request wins the same cycle even if a
    // CPU request is also waiting.  Once selected, the owner stays unchanged
    // through data beats and the controller's ready pulse.
    //
    // Do NOT use p0_available as the acceptance condition. sdram_fb defines
    // it as IDLE && !port_req, so it falls combinationally in the exact cycle
    // a forwarded request is asserted. Requiring it here creates a circular
    // handshake: the controller sees and queues the request while this
    // arbiter/CPU bridge never considers it accepted, holding the request
    // forever. Ownership itself is the accepted-request latch; sdram_fb
    // retains an accepted request internally until it raises p0_ready.
    wire select_fb  = (owner == OWNER_FB) ||
                      ((owner == OWNER_NONE) && fb_req);
    wire select_cpu = (owner == OWNER_CPU) ||
                      ((owner == OWNER_NONE) && !fb_req && cpu_req);

    assign p0_addr          = select_fb ? fb_addr          : cpu_addr;
    assign p0_data          = select_fb ? fb_data          : cpu_data;
    assign p0_byte_en       = select_fb ? fb_byte_en       : cpu_byte_en;
    assign p0_wr_len        = select_fb ? fb_wr_len        : cpu_wr_len;
    assign p0_wr_stream     = select_fb ? fb_wr_stream     : 1'b0;
    assign p0_wr_req        = select_fb ? fb_wr_req        : (select_cpu ? cpu_wr_req        : 1'b0);
    assign p0_rd_req        = select_fb ? fb_rd_req        : (select_cpu ? cpu_rd_req        : 1'b0);
    assign p0_end_burst_req = select_fb ? fb_end_burst_req : (select_cpu ? cpu_end_burst_req : 1'b0);
    assign wsrc_q           = fb_wsrc_q;
    assign fb_wsrc_addr     = wsrc_addr;

    // `available` is only meaningful before a request is accepted.  Do not
    // expose it while another owner is in-flight: mp3_fb uses it to decide when
    // it may assert a one-cycle request pulse.
    assign fb_available  = (owner == OWNER_NONE) && p0_available;
    assign cpu_available = (owner == OWNER_NONE) && p0_available;
    assign cpu_accepted  = (owner == OWNER_NONE) && !fb_req && cpu_req;
    assign p0_cpu_selected = select_cpu;

    assign fb_q              = p0_q;
    assign cpu_q             = p0_q;
    assign fb_ready          = (owner == OWNER_FB)  && p0_ready;
    assign cpu_ready         = (owner == OWNER_CPU) && p0_ready;
    assign fb_data_available = (owner == OWNER_FB)  && p0_data_available;
    assign cpu_data_available= (owner == OWNER_CPU) && p0_data_available;

    always @(posedge clk) begin
        if (rst) begin
            owner <= OWNER_NONE;
        end else if (owner == OWNER_NONE) begin
            if (fb_req)       owner <= OWNER_FB;
            else if (cpu_req) owner <= OWNER_CPU;
        end else if (p0_ready) begin
            owner <= OWNER_NONE;
        end
    end

endmodule

`default_nettype wire
