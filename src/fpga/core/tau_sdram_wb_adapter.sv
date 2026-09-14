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
    output reg         bridge_start,
    output reg         bridge_write,
    output reg  [24:0] bridge_addr,
    output reg  [31:0] bridge_wdata,
    output reg  [3:0]  bridge_byte_en,
    input  wire        bridge_busy,
    input  wire        bridge_done,
    input  wire [31:0] bridge_rdata
);
    localparam [1:0] S_IDLE = 2'd0, S_WAIT = 2'd1, S_RELEASE = 2'd2;
    reg [1:0] state;

    wire wb_req = wb_cyc && wb_stb;
    wire wb_classic = (wb_cti == 3'b000);
    assign wb_unsupported = wb_req && !wb_classic;

    always @(posedge clk) begin
        if (rst) begin
            state          <= S_IDLE;
            wb_rdata       <= 32'd0;
            wb_ack         <= 1'b0;
            bridge_start   <= 1'b0;
            bridge_write   <= 1'b0;
            bridge_addr    <= 25'd0;
            bridge_wdata   <= 32'd0;
            bridge_byte_en <= 4'd0;
        end else begin
            // Both acknowledgement and bridge-start are single-clock pulses.
            wb_ack       <= 1'b0;
            bridge_start <= 1'b0;
            case (state)
                S_IDLE: if (wb_req && wb_classic && !bridge_busy) begin
                    bridge_write   <= wb_we;
                    bridge_addr    <= wb_sdram_addr;
                    bridge_wdata   <= wb_wdata;
                    bridge_byte_en <= wb_sel;
                    bridge_start   <= 1'b1;
                    state          <= S_WAIT;
                end
                S_WAIT: if (bridge_done) begin
                    wb_rdata <= bridge_rdata;
                    wb_ack   <= 1'b1;
                    // Wishbone masters remove STB after ACK. Waiting for that
                    // release prevents a held request from starting twice.
                    state    <= S_RELEASE;
                end
                S_RELEASE: if (!wb_req) state <= S_IDLE;
                default: state <= S_IDLE;
            endcase
        end
    end
endmodule

`default_nettype wire
