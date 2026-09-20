// =============================================================================
// tau_psram_bus.sv -- classic (CTI=000) Wishbone slave in front of
// tau_psram_async. One CPU beat = one controller request. ACK/ERR are pulsed
// only after the response register has been loaded from the controller's held
// response (KB-008), and the adapter returns to idle two cycles after ACK
// because mp3_soc registers ACK once more (KB-024, A-092/A-093).
// =============================================================================
`default_nettype none

module tau_psram_bus #(
    parameter REL_CYC        = 2,  // test hook: 1 reproduces the A-092 duplicate-request bug
    parameter MUT_EARLY_ACK  = 0   // test hook: 1 pulses ACK before the response register is loaded
) (
    input  wire        clk,
    input  wire        rst,

    input  wire        wb_cyc,
    input  wire        wb_stb,
    input  wire        wb_we,
    input  wire [2:0]  wb_cti,
    input  wire [22:0] wb_adr,         // CPU word offset inside the PSRAM window
    input  wire [31:0] wb_dat_i,
    input  wire [3:0]  wb_sel,
    output reg  [31:0] wb_dat_o,       // registered, never combinational
    output reg         wb_ack,
    output reg         wb_err,         // guard word refused
    output wire        wb_unsupported,

    output wire        ctl_req,
    output reg         ctl_we,
    output reg  [22:0] ctl_word,
    output reg  [31:0] ctl_wdata,
    output reg  [3:0]  ctl_be,
    input  wire        ctl_done,
    input  wire [31:0] ctl_rdata,
    input  wire        ctl_guard
);
    localparam [2:0] S_IDLE = 3'd0, S_REQ = 3'd1, S_REL1 = 3'd2, S_REL2 = 3'd3, S_LATE = 3'd4;
    reg [2:0] state;
    reg       late_guard;

    wire wb_req     = wb_cyc && wb_stb;
    wire wb_classic = (wb_cti == 3'b000);
    assign wb_unsupported = wb_req && !wb_classic;
    assign ctl_req = (state == S_REQ);

    always @(posedge clk) begin
        wb_ack <= 1'b0;
        wb_err <= 1'b0;
        if (rst) begin
            state <= S_IDLE; wb_dat_o <= 32'd0; late_guard <= 1'b0;
            ctl_we <= 1'b0; ctl_word <= 23'd0; ctl_wdata <= 32'd0; ctl_be <= 4'd0;
        end else case (state)
            S_IDLE: if (wb_req && wb_classic) begin
                ctl_we <= wb_we; ctl_word <= wb_adr; ctl_wdata <= wb_dat_i; ctl_be <= wb_sel;
                state <= S_REQ;
            end
            S_REQ: if (ctl_done) begin
                if (MUT_EARLY_ACK != 0) begin
                    wb_ack     <= !ctl_guard;
                    wb_err     <= ctl_guard;
                    late_guard <= ctl_guard;
                    state      <= S_LATE;         // data loaded one cycle too late
                end else begin
                    wb_dat_o <= ctl_guard ? 32'd0 : ctl_rdata;
                    wb_ack   <= !ctl_guard;
                    wb_err   <= ctl_guard;
                    state    <= S_REL1;
                end
            end
            S_LATE: begin
                wb_dat_o <= late_guard ? 32'd0 : ctl_rdata;
                state    <= S_REL2;
            end
            S_REL1: state <= (REL_CYC == 1) ? S_IDLE : S_REL2;
            S_REL2: state <= S_IDLE;
            default: state <= S_IDLE;
        endcase
    end
endmodule

`default_nettype wire
