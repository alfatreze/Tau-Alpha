// =============================================================================
// tau_sdram_cpu_bridge.sv -- narrow diagnostic CPU-to-SDRAM bridge.
//
// A system-clock request is retained until its response crosses back from the
// SDRAM clock domain.  The request payload is held stable for the full trip;
// the toggle is synchronized for three destination clocks before that payload
// is sampled.  This is a one-outstanding-operation mailbox, not a FIFO.
//
// Each 32-bit request becomes two independent 16-bit controller operations.
// A read explicitly ends each controller read burst after its first word.  The
// arbiter can therefore give scanout a new priority decision between lower and
// upper halfwords instead of allowing a CPU cache-like burst to monopolise the
// controller.
// =============================================================================
`default_nettype none

module tau_sdram_cpu_bridge (
    // CPU/MMIO clock domain ---------------------------------------------------
    input  wire        clk_sys,
    input  wire        rst_sys,
    input  wire        sys_start,
    input  wire        sys_write,
    input  wire [24:0] sys_addr,
    input  wire [31:0] sys_wdata,
    input  wire [3:0]  sys_byte_en,
    output reg         sys_busy,
    output reg         sys_done,
    output reg  [31:0] sys_rdata,

    // SDRAM clock domain -----------------------------------------------------
    input  wire        clk_sdram,
    input  wire        rst_sdram,
    output wire [24:0] m_addr,
    output wire [15:0] m_data,
    output wire [1:0]  m_byte_en,
    output wire [10:0] m_wr_len,
    output wire        m_wr_req,
    output wire        m_rd_req,
    output wire        m_end_burst_req,
    input  wire [15:0] m_q,
    input  wire        m_accepted,
    input  wire        m_ready,
    input  wire        m_data_available
);

    // System domain request mailbox.  It must remain unchanged while busy.
    reg        req_write_sys;
    reg [24:0] req_addr_sys;
    reg [31:0] req_wdata_sys;
    reg [3:0]  req_byte_en_sys;
    reg        req_toggle_sys;
    reg [2:0]  rsp_sync_sys;
    reg        rsp_seen_sys;

    // The response word lives in the SDRAM domain and is stable until the next
    // command, so sampling it when the synchronized completion edge arrives is
    // safe.  This is the matching mailbox half of the stable request payload.
    reg [31:0] rsp_data_sdram;
    reg        rsp_toggle_sdram;

    always @(posedge clk_sys) begin
        if (rst_sys) begin
            req_write_sys  <= 1'b0;
            req_addr_sys   <= 25'd0;
            req_wdata_sys  <= 32'd0;
            req_byte_en_sys<= 4'd0;
            req_toggle_sys <= 1'b0;
            rsp_sync_sys   <= 3'b000;
            rsp_seen_sys   <= 1'b0;
            sys_busy       <= 1'b0;
            sys_done       <= 1'b0;
            sys_rdata      <= 32'd0;
        end else begin
            rsp_sync_sys <= {rsp_sync_sys[1:0], rsp_toggle_sdram};
            sys_done <= 1'b0;
            if (sys_start && !sys_busy) begin
                req_write_sys   <= sys_write;
                req_addr_sys    <= sys_addr;
                req_wdata_sys   <= sys_wdata;
                req_byte_en_sys <= sys_byte_en;
                req_toggle_sys  <= ~req_toggle_sys;
                sys_busy        <= 1'b1;
            end
            if (sys_busy && (rsp_sync_sys[2] != rsp_seen_sys)) begin
                rsp_seen_sys <= rsp_sync_sys[2];
                sys_rdata    <= rsp_data_sdram;
                sys_busy     <= 1'b0;
                sys_done     <= 1'b1;
            end
        end
    end

    // SDRAM domain request sequencer.
    reg [2:0] req_sync_sdram;
    reg       req_seen_sdram;
    reg       op_write;
    reg [24:0] op_addr;
    reg [31:0] op_wdata;
    reg [3:0]  op_byte_en;
    reg [15:0] read_lo, read_hi;

    localparam [3:0]
        S_IDLE       = 4'd0,
        S_WR_LO_REQ  = 4'd1,
        S_WR_LO_WAIT = 4'd2,
        S_WR_HI_REQ  = 4'd3,
        S_WR_HI_WAIT = 4'd4,
        S_RD_LO_REQ  = 4'd5,
        S_RD_LO_DATA = 4'd6,
        S_RD_LO_END  = 4'd7,
        S_RD_HI_REQ  = 4'd8,
        S_RD_HI_DATA = 4'd9,
        S_RD_HI_END  = 4'd10;
    reg [3:0] state;

    wire high_half = (state == S_WR_HI_REQ) || (state == S_WR_HI_WAIT) ||
                     (state == S_RD_HI_REQ) || (state == S_RD_HI_DATA) ||
                     (state == S_RD_HI_END);
    assign m_addr    = op_addr + (high_half ? 25'd1 : 25'd0);
    assign m_data    = high_half ? op_wdata[31:16] : op_wdata[15:0];
    assign m_byte_en = high_half ? op_byte_en[3:2] : op_byte_en[1:0];
    assign m_wr_len  = 11'd1;
    assign m_wr_req  = (state == S_WR_LO_REQ) || (state == S_WR_HI_REQ);
    assign m_rd_req  = (state == S_RD_LO_REQ) || (state == S_RD_HI_REQ);
    assign m_end_burst_req = (state == S_RD_LO_END) || (state == S_RD_HI_END);

    task complete;
        begin
            rsp_data_sdram   <= {read_hi, read_lo};
            rsp_toggle_sdram <= ~rsp_toggle_sdram;
            state             <= S_IDLE;
        end
    endtask

    always @(posedge clk_sdram) begin
        if (rst_sdram) begin
            req_sync_sdram   <= 3'b000;
            req_seen_sdram   <= 1'b0;
            rsp_data_sdram   <= 32'd0;
            rsp_toggle_sdram <= 1'b0;
            op_write         <= 1'b0;
            op_addr          <= 25'd0;
            op_wdata         <= 32'd0;
            op_byte_en       <= 4'd0;
            read_lo          <= 16'd0;
            read_hi          <= 16'd0;
            state            <= S_IDLE;
        end else begin
            req_sync_sdram <= {req_sync_sdram[1:0], req_toggle_sys};
            case (state)
                S_IDLE: begin
                    if (req_sync_sdram[2] != req_seen_sdram) begin
                        req_seen_sdram <= req_sync_sdram[2];
                        // Request payload was stable before the synchronized
                        // toggle edge and remains stable until completion.
                        op_write   <= req_write_sys;
                        op_addr    <= req_addr_sys;
                        op_wdata   <= req_wdata_sys;
                        op_byte_en <= req_byte_en_sys;
                        if (req_write_sys) state <= S_WR_LO_REQ;
                        else               state <= S_RD_LO_REQ;
                    end
                end
                S_WR_LO_REQ:  if (m_accepted) state <= S_WR_LO_WAIT;
                S_WR_LO_WAIT: if (m_ready)    state <= S_WR_HI_REQ;
                S_WR_HI_REQ:  if (m_accepted) state <= S_WR_HI_WAIT;
                S_WR_HI_WAIT: if (m_ready) begin
                    // Reads return data; writes return zero by contract.
                    read_lo <= 16'd0;
                    read_hi <= 16'd0;
                    complete();
                end
                S_RD_LO_REQ:  if (m_accepted)      state <= S_RD_LO_DATA;
                S_RD_LO_DATA: if (m_data_available) begin
                    read_lo <= m_q;
                    state   <= S_RD_LO_END;
                end
                S_RD_LO_END: if (m_ready) state <= S_RD_HI_REQ;
                S_RD_HI_REQ: if (m_accepted)      state <= S_RD_HI_DATA;
                S_RD_HI_DATA: if (m_data_available) begin
                    read_hi <= m_q;
                    state   <= S_RD_HI_END;
                end
                S_RD_HI_END: if (m_ready) complete();
                default: state <= S_IDLE;
            endcase
        end
    end

endmodule

`default_nettype wire
