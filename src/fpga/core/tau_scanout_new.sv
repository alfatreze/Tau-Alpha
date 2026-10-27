// =============================================================================
// tau_scanout_new.sv -- 400 x 360 scanout (12 MHz pixel clock, 500 x 400 total), Talos 2 T5.
//
// Written from docs/research/TALOS_SCANOUT_CONTRACT.md only (clean-room, D-T01/D-T07); a drop-in for tau_scanout,
// proved equal to it cycle for cycle by sim/tb_tau_scanout_diff.v.
//
// Pipeline (clk_vid): the counters (hc, vc) hold a position in cycle c. In cycle c+1 the line-buffer word for that
// position and the de/hs/vs flags are registered (stage 1); in cycle c+2 the four video outputs are registered (stage 2).
//
// BUG_* parameters are mutation hooks for the testbench (all 0 in the build): each one breaks one rule of the contract
// and the bench must fail for it.
// =============================================================================
`default_nettype none

module tau_scanout_new #(
    parameter BUG_ACT_X     = 0,  // active window one pixel late (hc 9..408)
    parameter BUG_HS        = 0,  // horizontal sync one pixel late
    parameter BUG_LATENCY   = 0,  // one extra output register
    parameter BUG_PARITY    = 0,  // line-buffer half taken from the wrong parity
    parameter BUG_VBLANK    = 0,  // vblank_tgl one line early
    parameter BUG_NO_PAINT  = 0,  // picture not gated by painted
    parameter BUG_LATE_HC   = 0,  // late check one pixel late
    parameter BUG_REQ_EARLY = 0,  // fill request two lines ahead
    parameter BUG_VS        = 0,  // vertical sync one line late
    parameter BUG_PAINT_SYN = 0,  // painted synchroniser only two flops
    parameter BUG_NO_NEXT   = 0,  // late check ignores "the next line is also fine"
    parameter BUG_EXPAND    = 0   // RGB565 expanded with bit replication
) (
    input  wire        reset,
    input  wire        clk_sys,
    input  wire        clk_sdram,
    input  wire        clk_vid,

    input  wire        cmd_push,

    input  wire        lb_we,
    input  wire [9:0]  lb_waddr,
    input  wire [15:0] lb_wdata,

    input  wire        fill_done_inc,
    input  wire [9:0]  fill_done_line,
    input  wire        fill_seen,

    output wire [15:0] underflow_cnt,

    output reg         fill_req_tgl  = 1'b0,
    output reg  [9:0]  fill_line_req = 10'd0,
    output reg         vblank_tgl    = 1'b0,

    output reg  [23:0] video_rgb     = 24'd0,
    output reg         video_de      = 1'b0,
    output reg         video_hs      = 1'b0,
    output reg         video_vs      = 1'b0,
    output wire [8:0]  scan_vc
);

    wire _unused_ok = &{1'b0, fill_done_inc, 1'b0};

    // ---- counters ----------------------------------------------------------
    reg [10:0] hc = 11'd0;
    reg [10:0] vc = 11'd0;
    wire h_last = (hc == 11'd499);
    wire v_last = (vc == 11'd399);

    always @(posedge clk_vid) begin
        if (reset) begin
            hc <= 11'd0;
            vc <= 11'd0;
        end else begin
            hc <= h_last ? 11'd0 : hc + 11'd1;
            if (h_last) vc <= v_last ? 11'd0 : vc + 11'd1;
        end
    end

    assign scan_vc = vc[8:0];

    // ---- fill request and vertical-blank toggle ----------------------------
    wire [10:0] vc_next  = v_last ? 11'd0 : vc + 11'd1;
    wire [10:0] vc_req   = (BUG_REQ_EARLY != 0) ? (vc_next + 11'd1) : vc_next;
    wire        req_act  = (vc_req >= 11'd4) && (vc_req <= 11'd363);
    wire        next_act = (vc_next >= 11'd4) && (vc_next <= 11'd363);
    wire [10:0] vb_line  = (BUG_VBLANK != 0) ? 11'd366 : 11'd367;

    always @(posedge clk_vid) begin
        if (hc == 11'd0) begin
            if (req_act) begin
                fill_line_req <= vc_req[9:0] - 10'd4;
                fill_req_tgl  <= ~fill_req_tgl;
            end
            if (vc == vb_line) vblank_tgl <= ~vblank_tgl;
        end
    end

    // ---- painted (clk_sys level -> three-flop synchroniser on clk_vid) -----
    reg painted = 1'b0;
    always @(posedge clk_sys) if (cmd_push) painted <= 1'b1;

    reg [2:0] paint_sync = 3'b000;
    always @(posedge clk_vid) paint_sync <= {paint_sync[1:0], painted};
    wire paint_vid = (BUG_PAINT_SYN != 0) ? paint_sync[1] : paint_sync[2];

    // ---- line buffer: one M10K array, clk_sdram write, registered clk_vid read
    (* ramstyle = "M10K" *) reg [15:0] linebuf [0:1023];

    always @(posedge clk_sdram) if (lb_we) linebuf[lb_waddr] <= lb_wdata;

    wire [8:0]  px_off  = hc[8:0] - 9'd8;
    wire        half    = (BUG_PARITY != 0) ? ~vc[0] : vc[0];
    wire [9:0]  rd_addr = {half, px_off};
    reg  [15:0] rd_word;
    always @(posedge clk_vid) rd_word <= linebuf[rd_addr];

    // ---- stage 1: flags of the position that the read word belongs to ------
    wire [10:0] x0   = (BUG_ACT_X != 0) ? 11'd9 : 11'd8;
    wire        act_h = (hc >= x0) && (hc <= x0 + 11'd399);
    wire        act_v = (vc >= 11'd4) && (vc <= 11'd363);
    wire        hs_w  = (BUG_HS != 0) ? ((hc >= 11'd421) && (hc <= 11'd460))
                                      : ((hc >= 11'd420) && (hc <= 11'd459));
    wire        vs_w  = (BUG_VS != 0) ? ((vc >= 11'd368) && (vc <= 11'd371))
                                      : ((vc >= 11'd367) && (vc <= 11'd370));

    reg s1_de = 1'b0, s1_hs = 1'b0, s1_vs = 1'b0;
    always @(posedge clk_vid) begin
        s1_de <= act_h && act_v;
        s1_hs <= hs_w;
        s1_vs <= vs_w;
    end

    // ---- stage 2: the video outputs -----------------------------------------
    wire        show  = s1_de && ((BUG_NO_PAINT != 0) ? 1'b1 : paint_vid);
    wire [23:0] rgb_x = (BUG_EXPAND != 0)
        ? {rd_word[15:11], rd_word[15:13], rd_word[10:5], rd_word[10:9], rd_word[4:0], rd_word[4:2]}
        : {rd_word[15:11], 3'b000, rd_word[10:5], 2'b00, rd_word[4:0], 3'b000};
    wire [23:0] rgb_n = show ? rgb_x : 24'd0;

    generate
        if (BUG_LATENCY != 0) begin : g_lat
            reg [23:0] r1 = 24'd0;
            reg        d1 = 1'b0, h1 = 1'b0, v1 = 1'b0;
            always @(posedge clk_vid) begin
                r1 <= rgb_n; d1 <= s1_de; h1 <= s1_hs; v1 <= s1_vs;
                video_rgb <= r1; video_de <= d1; video_hs <= h1; video_vs <= v1;
            end
        end else begin : g_nolat
            always @(posedge clk_vid) begin
                video_rgb <= rgb_n;
                video_de  <= s1_de;
                video_hs  <= s1_hs;
                video_vs  <= s1_vs;
            end
        end
    endgenerate

    // ---- late-line counter ---------------------------------------------------
    wire [9:0] done_line_vid;
    tau_cdc_gray_bus #(.W(10), .STAGES(3)) u_done_line (
        .clk_src(clk_sdram), .d_src(fill_done_line),
        .clk_dst(clk_vid),   .q_dst(done_line_vid)
    );

    reg [2:0] seen_sync = 3'b000;
    always @(posedge clk_vid) seen_sync <= {seen_sync[1:0], fill_seen};

    wire [10:0] chk_hc = (BUG_LATE_HC != 0) ? 11'd9 : 11'd8;
    wire [9:0]  n_line = vc[9:0] - 10'd4;
    wire        chk    = (hc == chk_hc) && (vc >= 11'd4) && (vc <= 11'd363) && seen_sync[2];
    wire        ok_now = (done_line_vid == n_line);
    wire        ok_nxt = (BUG_NO_NEXT == 0) && next_act && (done_line_vid == n_line + 10'd1);

    reg late_pulse = 1'b0;
    always @(posedge clk_vid) late_pulse <= chk && !(ok_now || ok_nxt);

    tau_cdc_gray_ctr #(.WIDTH(16)) u_late_cnt (
        .clk_src(clk_vid), .rst_src(reset), .inc(late_pulse),
        .clk_dst(clk_sys), .rst_dst(reset), .count_dst(underflow_cnt)
    );

endmodule

`default_nettype wire
