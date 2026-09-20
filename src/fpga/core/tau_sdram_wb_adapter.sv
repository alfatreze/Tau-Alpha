// =============================================================================
// tau_sdram_wb_adapter.sv -- bounded uncached Wishbone-to-SDRAM adapter.
//
// This is intentionally a CLASSIC (CTI=000) one-word slave. It turns exactly
// one CPU bus beat into exactly one existing bridge request, then waits for
// that response before acknowledging the beat. It does not accept cache-line
// bursts: a cached window needs its own beat-decomposing adapter so scanout
// retains an arbitration point between every 32-bit operation.
// =============================================================================
`default_nettype none

module tau_sdram_wb_adapter (
    input  wire        clk,
    input  wire        rst,

    // Uncached Wishbone slave side (word address already translated to SDRAM)
    input  wire        wb_cyc,
    input  wire        wb_stb,
    input  wire        wb_we,
    input  wire [2:0]  wb_cti,
    input  wire [24:0] wb_sdram_addr,
    input  wire [31:0] wb_wdata,
    input  wire [3:0]  wb_sel,
    output reg  [31:0] wb_rdata,
    output reg         wb_ack,
    output wire        wb_unsupported,

    // Existing one-outstanding CPU bridge interface
    output wire        bridge_req,
    output reg         bridge_write,
    output reg  [24:0] bridge_addr,
    output reg  [31:0] bridge_wdata,
    output reg  [3:0]  bridge_byte_en,
    input  wire        bridge_accept,
    input  wire        bridge_done,
    input  wire [31:0] bridge_rdata
);
    localparam [2:0] S_IDLE = 3'd0, S_ISSUE = 3'd1, S_WAIT = 3'd2, S_RELEASE = 3'd3,
                     S_RELEASE2 = 3'd4;
    reg [2:0] state;

    wire wb_req = wb_cyc && wb_stb;
    wire wb_classic = (wb_cti == 3'b000);
    assign wb_unsupported = wb_req && !wb_classic;
    assign bridge_req = (state == S_ISSUE);

    always @(posedge clk) begin
        if (rst) begin
            state          <= S_IDLE;
            wb_rdata       <= 32'd0;
            wb_ack         <= 1'b0;
            bridge_write   <= 1'b0;
            bridge_addr    <= 25'd0;
            bridge_wdata   <= 32'd0;
            bridge_byte_en <= 4'd0;
        end else begin
            // Acknowledgement is a single-clock pulse. The bridge request is
            // held in S_ISSUE until the shared-bridge mux accepts it.
            wb_ack       <= 1'b0;
            case (state)
                S_IDLE: if (wb_req && wb_classic) begin
                    bridge_write   <= wb_we;
                    bridge_addr    <= wb_sdram_addr;
                    bridge_wdata   <= wb_wdata;
                    bridge_byte_en <= wb_sel;
                    state          <= S_ISSUE;
                end
                S_ISSUE: if (bridge_accept) state <= S_WAIT;
                S_WAIT: if (bridge_done) begin
                    wb_rdata <= bridge_rdata;
                    wb_ack   <= 1'b1;
                    // Wishbone masters remove STB after ACK. Waiting for that
                    // release prevents a held request from starting twice.
                    state    <= S_RELEASE;
                end
                // VexRiscv may keep CYC/STB asserted while moving directly
                // to the next CLASSIC beat. Waiting for !wb_req here deadlocks
                // that legal back-to-back form, so return to IDLE after a
                // fixed turnaround instead. It must be TWO cycles: wb_ack is
                // registered again in mp3_soc (dACK), so the CPU still shows
                // the just-completed beat's STB for one cycle after this
                // adapter leaves S_WAIT and one more after S_RELEASE.
                // Returning to IDLE after one cycle (A-060) re-accepted that
                // old beat, issuing a duplicate bridge request whose response
                // then ACKed the NEXT beat with the previous beat's data
                // (A-092 Pocket result; tb_tau_sdram_wb_return_regression).
                S_RELEASE: state <= S_RELEASE2;
                S_RELEASE2: state <= S_IDLE;
                default: state <= S_IDLE;
            endcase
        end
    end
endmodule

`default_nettype wire
