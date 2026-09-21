// =============================================================================
// tau_psram_probe.sv -- P2 PSRAM diagnostic: firmware-facing MMIO mailbox in
// front of tau_psram_async, plus the four CRAM chip pin groups. Built only with
// TAU_PSRAM_PROBE; it is not part of a product bitstream.
//
// Mailbox (byte offsets inside the mp3_soc MMIO page, allocation table in
// docs/MMIO_ALLOCATION.md). All registers live in clk_sys.
//   0x88 ID     R   0x50535231 ("PSR1"): firmware refuses to run without it
//   0x8C ADDR   RW  [22:0] CPU word offset ([22] chip, [21] die, [20:0] word)
//   0x90 WDATA  RW  32-bit write data
//   0x94 CTRL   W   [0] REQ (ignored while busy), [1] WE, [5:2] byte enables,
//                   [6] CLR: clear sticky flags, TIMEOUT and WAIT samples
//   0x98 STATUS R   [0] BUSY  [1] DONE  [2] TIMEOUT(sticky)  [3] GUARD (last op
//                   refused)  [4] CE_CONFLICT(sticky)  [5] GUARD_HIT(sticky)
//                   [6] WAIT_LO seen  [7] WAIT_HI seen (sampled while OE# low)
//                   [15:8] completed-op counter (low 8 bits)
//   0x9C RDATA  R   read response, loaded at DONE and held until the next DONE
//   0xA0 CFG    RW  [3:0] extra read-capture clocks, [7:4] extra write clocks;
//                   [15:8] read-only: the build's read-sample index T_ACC;
//                   [16] read-only: CPU window present in this build
//   0xA4 LAST   R   [22:0] word, [23] WE, [27:24] byte enables of the last
//                   accepted request (latched when it was accepted)
//   0xA8 COUNT  R   completed mailbox ops, 32 bits
//   0xAC WCOUNT R   completed CPU-window ops, 32 bits (B-016)
//
// The controller is shared with the CPU window (win_* client, driven by the bus
// wrapper in mp3_soc). One owner at a time: the window has priority when both
// are waiting (the CPU is stalled on it); a waiting mailbox request keeps its
// place and runs next. STATUS BUSY covers both.
//
// The request handed to the controller is a snapshot taken at REQ, so a later
// write to ADDR/WDATA cannot change an operation in flight, and RDATA/LAST are
// transaction-locked (KB-008/KB-024 style).
// =============================================================================
`default_nettype none

// Read-sample index of the controller (margin experiment, B-012). Default 9 is the
// shipped value; a build sets it with VERILOG_MACRO "PSRAM_T_ACC=n".
`ifndef PSRAM_T_ACC
`define PSRAM_T_ACC 9
`endif

module tau_psram_probe #(
    parameter INIT_CYC = 12000,
    parameter WATCHDOG = 65535,    // clocks a request may stay pending before TIMEOUT
    parameter T_ACC    = `PSRAM_T_ACC,
    parameter WIN_PRESENT = 0       // 1 when the CPU-window client is wired (read back in CFG[16])
) (
    input  wire        clk,
    input  wire        rst,

    // expansion-MMIO port from mp3_soc
    input  wire [7:0]  xm_reg,
    input  wire        xm_wr,
    input  wire [31:0] xm_wdata,
    output reg  [31:0] xm_rdata,

    // CPU-window client (tie win_req low when unused); level request held until win_done
    input  wire        win_req,
    input  wire        win_we,
    input  wire [22:0] win_word,
    input  wire [31:0] win_wdata,
    input  wire [3:0]  win_be,
    output wire        win_done,
    output wire [31:0] win_rdata,
    output wire        win_guard,

    // chip 0
    output wire [21:16] cram0_a,  inout wire [15:0] cram0_dq,
    input  wire         cram0_wait, output wire     cram0_clk,
    output wire         cram0_adv_n, output wire    cram0_cre,
    output wire         cram0_ce0_n, output wire    cram0_ce1_n,
    output wire         cram0_oe_n,  output wire    cram0_we_n,
    output wire         cram0_ub_n,  output wire    cram0_lb_n,
    // chip 1
    output wire [21:16] cram1_a,  inout wire [15:0] cram1_dq,
    input  wire         cram1_wait, output wire     cram1_clk,
    output wire         cram1_adv_n, output wire    cram1_cre,
    output wire         cram1_ce0_n, output wire    cram1_ce1_n,
    output wire         cram1_oe_n,  output wire    cram1_we_n,
    output wire         cram1_ub_n,  output wire    cram1_lb_n
);
    localparam [7:0] R_ID = 8'h88, R_ADDR = 8'h8C, R_WDATA = 8'h90, R_CTRL = 8'h94,
                     R_STATUS = 8'h98, R_RDATA = 8'h9C, R_CFG = 8'hA0,
                     R_LAST = 8'hA4, R_COUNT = 8'hA8, R_WCOUNT = 8'hAC;
    localparam [31:0] PSRAM_ID = 32'h50535231;

    reg  [22:0] addr_r;
    reg  [31:0] wdata_r;
    reg  [7:0]  cfg_r;

    // request snapshot handed to the controller
    reg         req, t_we;
    reg  [22:0] t_word;
    reg  [31:0] t_wdata;
    reg  [3:0]  t_be;
    reg         clr;

    reg         done_l, guard_l, timeout_l, wait_lo, wait_hi;
    reg  [31:0] rdata_l, count, wcount;
    reg  [22:0] last_word;
    reg         last_we;
    reg  [3:0]  last_be;
    reg  [15:0] wd_cnt;

    wire        c_done, c_guard, c_busy, sce, sgh, oe_act, sel_chip;
    wire [31:0] c_rdata;

    // ---- controller ownership: window (priority) or mailbox ------------------
    reg  [1:0]  owner;                       // 0 free, 1 mailbox, 2 window
    wire        ctl_req = (owner != 2'd0);
    wire        own_win = (owner == 2'd2);
    wire        ctl_we  = own_win ? win_we    : t_we;
    wire [22:0] ctl_word= own_win ? win_word  : t_word;
    wire [31:0] ctl_wdata = own_win ? win_wdata : t_wdata;
    wire [3:0]  ctl_be  = own_win ? win_be    : t_be;
    assign win_done  = c_done && own_win;
    wire   mb_done   = c_done && (owner == 2'd1);
    assign win_rdata = c_rdata;
    assign win_guard = c_guard;
    always @(posedge clk) begin
        if (rst)                         owner <= 2'd0;
        else if (c_done)                 owner <= 2'd0;
        else if (owner == 2'd0 && !c_busy) begin
            if (win_req)      owner <= 2'd2;
            else if (req)     owner <= 2'd1;
        end
    end

    tau_psram_async #(.INIT_CYC(INIT_CYC), .T_ACC(T_ACC)) u_ctl (
        .clk(clk), .rst(rst),
        .req(ctl_req), .we(ctl_we), .cpu_word(ctl_word), .wdata(ctl_wdata), .be(ctl_be),
        .cfg_rd_extra(cfg_r[3:0]), .cfg_wr_extra(cfg_r[7:4]), .clr_sticky(clr),
        .done(c_done), .rdata(c_rdata), .guard(c_guard), .busy(c_busy),
        .sticky_ce_conflict(sce), .sticky_guard_hit(sgh),
        .oe_act(oe_act), .sel_chip(sel_chip),
        .cram0_a(cram0_a), .cram0_dq(cram0_dq), .cram0_wait(cram0_wait), .cram0_clk(cram0_clk),
        .cram0_adv_n(cram0_adv_n), .cram0_cre(cram0_cre),
        .cram0_ce0_n(cram0_ce0_n), .cram0_ce1_n(cram0_ce1_n),
        .cram0_oe_n(cram0_oe_n), .cram0_we_n(cram0_we_n),
        .cram0_ub_n(cram0_ub_n), .cram0_lb_n(cram0_lb_n),
        .cram1_a(cram1_a), .cram1_dq(cram1_dq), .cram1_wait(cram1_wait), .cram1_clk(cram1_clk),
        .cram1_adv_n(cram1_adv_n), .cram1_cre(cram1_cre),
        .cram1_ce0_n(cram1_ce0_n), .cram1_ce1_n(cram1_ce1_n),
        .cram1_oe_n(cram1_oe_n), .cram1_we_n(cram1_we_n),
        .cram1_ub_n(cram1_ub_n), .cram1_lb_n(cram1_lb_n)
    );

    // WAIT is asynchronous to clk: two-flop synchronizers, sampled only while
    // OE# is low on that chip (WAIT is High-Z otherwise). Observation only.
    reg [1:0] w0s, w1s;
    always @(posedge clk) begin w0s <= {w0s[0], cram0_wait}; w1s <= {w1s[0], cram1_wait}; end

    wire wr_ctrl = xm_wr && (xm_reg == R_CTRL);
    wire start   = wr_ctrl && xm_wdata[0] && !req && !c_busy;

    always @(posedge clk) begin
        clr <= 1'b0;
        if (rst) begin
            addr_r <= 23'd0; wdata_r <= 32'd0; cfg_r <= 8'd0;
            req <= 1'b0; t_we <= 1'b0; t_word <= 23'd0; t_wdata <= 32'd0; t_be <= 4'd0;
            done_l <= 1'b0; guard_l <= 1'b0; timeout_l <= 1'b0;
            wait_lo <= 1'b0; wait_hi <= 1'b0;
            rdata_l <= 32'd0; count <= 32'd0; wcount <= 32'd0;
            last_word <= 23'd0; last_we <= 1'b0; last_be <= 4'd0; wd_cnt <= 16'd0;
        end else begin
            if (xm_wr) case (xm_reg)
                R_ADDR:  addr_r  <= xm_wdata[22:0];
                R_WDATA: wdata_r <= xm_wdata;
                R_CFG:   cfg_r   <= xm_wdata[7:0];
                default: ;
            endcase

            if (wr_ctrl && xm_wdata[6]) begin
                clr <= 1'b1;
                timeout_l <= 1'b0; wait_lo <= 1'b0; wait_hi <= 1'b0;
            end

            if (start) begin
                req <= 1'b1; done_l <= 1'b0; guard_l <= 1'b0; wd_cnt <= 16'd0;
                t_we <= xm_wdata[1]; t_be <= xm_wdata[5:2];
                t_word <= addr_r; t_wdata <= wdata_r;
                last_word <= addr_r; last_we <= xm_wdata[1]; last_be <= xm_wdata[5:2];
            end

            if (req) begin
                wd_cnt <= wd_cnt + 16'd1;
                if (wd_cnt >= WATCHDOG) timeout_l <= 1'b1;
            end
            if (mb_done) begin
                req <= 1'b0; done_l <= 1'b1; guard_l <= c_guard;
                rdata_l <= c_rdata; count <= count + 32'd1;
            end
            if (win_done) wcount <= wcount + 32'd1;

            // WAIT of the selected chip, while an OE#-low phase is being driven
            // (taps come from the controller, never from the pin registers).
            if (oe_act) begin
                if (sel_chip ? w1s[1] : w0s[1]) wait_hi <= 1'b1; else wait_lo <= 1'b1;
            end
        end
    end

    always @(*) begin
        case (xm_reg)
            R_ID:     xm_rdata = PSRAM_ID;
            R_ADDR:   xm_rdata = {9'd0, addr_r};
            R_WDATA:  xm_rdata = wdata_r;
            R_STATUS: xm_rdata = {16'd0, count[7:0], wait_hi, wait_lo, sgh, sce,
                                  guard_l, timeout_l, done_l, (req | c_busy)};
            R_RDATA:  xm_rdata = rdata_l;
            R_CFG:    xm_rdata = {15'd0, WIN_PRESENT[0], T_ACC[7:0], cfg_r};
            R_LAST:   xm_rdata = {4'd0, last_be, last_we, last_word};
            R_COUNT:  xm_rdata = count;
            R_WCOUNT: xm_rdata = wcount;
            default:  xm_rdata = 32'd0;
        endcase
    end
endmodule

`default_nettype wire
