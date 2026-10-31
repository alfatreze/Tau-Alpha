// =============================================================================
// tau_sdr_stat_win.sv -- a read window for the per-port SDRAM statistics of tau_sdram_sched_v (CNT_EN = 1), from clk_sdram
// into the CPU's clock domain (tau_soc.v R_SDA_SEL 0x1D4 / R_SDA_VAL 0x1D8, docs/MMIO_ALLOCATION.md).
//
// The counters (3 ports x requests, words, busy cycles: 32 bits; worst wait, urgent-over-bound, bad requests: 16 bits) run
// freely in clk_sdram. The CPU selects one with a write of {port[1:0], stat[2:0]} to R_SDA_SEL:
//     stat 0 requests, 1 words, 2 busy cycles, 3 worst wait, 4 urgent over the bound, 5 bad requests; port 0 scan-out fill,
//     1 draw engine, 2 CPU. A selection outside that range reads 0.
// and reads the value from R_SDA_VAL once R_SDA_SEL bit 8 (pending) has dropped. The value is a SNAPSHOT taken in clk_sdram
// at the moment the request arrives: it does not move until the next selection, so two reads agree.
//
// Why a handshake and not tau_cdc_gray_bus on the live counter (the proposal in docs/features/SDRAM_SCHED_A_NOTES.md
// section 5): a Gray code only protects a value that moves by +1 per step. `words` steps by a burst length, `worst wait`
// and `urgent over` jump, and a change of the SELECTION itself is an arbitrary jump of the carried word, so a Gray-coded
// live bus could deliver a value the counter never held. The snapshot is registered in clk_sdram, held still, and only
// then read, so the crossing carries a STABLE multi-bit word: request toggle (clk_sys -> clk_sdram), snapshot loaded on its
// edge, acknowledge toggle (clk_sdram -> clk_sys), the word captured in clk_sys on the acknowledge edge. About 100 flops
// (the sketch said Gray bus plus counters: the counters themselves are the sched_v registers, which CNT_EN adds in any case).
//
// A selection written while one is pending is ignored (two toggles inside the synchroniser delay would cancel): firmware
// polls `pending` (bit 8) before each write. After reset the window answers `pending = 0`.
//
// BUG is a mutation hook for sim/tb_sdr_stat_win.v (nonzero must make it fail): 1 = the value is the LIVE selected counter
// (unsynchronised, moves between two reads), 2 = a selection is accepted while one is pending (the toggles can cancel).
// =============================================================================
`default_nettype none

module tau_sdr_stat_win #(
    parameter BUG = 0
) (
    input  wire        clk_sys,
    input  wire        rst_sys,
    input  wire        sel_we,                 // clk_sys: R_SDA_SEL written
    input  wire [4:0]  sel_wd,                 // clk_sys: {port[1:0], stat[2:0]}
    output wire [31:0] status,                 // clk_sys: R_SDA_SEL read {1, 22'd0, pending, 3'd0, sel}
    output wire [31:0] val,                    // clk_sys: R_SDA_VAL read

    input  wire        clk_sdram,
    input  wire [3*32-1:0] cnt_requests,
    input  wire [3*32-1:0] cnt_words,
    input  wire [3*32-1:0] cnt_busy,
    input  wire [3*16-1:0] cnt_bad,
    input  wire [3*16-1:0] cnt_worst_wait,
    input  wire [3*16-1:0] cnt_urgent_over
);
    // ---- clk_sys: selection, request toggle, acknowledge synchroniser ------------------------------------------------
    reg [4:0] sel = 5'd0;
    reg       req_tgl = 1'b0;
    reg [2:0] ack_sync = 3'b000;
    reg       ack_seen = 1'b0;                  // the acknowledge toggle value that matches the last request answered
    wire      ack_edge = ack_sync[2] ^ ack_sync[1];
    wire      pending  = (req_tgl != ack_seen);
    reg [31:0] val_q = 32'd0;
    wire [31:0] snap;                           // clk_sdram: the snapshot register (held still while no request is in flight)
    wire        ack_tgl;                        // clk_sdram: acknowledge toggle

    always @(posedge clk_sys) begin
        ack_sync <= {ack_sync[1:0], ack_tgl};
        if (rst_sys) begin
            sel <= 5'd0;
        end else if (sel_we && (!pending || BUG == 2)) begin
            sel     <= sel_wd;
            req_tgl <= ~req_tgl;
        end
        if (ack_edge) begin
            val_q    <= snap;                   // the snapshot has been still for at least three clk_sys cycles
            ack_seen <= ack_sync[1];            // the newer of the two compared values: what the acknowledge toggle now says
        end
    end

    // ---- clk_sdram: request synchroniser, snapshot, acknowledge toggle ---------------------------------------------
    reg [2:0]  req_sync = 3'b000;
    reg [31:0] snap_q = 32'd0;
    reg        ack_q = 1'b0;
    wire       req_edge = req_sync[2] ^ req_sync[1];
    reg [31:0] live;
    always @* begin
        live = 32'd0;
        case (sel[2:0])
            3'd0: live = (sel[4:3] == 2'd3) ? 32'd0 : cnt_requests   [sel[4:3]*32 +: 32];
            3'd1: live = (sel[4:3] == 2'd3) ? 32'd0 : cnt_words      [sel[4:3]*32 +: 32];
            3'd2: live = (sel[4:3] == 2'd3) ? 32'd0 : cnt_busy       [sel[4:3]*32 +: 32];
            3'd3: live = (sel[4:3] == 2'd3) ? 32'd0 : {16'd0, cnt_worst_wait [sel[4:3]*16 +: 16]};
            3'd4: live = (sel[4:3] == 2'd3) ? 32'd0 : {16'd0, cnt_urgent_over[sel[4:3]*16 +: 16]};
            3'd5: live = (sel[4:3] == 2'd3) ? 32'd0 : {16'd0, cnt_bad        [sel[4:3]*16 +: 16]};
            default: live = 32'd0;
        endcase
    end
    always @(posedge clk_sdram) begin
        req_sync <= {req_sync[1:0], req_tgl};
        if (req_edge) begin
            snap_q <= live;                      // `sel` was written at least three clk_sdram cycles before this edge and has not moved since
            ack_q  <= ~ack_q;
        end
    end
    assign snap    = snap_q;
    assign ack_tgl = ack_q;

    assign status = {1'b1, 22'd0, pending, 3'd0, sel};
    assign val    = (BUG == 1) ? live : val_q;
endmodule
