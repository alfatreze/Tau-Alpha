// =============================================================================
// tau_psram_async.sv -- asynchronous multiplexed-address PSRAM controller.
//
// One 32-bit CPU word = two ordered 16-bit PSRAM operations, one request
// outstanding. Everything runs in clk_sys. Timing values are parameters taken
// from docs/PSRAM_TIMING_CONTRACT.md (checked against the AS1C8M16PL datasheet).
//
// CPU word offset [22:0]:  [22] chip, [21] die, [20:0] word within die.
// PSRAM 16-bit word address = {cpu_word, half}: [23] chip, [22] die,
// [21:16] -> cram_a, [15:0] -> address phase on cram_dq.
//
// Rules enforced here: exactly one CE# low during an access and all high when
// idle; the last CPU word of every die (contains PSRAM word 3FFFFFh, the
// software-configuration hazard) is refused with no chip access; DQ is driven
// only in the address and write-data phases; the response is a controller
// register loaded before `done` is pulsed.
// =============================================================================
`default_nettype none

module tau_psram_async #(
    parameter INIT_CYC = 12000,  // power-up hold-off, clocks (placeholder, OPEN)
    parameter T_ADV    = 2,      // ADV# low cycles
    parameter T_AH     = 1,      // address hold after ADV# rises
    parameter T_ACC    = 8,      // read sample index: sampled T_ACC-3 cycles after ADV# rises
                                 // and T_ACC-5 cycles after OE# falls (datasheet tAADV 70, tOE 20)
    parameter T_WP     = 3,      // WE# low cycles
    parameter T_WTOT   = 5,      // minimum cycles from ADV# fall to WE# rise
    parameter T_REC    = 2       // CE# high recovery cycles between operations
) (
    input  wire        clk,
    input  wire        rst,

    // request (level held by the bus wrapper until `done`)
    input  wire        req,
    input  wire        we,
    input  wire [22:0] cpu_word,
    input  wire [31:0] wdata,
    input  wire [3:0]  be,
    input  wire [3:0]  cfg_rd_extra,   // extra read-capture cycles (slow-timing dial)
    input  wire [3:0]  cfg_wr_extra,   // extra WE# low cycles (slow-timing dial)
    input  wire        clr_sticky,

    output reg         done,           // single-clock pulse; rdata/guard valid
    output reg  [31:0] rdata,          // held until the next request completes
    output reg         guard,          // request refused: guard word
    output wire        busy,
    output reg         sticky_ce_conflict,
    output reg         sticky_guard_hit,

    // chip 0
    output reg  [21:16] cram0_a,  inout wire [15:0] cram0_dq,
    input  wire         cram0_wait, output wire     cram0_clk,
    output reg          cram0_adv_n, output wire    cram0_cre,
    output reg          cram0_ce0_n, output reg     cram0_ce1_n,
    output reg          cram0_oe_n,  output reg     cram0_we_n,
    output reg          cram0_ub_n,  output reg     cram0_lb_n,
    // chip 1
    output reg  [21:16] cram1_a,  inout wire [15:0] cram1_dq,
    input  wire         cram1_wait, output wire     cram1_clk,
    output reg          cram1_adv_n, output wire    cram1_cre,
    output reg          cram1_ce0_n, output reg     cram1_ce1_n,
    output reg          cram1_oe_n,  output reg     cram1_we_n,
    output reg          cram1_ub_n,  output reg     cram1_lb_n
);
    assign cram0_clk = 1'b0;  assign cram0_cre = 1'b0;   // async mode only
    assign cram1_clk = 1'b0;  assign cram1_cre = 1'b0;

    localparam [1:0] S_INIT = 2'd0, S_IDLE = 2'd1, S_OP = 2'd2, S_DONE = 2'd3;
    reg [1:0]  st;
    reg [31:0] initc;
    reg        half;
    reg        wr_l;
    reg [22:0] cw_l;
    reg [31:0] wd_l;
    reg [3:0]  be_l;
    reg [7:0]  t;
    reg [15:0] dq_q;

    assign busy = (st != S_IDLE);

    wire        chip     = cw_l[22];
    wire        die      = cw_l[21];
    wire [23:0] paddr    = {cw_l, half};
    wire [15:0] addr_lo  = paddr[15:0];
    wire [5:0]  addr_hi  = paddr[21:16];
    wire [15:0] wd_half  = half ? wd_l[31:16] : wd_l[15:0];
    wire [1:0]  be_half  = half ? be_l[3:2]   : be_l[1:0];

    // ---- timeline, in op-relative cycles ------------------------------------
    localparam T_DRIVE_END = T_ADV + T_AH;          // address on DQ while t < this
    localparam T_WS        = T_DRIVE_END + 1;       // WE# falls (1 cycle data setup)
    wire [7:0] wrise   = ((T_WS + T_WP > T_WTOT) ? (T_WS + T_WP) : T_WTOT) + cfg_wr_extra;
    wire [7:0] rcap    = T_ACC + cfg_rd_extra;
    wire [7:0] ce_last = wr_l ? wrise : rcap;
    wire [7:0] op_last = ce_last + T_REC;           // last cycle of the op incl. recovery

    wire act      = (st == S_OP);
    wire in_ce    = act && (t <= ce_last);
    wire adv_low  = act && (t < T_ADV);
    wire drv_addr = in_ce && (t < T_DRIVE_END);
    wire drv_data = in_ce && wr_l && (t >= T_DRIVE_END);
    wire we_low   = in_ce && wr_l && (t >= T_WS) && (t < wrise);
    wire oe_low   = in_ce && !wr_l && (t >= T_WS) && (t <= rcap);
    wire [15:0] dq_val = drv_addr ? addr_lo : wd_half;
    wire lb_n_w = wr_l ? ~be_half[0] : 1'b0;
    wire ub_n_w = wr_l ? ~be_half[1] : 1'b0;

    wire s0 = (chip == 1'b0);
    wire s1 = (chip == 1'b1);

    reg        dq_oe0, dq_oe1;
    reg [15:0] dq_out0, dq_out1;
    assign cram0_dq = dq_oe0 ? dq_out0 : 16'hZZZZ;
    assign cram1_dq = dq_oe1 ? dq_out1 : 16'hZZZZ;
    wire [15:0] dq_in = chip ? cram1_dq : cram0_dq;

    // ---- registered chip pins -----------------------------------------------
    always @(posedge clk) begin
        if (rst || !act) begin
            cram0_a <= 6'd0; cram0_adv_n <= 1'b1; cram0_ce0_n <= 1'b1; cram0_ce1_n <= 1'b1;
            cram0_oe_n <= 1'b1; cram0_we_n <= 1'b1; cram0_ub_n <= 1'b1; cram0_lb_n <= 1'b1;
            cram1_a <= 6'd0; cram1_adv_n <= 1'b1; cram1_ce0_n <= 1'b1; cram1_ce1_n <= 1'b1;
            cram1_oe_n <= 1'b1; cram1_we_n <= 1'b1; cram1_ub_n <= 1'b1; cram1_lb_n <= 1'b1;
            dq_oe0 <= 1'b0; dq_oe1 <= 1'b0; dq_out0 <= 16'd0; dq_out1 <= 16'd0;
        end else begin
            cram0_a      <= (s0 && in_ce) ? addr_hi : 6'd0;
            cram0_adv_n  <= !(s0 && adv_low);
            cram0_ce0_n  <= !(s0 && in_ce && !die);
            cram0_ce1_n  <= !(s0 && in_ce &&  die);
            cram0_oe_n   <= !(s0 && oe_low);
            cram0_we_n   <= !(s0 && we_low);
            cram0_lb_n   <= (s0 && in_ce) ? lb_n_w : 1'b1;
            cram0_ub_n   <= (s0 && in_ce) ? ub_n_w : 1'b1;
            dq_oe0       <= s0 && (drv_addr || drv_data);
            dq_out0      <= dq_val;

            cram1_a      <= (s1 && in_ce) ? addr_hi : 6'd0;
            cram1_adv_n  <= !(s1 && adv_low);
            cram1_ce0_n  <= !(s1 && in_ce && !die);
            cram1_ce1_n  <= !(s1 && in_ce &&  die);
            cram1_oe_n   <= !(s1 && oe_low);
            cram1_we_n   <= !(s1 && we_low);
            cram1_lb_n   <= (s1 && in_ce) ? lb_n_w : 1'b1;
            cram1_ub_n   <= (s1 && in_ce) ? ub_n_w : 1'b1;
            dq_oe1       <= s1 && (drv_addr || drv_data);
            dq_out1      <= dq_val;
        end
    end

    // ---- hardware-visible fault monitor on the actual pins ------------------
    wire [2:0] ce_low_cnt = {2'b00, !cram0_ce0_n} + {2'b00, !cram0_ce1_n}
                          + {2'b00, !cram1_ce0_n} + {2'b00, !cram1_ce1_n};
    wire ctl_clash = (!cram0_oe_n && !cram0_we_n) || (!cram1_oe_n && !cram1_we_n);

    // ---- sequencer ----------------------------------------------------------
    always @(posedge clk) begin
        done <= 1'b0;
        dq_q <= dq_in;
        if (rst) begin
            st <= S_INIT; initc <= 32'd0; t <= 8'd0; half <= 1'b0;
            rdata <= 32'd0; guard <= 1'b0;
            sticky_ce_conflict <= 1'b0; sticky_guard_hit <= 1'b0;
            wr_l <= 1'b0; cw_l <= 23'd0; wd_l <= 32'd0; be_l <= 4'd0;
        end else begin
            if (clr_sticky) begin
                sticky_ce_conflict <= 1'b0;
                sticky_guard_hit   <= 1'b0;
            end
            if (ce_low_cnt > 3'd1 || ctl_clash) sticky_ce_conflict <= 1'b1;

            case (st)
                S_INIT: if (initc >= INIT_CYC) st <= S_IDLE; else initc <= initc + 32'd1;
                S_IDLE: if (req) begin
                    wr_l <= we; cw_l <= cpu_word; wd_l <= wdata; be_l <= be;
                    half <= 1'b0; t <= 8'd0;
                    if (cpu_word[20:0] == 21'h1FFFFF) begin
                        guard <= 1'b1; sticky_guard_hit <= 1'b1; rdata <= 32'd0;
                        done <= 1'b1; st <= S_DONE;
                    end else begin
                        guard <= 1'b0; st <= S_OP;
                    end
                end
                S_OP: begin
                    if (!wr_l && t == rcap) begin
                        if (half) rdata[31:16] <= dq_q; else rdata[15:0] <= dq_q;
                    end
                    if (t == op_last) begin
                        t <= 8'd0;
                        if (half) begin done <= 1'b1; st <= S_DONE; end
                        else half <= 1'b1;
                    end else t <= t + 8'd1;
                end
                default: st <= S_IDLE;   // S_DONE: one cycle, then idle
            endcase
        end
    end
endmodule

`default_nettype wire
