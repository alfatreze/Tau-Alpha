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
    parameter T_ACC    = 9,      // read sample index: sampled T_ACC-3 cycles after ADV# rises
                                 // and T_ACC-5 cycles after OE# falls (datasheet tAADV 70, tOE 20).
                                 // 9 leaves ~30 ns before FPGA pad delays; 8 leaves only ~13 ns
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
    // Monitor taps for the WAIT sampler in tau_psram_probe. Deliberately NOT the
    // pin registers: those must drive only their pad so Quartus can pack them into
    // the I/O cells (FAST_*_REGISTER), and any extra fan-out would prevent that.
    output reg         oe_act,         // an OE#-low phase is being generated (either chip)
    output wire        sel_chip,       // chip of the current operation

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
    reg [15:0] dq0_q, dq1_q;   // per-chip pad registers (packable into the IO cell)

    assign busy = (st != S_IDLE);
    assign sel_chip = cw_l[22];

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

    // next-value CE# selects (active high here); shared by the pin registers and the monitor
    wire n_ce00 = s0 && in_ce && !die;
    wire n_ce01 = s0 && in_ce &&  die;
    wire n_ce10 = s1 && in_ce && !die;
    wire n_ce11 = s1 && in_ce &&  die;

    reg        dq_oe0, dq_oe1;
    reg [15:0] dq_out0, dq_out1;
    assign cram0_dq = dq_oe0 ? dq_out0 : 16'hZZZZ;
    assign cram1_dq = dq_oe1 ? dq_out1 : 16'hZZZZ;
    wire [15:0] dq_q = chip ? dq1_q : dq0_q;   // mux AFTER the pad registers

    // ---- registered chip pins -----------------------------------------------
    always @(posedge clk) begin
        if (rst || !act) begin
            cram0_adv_n <= 1'b1; cram0_ce0_n <= 1'b1; cram0_ce1_n <= 1'b1;
            cram0_oe_n <= 1'b1; cram0_we_n <= 1'b1; cram0_ub_n <= 1'b1; cram0_lb_n <= 1'b1;
            cram1_adv_n <= 1'b1; cram1_ce0_n <= 1'b1; cram1_ce1_n <= 1'b1;
            cram1_oe_n <= 1'b1; cram1_we_n <= 1'b1; cram1_ub_n <= 1'b1; cram1_lb_n <= 1'b1;
            dq_oe0 <= 1'b0; dq_oe1 <= 1'b0;
        end else begin
            cram0_adv_n  <= !(s0 && adv_low);
            cram0_ce0_n  <= !n_ce00;
            cram0_ce1_n  <= !n_ce01;
            cram0_oe_n   <= !(s0 && oe_low);
            cram0_we_n   <= !(s0 && we_low);
            cram0_lb_n   <= (s0 && in_ce) ? lb_n_w : 1'b1;
            cram0_ub_n   <= (s0 && in_ce) ? ub_n_w : 1'b1;
            dq_oe0       <= s0 && (drv_addr || drv_data);

            cram1_adv_n  <= !(s1 && adv_low);
            cram1_ce0_n  <= !n_ce10;
            cram1_ce1_n  <= !n_ce11;
            cram1_oe_n   <= !(s1 && oe_low);
            cram1_we_n   <= !(s1 && we_low);
            cram1_lb_n   <= (s1 && in_ce) ? lb_n_w : 1'b1;
            cram1_ub_n   <= (s1 && in_ce) ? ub_n_w : 1'b1;
            dq_oe1       <= s1 && (drv_addr || drv_data);
        end
    end

    // Address-high and DQ data registers are deliberately PLAIN (no reset, no mask, one
    // load: the pad). An I/O-cell register takes a single synchronous control, and with a
    // clear plus a data mux Quartus refused to pack them (B-008: "cannot simultaneously use
    // clear and load"). Their values only matter while CE# is low: A[21:16] is latched with
    // the address, and DQ is driven only in the address/write-data phases (dq_oe*). Both
    // chips see the same address-high value; the deselected chip ignores it.
    always @(posedge clk) begin
        cram0_a <= addr_hi;
        cram1_a <= addr_hi;
        dq_out0 <= dq_val;
        dq_out1 <= dq_val;
    end

    // ---- fault monitor on the values being driven ---------------------------
    // Built from the same next-pin values as the pin registers above, but with its
    // OWN register, so the pin registers keep a single load (their pad) and stay
    // packable into I/O cells. It flags: more than one CE# low, or OE# and WE#
    // low together. (It sees what the pins will do one clock later.)
    wire [2:0] ce_low_cnt = {2'b00, n_ce00} + {2'b00, n_ce01} + {2'b00, n_ce10} + {2'b00, n_ce11};
    wire mon_bad_d = (ce_low_cnt > 3'd1) || (oe_low && we_low);
    reg  mon_bad;
    always @(posedge clk) begin
        mon_bad <= rst ? 1'b0 : mon_bad_d;
        oe_act  <= rst ? 1'b0 : oe_low;
    end

    // ---- sequencer ----------------------------------------------------------
    always @(posedge clk) begin
        done <= 1'b0;
        dq0_q <= cram0_dq;
        dq1_q <= cram1_dq;
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
            if (mon_bad) sticky_ce_conflict <= 1'b1;

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
