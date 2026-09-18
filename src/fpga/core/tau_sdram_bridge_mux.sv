// Shared bridge owner lock for diagnostic MMIO and the Phase 2 uncached path.
`default_nettype none
module tau_sdram_bridge_mux (
    input wire clk, input wire rst,
    input wire diag_req, input wire diag_write, input wire [24:0] diag_addr,
    input wire [31:0] diag_wdata, input wire [3:0] diag_be,
    input wire wb_req, input wire wb_write, input wire [24:0] wb_addr,
    input wire [31:0] wb_wdata, input wire [3:0] wb_be,
    output reg wb_accept, output reg diag_done, output reg wb_done,
    output reg [31:0] diag_rdata, output reg [31:0] wb_rdata,
    output wire busy,
    output reg bridge_start, output reg bridge_write, output reg [24:0] bridge_addr,
    output reg [31:0] bridge_wdata, output reg [3:0] bridge_be,
    // Diagnostic provenance: high only with a bridge_start owned by WB.
    output reg bridge_wb_start,
    input wire bridge_busy, input wire bridge_done, input wire [31:0] bridge_rdata
);
    localparam [1:0] NONE=0, DIAG=1, WB=2;
    reg [1:0] owner;
    assign busy = (owner != NONE) || bridge_busy;
    always @(posedge clk) begin
        if (rst) begin
            owner <= NONE; wb_accept <= 0; diag_done <= 0; wb_done <= 0;
            diag_rdata <= 0; wb_rdata <= 0; bridge_start <= 0; bridge_write <= 0;
            bridge_wb_start <= 0;
            bridge_addr <= 0; bridge_wdata <= 0; bridge_be <= 0;
        end else begin
            wb_accept <= 0; diag_done <= 0; wb_done <= 0; bridge_start <= 0;
            bridge_wb_start <= 0;
            if (owner == NONE && !bridge_busy) begin
                if (diag_req) begin
                    owner <= DIAG; bridge_start <= 1; bridge_write <= diag_write;
                    bridge_addr <= diag_addr; bridge_wdata <= diag_wdata; bridge_be <= diag_be;
                end else if (wb_req) begin
                    owner <= WB; wb_accept <= 1; bridge_start <= 1; bridge_wb_start <= 1; bridge_write <= wb_write;
                    bridge_addr <= wb_addr; bridge_wdata <= wb_wdata; bridge_be <= wb_be;
                end
            end
            if (bridge_done) begin
                if (owner == DIAG) begin diag_done <= 1; diag_rdata <= bridge_rdata; end
                if (owner == WB) begin wb_done <= 1; wb_rdata <= bridge_rdata; end
                owner <= NONE;
            end
        end
    end
endmodule
`default_nettype wire
