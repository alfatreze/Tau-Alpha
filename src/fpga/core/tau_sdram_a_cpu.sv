// Tau Alpha SDRAM scheduler path: the CPU bridge for scheduler port 2. MIT licence.
// The system-clock half is the mailbox of tau_sdram_cpu_bridge.sv (the same project file's logic: a request payload held
// stable for the whole trip, a toggle synchronised for three destination clocks, the response word sampled when the
// synchronised completion toggle arrives). The SDRAM-clock half is new: one 32-bit access is ONE scheduler request of
// two words (a write with the four byte enables as two 2-bit enables, a read returning two words), acknowledged on
// `done`; the old bridge made two separate 16-bit controller operations to give the scan-out an arbitration point in
// between, which the scheduler's urgent port does not need.
//
// Single-word latency: the request is presented in the very cycle the synchronised toggle is seen (the payload is stable
// since before the toggle, so no register is taken); the write data is selected by w_idx and delayed W_LEAD = 2 cycles.
// The debug outputs of the old bridge are not provided (they belong to the old controller's probe).
`default_nettype none
module tau_sdram_a_cpu (
    input  wire        clk_sys,
    input  wire        rst_sys,
    input  wire        sys_start,
    input  wire        sys_wb_start,        // diagnostic provenance only (unused)
    input  wire        sys_write,
    input  wire [24:0] sys_addr,
    input  wire [31:0] sys_wdata,
    input  wire [3:0]  sys_byte_en,
    output reg         sys_busy,
    output reg         sys_done,
    output reg  [31:0] sys_rdata,

    input  wire        clk_sdram,
    input  wire        rst_sdram,
    // scheduler port (contract section 4)
    output wire        req_valid,
    input  wire        req_ready,
    output wire        req_we,
    output wire [24:0] req_addr,
    output wire [9:0]  req_len,
    input  wire        w_next,
    input  wire [9:0]  w_idx,
    output wire [15:0] w_data,
    output wire [1:0]  w_be,
    input  wire        r_valid,
    input  wire [15:0] r_data,
    input  wire        r_last,
    input  wire        done
);
    // ---------------- system domain mailbox (as tau_sdram_cpu_bridge) ----------------
    reg        req_write_sys;
    reg [24:0] req_addr_sys;
    reg [31:0] req_wdata_sys;
    reg [3:0]  req_byte_en_sys;
    reg        req_toggle_sys;
    reg [2:0]  rsp_sync_sys;
    reg        rsp_seen_sys;
    reg [31:0] rsp_data_sdram;
    reg        rsp_toggle_sdram;

    always @(posedge clk_sys) begin
        if (rst_sys) begin
            req_write_sys <= 1'b0; req_addr_sys <= 25'd0; req_wdata_sys <= 32'd0; req_byte_en_sys <= 4'd0;
            req_toggle_sys <= 1'b0; rsp_sync_sys <= 3'b000; rsp_seen_sys <= 1'b0;
            sys_busy <= 1'b0; sys_done <= 1'b0; sys_rdata <= 32'd0;
        end else begin
            rsp_sync_sys <= {rsp_sync_sys[1:0], rsp_toggle_sdram};
            sys_done <= 1'b0;
            if (sys_start && !sys_busy) begin
                req_write_sys <= sys_write; req_addr_sys <= sys_addr; req_wdata_sys <= sys_wdata;
                req_byte_en_sys <= sys_byte_en; req_toggle_sys <= ~req_toggle_sys; sys_busy <= 1'b1;
            end
            if (sys_busy && (rsp_sync_sys[2] != rsp_seen_sys)) begin
                rsp_seen_sys <= rsp_sync_sys[2]; sys_rdata <= rsp_data_sdram; sys_busy <= 1'b0; sys_done <= 1'b1;
            end
        end
    end

    // ---------------- SDRAM domain ----------------
    reg [2:0]  req_sync_sdram;
    reg        req_seen_sdram;
    reg        st_req, st_wait;          // request presented and not yet accepted / accepted, waiting for done
    reg [15:0] rd_lo;
    reg        rd_hi_pending;            // the low word has been taken
    reg        wi1, wi2;

    wire new_req = (req_sync_sdram[2] != req_seen_sdram) && !st_req && !st_wait;
    assign req_valid = new_req | st_req;
    assign req_we    = req_write_sys;
    assign req_addr  = req_addr_sys;
    assign req_len   = 10'd2;
    assign w_data    = wi2 ? req_wdata_sys[31:16] : req_wdata_sys[15:0];
`ifdef MUT_A_CPUBE
    assign w_be      = wi2 ? req_byte_en_sys[1:0] : req_byte_en_sys[3:2];
`else
    assign w_be      = wi2 ? req_byte_en_sys[3:2] : req_byte_en_sys[1:0];
`endif

    always @(posedge clk_sdram) begin
        wi1 <= w_idx[0]; wi2 <= wi1;
        if (rst_sdram) begin
            req_sync_sdram <= 3'b000; req_seen_sdram <= 1'b0; st_req <= 1'b0; st_wait <= 1'b0;
            rsp_data_sdram <= 32'd0; rsp_toggle_sdram <= 1'b0; rd_lo <= 16'd0; rd_hi_pending <= 1'b0;
        end else begin
            req_sync_sdram <= {req_sync_sdram[1:0], req_toggle_sys};
            if (new_req) begin
                req_seen_sdram <= req_sync_sdram[2];
                rd_hi_pending <= 1'b0;
                if (req_ready) st_wait <= 1'b1; else st_req <= 1'b1;
            end else if (st_req && req_ready) begin
                st_req <= 1'b0; st_wait <= 1'b1;
            end
            if (st_wait) begin
                if (r_valid && !rd_hi_pending) begin rd_lo <= r_data; rd_hi_pending <= 1'b1; end
                if (done) begin
                    st_wait <= 1'b0;
                    rsp_data_sdram <= req_write_sys ? 32'd0 : {r_data, rd_lo};   // done coincides with the last (high) word of a read
                    rsp_toggle_sdram <= ~rsp_toggle_sdram;
                end
            end
        end
    end
    wire _unused = &{1'b0, sys_wb_start, w_next, r_last};
endmodule
`default_nettype wire
