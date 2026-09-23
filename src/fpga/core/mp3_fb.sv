// ============================================================================
// mp3_fb.sv -- SDRAM-backed framebuffer + 2D draw engine for the player UI.
//
// Scanout half adapted from HarpMudd.starwars' pocket_vector_fb.sv (hardware-
// validated via HarpMudd.sdramfbtest): parity-split double-buffered line
// buffer, fill-request CDC, and the HOFF/VOFF/guard-band lessons that were
// paid for on real hardware there. Everything vector-specific (AVG raster,
// phosphor copy-fade) is gone; this is a plain single-buffered RGB565
// framebuffer that a 2D engine draws into on the CPU's behalf.
//
// Geometry: 400x360 @ 12 MHz, 500x400 total = exactly 60.000 Hz. Chosen as an
// exact 4x integer scale of the Pocket's native 1600x1440 panel (confirmed via
// Analogue's own spec: the panel is BUILT around integer scaling), so nothing
// is filtered/blurred by the scaler.
//
// Memory: 1 pixel = 1 SDRAM word (RGB565), stride 512 words/line even though
// only 400 are used -- keeps every row inside a single SDRAM page. Total:
// 512*360*2 bytes =~360 KB of 32 MB SDRAM.
//
// ---------------------------------------------------------------------------
// REV 7: the CPU no longer draws row-by-row.
//
// The original engine had exactly one primitive: RUN (fill `len` words of one
// colour). Everything 2D was therefore N commands -- a rect cost one command
// per row, and TEXT cost one command per run of set bits per scaled row, so a
// five-character clock readout at 2x was ~180 MMIO round-trips. That is CPU
// work landing directly in the same per-frame budget that keeps the PCM FIFO
// fed, and a clean A/B on hardware (drawing fully disabled vs. enabled)
// confirmed it is what makes the audio jitter.
//
// So the engine now owns the loops instead of the CPU:
//   RUN   len words of one colour                        (1 command)
//   RECT  w x h block of one colour                      (1 command, was h)
//   CHAR  one anti-aliased scaled glyph, fg on bg        (1 command, was ~30)
// A 20-character title is now 20 pushes, not ~600, and each push is 2 MMIO
// writes because colour/size registers persist between glyphs.
//
// CHAR also fixes the OTHER complaint about the old UI -- jagged text. The
// source is a real typeface (Inter) rendered to a 16x16 cell at 4 bits of
// coverage per pixel by tools/gen_font_rom.py, so the anti-aliasing is genuine
// greyscale rather than reconstructed: the engine blends fg->bg by the stored
// coverage directly. (An earlier design stored an 8x8 1-bit font and upscaled
// it with Scale2x/EPX, which yields only 0, 1/2 and 1 coverage and still reads
// as a bitmap font pretending to be bigger. That is gone -- do not reason from
// it.) See cov_weight() below for why coverage is not used as the weight.
//
//   clk_sys (CPU) --async FIFO--> clk_sdram ENGINE+ARBITER
//     FILL  : burst-read display line -> scanout line buffer (time-critical:
//             must finish inside one scanline, ~4167 clk_sdram cycles @100MHz)
//     RECT  : one constant-data burst write per row, h rows
//     CHAR  : compose one output row into glyphbuf, then one STREAMING burst
//             write (varying data via sdram_fb's wsrc port), 16*sy rows
//   clk_vid: read line buffer -> video_rgb / de / hs / vs
//
// Scanout FILL always wins arbitration. Everything else is decomposed into
// single-burst units that return to the dispatcher between rows, so the worst
// case a fill ever waits is one burst (<= ~500 cycles) against ~4167 of slack.
// ============================================================================

`default_nettype none

module mp3_fb #(
    // Mutation-test hook only (sim/tb_mp3_fb.v's -PBUG_IGNORE_BLIT_STRIDE=1 target
    // in make test-rtl-fb-mutation): 1 makes OP_BLIT step by the fixed 512 stride
    // COPY uses instead of the sticky blt_*_stride registers, reproducing the exact
    // hardcoded-stride bug B1 exists to fix. Never set outside that test.
    parameter BUG_IGNORE_BLIT_STRIDE = 0,
    // Mutation-test hook only (-PBUG_IGNORE_KEY=1, make test-rtl-fb-mutation): 1
    // disables B2's colour-key compare in A_COPYRD, so a keyed source pixel
    // always overwrites the destination instead of being dropped. Never set
    // outside that test.
    parameter BUG_IGNORE_KEY = 0,
    // Mutation-test hook only (-PBUG_SBLIT_NO_SCALE=1, make test-rtl-fb-mutation):
    // 1 forces B4's X-Bresenham to advance the source column on every output
    // pixel, ignoring char_num/char_den -- a naive 1:1 copy instead of a real
    // scaled blit. Never set outside that test.
    parameter BUG_SBLIT_NO_SCALE = 0,
    // Phase F B5, per PHASE_F_SPEC.md section 10's build plan: kept behind its
    // own parameter, separate from BLIT_ENABLE, specifically so it is droppable
    // on its own if it tips the compose pipeline into the documented -1.888 ns
    // timing cliff -- the rest of the blit engine (B1/B2/B4/B6) must not have
    // to come out with it. Inert (blend logic provably dead, same convention
    // as every other *_ENABLE parameter here) when 0.
    parameter BLIT_BLEND_ENABLE = 0,
    // Mutation-test hook only (-PBUG_BLEND_ALWAYS_SRC=1, make test-rtl-fb-mutation):
    // 1 makes a blended blit silently behave like a plain one (always write the
    // source pixel, ignoring blend_active entirely). Never set outside that test.
    parameter BUG_BLEND_ALWAYS_SRC = 0,
    // Mutation-test hook only (-PBUG_CBLIT_NO_LOOKUP=1, make test-rtl-fb-mutation):
    // 1 makes OP_CBLIT write the raw palette INDEX (zero-extended) instead of
    // looking it up in the CLUT -- proves the test actually exercises the CLUT
    // mechanism, not just that some value lands at the right address. Never set
    // outside that test.
    parameter BUG_CBLIT_NO_LOOKUP = 0
) (
    input  wire        reset,
    input  wire        clk_sys,     // CPU / FIFO write domain
    input  wire        clk_sdram,   // SDRAM controller + engine (~100 MHz)
    input  wire        clk_vid,     // pixel clock (12 MHz)

    // CPU draw command (clk_sys domain) -------------------------------------
    input  wire        cmd_push,
    input  wire [2:0]  cmd_op,      // 0=RUN 1=RECT 2=CHAR 3=COPY 4=BLIT (Phase F B1)
    input  wire [18:0] cmd_addr,    // word address of top-left, y*512+x
    input  wire [8:0]  cmd_w,       // RUN: run length; RECT: width (words)
    input  wire [8:0]  cmd_h,       // RECT: height (rows)
    input  wire [15:0] cmd_fg,      // RGB565 fill / glyph foreground
    input  wire [15:0] cmd_bg,      // RGB565 glyph background
    input  wire [6:0]  cmd_glyph,   // CHAR: ASCII code
    input  wire [1:0]  cmd_sx,      // CHAR: h scale 0=1x 1=1.5x 2=2x 3=3x
    input  wire [1:0]  cmd_sy,      // CHAR: v scale, same encoding
    output wire        cmd_full,

    // Phase F B1 (section 9): sticky blit addressing state, from mp3_soc.v's
    // R_BLT_IDX/R_BLT_DATA registers. Read only by OP_BLIT below; RUN/RECT/
    // CHAR/COPY are entirely unaffected (unchanged FB_BASE/512-stride math).
    input  wire [24:0] blt_src_base,
    input  wire [9:0]  blt_src_stride,
    input  wire [24:0] blt_dst_base,
    input  wire [9:0]  blt_dst_stride,
    // Phase F B2: colour-key transparency. Read only when blit_mode (OP_BLIT);
    // OP_COPY is untouched and never keys, matching B1's own precedent of
    // leaving COPY as the simple, unmodified case.
    input  wire        blt_key_en,
    input  wire [15:0] blt_key,
    // Phase F B5: alpha blend. Also read only when blit_mode; also shares B2's
    // destination pre-read phase (A_KEYDST), generalised below to trigger on
    // "blt_key_en OR blt_blend_en" instead of key alone.
    input  wire        blt_blend_en,
    input  wire [2:0]  blt_blend_mode,   // 0=DSP alpha, 1-4=PSX shift-add ratios
    input  wire [7:0]  blt_blend_alpha,  // DSP mode only, 0-255

    // Phase F B8 (section 5, "B8 detailed design"): 256-entry CLUT, loaded by
    // the CPU (clk_sys) through mp3_soc.v's R_CLUT_IDX/R_CLUT_DATA. Independent
    // write (clk_sys) / read (clk_sdram) ports, a standard dual-clock M10K --
    // no synchronizer needed for the block RAM itself, and the write side is
    // CPU-paced configuration (a palette load, not a value sampled every
    // cycle), the same informal-CDC precedent blt_src_base etc. already use.
    input  wire        clut_wr,
    input  wire [7:0]  clut_waddr,
    input  wire [15:0] clut_wdata,

    // SDRAM master port (clk_sdram) -> wired to sdram_fb in core_game.vh ----
    input  wire        sdram_init_complete,
    output reg  [24:0] p0_addr,
    output reg  [15:0] p0_data,
    output reg  [1:0]  p0_byte_en,
    output reg  [10:0] p0_wr_len,
    output reg         p0_wr_stream,
    input  wire [15:0] p0_q,
    output reg         p0_wr_req,
    output reg         p0_rd_req,
    output reg         p0_end_burst_req,
    input  wire        p0_available,
    input  wire        p0_ready,
    input  wire        p0_data_available,

    // Streaming-write source port: sdram_fb pulls one word per burst beat from
    // here when p0_wr_stream was set on the request. Used by CHAR, whose rows
    // are varying-colour (a constant-data burst cannot express a glyph).
    input  wire [10:0] wsrc_addr,
    output wire [15:0] wsrc_q,

    // Video output (clk_vid domain) ------------------------------------------
    output reg  [23:0] video_rgb,
    output reg         video_de,
    output reg         video_hs,
    output reg         video_vs
);

    // ---- Geometry ----------------------------------------------------------
    localparam H_ACT = 11'd400, V_ACT = 11'd360;
    localparam H_TOT = 11'd500, V_TOT = 11'd400;
    localparam [10:0] HOFF = 11'd8,  VOFF = 11'd4;
    localparam [10:0] HS_ST = HOFF + H_ACT + 11'd12, HS_EN = HS_ST + 11'd40;
    localparam [10:0] VS_ST = VOFF + V_ACT + 11'd3,  VS_EN = VS_ST + 11'd4;
    localparam [9:0]  STRIDE = 10'd512;             // words/line, page-aligned
    localparam [24:0] FB_BASE = 25'd0;

    localparam [2:0] OP_RUN = 3'd0, OP_RECT = 3'd1, OP_CHAR = 3'd2, OP_COPY = 3'd3,
                     OP_BLIT = 3'd4, OP_BAR = 3'd5, OP_SBLIT = 3'd6, OP_CBLIT = 3'd7;
    // COPY moves a w x h block SDRAM->SDRAM. It exists for the album-art panel:
    // sliding an image by re-sending its pixels from the CPU would be thousands
    // of commands per animation step and would starve the decoder, whereas the
    // engine can read a row and write it back with the CPU issuing ONE command
    // for the whole block. Source address rides in the fg/bg fields, which a
    // copy has no other use for.
    //
    // BLIT (Phase F B1, PHASE_F_SPEC.md section 5) generalises COPY: same w x h
    // block move, same row-at-a-time streaming-write datapath, but source and
    // destination are each `sticky_base + flat_offset`, advancing by the sticky
    // STRIDE per row instead of COPY's fixed FB_BASE=0/512. This is genuinely
    // cheap -- the per-row step was already a plain add, so swapping the
    // hardcoded 512 for a register costs nothing extra. What is NOT generalised
    // here (deliberately, a follow-up not this delivery): the row-buffer width
    // limit COPY already has (`glyphbuf` is 128 entries, so widths above 127
    // silently truncate) -- fixing that needs a wider row buffer, which is an
    // M10K/MLAB cost decision of its own, not bundled into an addressing change.
    //
    // BAR (Phase F B6, section 5): "A bar is (x, base_y, height, lit, unlit)."
    // Reuses RECT's exact single-row-per-cycle write loop TWICE per command --
    // no new burst mechanism -- chained by the existing rect_rows==1 completion
    // check in A_WRWAIT. cmd_addr is the top-left of the whole height-row span
    // (same top-left convention every other opcode uses); cmd_w/cmd_h are the
    // span's width/height exactly as RECT; cmd_glyph (otherwise unused outside
    // CHAR) carries the LIT row count, clamped to cmd_h. Convention, since nothing
    // upstream pins one down: lit rows are the BOTTOM of the span (the usual
    // meter-fills-from-the-floor reading of "base_y"), unlit rows the top. Colours
    // reuse cmd_fg (lit) / cmd_bg (unlit), the same "no other use for these
    // fields" reasoning COPY's source-address packing already established.
    //
    // B3 (sub-pixel skew + first/last-column masks): NOT built here, on purpose,
    // after real analysis rather than being skipped quietly. The Amiga mechanism
    // section 5 describes is bit-level -- a barrel shifter and word masks for a
    // format that packs many 1bpp pixels per word. This engine is one pixel per
    // SDRAM word; there is no sub-word packing here to mask or shift, so the
    // literal mechanism does not translate. What it WOULD buy -- an arbitrary
    // source column offset and an output width independent of the source
    // rectangle -- OP_BLIT already has, from B1's own generalised addressing; no
    // new hardware is needed for blits. The one real gap is CHAR-specific: the
    // marquee's own comment ("does not clip one partially off the left edge") is
    // about sub-GLYPH clipping, and retrofitting cmd_w/cmd_h onto CHAR to carry
    // clip fields is NOT safe without a firmware change first -- fw/player.c's
    // fb_char() never writes R_FB_SIZE at all, so cmd_w/cmd_h at CHAR dispatch
    // time is whatever an unrelated earlier fb_rect()/fb_copy_span() left there.
    // Confirmed by reading the source, not assumed. Needs firmware coordination
    // (new, dedicated clip fields firmware actually sets before every fb_char()
    // call), which is out of scope for an RTL-only delivery.
    //
    // B4 (scaled blit, nearest): OP_SBLIT. Reuses CHAR's own Bresenham
    // machinery -- literally the same char_num/char_den/acc_x/char_numy/
    // char_deny/acc_y registers, since CHAR and a blit are never in flight at
    // the same time -- against a variable SOURCE width/height (cmd_w/cmd_h,
    // safe here because OP_SBLIT is a brand new opcode with no existing caller
    // to break) instead of the fixed 16px font cell. Per the section 5 finding
    // ("no line buffer needed... one read per output pixel"): every output
    // pixel issues its OWN single-word SDRAM read at the Bresenham-selected
    // source column, going back through A_IDLE between pixels like every other
    // transaction here -- scanout can still preempt between ANY two words, not
    // just between rows. Destination still advances by the sticky DST_STRIDE
    // every output row (reusing blit_dst_addr/blit_mode from B1 unchanged);
    // only the SOURCE row advances, and only when the Y-Bresenham says to.
    // Same 128-entry glyphbuf/127-word limit as OP_COPY/OP_BLIT, same reason,
    // same "not silently fixed here" note.

    // Declared before the scale helpers consume them. Quartus accepted the
    // former declaration-after-use ordering, but standards-strict simulators
    // could not elaborate the module.
    wire [1:0] q_sx;
    wire [1:0] q_sy;

    // scale sel -> (num, den) and the resulting painted extent of a 16px cell
    function [5:0] scale_nd(input [1:0] sel);
        case (sel)
            2'd0:    scale_nd = {3'd1, 3'd1};   // 1x
            2'd1:    scale_nd = {3'd2, 3'd3};   // 1.5x
            2'd2:    scale_nd = {3'd1, 3'd2};   // 2x
            default: scale_nd = {3'd1, 3'd3};   // 3x
        endcase
    endfunction
    wire [5:0] nd_x = scale_nd(q_sx);
    wire [5:0] nd_y = scale_nd(q_sy);
    wire [8:0] ext_x = scale_ext(q_sx);
    wire [8:0] ext_y = scale_ext(q_sy);
    function [8:0] scale_ext(input [1:0] sel);
        case (sel)
            2'd0: scale_ext = 9'd16;
            2'd1: scale_ext = 9'd24;
            2'd2: scale_ext = 9'd32;
            default: scale_ext = 9'd48;
        endcase
    endfunction

    // B4: the same scale_nd() ratios as CHAR/scale_ext, but against a variable
    // SOURCE width instead of a fixed 16px cell. Multiply-by-small-constant
    // (x3) plus a shift, not a general divider -- cheap, matching section 5's
    // "0 M10K" for this feature (this is ALM cost, a different budget, but
    // still small: a 9-bit-by-3 multiply is adds, not a DSP block). Clamped to
    // 127 -- the same glyphbuf-width limit OP_COPY/OP_BLIT already carry,
    // documented there and not silently widened here either.
    function [8:0] sblit_ext(input [8:0] src, input [1:0] sel);
        reg [10:0] scaled;
        begin
            case (sel)
                2'd0: scaled = {2'd0, src};
                2'd1: scaled = ({2'd0, src} * 11'd3) >> 1;
                2'd2: scaled = {2'd0, src} << 1;
                default: scaled = {2'd0, src} * 11'd3;
            endcase
            sblit_ext = (scaled > 11'd127) ? 9'd127 : scaled[8:0];
        end
    endfunction

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
    // Async FIFO (clk_sys write -> clk_sdram read), Gray-coded pointers.
    // Same CDC idiom as pocket_vector_fb.sv's vector FIFO -- proven on
    // hardware there.
    //
    // 256 deep, was 512: a command is now a whole rect or a whole glyph
    // rather than a single row, so the queue holds vastly more DRAWING per
    // entry. A full-screen clear went from 360 entries to 1. Wider entries
    // (88b vs 44b) at half the depth costs the same three M10K blocks, which
    // matters -- this core is already at ~90% BRAM.
    // ======================================================================
    localparam FAW = 8;                              // 256 entries
    localparam CW  = 88;                             // command width
    (* ramstyle = "M10K" *) reg [CW-1:0] cmd_mem [0:255];
    reg [FAW:0] wr_ptr = 0, wr_ptr_g = 0;             // clk_sys
    reg [FAW:0] rd_ptr = 0, rd_ptr_g = 0;             // clk_sdram

    function [FAW:0] b2g(input [FAW:0] b); b2g = b ^ (b >> 1); endfunction
    function [FAW:0] g2b(input [FAW:0] g);
        integer i;
        begin
            g2b[FAW] = g[FAW];
            for (i = FAW-1; i >= 0; i = i - 1) g2b[i] = g2b[i+1] ^ g[i];
        end
    endfunction

    reg [FAW:0] rd_ptr_g_s1 = 0, rd_ptr_g_s2 = 0;
    always @(posedge clk_sys) begin
        rd_ptr_g_s1 <= rd_ptr_g; rd_ptr_g_s2 <= rd_ptr_g_s1;
    end
    wire [FAW:0] rd_ptr_bin = g2b(rd_ptr_g_s2);
    wire [FAW:0] fifo_fill  = wr_ptr - rd_ptr_bin;
    assign cmd_full = (fifo_fill >= {1'b0, {FAW{1'b1}}});

    always @(posedge clk_sys) begin
        if (reset) begin
            wr_ptr <= 0; wr_ptr_g <= 0;
        end else if (cmd_push && !cmd_full) begin
            cmd_mem[wr_ptr[FAW-1:0]] <= {cmd_op, cmd_addr, cmd_fg, cmd_bg,
                                         cmd_w, cmd_h, cmd_glyph,
                                         cmd_sx, cmd_sy, 5'd0};
            wr_ptr   <= wr_ptr + 1'b1;
            wr_ptr_g <= b2g(wr_ptr + 1'b1);
        end
    end

    reg [FAW:0] wr_ptr_g_s1 = 0, wr_ptr_g_s2 = 0;
    always @(posedge clk_sdram) begin
        wr_ptr_g_s1 <= wr_ptr_g; wr_ptr_g_s2 <= wr_ptr_g_s1;
    end
    wire fifo_empty = (rd_ptr_g == wr_ptr_g_s2);

    wire [CW-1:0] cmd_mem_rd = cmd_mem[rd_ptr[FAW-1:0]];
    reg [CW-1:0] cmd_q;
    always @(posedge clk_sdram) cmd_q <= cmd_mem_rd;
    wire [2:0]  q_op    = cmd_q[87:85];
    wire [18:0] q_addr  = cmd_q[84:66];
    wire [15:0] q_fg    = cmd_q[65:50];
    wire [15:0] q_bg    = cmd_q[49:34];
    wire [8:0]  q_w     = cmd_q[33:25];
    wire [8:0]  q_h     = cmd_q[24:16];
    assign q_sx = cmd_q[8:7];
    assign q_sy = cmd_q[6:5];

    // BAR (B6): lit-row count from cmd_glyph, clamped to the span height.
    // Retimed (B-110): computed off cmd_mem_rd -- the same raw BRAM read data
    // cmd_q itself registers from -- on the SAME clock edge as cmd_q, instead
    // of combinationally from cmd_q afterward. This was found to be the tail
    // of the worst setup path once TAU_BLIT_BLEND's congestion was removed
    // (a clamp compare + subtract feeding straight into the char_fg register
    // in the same cycle as cmd_q's own BRAM-output register): q_bar_lit/
    // q_bar_unlit are already valid registers by the time cmd_q is used for
    // dispatch, so the OP_BAR mux below only sees a plain 2:1 select, not the
    // compare/subtract chain. Same function of the same source data, so BAR
    // command behaviour is bit-for-bit unchanged -- only the pipeline stage
    // the arithmetic sits in moved.
    wire [8:0] pre_bar_lit_raw = {2'd0, cmd_mem_rd[15:9]};   // cmd_glyph field
    wire [8:0] pre_bar_h       = cmd_mem_rd[24:16];          // cmd_h field
    wire [8:0] pre_bar_lit     = (pre_bar_lit_raw > pre_bar_h) ? pre_bar_h : pre_bar_lit_raw;
    reg  [8:0] q_bar_lit, q_bar_unlit;
    always @(posedge clk_sdram) begin
        q_bar_lit   <= pre_bar_lit;
        q_bar_unlit <= pre_bar_h - pre_bar_lit;
    end
    wire [8:0] bar_lit   = q_bar_lit;
    wire [8:0] bar_unlit = q_bar_unlit;

    // B4 (OP_SBLIT): output extent from source width/height (q_w/q_h) and scale.
    // Retimed (B-113, following the audit that found this has the identical shape
    // to the BAR bug B-111 fixed): computed off cmd_mem_rd -- the same raw BRAM
    // read cmd_q itself registers from -- on the SAME clock edge as cmd_q, instead
    // of combinationally after it (multiply/shift + compare + clamp, chained
    // straight into char_w/char_rows_left in OP_SBLIT's dispatch). Same function
    // of the same source data, so SBLIT output-extent behaviour is unchanged.
    reg [8:0] q_sblit_out_w, q_sblit_out_h;
    always @(posedge clk_sdram) begin
        q_sblit_out_w <= sblit_ext(cmd_mem_rd[33:25], cmd_mem_rd[8:7]);   // q_w, q_sx
        q_sblit_out_h <= sblit_ext(cmd_mem_rd[24:16], cmd_mem_rd[6:5]);   // q_h, q_sy
    end
    wire [8:0] sblit_out_w = q_sblit_out_w;
    wire [8:0] sblit_out_h = q_sblit_out_h;

    // OP_CHAR: glyph-atlas base offset, retimed the same way (B-113) -- a milder
    // instance of the same shape (two compares, a subtract, a mux) off raw
    // cmd_q/q_glyph, chained into char_base in the dispatch cycle. Computed off
    // cmd_mem_rd on cmd_q's own clock edge instead.
    wire [6:0] pre_glyph = cmd_mem_rd[15:9];
    reg [11:0] q_char_base;
    always @(posedge clk_sdram)
        q_char_base <= ((pre_glyph >= 7'h20) && (pre_glyph <= 7'h7E))
                     ? {1'b0, (pre_glyph - 7'h20), 5'd0}
                     : 12'd0;

    // ======================================================================
    // Scanout line buffer: parity-split double buffer, exactly as
    // pocket_vector_fb.sv -- one half drains to clk_vid while the other half
    // fills from clk_sdram for the NEXT line, so fill and scanout of the same
    // line never race.
    // ======================================================================
    (* ramstyle = "M10K" *) reg [15:0] linebuf [0:1023];
    reg        lb_we;
    reg [9:0]  lb_waddr;
    reg [15:0] lb_wdata;
    always @(posedge clk_sdram) if (lb_we) linebuf[lb_waddr] <= lb_wdata;

    // ======================================================================
    // Glyph row buffer -- one composed output row of a CHAR, handed to
    // sdram_fb's streaming-write port one word per burst beat. 64 entries =
    // the widest glyph (8 source px * 2 EPX * 4x scale). Small enough that
    // Quartus will use MLAB rather than spending an M10K.
    // ======================================================================
    // 128 entries: shared by CHAR (max 48 px wide) and COPY, whose width is the
    // album-art panel rather than a glyph.
`ifdef TAU_MLAB_MIGRATE
    // PHASE_F_SPEC.md section 2: every saved fit report shows zero MLAB
    // usage despite the comment above hoping for it -- Quartus was not
    // inferring MLAB on its own, so this forces it explicitly. Simple-dual-
    // port (one write port, one read port), so it is MLAB-legal.
    (* ramstyle = "MLAB, no_rw_check" *) reg [15:0] glyphbuf [0:127];
`else
    reg [15:0] glyphbuf [0:127];
`endif
    reg [15:0] glyph_q;
    always @(posedge clk_sdram) glyph_q <= glyphbuf[wsrc_addr[6:0]];
    assign wsrc_q = glyph_q;

    // ---- Font ROM (generated; see tools/gen_font_rom.py) -------------------
    reg  [11:0] font_addr;
    wire [31:0] font_q;
    font_rom u_font (.clk(clk_sdram), .addr(font_addr), .q(font_q));

    // ---- Phase F B8: 256-entry CLUT (one M10K, per PHASE_F_SPEC.md's budget) --
    // Independent write (clk_sys) / read (clk_sdram) ports -- a standard
    // dual-clock block RAM, same registered-read idiom as glyph_q above.
    reg [15:0] clut [0:255];
    always @(posedge clk_sys) if (clut_wr) clut[clut_waddr] <= clut_wdata;
    // clut_raddr is COMBINATIONAL (p0_q's low byte, valid whenever a source word
    // is present), not a register: clut_q's own update below fires on the SAME
    // clk_sdram edge A_CBLIT_RD captures p0_q, so it needs the address available
    // that same cycle to land in clut_q one cycle later, in A_CBLIT_WAIT. A
    // registered clut_raddr (set <= this edge, valid only NEXT edge) would put
    // the CLUT's answer a cycle later than A_CBLIT_WAIT expects it -- found by
    // simulation (an 'x' in the dump), not spotted in review.
    wire [7:0] clut_raddr = p0_q[7:0];
    reg [15:0] clut_q;
    always @(posedge clk_sdram) clut_q <= clut[clut_raddr];

    // ======================================================================
    // Engine + arbiter (clk_sdram).
    //
    // A_IDLE is the single dispatch point, and every unit of work returns to
    // it, so scanout FILL -- the only deadline in the design -- can preempt
    // between any two bursts.
    // ======================================================================
    localparam A_IDLE=4'd0, A_FILL=4'd1, A_FILL_END=4'd2, A_WRWAIT=4'd3,
               A_ROWFETCH=4'd4, A_COMPOSE=4'd5, A_COPYRD=4'd6, A_KEYDST=4'd7,
               A_SBLIT=4'd8, A_CBLIT_RD=4'd9, A_CBLIT_WAIT=4'd10;
    reg [3:0]  astate = A_IDLE;
    reg [10:0] fill_cnt = 0;

    reg       fill_req_tgl = 0;
    reg [9:0] fill_line_req = 0;
    reg [2:0] fill_req_s = 0;
    wire      fill_req_edge = (fill_req_s[2] ^ fill_req_s[1]);
    reg       fill_pending = 0;
    reg [9:0] fill_line = 0;
    reg       fill_lb = 0;

    // RECT/RUN state (a RUN is just a RECT of height 1 -- one code path)
    reg        rect_active = 0;
    reg [18:0] rect_addr;
    reg [8:0]  rect_w, rect_rows;

    // CHAR state
    reg        char_rows_left_nz = 0;   // more rows still to COMPOSE
    reg [18:0] char_addr;
    reg [15:0] char_fg, char_bg;
    reg [1:0]  char_sx, char_sy;
    reg [8:0]  char_rows_left;
    reg        char_row_ready = 0;      // glyphbuf holds a row awaiting write
    reg [6:0]  char_w;                  // 16 * (sx+1)
    // Fractional scaling by Bresenham rather than integer replication: the
    // source position advances num/den per output pixel, so 2/3 gives 1.5x.
    // Integer-only scaling meant the smallest step above 16px was 32px --
    // double, with nothing usable in between for typographic hierarchy.
    reg [3:0]  ey;                      // source row 0..15
    reg [2:0]  acc_y;                   // vertical Bresenham accumulator
    reg [6:0]  ox;                      // output column being composed
    reg [3:0]  ex;                      // source column 0..15
    reg [2:0]  acc_x;                   // horizontal accumulator
    reg [2:0]  char_num, char_den;      // X: src pixels per output pixel
    reg [2:0]  char_numy, char_deny;    // Y: same, latched at command pop
    reg [1:0]  rowf_cnt;                // row-fetch sequencer
    reg [31:0] rowlo, rowhi;            // one source row, 16 px x 4bpp
    reg [11:0] char_base;               // (glyph - 0x20) * 32

    // Which unit of work the in-flight burst belongs to
    reg        wr_is_char;

    // COPY state. Reuses the CHAR row-write path (streaming burst out of
    // glyphbuf); only the way a row gets INTO glyphbuf differs.
    reg        copy_mode;
    reg [18:0] copy_src;
    reg [7:0]  copy_cnt;

    // BLIT state (Phase F B1). Full 25-bit addresses (base + offset already
    // summed at dispatch), so it can reach anywhere in SDRAM, not just the
    // FB_BASE-relative 19-bit window COPY is confined to. blit_mode selects
    // this address pair over char_addr/copy_src at every site that drives
    // p0_addr or steps to the next row; everything else (A_COPYRD, A_WRWAIT,
    // the glyphbuf streaming write) is shared, unmodified COPY logic.
    reg        blit_mode;
    reg [24:0] blit_dst_addr, blit_src_addr;

    // BAR state (Phase F B6). A second, queued RECT segment: when the first
    // segment's last row retires in A_WRWAIT, if bar2_pending is set, the
    // engine re-arms rect_active with these values instead of going idle --
    // the width (rect_w) is shared between both segments and untouched.
    reg        bar2_pending;
    reg [18:0] bar2_addr;
    reg [8:0]  bar2_rows;
    reg [15:0] bar2_fg;

    // B2 state: whether THIS row's destination has already been pre-read into
    // glyphbuf. Set when A_KEYDST's burst completes, cleared at the end of
    // every A_COPYRD (so it is always fresh 0 at the start of a new row/command
    // regardless of what the previous command left it at).
    reg        key_dst_done = 1'b0;

    // B4 (OP_SBLIT) state. sblit_mode selects the conditional (Y-Bresenham-
    // gated) source row step in A_WRWAIT over BLIT's own unconditional one;
    // blit_dst_addr/blit_mode are reused unchanged for the destination side.
    // char_num/char_den/acc_x (X) and char_numy/char_deny/acc_y (Y) are
    // CHAR's own Bresenham registers, reused here rather than duplicated --
    // CHAR and a blit are never in flight at the same time.
    reg        sblit_mode;
    reg [8:0]  sblit_ex;              // current source column, 0..source_w-1
    reg [24:0] sblit_src_row_addr;    // current source ROW's base address

    // B8 (Phase F, section 5 detailed design) state. cblit_mode selects the
    // one-pixel-at-a-time read path (A_CBLIT_RD/A_CBLIT_WAIT below) over
    // COPY/BLIT's multi-word A_COPYRD burst -- deliberately, so the CLUT's own
    // one-cycle M10K read latency never has to be pipelined into that shared,
    // already-hardware-verified burst loop. blit_mode is ALSO set (reused,
    // exactly as OP_SBLIT already does) purely to get blit_dst_addr/
    // blit_src_addr's existing per-row sticky-stride step in A_WRWAIT for free.
    reg        cblit_mode;

    // ---- 4bpp coverage sampling (combinational) --------------------------
    // rowbits holds the CURRENT source row: 16 pixels x 4 bits, fetched as two
    // 32-bit words before the row is composed, so every pixel of the row is
    // available without another ROM access mid-compose.
    wire [63:0] rowbits = {rowhi, rowlo};
    wire [3:0]  cov     = rowbits[{ex, 2'b00} +: 4];

    // The coverage value IS the anti-aliasing -- blend fg->bg by it directly.
    //
    // The weight is NOT the coverage. RGB565 is gamma-encoded (~2.2), so
    // interpolating code values linearly does not interpolate LIGHT: a pixel at
    // half coverage, weighted 8/16, emits about 22% of the foreground rather
    // than 50%. Every partially covered pixel was therefore under-lit, which on
    // light-on-dark type reads as thin, hazy stems -- "not crisp". It never
    // looked "too dark", which is why it went unnoticed for so long.
    //
    // These weights are fitted to the palette this core actually draws, over
    // the background ramp it actually draws on, luma-weighted. Fitting rather
    // than deriving because one table has to serve every fg/bg pair; the fit
    // cuts RMS error by 3x to 16x per level, worst at the extremes and best in
    // the midtones. Regenerate with tools/gen_text_gamma.py if the palette
    // changes materially; tools/gen_text_gamma.py --check asserts they agree.
    //
    // 0 and 15 stay exact. An untouched pixel IS the background and a solid one
    // IS the foreground; fitting those would trade two exactly-right answers
    // for a slightly better average and make glyph interiors subtly wrong.
    //
    // Cost: 16 five-bit constants. Synthesises to logic -- no M10K, no DSP, and
    // the multiplier it feeds is the one that was already there.
    function [4:0] cov_weight(input [3:0] c);
        case (c)
            4'd0 : cov_weight = 5'd0;
            4'd1 : cov_weight = 5'd4;
            4'd2 : cov_weight = 5'd6;
            4'd3 : cov_weight = 5'd7;
            4'd4 : cov_weight = 5'd8;
            4'd5 : cov_weight = 5'd10;
            4'd6 : cov_weight = 5'd10;
            4'd7 : cov_weight = 5'd11;
            4'd8 : cov_weight = 5'd12;
            4'd9 : cov_weight = 5'd13;
            4'd10: cov_weight = 5'd13;
            4'd11: cov_weight = 5'd14;
            4'd12: cov_weight = 5'd15;
            4'd13: cov_weight = 5'd15;
            4'd14: cov_weight = 5'd16;
            4'd15: cov_weight = 5'd16;
        endcase
    endfunction

    wire [4:0] cov16 = cov_weight(cov);
    wire [4:0] inv16 = 5'd16 - cov16;

    wire [10:0] mix_r = char_fg[15:11] * cov16 + char_bg[15:11] * inv16;
    wire [11:0] mix_g = char_fg[10:5]  * cov16 + char_bg[10:5]  * inv16;
    wire [10:0] mix_b = char_fg[4:0]   * cov16 + char_bg[4:0]   * inv16;

    wire [15:0] px_color = {mix_r[8:4], mix_g[9:4], mix_b[8:4]};

    // ---- B5: alpha blend (combinational) ---------------------------------
    // One function, reused for R/G/B by passing the channel's own max value --
    // cheaper than three near-identical copies. DSP mode approximates /255 as
    // >>8 (weight 256, not 255), the same pragmatic trade CHAR's own cov_weight
    // above already makes (documented there); at alpha=255 the destination
    // still contributes 1/256, a known, accepted artifact of this common
    // approximation, not an oversight. PSX modes are exactly the shift-add
    // ratios section 5 lists: B/2+F/2 (average), B+F and B+F/4 (saturating
    // add), B-F (saturating subtract, clamped to 0 not wrapped).
    function [7:0] blend_ch(input [7:0] b, input [7:0] f, input [2:0] mode,
                             input [7:0] alpha, input [7:0] chmax);
        reg [15:0] dsp;
        reg [8:0]  sum;
        begin
            case (mode)
                3'd0: begin
                    dsp = f * {1'b0, alpha} + b * (9'd256 - {1'b0, alpha});
                    blend_ch = dsp[15:8];
                end
                3'd1: blend_ch = (b + f) >> 1;
                3'd2: begin sum = {1'b0,b} + {1'b0,f};      blend_ch = (sum > {1'b0,chmax}) ? chmax : sum[7:0]; end
                3'd3: blend_ch = (f > b) ? 8'd0 : (b - f);
                default: begin sum = {1'b0,b} + {1'b0,(f >> 2)}; blend_ch = (sum > {1'b0,chmax}) ? chmax : sum[7:0]; end
            endcase
        end
    endfunction

    // B (destination, already sitting in glyphbuf from the A_KEYDST pre-read)
    // and F (source, the word A_COPYRD just read) -- see that state below for
    // where this is actually used. Channels zero-extended to 8 bits so one
    // function serves the 5-bit R/B and 6-bit G channels alike.
    function [15:0] blend_px(input [15:0] bg, input [15:0] fg, input [2:0] mode, input [7:0] alpha);
        reg [7:0] r, g, b;
        begin
            r = blend_ch({3'd0,bg[15:11]}, {3'd0,fg[15:11]}, mode, alpha, 8'd31);
            g = blend_ch({2'd0,bg[10:5]},  {2'd0,fg[10:5]},  mode, alpha, 8'd63);
            b = blend_ch({3'd0,bg[4:0]},   {3'd0,fg[4:0]},   mode, alpha, 8'd31);
            blend_px = {r[4:0], g[5:0], b[4:0]};
        end
    endfunction

    // B2/B5: whether the word A_COPYRD just read is keyed out. Explicitly
    // checks blt_key_en, not just key_dst_done -- key_dst_done alone used to
    // imply blt_key_en (the only thing that could trigger a pre-read), but
    // now blt_blend_en can trigger one too, so without this check a blend-only
    // blit could accidentally treat a pixel equal to a stale/leftover blt_key
    // value as keyed, even with keying never enabled.
    wire pixel_keyed = key_dst_done && blt_key_en && (p0_q == blt_key) && !BUG_IGNORE_KEY;

    // B5: gated separately from blt_blend_en's own sticky enable bit, per
    // BLIT_BLEND_ENABLE's own comment above.
    wire blend_active = (BLIT_BLEND_ENABLE != 0) && blt_blend_en;

    // Dispatch guards: a new command may only be popped once the previous one
    // has fully retired, and any SDRAM work needs the controller idle.
    wire engine_busy = rect_active || char_rows_left_nz || char_row_ready;
    wire can_sdram   = p0_available && sdram_init_complete;

    always @(posedge clk_sdram) begin
        p0_wr_req        <= 1'b0;
        p0_rd_req        <= 1'b0;
        p0_end_burst_req <= 1'b0;
        p0_wr_stream     <= 1'b0;
        lb_we            <= 1'b0;

        fill_req_s <= {fill_req_s[1:0], fill_req_tgl};
        if (fill_req_edge) begin
            fill_pending <= 1'b1;
            fill_line    <= fill_line_req;
            fill_lb      <= fill_line_req[0];
        end

        if (reset) begin
            astate <= A_IDLE;
            fill_pending <= 1'b0;
            rect_active <= 1'b0;
            char_rows_left_nz <= 1'b0;
            char_row_ready <= 1'b0;
            copy_mode <= 1'b0;
            blit_mode <= 1'b0;
            bar2_pending <= 1'b0;
            key_dst_done <= 1'b0;
            sblit_mode   <= 1'b0;
            cblit_mode   <= 1'b0;
            rd_ptr <= 0; rd_ptr_g <= 0;
        end else begin
            case (astate)
                // ---------------------------------------------------- IDLE --
                A_IDLE: begin
                    if (fill_pending && can_sdram) begin
                        p0_addr   <= FB_BASE + {6'd0, fill_line, 9'd0};   // *512
                        p0_rd_req <= 1'b1;
                        fill_cnt  <= 0;
                        astate    <= A_FILL;

                    // A composed glyph row is written with a STREAMING burst:
                    // each beat's data comes from glyphbuf via wsrc_q.
                    end else if (char_row_ready && can_sdram) begin
                        p0_addr      <= blit_mode ? blit_dst_addr : (FB_BASE + {6'd0, char_addr});
                        p0_byte_en   <= 2'b11;
                        p0_wr_len    <= {4'd0, char_w};
                        p0_wr_stream <= 1'b1;
                        p0_wr_req    <= 1'b1;
                        wr_is_char   <= 1'b1;
                        astate       <= A_WRWAIT;

                    // One rect row = one constant-data burst.
                    end else if (rect_active && can_sdram) begin
                        p0_addr      <= FB_BASE + {6'd0, rect_addr};
                        p0_data      <= char_fg;   // shared fill-colour reg
                        p0_byte_en   <= 2'b11;
                        p0_wr_len    <= {2'b00, rect_w};
                        p0_wr_stream <= 1'b0;
                        p0_wr_req    <= 1'b1;
                        wr_is_char   <= 1'b0;
                        astate       <= A_WRWAIT;

                    // Composing needs no SDRAM, so it runs only once nothing
                    // else wants the bus -- it can never delay a fill by more
                    // than the one row it is part-way through.
                    // B2/B5: a keyed or blended BLIT pre-reads the destination
                    // row into glyphbuf before the source row, so A_COPYRD's
                    // per-word compare/blend below has something to work with.
                    // Every other case (COPY, a plain BLIT) goes straight to
                    // A_COPYRD exactly as before -- key_dst_done starts each
                    // row/command at 0, so this adds nothing unless one of the
                    // two sticky enables is set.
                    end else if (copy_mode && char_rows_left_nz
                                 && !char_row_ready && can_sdram
                                 && blit_mode && (blt_key_en || blend_active) && !key_dst_done) begin
                        p0_addr   <= blit_dst_addr;
                        p0_rd_req <= 1'b1;
                        copy_cnt  <= 8'd0;
                        astate    <= A_KEYDST;
                    end else if (copy_mode && char_rows_left_nz
                                 && !char_row_ready && can_sdram) begin
                        p0_addr   <= blit_mode ? blit_src_addr : (FB_BASE + {6'd0, copy_src});
                        p0_rd_req <= 1'b1;
                        copy_cnt  <= 8'd0;
                        astate    <= A_COPYRD;
                    // B4: one source-pixel read per output column, each its own
                    // transaction back through A_IDLE (see the module-header
                    // comment) -- scanout can preempt between any two pixels,
                    // not just between rows.
                    end else if (sblit_mode && char_rows_left_nz
                                 && !char_row_ready && can_sdram) begin
                        p0_addr   <= sblit_src_row_addr + {16'd0, sblit_ex};
                        p0_rd_req <= 1'b1;
                        astate    <= A_SBLIT;

                    // Phase F B8: one source word per pixel, same shape as SBLIT's own
                    // one-word-per-transaction read above -- copy_cnt tracks the column
                    // within the row (reset at dispatch, and again per row in A_WRWAIT).
                    end else if (cblit_mode && char_rows_left_nz
                                 && !char_row_ready && can_sdram) begin
                        p0_addr   <= blit_src_addr + {17'd0, copy_cnt};
                        p0_rd_req <= 1'b1;
                        astate    <= A_CBLIT_RD;

                    end else if (!copy_mode && !sblit_mode && !cblit_mode && char_rows_left_nz && !char_row_ready) begin
                        rowf_cnt <= 2'd0;
                        astate   <= A_ROWFETCH;

                    end else if (!fifo_empty && !engine_busy && can_sdram) begin
                        rd_ptr   <= rd_ptr + 1'b1;
                        rd_ptr_g <= b2g(rd_ptr + 1'b1);
                        char_fg  <= q_fg;
                        case (q_op)
                            OP_CHAR: begin
                                char_addr <= q_addr;
                                char_bg   <= q_bg;
                                copy_mode <= 1'b0;
                                blit_mode <= 1'b0;
                                bar2_pending <= 1'b0;
                                sblit_mode   <= 1'b0;
                                cblit_mode   <= 1'b0;
                                char_sx   <= q_sx;
                                char_sy   <= q_sy;
                                // EPX doubles 8x8 -> 16x16, then each axis is
                                // replicated (scale+1) times: 16*(sx+1) wide,
                                // 16*(sy+1) rows. Max 64x64.
                                char_num  <= nd_x[5:3];
                                char_den  <= nd_x[2:0];
                                char_numy <= nd_y[5:3];
                                char_deny <= nd_y[2:0];
                                char_w            <= ext_x[6:0];
                                char_rows_left    <= ext_y;
                                char_rows_left_nz <= 1'b1;
                                ey <= 4'd0; acc_y <= 3'd0;
                                /* Glyphs below 0x20 or above 0x7E fall back to
                                 * space rather than reading past the atlas. */
                                char_base <= q_char_base;
                                rowf_cnt  <= 2'd0;
                                astate    <= A_ROWFETCH;
                            end
                            OP_RECT: begin
                                rect_addr   <= q_addr;
                                rect_w      <= q_w;
                                rect_rows   <= q_h;
                                rect_active <= (q_h != 9'd0) && (q_w != 9'd0);
                                blit_mode   <= 1'b0;
                                bar2_pending <= 1'b0;
                                sblit_mode   <= 1'b0;
                                cblit_mode   <= 1'b0;
                            end
                            OP_COPY: begin
                                copy_src  <= {q_fg[2:0], q_bg};
                                copy_mode <= 1'b1;
                                blit_mode <= 1'b0;
                                bar2_pending <= 1'b0;
                                sblit_mode   <= 1'b0;
                                cblit_mode   <= 1'b0;
                                char_addr <= q_addr;
                                char_w    <= q_w[6:0];
                                char_rows_left    <= q_h;
                                char_rows_left_nz <= (q_h != 9'd0) && (q_w != 9'd0);
                            end
                            // Phase F B1: same shape as OP_COPY, but source and destination
                            // are each `sticky_base + flat_offset` rather than FB_BASE/0.
                            // q_addr is the dest offset (as COPY's dest always was); the
                            // source offset reuses the exact {q_fg[2:0],q_bg} packing COPY
                            // already established. Per-row stepping (A_WRWAIT below) adds
                            // the sticky STRIDE instead of a fixed 512.
                            OP_BLIT: begin
                                copy_mode <= 1'b1;
                                blit_mode <= 1'b1;
                                bar2_pending <= 1'b0;
                                sblit_mode   <= 1'b0;
                                cblit_mode   <= 1'b0;
                                blit_dst_addr <= blt_dst_base + {6'd0, q_addr};
                                blit_src_addr <= blt_src_base + {6'd0, {q_fg[2:0], q_bg}};
                                char_w    <= q_w[6:0];
                                char_rows_left    <= q_h;
                                char_rows_left_nz <= (q_h != 9'd0) && (q_w != 9'd0);
                            end
                            // Phase F B6: two chained RECT fills (see the module-header
                            // comment above for the field convention). Phase 1 fires now
                            // (unlit, top of the span, if any rows); phase 2 (lit, bottom)
                            // is queued and fires from A_WRWAIT when phase 1's last row
                            // retires. If there is no unlit segment, skip straight to lit.
                            OP_BAR: begin
                                blit_mode  <= 1'b0;
                                sblit_mode <= 1'b0;
                                cblit_mode <= 1'b0;
                                rect_addr <= q_addr;
                                rect_w    <= q_w;
                                if (bar_unlit != 9'd0) begin
                                    rect_rows    <= bar_unlit;
                                    rect_active  <= (q_w != 9'd0);
                                    char_fg      <= q_bg;   // unlit colour first
                                    bar2_addr    <= q_addr + {1'b0, bar_unlit, 9'd0};
                                    bar2_rows    <= bar_lit;
                                    bar2_fg      <= q_fg;   // lit colour, queued
                                    bar2_pending <= (bar_lit != 9'd0) && (q_w != 9'd0);
                                end else begin
                                    rect_rows    <= bar_lit;
                                    rect_active  <= (bar_lit != 9'd0) && (q_w != 9'd0);
                                    char_fg      <= q_fg;   // fully-lit bar, no phase 2
                                    bar2_pending <= 1'b0;
                                end
                            end
                            // Phase F B4: see the module-header comment above. Output
                            // extent (sblit_out_w/h) and the Bresenham ratios (nd_x/nd_y,
                            // shared with CHAR) are computed once here; A_IDLE issues one
                            // source-pixel read per output column from here on.
                            OP_SBLIT: begin
                                copy_mode  <= 1'b0;   // NOT the COPY/BLIT burst-read path
                                blit_mode  <= 1'b1;   // reuse blit_dst_addr's per-row dest step
                                sblit_mode <= 1'b1;
                                cblit_mode <= 1'b0;
                                bar2_pending <= 1'b0;
                                blit_dst_addr      <= blt_dst_base + {6'd0, q_addr};
                                sblit_src_row_addr <= blt_src_base + {6'd0, {q_fg[2:0], q_bg}};
                                char_num  <= nd_x[5:3];
                                char_den  <= nd_x[2:0];
                                char_numy <= nd_y[5:3];
                                char_deny <= nd_y[2:0];
                                char_w            <= sblit_out_w[6:0];
                                char_rows_left    <= sblit_out_h;
                                char_rows_left_nz <= (sblit_out_w != 9'd0) && (sblit_out_h != 9'd0);
                                sblit_ex <= 9'd0; acc_x <= 3'd0; acc_y <= 3'd0;
                                copy_cnt <= 8'd0;
                            end
                            // Phase F B8 ("B8 detailed design", PHASE_F_SPEC.md section 5): CLUT
                            // blit. Reuses OP_BLIT's own addressing (blit_dst_addr/blit_src_addr,
                            // sticky base+stride via blit_mode) but reads ONE source word per
                            // pixel (A_CBLIT_RD/A_CBLIT_WAIT below), like OP_SBLIT's single-word
                            // transactions -- not OP_COPY/OP_BLIT's multi-word A_COPYRD burst --
                            // specifically so the CLUT's one-cycle M10K read latency never has to
                            // be pipelined into that shared, already-hardware-verified burst loop.
                            OP_CBLIT: begin
                                copy_mode  <= 1'b0;
                                blit_mode  <= 1'b1;   // reuse blit_dst_addr/blit_src_addr's per-row stride step
                                sblit_mode <= 1'b0;
                                cblit_mode <= 1'b1;
                                bar2_pending <= 1'b0;
                                blit_dst_addr <= blt_dst_base + {6'd0, q_addr};
                                blit_src_addr <= blt_src_base + {6'd0, {q_fg[2:0], q_bg}};
                                char_w    <= q_w[6:0];
                                char_rows_left    <= q_h;
                                char_rows_left_nz <= (q_h != 9'd0) && (q_w != 9'd0);
                                copy_cnt  <= 8'd0;
                            end
                            default: begin   // OP_RUN -- a one-row rect
                                rect_addr   <= q_addr;
                                rect_w      <= q_w;
                                rect_rows   <= 9'd1;
                                rect_active <= (q_w != 9'd0);
                                cblit_mode  <= 1'b0;
                                blit_mode   <= 1'b0;
                                bar2_pending <= 1'b0;
                                sblit_mode   <= 1'b0;
                            end
                        endcase
                    end
                end

                // ------------------------------------------- scanout FILL --
                A_FILL: begin
                    if (p0_data_available) begin
                        lb_we    <= 1'b1;
                        lb_waddr <= {fill_lb, fill_cnt[8:0]};
                        lb_wdata <= p0_q;
                        if (fill_cnt == {1'b0, STRIDE} - 1'b1) begin
                            p0_end_burst_req <= 1'b1;
                            astate <= A_FILL_END;
                        end else fill_cnt <= fill_cnt + 1'b1;
                    end
                end
                A_FILL_END: begin
                    fill_pending <= 1'b0;
                    if (p0_available) astate <= A_IDLE;
                end

                // ------------------------------------- burst write retire --
                A_WRWAIT: if (p0_ready) begin
                    if (wr_is_char) begin
                        char_row_ready <= 1'b0;
                        char_addr      <= char_addr + 19'd512;   // next row (CHAR/COPY)
                        copy_src       <= copy_src  + 19'd512;
                        // BLIT's own address pair steps by the sticky stride instead
                        // (dead but harmless for every other op, same as ey/acc_y below).
                        blit_dst_addr  <= blit_dst_addr + (BUG_IGNORE_BLIT_STRIDE ? 25'd512 : {15'd0, blt_dst_stride});
                        blit_src_addr  <= blit_src_addr + (BUG_IGNORE_BLIT_STRIDE ? 25'd512 : {15'd0, blt_src_stride});
                        char_rows_left <= char_rows_left - 9'd1;
                        if (char_rows_left == 9'd1) begin
                            char_rows_left_nz <= 1'b0;
                            copy_mode         <= 1'b0;
                            blit_mode         <= 1'b0;
                            sblit_mode        <= 1'b0;
                            cblit_mode        <= 1'b0;
                        end
                        // B4: reset the per-row pixel scan for the NEXT output row.
                        // Dead but harmless for every other op (same reasoning as the
                        // char_addr/copy_src/blit_*_addr steps above, which SBLIT
                        // itself never reads).
                        if (sblit_mode) begin
                            copy_cnt <= 8'd0;
                            sblit_ex <= 9'd0;
                            acc_x    <= 3'd0;
                        end
                        // B8: same per-row reset, cblit's own column counter only
                        // (it has no source-column Bresenham state to reset).
                        if (cblit_mode) copy_cnt <= 8'd0;
                        // Bresenham step in Y: same accumulator idea as X, so
                        // vertical scaling can be fractional too. SBLIT steps its
                        // OWN source-row address (sblit_src_row_addr, by the sticky
                        // STRIDE) instead of CHAR's font-cell row index (ey).
                        if (acc_y + char_numy >= char_deny) begin
                            acc_y <= acc_y + char_numy - char_deny;
                            if (sblit_mode)
                                sblit_src_row_addr <= sblit_src_row_addr + {15'd0, blt_src_stride};
                            else
                                ey <= ey + 4'd1;
                        end else begin
                            acc_y <= acc_y + char_numy;
                        end
                    end else begin
                        rect_addr <= rect_addr + 19'd512;        // next row
                        rect_rows <= rect_rows - 9'd1;
                        if (rect_rows == 9'd1) begin
                            // Phase F B6: the last row of the current segment just
                            // retired. If a second (lit) segment is queued, re-arm
                            // rect_active with it instead of going idle -- these
                            // overrides win over the two lines above (same cycle,
                            // same always block: last non-blocking assign to a
                            // signal wins).
                            if (bar2_pending) begin
                                rect_addr    <= bar2_addr;
                                rect_rows    <= bar2_rows;
                                rect_active  <= 1'b1;
                                char_fg      <= bar2_fg;
                                bar2_pending <= 1'b0;
                            end else begin
                                rect_active <= 1'b0;
                            end
                        end
                    end
                    astate <= A_IDLE;
                end

                // ------------------------------------------- copy row read --
                // B2: when this row's destination was pre-read (key_dst_done),
                // a source word equal to the sticky KEY colour is dropped --
                // glyphbuf already holds that destination pixel from A_KEYDST,
                // so simply not overwriting it IS the "show destination
                // through" behaviour. B5: otherwise, if blending is enabled,
                // combine the just-read source with the destination pixel
                // A_KEYDST already left sitting in this same glyphbuf slot --
                // an ordinary read-then-write of one memory word, not a
                // same-cycle read/write race (the read reflects A_KEYDST's
                // write from several cycles earlier, not this one). Key takes
                // priority over blend: a keyed pixel is fully transparent, so
                // blending it would be wrong, not just redundant.
                A_COPYRD: begin
                    if (p0_data_available) begin
                        if (!pixel_keyed) begin
                            if (key_dst_done && blend_active && !BUG_BLEND_ALWAYS_SRC)
                                glyphbuf[copy_cnt[6:0]] <= blend_px(glyphbuf[copy_cnt[6:0]], p0_q,
                                                                     blt_blend_mode, blt_blend_alpha);
                            else
                                glyphbuf[copy_cnt[6:0]] <= p0_q;
                        end
                        if (copy_cnt == char_w[6:0] - 7'd1) begin
                            p0_end_burst_req <= 1'b1;
                            char_row_ready   <= 1'b1;
                            key_dst_done     <= 1'b0;   // fresh for the next row
                            astate <= A_IDLE;
                        end else copy_cnt <= copy_cnt + 8'd1;
                    end
                end

                // -------------------------------- B2: destination pre-read --
                // Structurally identical to A_COPYRD's own read loop (same
                // burst-into-glyphbuf shape), but unconditional -- every
                // destination pixel is kept until the source phase decides
                // whether to overwrite it.
                A_KEYDST: begin
                    if (p0_data_available) begin
                        glyphbuf[copy_cnt[6:0]] <= p0_q;
                        if (copy_cnt == char_w[6:0] - 7'd1) begin
                            p0_end_burst_req <= 1'b1;
                            key_dst_done     <= 1'b1;
                            astate <= A_IDLE;
                        end else copy_cnt <= copy_cnt + 8'd1;
                    end
                end

                // -------------------------------------- B4: one-pixel read --
                // Response to the single-word read A_IDLE just issued. Stores
                // it, ends that one-word transaction, and either marks the row
                // done (char_row_ready, same write path every other opcode
                // uses) or steps the X-Bresenham and goes back to A_IDLE for
                // the NEXT pixel's read -- never chains a second request
                // directly from here.
                A_SBLIT: begin
                    if (p0_data_available) begin
                        glyphbuf[copy_cnt[6:0]] <= p0_q;
                        p0_end_burst_req <= 1'b1;
                        if (copy_cnt == char_w[6:0] - 7'd1) begin
                            char_row_ready <= 1'b1;
                        end else begin
                            copy_cnt <= copy_cnt + 8'd1;
                            if (BUG_SBLIT_NO_SCALE || (acc_x + char_num >= char_den)) begin
                                acc_x    <= acc_x + char_num - char_den;
                                sblit_ex <= sblit_ex + 9'd1;
                            end else begin
                                acc_x <= acc_x + char_num;
                            end
                        end
                        astate <= A_IDLE;
                    end
                end

                // -------------------------------------- B8: CLUT blit read --
                // Two cycles per pixel, same "one word, its own transaction"
                // shape as A_SBLIT above: A_CBLIT_RD ends the single-word burst
                // the instant the source word arrives (clut_raddr, a wire, is
                // already presenting p0_q's low byte to the CLUT that same
                // cycle); A_CBLIT_WAIT is the one cycle clut_q's registered
                // (M10K) output needs to become valid, then writes it into
                // glyphbuf exactly where every other opcode's per-pixel write
                // lands.
                A_CBLIT_RD: begin
                    if (p0_data_available) begin
                        p0_end_burst_req <= 1'b1;
                        astate           <= A_CBLIT_WAIT;
                    end
                end
                A_CBLIT_WAIT: begin
                    glyphbuf[copy_cnt[6:0]] <= BUG_CBLIT_NO_LOOKUP ? {8'd0, clut_raddr} : clut_q;
                    if (copy_cnt == char_w[6:0] - 7'd1) begin
                        char_row_ready <= 1'b1;
                    end else begin
                        copy_cnt <= copy_cnt + 8'd1;
                    end
                    astate <= A_IDLE;
                end

                // ----------------------------------------- glyph row fetch --
                // Two words = one 16-pixel source row. font_rom is registered,
                // so the word requested at count N arrives at N+2; fetching the
                // row up front means composing it needs no ROM port at all.
                A_ROWFETCH: begin
                    case (rowf_cnt)
                        2'd0: font_addr <= char_base + {7'd0, ey, 1'b0};
                        2'd1: font_addr <= char_base + {7'd0, ey, 1'b1};
                        2'd2: rowlo <= font_q;
                        2'd3: begin
                            rowhi  <= font_q;
                            ox <= 7'd0; ex <= 4'd0; acc_x <= 3'd0;
                            astate <= A_COMPOSE;
                        end
                    endcase
                    rowf_cnt <= rowf_cnt + 2'd1;
                end

                // ------------------------------------------ glyph compose --
                // One output pixel per cycle into glyphbuf. Worst case 64
                // cycles for a 4x glyph -- 1.5% of a scanline's slack.
                A_COMPOSE: begin
                    glyphbuf[ox[5:0]] <= px_color;
                    if (ox == char_w - 7'd1) begin
                        char_row_ready <= 1'b1;
                        astate <= A_IDLE;
                    end else begin
                        ox <= ox + 7'd1;
                        if (acc_x + char_num >= char_den) begin
                            acc_x <= acc_x + char_num - char_den;
                            ex    <= ex + 4'd1;
                        end else begin
                            acc_x <= acc_x + char_num;
                        end
                    end
                end

                default: astate <= A_IDLE;
            endcase
        end
    end

    // ======================================================================
    // Video timing (clk_vid domain) -- same structure as pocket_vector_fb.sv.
    // ======================================================================
    reg [10:0] hc = 0, vc = 0;
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

endmodule

`default_nettype wire
