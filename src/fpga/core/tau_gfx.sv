// ============================================================================
// tau_gfx.sv -- the graphics path top (Talos 2, T2, D-T02): joins tau_talos (command FIFO, draw
// engine, SDRAM master port, fonts, CLUT) and tau_scanout (video timing, line buffer). It
// replaces mp3_fb.sv with the identical port list, so core_game.vh changes only the module
// name. No behaviour change: sim/tb_tau_gfx_diff.v compares it with the preserved original
// (sim/oracle/tau_talos1_oracle.sv) cycle by cycle.
// ============================================================================

`default_nettype none

module tau_gfx #(
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
    parameter BUG_CBLIT_NO_LOOKUP = 0,
    // Mutation-test hook only (-PBUG_IGNORE_REINDEX=1, make test-rtl-fb-mutation):
    // 1 forces the CLUT read address to the raw source index always, ignoring
    // the sticky blt_reindex offset -- proves B9's own test actually exercises
    // the offset add, not just that a CBLIT with reindex=0 still works. Never
    // set outside that test.
    parameter BUG_IGNORE_REINDEX = 0,
    // Mutation-test hook only (-PBUG_IGNORE_TEXT_LIGHT=1, make test-rtl-fb-mutation): 1 ignores
    // the text_light input and always uses the dark-polarity weight table -- proves the scene test
    // really exercises the second table. Never set outside that test.
    parameter BUG_IGNORE_TEXT_LIGHT = 0,
    // Mutation-test hook only (-PBUG_IGNORE_RC_CUT=1, make test-rtl-fb-mutation): 1 forces
    // rrect_cut to always read 0, degenerating B11's rounded-rect into a square rect (the LUT
    // mechanism is never actually exercised) -- proves the test catches a corner-cut table that
    // silently does nothing, not just that an RRECT with a real table still draws a fill. Never
    // set outside that test.
    parameter BUG_IGNORE_RC_CUT = 0,
    // Mutation-test hook only (-PBUG_IGNORE_BAR_HI=1, make test-rtl-fb-mutation): 1 forces
    // pre_bar_lit_raw's high 2 bits (cmd_glyph_hi) to always read 0, degenerating BAR's lit-row
    // count back to its old 7-bit-only behaviour -- proves the test actually exercises a lit count
    // above 127, not just that BAR with a small lit count still draws correctly. Never set outside
    // that test.
    parameter BUG_IGNORE_BAR_HI = 0,
    // Helios H2 (docs/HELIOS_SPEC.md section 5, B-340): double buffering via base-pointer swap.
    // Inert (dbuf_addr() is a no-op, byte-identical to today) when 0.
    parameter DBUF_ENABLE = 0,
    // Writable font memory (docs/features/FONT_SPEC.md, TAU_FONT_RAM): the glyph atlas is a RAM the CPU
    // loads (Latin-1, 6,112 words, 13-bit address) instead of the 3,040-word ASCII ROM. Inert (the old ROM,
    // byte-identical behaviour) when 0. FONT_BUG_* are mutation hooks for sim/tb_font_ram.v.
    parameter FONT_RAM_ENABLE = 0,
    parameter BUG_FONT_NO_HI = 0,        // 1: glyph bit 7 (cmd_glyph_hi[0]) ignored -> 0x80.. codes read the ASCII range
    parameter BUG_FONT_RANGE = 0,        // 1: the 0xA0.. range starts one glyph too high
    parameter BUG_FONT_NO_WRITE = 0      // 1: the CPU write port is ignored
) (
    input  wire        reset,
    input  wire        clk_sys,     // CPU / FIFO write domain
    input  wire        clk_sdram,   // SDRAM controller + engine (~100 MHz)
    input  wire        clk_vid,     // pixel clock (12 MHz)

    // CPU draw command (clk_sys domain) -------------------------------------
    input  wire        cmd_push,
    input  wire [3:0]  cmd_op,      // 0=RUN 1=RECT 2=CHAR 3=COPY 4=BLIT 5=BAR 6=SBLIT 7=CBLIT 8=RRECT (B11)
    input  wire [18:0] cmd_addr,    // word address of top-left, y*512+x
    input  wire [8:0]  cmd_w,       // RUN: run length; RECT: width (words)
    input  wire [8:0]  cmd_h,       // RECT: height (rows)
    input  wire [15:0] cmd_fg,      // RGB565 fill / glyph foreground
    input  wire [15:0] cmd_bg,      // RGB565 glyph background
    input  wire [6:0]  cmd_glyph,   // CHAR: ASCII code; BAR: lit-row count, LOW 7 bits (see cmd_glyph_hi)
    input  wire [1:0]  cmd_sx,      // CHAR: h scale 0=1x 1=1.5x 2=2x 3=3x
    input  wire [1:0]  cmd_sy,      // CHAR: v scale, same encoding
    // B12 (docs/AUDIT_TRAIL.md B-454): BAR's lit-row count widened 7->9 bits (max 127 -> 511), enough
    // for the tallest fullscreen bar (FS_FIG_H=323). A NEW, separate field rather than widening
    // cmd_glyph itself -- CHAR (ASCII, 7 bits is exactly right) and OP_RRECT (radius, clamped to 15)
    // both keep reading cmd_glyph's original 7-bit [14:8] slice completely unchanged; only BAR's own
    // lit-count math additionally consults this. Claims 2 of the FIFO word's 4 remaining padding bits
    // (CW stays 88) and 2 of R_FB_GO's genuinely-unused bits (15:16) at the tau_soc.v boundary -- see
    // that file's own comment. On any bitstream without this port at all (old RTL), the write simply
    // has nowhere to land; BAR_WIDE_READY() (fw/blit_probe.inc) is the runtime test that tells firmware
    // which behaviour it is actually talking to, so old and new bitstreams both behave safely.
    input  wire [1:0]  cmd_glyph_hi,
    // Writable font memory, clk_sys domain (tau_soc.v registers FONT_IDX/FONT_DATA); ignored when FONT_RAM_ENABLE = 0.
    input  wire        font_we,
    input  wire [12:0] font_waddr,
    input  wire [31:0] font_wdata,
    output wire        cmd_full,
    // Theme step 0b/gamma (docs/THEME_SPEC.md, tools/gen_text_gamma.py): 0 = the text weight table
    // fitted for light text on a dark ramp (cov_weight), 1 = the table fitted for dark text on a
    // light ramp (cov_weight_light). Already in the clk_sdram domain (the caller synchronises it);
    // quasi-static, changed only by the theme switch which repaints everything afterwards.
    input  wire        text_light,

    // Phase F B1 (section 9): sticky blit addressing state, from tau_soc.v's
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

    // B11 (section 5): the corner-cut lookup table, loaded by the CPU (clk_sys) through
    // tau_soc.v's R_RC_IDX/R_RC_DATA -- a bulk table load, own register pair (not folded into
    // R_BLT_IDX/DATA, same reasoning B8's own CLUT-vs-sticky-field split already gives). 16
    // entries x 5 bits, indexed by dy = r - row (never by the row itself -- see the module-header
    // comment on OP_RRECT). Plain flops (80 bits, no M10K), informal CDC: CPU-paced configuration
    // loaded once per radius change, not sampled every cycle, same precedent blt_src_base etc.
    // already use.
    input  wire [79:0] rc_cut_lut,

    // Phase F B9 (section 5, Tier 2): palette re-index for dim/highlight. An
    // 8-bit offset added to OP_CBLIT's palette index before the CLUT lookup --
    // the Genesis shadow/highlight trick (re-index, don't blend). Read only
    // when cblit_mode; every other opcode is unaffected. Default 0 is a true
    // no-op (index + 0 = index), so this needs no separate enable bit the way
    // B2/B5 do.
    input  wire [7:0]  blt_reindex,

    // Phase F B8 (section 5, "B8 detailed design"): 256-entry CLUT, loaded by
    // the CPU (clk_sys) through tau_soc.v's R_CLUT_IDX/R_CLUT_DATA. Independent
    // write (clk_sys) / read (clk_sdram) ports, a standard dual-clock M10K --
    // no synchronizer needed for the block RAM itself, and the write side is
    // CPU-paced configuration (a palette load, not a value sampled every
    // cycle), the same informal-CDC precedent blt_src_base etc. already use.
    input  wire        clut_wr,
    input  wire [7:0]  clut_waddr,
    input  wire [15:0] clut_wdata,

    // Helios H2 (B-340): double buffering. `dbuf_cpu_buf` is a plain port, but it is read on EVERY
    // ordinary (non-blit) dispatch -- as frequent as `text_light` -- so, like `text_light`, it arrives
    // here already synchronised into clk_sdram by the caller (`tau_cdc_sync1`, core_game.vh), never a
    // raw clk_sys level. It selects which physical copy of the VISIBLE framebuffer (row < V_ACT, i.e.
    // word address < DBUF_VISIBLE_WORDS) ordinary RECT/CHAR/COPY(non-blit) commands read and write;
    // every blit-mode opcode (BLIT/BAR/SBLIT/CBLIT/RRECT) already has fully firmware-programmable
    // addressing via the sticky blt_*_base fields and is untouched by this, and any address
    // >= DBUF_VISIBLE_WORDS (the packed off-screen stash region: art/thumbnail/Chladni/TIM1/probe rows)
    // is untouched regardless of dbuf_cpu_buf -- see the module-header note on why an unconditional
    // offset would corrupt that region. `dbuf_flip_req_tgl` toggles once per requested flip (clk_sys);
    // the flip itself is only ever applied during vertical blanking (clk_vid's own vs_pulse window),
    // synchronised into clk_sdram the same toggle-and-sample way `fill_req_tgl` already is below --
    // never a raw level crossing a clock domain. `dbuf_disp_buf`/`dbuf_flip_pending` are read back by
    // tau_soc for its status register; both are clk_sdram registers, informally sampled by clk_sys the
    // same way blt_* fields are sampled the other direction (a status bit changing at most once every
    // ~16.7 ms is not a hazard worth a formal synchroniser).
    input  wire        dbuf_cpu_buf,
    input  wire        dbuf_flip_req_tgl,
    output wire        dbuf_disp_buf,
    output wire        dbuf_flip_pending,

    // SDRAM master port (clk_sdram) -> wired to tau_sdram_ctrl in core_game.vh ----
    input  wire        sdram_init_complete,
    output wire  [24:0] p0_addr,
    output wire  [15:0] p0_data,
    output wire  [1:0]  p0_byte_en,
    output wire  [10:0] p0_wr_len,
    output wire         p0_wr_stream,
    input  wire [15:0] p0_q,
    output wire         p0_wr_req,
    output wire         p0_rd_req,
    output wire         p0_end_burst_req,
    input  wire        p0_available,
    input  wire        p0_ready,
    input  wire        p0_data_available,

    // Streaming-write source port: tau_sdram_ctrl pulls one word per burst beat from
    // here when p0_wr_stream was set on the request. Used by CHAR, whose rows
    // are varying-colour (a constant-data burst cannot express a glyph).
    input  wire [10:0] wsrc_addr,
    output wire [15:0] wsrc_q,

    // Video output (clk_vid domain) ------------------------------------------
    output wire  [23:0] video_rgb,
    output wire         video_de,
    output wire         video_hs,
    output wire         video_vs,
    // Helios beam position (B-267): the video line counter, 0..V_TOT-1 (V_TOT is 400, so 9 bits). The row being scanned
    // is vc - VOFF while VOFF <= vc < VOFF + V_ACT. Carried to the CPU by tau_cdc_gray_bus (it steps by +1 per line).
    output wire [8:0]  scan_vc
);

    wire        fill_req_tgl, vblank_tgl;
    wire [9:0]  fill_line_req;
    wire        lb_we;
    wire [9:0]  lb_waddr;
    wire [15:0] lb_wdata;

    tau_talos #(
        .BUG_IGNORE_BLIT_STRIDE(BUG_IGNORE_BLIT_STRIDE),
        .BUG_IGNORE_KEY(BUG_IGNORE_KEY),
        .BUG_SBLIT_NO_SCALE(BUG_SBLIT_NO_SCALE),
        .BLIT_BLEND_ENABLE(BLIT_BLEND_ENABLE),
        .BUG_BLEND_ALWAYS_SRC(BUG_BLEND_ALWAYS_SRC),
        .BUG_CBLIT_NO_LOOKUP(BUG_CBLIT_NO_LOOKUP),
        .BUG_IGNORE_REINDEX(BUG_IGNORE_REINDEX),
        .BUG_IGNORE_TEXT_LIGHT(BUG_IGNORE_TEXT_LIGHT),
        .BUG_IGNORE_RC_CUT(BUG_IGNORE_RC_CUT),
        .BUG_IGNORE_BAR_HI(BUG_IGNORE_BAR_HI),
        .DBUF_ENABLE(DBUF_ENABLE),
        .FONT_RAM_ENABLE(FONT_RAM_ENABLE),
        .BUG_FONT_NO_HI(BUG_FONT_NO_HI),
        .BUG_FONT_RANGE(BUG_FONT_RANGE),
        .BUG_FONT_NO_WRITE(BUG_FONT_NO_WRITE)
    ) u_talos (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push),
        .cmd_op(cmd_op),
        .cmd_addr(cmd_addr),
        .cmd_w(cmd_w),
        .cmd_h(cmd_h),
        .cmd_fg(cmd_fg),
        .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph),
        .cmd_sx(cmd_sx),
        .cmd_sy(cmd_sy),
        .cmd_glyph_hi(cmd_glyph_hi),
        .font_we(font_we),
        .font_waddr(font_waddr),
        .font_wdata(font_wdata),
        .cmd_full(cmd_full),
        .text_light(text_light),
        .blt_src_base(blt_src_base),
        .blt_src_stride(blt_src_stride),
        .blt_dst_base(blt_dst_base),
        .blt_dst_stride(blt_dst_stride),
        .blt_key_en(blt_key_en),
        .blt_key(blt_key),
        .blt_blend_en(blt_blend_en),
        .blt_blend_mode(blt_blend_mode),
        .blt_blend_alpha(blt_blend_alpha),
        .rc_cut_lut(rc_cut_lut),
        .blt_reindex(blt_reindex),
        .clut_wr(clut_wr),
        .clut_waddr(clut_waddr),
        .clut_wdata(clut_wdata),
        .dbuf_cpu_buf(dbuf_cpu_buf),
        .dbuf_flip_req_tgl(dbuf_flip_req_tgl),
        .dbuf_disp_buf(dbuf_disp_buf),
        .dbuf_flip_pending(dbuf_flip_pending),
        .sdram_init_complete(sdram_init_complete),
        .p0_addr(p0_addr),
        .p0_data(p0_data),
        .p0_byte_en(p0_byte_en),
        .p0_wr_len(p0_wr_len),
        .p0_wr_stream(p0_wr_stream),
        .p0_q(p0_q),
        .p0_wr_req(p0_wr_req),
        .p0_rd_req(p0_rd_req),
        .p0_end_burst_req(p0_end_burst_req),
        .p0_available(p0_available),
        .p0_ready(p0_ready),
        .p0_data_available(p0_data_available),
        .wsrc_addr(wsrc_addr),
        .wsrc_q(wsrc_q),
        .fill_req_tgl(fill_req_tgl), .fill_line_req(fill_line_req), .vblank_tgl(vblank_tgl),
        .lb_we(lb_we), .lb_waddr(lb_waddr), .lb_wdata(lb_wdata)
    );

    tau_scanout u_scanout (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push),
        .lb_we(lb_we), .lb_waddr(lb_waddr), .lb_wdata(lb_wdata),
        .fill_req_tgl(fill_req_tgl), .fill_line_req(fill_line_req), .vblank_tgl(vblank_tgl),
        .video_rgb(video_rgb), .video_de(video_de), .video_hs(video_hs), .video_vs(video_vs),
        .scan_vc(scan_vc)
    );

endmodule

`default_nettype wire
