// ============================================================================
// tau_scanout.sv -- the scanout half of the old mp3_fb.sv (Talos 2, T2, D-T02).
//
// Video timing (clk_vid), the line buffer (parity-split double buffer: one half
// drains to clk_vid while the other fills from clk_sdram for the NEXT line), the
// prefetch request toggle (fill_req_tgl/fill_line_req), the per-frame vblank
// toggle, and the blank-until-painted level. Moved out of mp3_fb.sv with NO
// behaviour change: the differential test (sim/tb_tau_gfx_diff.v) compares every
// output of the whole split against the preserved original, cycle by cycle.
// The engine side is tau_talos.sv; tau_gfx.sv joins the two.
// ============================================================================

`default_nettype none

module tau_scanout (
    input  wire        reset,
    input  wire        clk_sys,      // only for the "painted" level (cmd_push)
    input  wire        clk_sdram,    // line buffer write side
    input  wire        clk_vid,      // pixel clock (12 MHz)
    input  wire        cmd_push,     // clk_sys: one command pushed is enough to know the CPU is painting

    // From tau_talos (clk_sdram): the line-buffer write port
    input  wire        lb_we,
    input  wire [9:0]  lb_waddr,
    input  wire [15:0] lb_wdata,

    // Talos 2 T3, from tau_talos (clk_sdram): which line the last finished fill was for, and that one has finished
    input  wire        fill_done_inc,
    input  wire [9:0]  fill_done_line,
    input  wire        fill_seen,
    // Scanout underflow: lines whose fill had not finished when their display started (clk_sys, saturating)
    output wire [15:0] underflow_cnt,

    // To tau_talos (clk_vid toggles, sampled in clk_sdram there)
    output reg         fill_req_tgl = 0,
    output reg  [9:0]  fill_line_req = 0,
    output reg         vblank_tgl = 0,   // H2 (B-340): clk_vid domain, toggles once per frame inside vblank

    output reg  [23:0] video_rgb,
    output reg         video_de,
    output reg         video_hs,
    output reg         video_vs,
    // Helios beam position (B-267): the video line counter, 0..V_TOT-1 (V_TOT is 400, so 9 bits). The row being scanned
    // is vc - VOFF while VOFF <= vc < VOFF + V_ACT. Carried to the CPU by tau_cdc_gray_bus (it steps by +1 per line).
    output wire [8:0]  scan_vc
);

    // ---- Geometry ----------------------------------------------------------
    localparam H_ACT = 11'd400, V_ACT = 11'd360;
    localparam H_TOT = 11'd500, V_TOT = 11'd400;
    localparam [10:0] HOFF = 11'd8,  VOFF = 11'd4;
    localparam [10:0] HS_ST = HOFF + H_ACT + 11'd12, HS_EN = HS_ST + 11'd40;
    localparam [10:0] VS_ST = VOFF + V_ACT + 11'd3,  VS_EN = VS_ST + 11'd4;

    // Top black guard-band, INHERITED FROM pocket_vector_fb.sv AND NOW 0.
    //
    // That core found the scaler overshoots into a bright artifact at a
    // black->content luminance step on the FIRST active line, and forced the
    // first few active lines to black to move the step away from the DE edge.
    // It was invisible there because a vector display's background IS black.
    //
    // Here it is not. The gradient's top is 0x2124, luma 34/255, so three
    // forced-black lines read as a seam above the background -- visible in
    // screenshots and over HDMI from the dock, less so on the handheld panel.
    //
    // And it was not buying anything. The guard does not REMOVE the
    // black->content step, it relocates it; relocating a 34/255 step is worth
    // nothing, where the artifact it was designed against was a step to bright
    // vector lines at luma ~255. There has never been a bottom guard here
    // either, and no artifact has ever been reported at that edge -- which is
    // the same gentle step, and evidence the scaler copes with it.
    //
    // Left as a parameter rather than deleted: if a bright artifact ever does
    // appear on the top line, 3 is the value that hides it.
    localparam [10:0] GUARD_TOP = 11'd0;


    // ======================================================================
    // Scanout line buffer: parity-split double buffer, exactly as
    // pocket_vector_fb.sv -- one half drains to clk_vid while the other half
    // fills from clk_sdram for the NEXT line, so fill and scanout of the same
    // line never race.
    // ======================================================================
    (* ramstyle = "M10K" *) reg [15:0] linebuf [0:1023];
    always @(posedge clk_sdram) if (lb_we) linebuf[lb_waddr] <= lb_wdata;

    // ======================================================================
    // Video timing (clk_vid domain) -- same structure as pocket_vector_fb.sv.
    // ======================================================================
    reg [10:0] hc = 0, vc = 0;
    assign scan_vc = vc[8:0];
    always @(posedge clk_vid) begin
        if (reset) begin hc <= 0; vc <= 0; end
        else if (hc == H_TOT - 1'b1) begin
            hc <= 0;
            vc <= (vc == V_TOT - 1'b1) ? 11'd0 : vc + 1'b1;
        end else hc <= hc + 1'b1;
    end

    wire active   = (hc >= HOFF) && (hc < HOFF + H_ACT) &&
                    (vc >= VOFF) && (vc < VOFF + V_ACT);
    wire hs_pulse = (hc >= HS_ST) && (hc < HS_EN);
    wire vs_pulse = (vc >= VS_ST) && (vc < VS_EN);

    // Prefetch the row the NEXT scanline will display, so its burst-fill has
    // a full scanline period to complete before it's actually scanned out.
    wire [10:0] next_sl = (vc == V_TOT - 1'b1) ? 11'd0 : vc + 1'b1;
    wire        do_fill = (next_sl >= VOFF) && (next_sl < VOFF + V_ACT);
    wire [9:0]  pf_row  = next_sl[9:0] - VOFF[9:0];
    always @(posedge clk_vid) begin
        if (hc == 11'd0 && do_fill) begin
            fill_line_req <= pf_row;
            fill_req_tgl  <= ~fill_req_tgl;
        end
        // H2 (B-340): one shot per frame, well inside vertical blanking and well before do_fill's own
        // wrap-around prefetch of the new frame's row 0 (vc==V_TOT-1) -- see the comment on flip_req_s
        // above for why applying the flip by then matters.
        if (hc == 11'd0 && vc == VS_ST) vblank_tgl <= ~vblank_tgl;
    end

    reg [15:0] lb_q;
    reg        active_p1, hs_p1, vs_p1, top_guard_p1;
    wire [10:0] hcsub = hc - HOFF;
    wire [10:0] vsub  = vc - VOFF;
    wire        top_guard = active && (vsub < GUARD_TOP);

    // Blank until the CPU has drawn something.
    //
    // SDRAM powers up holding garbage and scanout displays it the moment video
    // comes alive, which is well before the firmware is even loaded. That used
    // to be hidden because APF put its file browser in front of the core at
    // launch; now the core comes up directly and the garbage is the first thing
    // on screen. One command pushed is enough to know the CPU is running and
    // painting -- main() clears the whole frame before anything else.
    reg painted_sys = 1'b0;
    always @(posedge clk_sys) if (cmd_push) painted_sys <= 1'b1;

    // A level that only ever goes 0 -> 1, so a plain synchroniser is enough.
    reg [2:0] painted_vid = 3'b0;
    always @(posedge clk_vid) painted_vid <= {painted_vid[1:0], painted_sys};

    always @(posedge clk_vid) begin
        lb_q         <= linebuf[{vc[0], hcsub[8:0]}];   // vc-VOFF parity = vc[0] (VOFF even)
        active_p1    <= active;
        top_guard_p1 <= top_guard;
        hs_p1        <= hs_pulse;
        vs_p1        <= vs_pulse;
        video_rgb    <= (active_p1 && !top_guard_p1 && painted_vid[2])
                       ? {lb_q[15:11], 3'b0, lb_q[10:5], 2'b0, lb_q[4:0], 3'b0}  // RGB565 -> 24-bit
                       : 24'h000000;
        video_de     <= active_p1;
        video_hs     <= hs_p1;
        video_vs     <= vs_p1;
    end


    // ======================================================================
    // Talos 2 T3: underflow detection (the instrument the central claim "scanout always wins" was missing).
    // Line n is displayed during scanline n + VOFF; its fill was requested one scanline earlier and must have
    // finished by hc == HOFF, when the display starts. At that instant the last finished fill is line n (its own),
    // or line n + 1 if the next line's fill (requested at hc == 0) is already done too. Anything else means line n's
    // buffer half was not filled in time. fill_done_line steps by +1 per fill, so it crosses with tau_cdc_gray_bus
    // (the 359 -> 0 wrap happens in vertical blanking, where no line is being checked). Armed after the first fill.
    // ======================================================================
    wire [9:0] done_line_vid;
    tau_cdc_gray_bus #(.W(10), .STAGES(3)) u_done_line (.clk_src(clk_sdram), .d_src(fill_done_line), .clk_dst(clk_vid), .q_dst(done_line_vid));
    reg [2:0] seen_vid = 3'b0;
    always @(posedge clk_vid) seen_vid <= {seen_vid[1:0], fill_seen};
    reg uf_pulse = 1'b0;
    wire [10:0] vsub_chk = vc - VOFF;
    wire        in_active_line = (vc >= VOFF) && (vc < VOFF + V_ACT);
    always @(posedge clk_vid) begin
        uf_pulse <= 1'b0;
        if (hc == HOFF && in_active_line && seen_vid[2]) begin
            if (!(done_line_vid == vsub_chk[9:0] || (do_fill && done_line_vid == vsub_chk[9:0] + 10'd1))) uf_pulse <= 1'b1;
        end
    end
    tau_cdc_gray_ctr #(.WIDTH(16)) u_underflow (.clk_src(clk_vid), .rst_src(reset), .inc(uf_pulse),
                                                 .clk_dst(clk_sys), .rst_dst(reset), .count_dst(underflow_cnt));

endmodule

`default_nettype wire
