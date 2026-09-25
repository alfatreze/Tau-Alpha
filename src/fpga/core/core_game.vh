// =============================================================================
// core_game.vh -- MP3 player core logic, `included into the frozen core_top.v.
//
// This is NOT an arcade port: there is no MiSTer RTL and no game ROM. The core
// is a small SoC -- VexRiscv + BRAM + peripherals -- running C firmware that is
// itself loaded from the SD card like a ROM.
//
// Why firmware-from-SD rather than baked into the bitstream: a firmware change
// then costs a rebuild measured in seconds instead of a full Quartus compile.
// Stages 2-4 are almost entirely firmware work, so this is the single biggest
// lever on iteration speed (CLAUDE.md: "cut compile->test cycles").
//
// Everything here is inside module core_top, so the shell's nets -- including
// the target_dataslot_* registers -- are in scope and drivable from here. The
// shell itself stays untouched.
// =============================================================================

// -- 1. PLL / clocks ----------------------------------------------------------
// outclk_0 = 60 MHz  CPU/system.  Stage 0 measured 45.7 MHz as the worst-case
//            requirement (320 kbps) at 0 wait states. Bring-up ran at 50 MHz,
//            which left too little headroom once UI drawing and SD reads shared
//            the CPU with the decoder -- audible tics under load. 60 MHz is the
//            shipping speed and still closes timing comfortably.
// outclk_1 = 12 MHz  pixel clock: 500x400 total = exactly 60.000 Hz, driving
//            a 400x360 active image -- an exact 4x integer scale of the
//            Pocket's native 1600x1440 panel (confirmed via Analogue's own
//            spec: the panel is built around integer scaling).
// outclk_2 = 12 MHz shifted 90 degrees (APF requires both edges).
// outclk_3 = 100 MHz SDRAM framebuffer controller (mp3_fb.sv / sdram_fb.sv).
//            No phase shift needed -- confirmed against HarpMudd.starwars'
//            proven SDRAM PLL: the DAC-side DDR forwarding happens inside the
//            controller itself, not via a separately-phased clock input.
wire clk_sys;
wire clk_vid;
wire clk_vid_90;
wire clk_sdram;
wire pll_locked;
wire pll_locked_s;

mf_pllbase mp1 (
    .refclk   (clk_74a),
    .rst      (1'b0),
    .outclk_0 (clk_sys),
    .outclk_1 (clk_vid),
    .outclk_2 (clk_vid_90),
    .outclk_3 (clk_sdram),
    .locked   (pll_locked)
);

synch_3 s_pll (pll_locked, pll_locked_s, clk_74a);

// -- 2. Firmware load + streamed data via the APF bridge ----------------------
// One data_loader serves BOTH purposes: the boot-time firmware image AND the
// bytes APF pushes back during a target-slot read, because both arrive as
// bridge writes. Target reads therefore land directly in the SoC's own RAM at
// whatever bridge address the firmware asked for -- no separate receive buffer.
// ADDRESS_SIZE 18 covers the 256 KB RAM.
wire [17:0] dn_addr;
wire [7:0]  dn_data;
wire        dn_wr;
reg         fw_loaded_74 = 1'b0;
wire        fw_loaded;

// Shell contract: core_top.v drives status_setup_done from rom_loaded_s, and it
// must be in the clk_74a domain (APF needs a rising edge once loading is done).
wire        rom_loaded_s = fw_loaded_74;

synch_3 s_fw_to_sys (fw_loaded_74, fw_loaded, clk_sys);

data_loader #(
    .ADDRESS_MASK_UPPER_4 (4'h0),
    .ADDRESS_SIZE         (18),
    .OUTPUT_WORD_SIZE     (1)
) u_fw_loader (
    .clk_74a (clk_74a), .clk_memory (clk_sys),
    .bridge_wr (bridge_wr), .bridge_endian_little (bridge_endian_little),
    .bridge_addr (bridge_addr), .bridge_wr_data (bridge_wr_data),
    .write_en (dn_wr), .write_addr (dn_addr), .write_data (dn_data)
);

always @(posedge clk_74a) if (dataslot_allcomplete) fw_loaded_74 <= 1'b1;

// -- 3. Reset -----------------------------------------------------------------
// The CPU must stay in reset until the firmware image is fully in RAM,
// otherwise it executes whatever BRAM powers up holding.
wire reset_n_sys;
synch_3 s_resetn (reset_n, reset_n_sys, clk_sys);
reg  [7:0] reset_ctr = 8'hFF;
wire       cpu_reset_n = (reset_ctr == 8'h0) && fw_loaded && reset_n_sys;
wire       cpu_reset   = !cpu_reset_n;
always @(posedge clk_sys) begin
    if (!pll_locked)    reset_ctr <= 8'hFF;
    else if (reset_ctr) reset_ctr <= reset_ctr - 1'd1;
end

// -- 4. SoC -------------------------------------------------------------------
wire [15:0] soc_audio_l, soc_audio_r;
wire [31:0] soc_status0, soc_status1, soc_status2, soc_status3;
wire        soc_con_wr;
wire [7:0]  soc_con_char;

wire        soc_tgt_go;
wire [2:0]  soc_tgt_cmd_sel;
wire [3:0]  soc_set_idx;
wire        soc_set_wr;
wire [31:0] soc_set_wdata;
wire [31:0] soc_set_rdata;
wire [9:0]  soc_dt_addr;
wire        soc_dt_wren;
wire [31:0] soc_dt_wdata;
wire [15:0] soc_tgt_id;
wire [31:0] soc_tgt_slotoffset, soc_tgt_bridgeaddr, soc_tgt_length;
wire        soc_tgt_busy, soc_tgt_done;
wire [7:0]  soc_tgt_seq;
wire [2:0]  soc_tgt_err;

wire        soc_fb_cmd_push;
wire [3:0]  soc_fb_cmd_op;
wire [18:0] soc_fb_cmd_addr;
wire [8:0]  soc_fb_cmd_w, soc_fb_cmd_h;
wire [15:0] soc_fb_cmd_fg, soc_fb_cmd_bg;
wire [6:0]  soc_fb_cmd_glyph;
wire [1:0]  soc_fb_cmd_sx, soc_fb_cmd_sy;
wire        soc_fb_cmd_full;
// Phase F B1 (section 9): sticky blit addressing state, mp3_soc -> mp3_fb.
wire [24:0] soc_blt_src_base, soc_blt_dst_base;
wire [9:0]  soc_blt_src_stride, soc_blt_dst_stride;
// Phase F B2: colour-key transparency, sticky field 4.
wire        soc_blt_key_en;
wire [15:0] soc_blt_key;
// Phase F B5: alpha blend, sticky field 5.
wire        soc_blt_blend_en;
wire [2:0]  soc_blt_blend_mode;
wire [7:0]  soc_blt_blend_alpha;
// Phase F B9 (Tier 2): palette re-index offset, sticky field 6.
wire [7:0]  soc_blt_reindex;
// Phase F B8 ("B8 detailed design"): CLUT load, mp3_soc -> mp3_fb.
wire        soc_clut_wr;
wire [7:0]  soc_clut_waddr;
wire [15:0] soc_clut_wdata;
// B11 (section 5): corner-cut LUT, mp3_soc -> mp3_fb.
wire [79:0] soc_rc_cut_lut;

// SDRAM Phase 1 diagnostic mailbox.  These are MMIO-only controls; no normal
// instruction or data fetch is routed to external memory at this stage.
wire        soc_sdram_start, soc_sdram_write, soc_sdram_busy, soc_sdram_done;
wire [24:0] soc_sdram_addr;
wire [31:0] soc_sdram_wdata, soc_sdram_rdata;
wire [3:0]  soc_sdram_byte_en;
wire        soc_sdram_wb_req, soc_sdram_wb_write;
wire [24:0] soc_sdram_wb_addr;
wire [31:0] soc_sdram_wb_wdata, soc_sdram_wb_rdata;
wire [3:0]  soc_sdram_wb_byte_en;
wire        soc_sdram_wb_accept, soc_sdram_wb_done;
wire        soc_sdram_wb_debug_cpu_req, soc_sdram_wb_debug_we, soc_sdram_wb_debug_ack;
wire [31:0] soc_sdram_wb_debug_wdata;
wire        soc_sdram_wb_debug_unsupported;
wire [2:0]  soc_sdram_wb_debug_cti;
wire [3:0]  soc_sdram_wb_debug_sel;
wire        soc_sdram_wb_debug_cpu_ack;
wire [31:0] soc_sdram_wb_debug_adapter_rdata, soc_sdram_wb_debug_cpu_rdata;

// Phase 2 is enabled only by adding TAU_PHASE2_WINDOW to a dedicated
// build's Verilog macros. Release/default builds stay on the proven legacy
// BRAM/MMIO decode.
//
// TAU_PHASE2_WINDOW enables ONLY the CPU window. The controller-boundary debug
// taps, tau_sdram_cpu_window_probe and the red/green top-edge overlay are a
// separate opt-in, TAU_PHASE2_PROBE (with the return-path selectors
// TAU_PHASE2_MUX_PROBE / _ADAPTER_PROBE / _RETURN_PROBE, which are meaningless
// without it). Before A-113 the window macro switched the probe on as well.
// PSRAM CPU window (B-016): TAU_PSRAM_WINDOW needs the SDRAM Phase 2 decode (the legacy
// decode aliases 0xA400_0000.. onto MMIO) and, for now, the probe module that owns the
// controller (a window-only wrapper for product builds comes with P5).
`ifdef TAU_PSRAM_WINDOW
`ifndef TAU_PHASE2_WINDOW
`error "TAU_PSRAM_WINDOW requires TAU_PHASE2_WINDOW"
`endif
`ifndef TAU_PSRAM_PROBE
`error "TAU_PSRAM_WINDOW requires TAU_PSRAM_PROBE"
`endif
`define TAU_PSRAM_WIN_EN 1
`else
`define TAU_PSRAM_WIN_EN 0
`endif
// Phase G2: instruction fetch from PSRAM (alias 0x2400_0000, docs/PHASE_G_SPEC.md). Needs the PSRAM window.
`ifdef TAU_PSRAM_IFETCH
`ifndef TAU_PSRAM_WINDOW
`error "TAU_PSRAM_IFETCH requires TAU_PSRAM_WINDOW"
`endif
`define TAU_PSRAM_IFE_EN 1
`else
`define TAU_PSRAM_IFE_EN 0
`endif
// Phase F B7: SDRAM busy-cycle counter (PHASE_F_SPEC.md section 5). Independent of the
// PSRAM/Phase2 macros above -- it only observes the existing single-port SDRAM arbiter
// (tau_sdram_arbiter below), so it needs no RTL dependency on the blit engine itself.
`ifdef TAU_SDRAM_BUSY
`define TAU_SDR_BUSY_EN 1
`else
`define TAU_SDR_BUSY_EN 0
`endif
// Phase F B1: blit engine, starting with the generalised blit (section 5) and its sticky
// addressing state (section 9). Independent of the macros above.
`ifdef TAU_BLIT
`define TAU_BLIT_EN 1
`else
`define TAU_BLIT_EN 0
`endif
// Phase F B5: alpha blend, kept as its OWN macro per section 10's build plan
// (the documented -1.888 ns timing-cliff risk) -- droppable without losing
// B1/B2/B4/B6. `TAU_BLIT` alone does not imply this; both must be defined.
`ifdef TAU_BLIT_BLEND
`ifndef TAU_BLIT
`error "TAU_BLIT_BLEND requires TAU_BLIT"
`endif
`define TAU_BLIT_BLEND_EN 1
`else
`define TAU_BLIT_BLEND_EN 0
`endif
wire        soc_psram_req, soc_psram_we;
wire [22:0] soc_psram_word;
wire [31:0] soc_psram_wdata, soc_psram_rdata;
wire [3:0]  soc_psram_be;
wire        soc_psram_done, soc_psram_guard;

// Expansion-MMIO wires (declared before use: an implicit net would be 1 bit).
wire [7:0]  soc_xm_reg;
wire        soc_xm_wr;
wire [31:0] soc_xm_wdata;
wire [31:0] soc_xm_rdata;

// Phase F B7: SDRAM busy-cycle counter. `arb_p0_available` is sdram_fb's own p0_available
// (declared below, driven by u_sdram) -- per tau_sdram_arbiter.sv's own comment, that signal
// is asserted only while the single SDRAM port is idle with no request pending, so its inverse
// is exactly "the SDRAM port is busy", independent of which master (framebuffer or CPU) holds
// it. Counts in clk_sdram, a domain the MMIO decoder never sees directly, so it crosses via
// tau_cdc_gray_ctr (Gray-coded, matching mp3_fb.sv's own FIFO-pointer CDC) into clk_sys.
wire [31:0] soc_sdram_busy_rd;
`ifdef TAU_SDRAM_BUSY
tau_cdc_gray_ctr #(.WIDTH(32)) u_sdram_busy_ctr (
    .clk_src(clk_sdram), .rst_src(~pll_locked), .inc(~arb_p0_available),
    .clk_dst(clk_sys),   .rst_dst(cpu_reset),    .count_dst(soc_sdram_busy_rd)
);
`else
assign soc_sdram_busy_rd = 32'd0;
`endif

`ifdef TAU_PHASE2_WINDOW
mp3_soc #(.PHASE2_WINDOW_ENABLE(1), .PSRAM_WINDOW_ENABLE(`TAU_PSRAM_WIN_EN), .PSRAM_IFETCH_ENABLE(`TAU_PSRAM_IFE_EN), .SDRAM_BUSY_ENABLE(`TAU_SDR_BUSY_EN), .BLIT_ENABLE(`TAU_BLIT_EN)) u_soc (
`else
mp3_soc #(.SDRAM_BUSY_ENABLE(`TAU_SDR_BUSY_EN), .BLIT_ENABLE(`TAU_BLIT_EN)) u_soc (
`endif
    .clk     (clk_sys),
    .rst     (cpu_reset),
    .clk_74a (clk_74a),

    .ld_wr   (dn_wr),
    .ld_addr ({14'd0, dn_addr}),
    .ld_data (dn_data),

    // osnotify_inmenu lets firmware auto-pause when the Pocket's OS menu opens.
    // NOTE: a core cannot exit itself -- core_bridge_cmd implements only
    // 0180/0184/0190/0192, and leaving a core is an OS-level action.
    .cont_key (cont1_key),
    .in_menu  (osnotify_inmenu),

    // Fires when the user picks a new file via "Load MP3" (slot is User
    // Reloadable). Both wires already exist in core_top.v from
    // core_bridge_cmd -- no shell change needed, just routing.
    .dataslot_update      (dataslot_update),
    .dataslot_update_id   (dataslot_update_id),
    .dataslot_update_size (dataslot_update_size),

    // 008F "all slot access complete" -- the signal the BOOT path above
    // already gates on (fw_loaded_74). Routing it to the SoC too lets the
    // reload path wait for the same thing instead of reading the slot the
    // moment the user picks a file.
    .dataslot_allcomplete (dataslot_allcomplete),

    .audio_l (soc_audio_l),
    .audio_r (soc_audio_r),

    .status0 (soc_status0),
    .status1 (soc_status1),
    .status2 (soc_status2),
    .status3 (soc_status3),

    .con_wr   (soc_con_wr),
    .con_char (soc_con_char),

    .tgt_go          (soc_tgt_go),
    .tgt_cmd_sel     (soc_tgt_cmd_sel),
    .tgt_id          (soc_tgt_id),
    .tgt_slotoffset  (soc_tgt_slotoffset),
    .tgt_bridgeaddr  (soc_tgt_bridgeaddr),
    .tgt_length      (soc_tgt_length),
    .tgt_busy        (soc_tgt_busy),
    .tgt_done        (soc_tgt_done),
    .tgt_seq         (soc_tgt_seq),
    .tgt_err         (soc_tgt_err),

    .fb_cmd_push  (soc_fb_cmd_push),
    .fb_cmd_op    (soc_fb_cmd_op),
    .fb_cmd_addr  (soc_fb_cmd_addr),
    .fb_cmd_w     (soc_fb_cmd_w),
    .fb_cmd_h     (soc_fb_cmd_h),
    .fb_cmd_fg    (soc_fb_cmd_fg),
    .fb_cmd_bg    (soc_fb_cmd_bg),
    .fb_cmd_glyph (soc_fb_cmd_glyph),
    .fb_cmd_sx    (soc_fb_cmd_sx),
    .fb_cmd_sy    (soc_fb_cmd_sy),
    .fb_cmd_full  (soc_fb_cmd_full),

    .dt_addr  (soc_dt_addr),
    .dt_wren  (soc_dt_wren),
    .dt_wdata (soc_dt_wdata),
    .dt_q     (datatable_q),
    .set_idx  (soc_set_idx),
    .set_wr   (soc_set_wr),
    .set_wdata(soc_set_wdata),
    .set_rdata(soc_set_rdata),

    .sdram_start  (soc_sdram_start),
    .sdram_write  (soc_sdram_write),
    .sdram_addr   (soc_sdram_addr),
    .sdram_wdata  (soc_sdram_wdata),
    .sdram_byte_en(soc_sdram_byte_en),
    .sdram_busy   (sdram_mux_busy),
    .sdram_done   (sdram_mux_diag_done),
    .sdram_rdata  (sdram_mux_diag_rdata),

    .sdram_wb_req      (soc_sdram_wb_req),
    .sdram_wb_write    (soc_sdram_wb_write),
    .sdram_wb_addr     (soc_sdram_wb_addr),
    .sdram_wb_wdata    (soc_sdram_wb_wdata),
    .sdram_wb_byte_en  (soc_sdram_wb_byte_en),
    .sdram_wb_accept   (soc_sdram_wb_accept),
    .sdram_wb_done     (soc_sdram_wb_done),
    .sdram_wb_rdata    (soc_sdram_wb_rdata),
    .sdram_wb_debug_cpu_req(soc_sdram_wb_debug_cpu_req),
    .sdram_wb_debug_we     (soc_sdram_wb_debug_we),
    .sdram_wb_debug_wdata  (soc_sdram_wb_debug_wdata),
    .sdram_wb_debug_cti    (soc_sdram_wb_debug_cti),
    .sdram_wb_debug_sel    (soc_sdram_wb_debug_sel),
    .sdram_wb_debug_ack    (soc_sdram_wb_debug_ack),
    .sdram_wb_debug_unsupported(soc_sdram_wb_debug_unsupported),
    .sdram_wb_debug_adapter_rdata(soc_sdram_wb_debug_adapter_rdata),
    .sdram_wb_debug_cpu_ack(soc_sdram_wb_debug_cpu_ack),
    .sdram_wb_debug_cpu_rdata(soc_sdram_wb_debug_cpu_rdata),

    .xm_reg   (soc_xm_reg),
    .xm_wr    (soc_xm_wr),
    .xm_wdata (soc_xm_wdata),
    .xm_rdata (soc_xm_rdata),

    .psram_req   (soc_psram_req),
    .psram_we    (soc_psram_we),
    .psram_word  (soc_psram_word),
    .psram_wdata (soc_psram_wdata),
    .psram_be    (soc_psram_be),
    .psram_done  (soc_psram_done),
    .psram_rdata (soc_psram_rdata),
    .psram_guard (soc_psram_guard),

    .sdram_busy_rd (soc_sdram_busy_rd),

    .blt_src_base   (soc_blt_src_base),
    .blt_src_stride (soc_blt_src_stride),
    .blt_dst_base   (soc_blt_dst_base),
    .blt_dst_stride (soc_blt_dst_stride),
    .blt_key_en     (soc_blt_key_en),
    .blt_key        (soc_blt_key),
    .blt_blend_en   (soc_blt_blend_en),
    .blt_blend_mode (soc_blt_blend_mode),
    .blt_blend_alpha(soc_blt_blend_alpha),
    .blt_reindex    (soc_blt_reindex),

    .clut_wr   (soc_clut_wr),
    .clut_waddr(soc_clut_waddr),
    .clut_wdata(soc_clut_wdata),

    .rc_cut_lut(soc_rc_cut_lut)
);

// PSRAM diagnostic (P2). Opt-in TAU_PSRAM_PROBE only; it owns the CRAM pins and
// the expansion-MMIO window. Without the macro the pins stay tied idle in
// core_top.v and the window reads as zero.
`ifdef TAU_PSRAM_PROBE
tau_psram_probe #(.WIN_PRESENT(`TAU_PSRAM_WIN_EN)) u_psram (
    .clk(clk_sys), .rst(cpu_reset),
    .xm_reg(soc_xm_reg), .xm_wr(soc_xm_wr), .xm_wdata(soc_xm_wdata), .xm_rdata(soc_xm_rdata),
    .win_req(soc_psram_req), .win_we(soc_psram_we), .win_word(soc_psram_word),
    .win_wdata(soc_psram_wdata), .win_be(soc_psram_be),
    .win_done(soc_psram_done), .win_rdata(soc_psram_rdata), .win_guard(soc_psram_guard),
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
`else
assign soc_xm_rdata = 32'd0;
assign soc_psram_done = 1'b0;
assign soc_psram_rdata = 32'd0;
assign soc_psram_guard = 1'b0;
`endif

// core_bridge_cmd's datatable, user-side port. core_top declares these wires
// and connects them but never drives them, so this is the intended extension
// point -- and the ONLY way to hand APF a memory region it can read, since the
// shell's bridge_rd_data mux serves core_bridge_cmd alone.
assign datatable_addr = soc_dt_addr;
assign datatable_wren = soc_dt_wren;
assign datatable_data = soc_dt_wdata;

// Where APF fetches/deposits the 0192 parameter and 0190 response structs.
// The datatable lives at bridge 0xF8xx2xxx and is word-addressed
// (b_datatable_addr = bridge_addr >> 2), so these are 256 words apart and
// cannot overlap.
// mf_datatable is 256 WORDS (widthad = 8), so word 256 does not exist -- the
// address wraps and the parameter struct landed on top of the response struct
// at word 0. They must not overlap: 0192's parameters are built FROM a 0190
// response, so one clobbering the other is a read-after-write on itself.
// Response occupies words 0..63; parameters start at word 64.
// MEASURED 2026-08-05, not assumed. A boot-time dump of this BRAM before the
// core touched it read:
//
//   w0=1 w1=0x1CB40(117568)  w2=2 w3=0        w4=3 w5=0x395(917)  w6=4 w7=0x20(32)
//
// which is APF's DATASLOT ID/SIZE TABLE -- {slot_id, size} pairs at stride 2 --
// matching tau.rom, the empty audio slot, playlist.m3u and settings.bin
// exactly. Analogue's docs say a slot's size "is determined by the Dataslot
// ID/Size Table BRAM in the core"; this is that table.
//
// The response struct USED TO SIT AT WORD 0. APF writes 64 words there on every
// 0190 getfile, and pl_open_name() issues one on every track change -- so the
// first skip destroyed APF's record of every slot's size. That is the root of
// the 0184 file corruption and the nonvolatile boot hang both.
//
// Everything now lives above word 63. data.json allows up to 32 slots, so the
// table could in principle reach word 63; starting at 64 is safe for a full
// complement. Words 224-226 also hold something APF writes (they decode as a
// date and a time), so nothing goes near them either.
//
//   64..127  0190 response      128..191  0192 parameters      192..199 settings
assign target_buffer_resp_struct  = 32'hF8002100;   // word  64
assign target_buffer_param_struct = 32'hF8002200;   // word 128

// -- 5. APF target-command bridge (clk_sys <-> clk_74a) -----------------------
// STAGE 2 GOAL: no core in this workspace has ever driven these. 0192 hands off
// to the Pocket's own file browser; 0180 reads from an arbitrary offset.
wire tgt_t_read, tgt_t_openfile, tgt_t_getfile, tgt_t_write, tgt_t_flush;

tgt_cmd u_tgt (
    .clk_sys     (clk_sys),
    .rst_sys     (cpu_reset),
    .clk_74a     (clk_74a),

    .go          (soc_tgt_go),
    .cmd_sel     (soc_tgt_cmd_sel),
    .busy        (soc_tgt_busy),
    .done        (soc_tgt_done),
    .seq         (soc_tgt_seq),
    .err         (soc_tgt_err),

    .t_read      (tgt_t_read),
    .t_openfile  (tgt_t_openfile),
    .t_getfile   (tgt_t_getfile),
    .t_write     (tgt_t_write),
    .t_flush     (tgt_t_flush),
    .t_ack       (target_dataslot_ack),
    .t_done      (target_dataslot_done),
    .t_err       (target_dataslot_err)
);

// core_top declares these as `reg`, so they must be driven procedurally rather
// than connected as module outputs. Registering in clk_74a is also the correct
// domain for core_bridge_cmd. The parameter registers are quasi-static: firmware
// writes them before pulsing go and holds them for the whole command.
always @(posedge clk_74a) begin
    target_dataslot_read       <= tgt_t_read;
    target_dataslot_openfile   <= tgt_t_openfile;
    target_dataslot_write      <= tgt_t_write;
    target_dataslot_flush      <= tgt_t_flush;
    target_dataslot_getfile    <= tgt_t_getfile;
    target_dataslot_id         <= soc_tgt_id;
    target_dataslot_slotoffset <= soc_tgt_slotoffset;
    target_dataslot_bridgeaddr <= soc_tgt_bridgeaddr;
    target_dataslot_length     <= soc_tgt_length;
end

// -- 6. Video -- SDRAM-backed framebuffer UI -----------------------------------
// Replaced the Stage 1-3 bring-up display (32-bit-block status rows) entirely;
// that module is gone. SDRAM is otherwise completely unused by this core -- Stage 0
// measured the decoder fits entirely in BRAM -- so the framebuffer costs
// essentially nothing (~360 KB of 32 MB) and needed no BRAM budget, which
// mattered: the core is already at 90% BRAM utilisation.
//
// reset(~pll_locked) directly, no synchroniser: matches HarpMudd.starwars'
// proven sdram_fb instantiation exactly (PLL lock status is a slow, stable
// flag in practice, not something that has bitten this pattern on hardware).
wire        sdram_init_complete;
wire [24:0] fb_p0_addr;
wire [15:0] fb_p0_data;
wire [1:0]  fb_p0_byte_en;
wire [10:0] fb_p0_wr_len;
wire        fb_p0_wr_stream;
wire [15:0] fb_p0_q;
wire        fb_p0_wr_req, fb_p0_rd_req, fb_p0_end_burst_req;
wire        fb_p0_available, fb_p0_ready, fb_p0_data_available;
wire [10:0] fb_wsrc_addr;
wire [15:0] fb_wsrc_q;

wire [24:0] cpu_p0_addr;
wire [15:0] cpu_p0_data, cpu_p0_q;
wire [1:0]  cpu_p0_byte_en;
wire [10:0] cpu_p0_wr_len;
wire        cpu_p0_wr_req, cpu_p0_rd_req, cpu_p0_end_burst_req;
wire        cpu_p0_available, cpu_p0_ready, cpu_p0_data_available, cpu_p0_accepted;

wire [24:0] arb_p0_addr;
wire [15:0] arb_p0_data, arb_p0_q;
wire [1:0]  arb_p0_byte_en;
wire [10:0] arb_p0_wr_len, arb_wsrc_addr;
wire        arb_p0_wr_stream, arb_p0_wr_req, arb_p0_rd_req, arb_p0_end_burst_req;
wire        arb_p0_available, arb_p0_ready, arb_p0_data_available;
wire [15:0] arb_wsrc_q;

// Phase 2 shared bridge boundary. The mapped-window client can be enabled only
// in the explicitly flagged diagnostic build; normal builds drive it inactive
// at mp3_soc and diagnostic MMIO always traverses the owner mux.
wire sdram_mux_busy, sdram_mux_diag_done;
wire [31:0] sdram_mux_diag_rdata;
wire sdram_mux_start, sdram_mux_write, sdram_mux_wb_start;
wire [24:0] sdram_mux_addr;
wire [31:0] sdram_mux_wdata;
wire [3:0] sdram_mux_be;
// Unused and removed by synthesis in macro-off builds; consumed only by the
// opt-in Phase-2 diagnostic overlay below.
wire sdram_bridge_write_seen, sdram_bridge_op_write;
wire sdram_bridge_wdata_all_ones, sdram_bridge_be_all_enabled;
wire sdram_bridge_lo_write_accepted, sdram_bridge_hi_write_accepted;
wire sdram_bridge_follow_read_seen, sdram_bridge_follow_read_data_zero;
wire sdram_bridge_follow_read_data_all_ones;
wire sdram_arb_p0_cpu_selected;
wire sdram_ctrl_write_latched, sdram_ctrl_write_data_all_ones;
wire sdram_ctrl_write_be_all_enabled, sdram_ctrl_write_command;
wire sdram_ctrl_write_dq_enabled, sdram_ctrl_write_dq_all_ones;
wire sdram_ctrl_write_dqm_unmasked, sdram_ctrl_follow_read_seen;
wire sdram_ctrl_follow_read_data_seen, sdram_ctrl_follow_read_data_zero;
wire sdram_ctrl_follow_read_data_all_ones;
tau_sdram_bridge_mux u_sdram_bridge_mux (
    .clk(clk_sys), .rst(cpu_reset),
    .diag_req(soc_sdram_start), .diag_write(soc_sdram_write),
    .diag_addr(soc_sdram_addr), .diag_wdata(soc_sdram_wdata), .diag_be(soc_sdram_byte_en),
    .wb_req(soc_sdram_wb_req), .wb_write(soc_sdram_wb_write),
    .wb_addr(soc_sdram_wb_addr), .wb_wdata(soc_sdram_wb_wdata),
    .wb_be(soc_sdram_wb_byte_en), .wb_accept(soc_sdram_wb_accept),
    .diag_done(sdram_mux_diag_done), .wb_done(soc_sdram_wb_done),
    .diag_rdata(sdram_mux_diag_rdata), .wb_rdata(soc_sdram_wb_rdata), .busy(sdram_mux_busy),
    .bridge_start(sdram_mux_start), .bridge_write(sdram_mux_write),
    .bridge_addr(sdram_mux_addr), .bridge_wdata(sdram_mux_wdata), .bridge_be(sdram_mux_be),
    .bridge_wb_start(sdram_mux_wb_start),
    .bridge_busy(soc_sdram_busy), .bridge_done(soc_sdram_done), .bridge_rdata(soc_sdram_rdata)
);

tau_sdram_cpu_bridge u_sdram_cpu_bridge (
    .clk_sys(clk_sys), .rst_sys(cpu_reset),
    .sys_start(sdram_mux_start), .sys_wb_start(sdram_mux_wb_start), .sys_write(sdram_mux_write),
    .sys_addr(sdram_mux_addr), .sys_wdata(sdram_mux_wdata),
    .sys_byte_en(sdram_mux_be), .sys_busy(soc_sdram_busy),
    .sys_done(soc_sdram_done), .sys_rdata(soc_sdram_rdata),
    .debug_wb_write_seen(sdram_bridge_write_seen),
    .debug_op_write(sdram_bridge_op_write),
    .debug_wdata_all_ones(sdram_bridge_wdata_all_ones),
    .debug_be_all_enabled(sdram_bridge_be_all_enabled),
    .debug_lo_write_accepted(sdram_bridge_lo_write_accepted),
    .debug_hi_write_accepted(sdram_bridge_hi_write_accepted),
    .debug_follow_read_seen(sdram_bridge_follow_read_seen),
    .debug_follow_read_data_zero(sdram_bridge_follow_read_data_zero),
    .debug_follow_read_data_all_ones(sdram_bridge_follow_read_data_all_ones),
    .clk_sdram(clk_sdram), .rst_sdram(~pll_locked),
    .m_addr(cpu_p0_addr), .m_data(cpu_p0_data), .m_byte_en(cpu_p0_byte_en),
    .m_wr_len(cpu_p0_wr_len), .m_wr_req(cpu_p0_wr_req), .m_rd_req(cpu_p0_rd_req),
    .m_end_burst_req(cpu_p0_end_burst_req), .m_q(cpu_p0_q),
    .m_accepted(cpu_p0_accepted), .m_ready(cpu_p0_ready),
    .m_data_available(cpu_p0_data_available)
);

tau_sdram_arbiter u_sdram_arbiter (
    .clk(clk_sdram), .rst(~pll_locked),
    .fb_addr(fb_p0_addr), .fb_data(fb_p0_data), .fb_byte_en(fb_p0_byte_en),
    .fb_wr_len(fb_p0_wr_len), .fb_wr_stream(fb_p0_wr_stream),
    .fb_wr_req(fb_p0_wr_req), .fb_rd_req(fb_p0_rd_req),
    .fb_end_burst_req(fb_p0_end_burst_req), .fb_q(fb_p0_q),
    .fb_available(fb_p0_available), .fb_ready(fb_p0_ready),
    .fb_data_available(fb_p0_data_available), .fb_wsrc_q(fb_wsrc_q),
    .fb_wsrc_addr(fb_wsrc_addr),
    .cpu_addr(cpu_p0_addr), .cpu_data(cpu_p0_data), .cpu_byte_en(cpu_p0_byte_en),
    .cpu_wr_len(cpu_p0_wr_len), .cpu_wr_req(cpu_p0_wr_req),
    .cpu_rd_req(cpu_p0_rd_req), .cpu_end_burst_req(cpu_p0_end_burst_req),
    .cpu_q(cpu_p0_q), .cpu_available(cpu_p0_available), .cpu_ready(cpu_p0_ready),
    .cpu_data_available(cpu_p0_data_available), .cpu_accepted(cpu_p0_accepted),
    .p0_cpu_selected(sdram_arb_p0_cpu_selected),
    .p0_addr(arb_p0_addr), .p0_data(arb_p0_data), .p0_byte_en(arb_p0_byte_en),
    .p0_wr_len(arb_p0_wr_len), .p0_wr_stream(arb_p0_wr_stream), .p0_q(arb_p0_q),
    .p0_wr_req(arb_p0_wr_req), .p0_rd_req(arb_p0_rd_req),
    .p0_end_burst_req(arb_p0_end_burst_req), .p0_available(arb_p0_available),
    .p0_ready(arb_p0_ready), .p0_data_available(arb_p0_data_available),
    .wsrc_addr(arb_wsrc_addr), .wsrc_q(arb_wsrc_q)
);

sdram_fb #(.CLOCK_SPEED_MHZ(100), .BURST_TYPE(0), .CAS_LATENCY(2), .WRITE_BURST(1)) u_sdram (
    .clk(clk_sdram), .reset(~pll_locked), .init_complete(sdram_init_complete),
    .p0_addr(arb_p0_addr), .p0_data(arb_p0_data), .p0_byte_en(arb_p0_byte_en),
    .p0_wr_len(arb_p0_wr_len), .p0_q(arb_p0_q),
    .p0_wr_stream(arb_p0_wr_stream), .wsrc_addr(arb_wsrc_addr), .wsrc_q(arb_wsrc_q),
    .p0_wr_req(arb_p0_wr_req), .p0_rd_req(arb_p0_rd_req), .p0_end_burst_req(arb_p0_end_burst_req),
`ifdef TAU_PHASE2_PROBE
    .debug_p0_cpu_selected(sdram_arb_p0_cpu_selected),
    .debug_cpu_allones_write_latched(sdram_ctrl_write_latched),
    .debug_cpu_allones_data_latched(sdram_ctrl_write_data_all_ones),
    .debug_cpu_allones_be_latched(sdram_ctrl_write_be_all_enabled),
    .debug_cpu_allones_write_command(sdram_ctrl_write_command),
    .debug_cpu_allones_dq_enabled(sdram_ctrl_write_dq_enabled),
    .debug_cpu_allones_dq_allones(sdram_ctrl_write_dq_all_ones),
    .debug_cpu_allones_dqm_unmasked(sdram_ctrl_write_dqm_unmasked),
    .debug_cpu_follow_read_seen(sdram_ctrl_follow_read_seen),
    .debug_cpu_follow_read_data_seen(sdram_ctrl_follow_read_data_seen),
    .debug_cpu_follow_read_data_zero(sdram_ctrl_follow_read_data_zero),
    .debug_cpu_follow_read_data_allones(sdram_ctrl_follow_read_data_all_ones),
`endif
    .p0_available(arb_p0_available), .p0_ready(arb_p0_ready), .p0_data_available(arb_p0_data_available),
    .SDRAM_DQ(dram_dq), .SDRAM_A(dram_a), .SDRAM_DQM(dram_dqm), .SDRAM_BA(dram_ba),
    .SDRAM_nCS(), .SDRAM_nWE(dram_we_n), .SDRAM_nRAS(dram_ras_n), .SDRAM_nCAS(dram_cas_n),
    .SDRAM_CKE(dram_cke), .SDRAM_CLK(dram_clk)
);

wire [23:0] vid_rgb_w;
wire        vid_hs_w, vid_vs_w, vid_de_w;

// The probe is excluded from normal builds. It records the first mapped CPU
// transaction in clk_sys and displays 49 persistent eight-pixel cells in the
// top active scan lines. Green means the corresponding bit was observed;
// red means it was not. This remains useful after a CPU-side stall.
`ifdef TAU_PHASE2_PROBE
wire [48:0] sdram_probe_bits;
tau_sdram_cpu_window_probe
`ifdef TAU_PHASE2_MUX_PROBE
    #(.RETURN_PATH_MODE(3))
`else
`ifdef TAU_PHASE2_ADAPTER_PROBE
    #(.RETURN_PATH_MODE(2))
`else
`ifdef TAU_PHASE2_RETURN_PROBE
    #(.RETURN_PATH_MODE(1))
`endif
`endif
`endif
u_sdram_cpu_window_probe (
    .clk(clk_sys), .rst(cpu_reset),
    .cpu_req(soc_sdram_wb_debug_cpu_req), .cpu_we(soc_sdram_wb_debug_we),
    .cpu_wdata(soc_sdram_wb_debug_wdata),
    .cpu_cti(soc_sdram_wb_debug_cti),
    .cpu_sel(soc_sdram_wb_debug_sel), .adapter_req(soc_sdram_wb_req),
    .mux_accept(soc_sdram_wb_accept), .mux_start(sdram_mux_start),
    .bridge_busy(soc_sdram_busy), .bridge_done(soc_sdram_done),
    .adapter_done(soc_sdram_wb_done), .mux_done(soc_sdram_wb_done),
    .mux_rdata(soc_sdram_wb_rdata), .wb_ack(soc_sdram_wb_debug_ack),
    .adapter_rdata(soc_sdram_wb_debug_adapter_rdata),
    .unsupported(soc_sdram_wb_debug_unsupported),
    .adapter_write(soc_sdram_wb_write), .adapter_wdata(soc_sdram_wb_wdata),
    .adapter_be(soc_sdram_wb_byte_en), .mux_wb_start(sdram_mux_wb_start),
    .mux_write(sdram_mux_write), .mux_wdata(sdram_mux_wdata), .mux_be(sdram_mux_be),
    .bridge_write_seen(sdram_bridge_write_seen), .bridge_op_write(sdram_bridge_op_write),
    .bridge_wdata_all_ones(sdram_bridge_wdata_all_ones),
    .bridge_be_all_enabled(sdram_bridge_be_all_enabled),
    .bridge_lo_write_accepted(sdram_bridge_lo_write_accepted),
    .bridge_hi_write_accepted(sdram_bridge_hi_write_accepted),
    .ctrl_write_latched(sdram_ctrl_write_latched),
    .ctrl_write_data_all_ones(sdram_ctrl_write_data_all_ones),
    .ctrl_write_be_all_enabled(sdram_ctrl_write_be_all_enabled),
    .ctrl_write_command(sdram_ctrl_write_command),
    .ctrl_write_dq_enabled(sdram_ctrl_write_dq_enabled),
    .ctrl_write_dq_all_ones(sdram_ctrl_write_dq_all_ones),
    .ctrl_write_dqm_unmasked(sdram_ctrl_write_dqm_unmasked),
    .ctrl_follow_read_seen(sdram_ctrl_follow_read_seen),
    .ctrl_follow_read_data_seen(sdram_ctrl_follow_read_data_seen),
    .ctrl_follow_read_data_zero(sdram_ctrl_follow_read_data_zero),
    .ctrl_follow_read_data_all_ones(sdram_ctrl_follow_read_data_all_ones),
    .bridge_follow_read_seen(sdram_bridge_follow_read_seen),
    .bridge_follow_read_data_zero(sdram_bridge_follow_read_data_zero),
    .bridge_follow_read_data_all_ones(sdram_bridge_follow_read_data_all_ones),
    .cpu_ack(soc_sdram_wb_debug_cpu_ack),
    .cpu_rdata(soc_sdram_wb_debug_cpu_rdata),
    .bits(sdram_probe_bits)
);

reg [48:0] sdram_probe_bits_vid_1, sdram_probe_bits_vid_2;
reg        sdram_probe_de_d;
reg [8:0]  sdram_probe_x;
reg [8:0]  sdram_probe_y;
always @(posedge clk_vid) begin
    sdram_probe_bits_vid_1 <= sdram_probe_bits;
    sdram_probe_bits_vid_2 <= sdram_probe_bits_vid_1;
    sdram_probe_de_d <= vid_de_w;
    if (vid_vs_w) sdram_probe_y <= 9'd0;
    else if (vid_de_w && !sdram_probe_de_d) sdram_probe_y <= sdram_probe_y + 1'd1;
    if (!vid_de_w) sdram_probe_x <= 9'd0;
    else if (!sdram_probe_de_d) sdram_probe_x <= 9'd0;
    else sdram_probe_x <= sdram_probe_x + 1'd1;
end
wire sdram_probe_pixel = vid_de_w && (sdram_probe_y < 9'd8) &&
    (sdram_probe_x < 9'd392) && sdram_probe_bits_vid_2[sdram_probe_x[8:3]];
wire sdram_probe_bar = vid_de_w && (sdram_probe_y < 9'd8) &&
    (sdram_probe_x < 9'd392);
`endif

mp3_fb #(.BLIT_BLEND_ENABLE(`TAU_BLIT_BLEND_EN)) u_fb (
    .reset    (~pll_locked),
    .clk_sys  (clk_sys),
    .clk_sdram(clk_sdram),
    .clk_vid  (clk_vid),

    .cmd_push  (soc_fb_cmd_push),
    .cmd_op    (soc_fb_cmd_op),
    .cmd_addr  (soc_fb_cmd_addr),
    .cmd_w     (soc_fb_cmd_w),
    .cmd_h     (soc_fb_cmd_h),
    .cmd_fg    (soc_fb_cmd_fg),
    .cmd_bg    (soc_fb_cmd_bg),
    .cmd_glyph (soc_fb_cmd_glyph),
    .cmd_sx    (soc_fb_cmd_sx),
    .cmd_sy    (soc_fb_cmd_sy),
    .cmd_full  (soc_fb_cmd_full),

    .blt_src_base   (soc_blt_src_base),
    .blt_src_stride (soc_blt_src_stride),
    .blt_dst_base   (soc_blt_dst_base),
    .blt_dst_stride (soc_blt_dst_stride),
    .blt_key_en     (soc_blt_key_en),
    .blt_key        (soc_blt_key),
    .blt_blend_en   (soc_blt_blend_en),
    .blt_blend_mode (soc_blt_blend_mode),
    .blt_blend_alpha(soc_blt_blend_alpha),
    .blt_reindex    (soc_blt_reindex),

    .clut_wr   (soc_clut_wr),
    .clut_waddr(soc_clut_waddr),
    .clut_wdata(soc_clut_wdata),
    .rc_cut_lut(soc_rc_cut_lut),

    .sdram_init_complete(sdram_init_complete),
    .p0_addr(fb_p0_addr), .p0_data(fb_p0_data), .p0_byte_en(fb_p0_byte_en),
    .p0_wr_len(fb_p0_wr_len), .p0_q(fb_p0_q),
    .p0_wr_stream(fb_p0_wr_stream), .wsrc_addr(fb_wsrc_addr), .wsrc_q(fb_wsrc_q),
    .p0_wr_req(fb_p0_wr_req), .p0_rd_req(fb_p0_rd_req), .p0_end_burst_req(fb_p0_end_burst_req),
    .p0_available(fb_p0_available), .p0_ready(fb_p0_ready), .p0_data_available(fb_p0_data_available),

    .video_rgb(vid_rgb_w), .video_de(vid_de_w), .video_hs(vid_hs_w), .video_vs(vid_vs_w)
);

`ifdef TAU_PHASE2_PROBE
assign video_rgb          = sdram_probe_bar ?
                            (sdram_probe_pixel ? 24'h40FF40 : 24'hFF3030) : vid_rgb_w;
`else
assign video_rgb          = vid_rgb_w;
`endif
assign video_rgb_clock    = clk_vid;
assign video_rgb_clock_90 = clk_vid_90;
assign video_de           = vid_de_w;
assign video_skip         = 1'b0;
assign video_vs           = vid_vs_w;
assign video_hs           = vid_hs_w;

// -- 7. Audio -----------------------------------------------------------------
// Firmware writes signed 16-bit samples straight to the audio MMIO register, so
// no box-filter decimation is needed here (unlike an arcade core sampling a
// continuously-running sound chip). SIGNED_INPUT(1) -- PCM is two's complement.
sound_i2s #(.CHANNEL_WIDTH(16), .SIGNED_INPUT(1)) u_sound_i2s (
    .clk_74a (clk_74a), .clk_audio (clk_sys),
    .audio_l (soc_audio_l), .audio_r (soc_audio_r),
    .audio_mclk (audio_mclk), .audio_dac (audio_dac), .audio_lrck (audio_lrck)
);

// -- 8. Persistent settings, via interact.json -----------------------------------
// Eight words at 0x20000000. interact.json declares one `persist` variable per
// word; APF reads them back from here EVERY FRAME, lets the user adjust them in
// the Core Settings menu, writes them back, and saves them to its own
// /Settings/<core>/Interact/interact_persist.json when the core shuts down.
//
// APF does the storing, and it stores to its OWN file. No data slot is involved
// at any point, which is the whole reason this route was chosen: the two
// mechanisms tried before -- the 0184 target write and a nonvolatile data slot
// -- both reached the user's .mp3 files. This cannot.
//
// ONE array, not two. The interact protocol is a read-MODIFY-write at a single
// address: APF reads the word, applies the user's adjustment, writes it back,
// and reads it again next frame. Splitting it by direction -- bridge writes one
// array, bridge reads another -- meant APF never read back what it had just
// written, so every setting snapped straight back to the core's value and the
// menu items blinked without changing.
//
// One array means two writers in two clock domains, so the CPU's writes cross
// on a TOGGLE, the same idiom tgt_cmd.v uses for its command strobe: a level
// pulse would not survive the clk_sys -> clk_74a ratio.
//
// CPU WRITES WIN a same-cycle collision. APF rewrites every word every frame,
// so a dropped bridge write is re-sent 16 ms later and costs nothing; a dropped
// CPU write would silently lose the user's button press for good.
//
// CPU reads are asynchronous and quasi-static -- these words change on a button
// press, not continuously -- which is the same argument tgt_cmd.v makes for its
// parameter registers.
/* SIXTEEN words, was eight. The eighth was the last one free and a playlist
 * name needs three, so the index went from 3 bits to 4.
 *
 * Still ramstyle=logic: at 16x32 this is 512 flip-flops out of ~7000, and
 * block RAM is the binding resource on this device at 97%. Letting Quartus
 * infer an M10K here would spend one of the eight remaining blocks on
 * something that fits comfortably in fabric. */
(* ramstyle = "logic" *) reg [31:0] set_reg [0:15];

reg  [3:0]  cpu_set_idx;
reg  [31:0] cpu_set_dat;
reg         cpu_set_tgl = 1'b0;

always @(posedge clk_sys) begin
    if (soc_set_wr) begin
        cpu_set_idx <= soc_set_idx;
        cpu_set_dat <= soc_set_wdata;
        cpu_set_tgl <= ~cpu_set_tgl;
    end
end

reg [2:0] cpu_set_sync = 3'b0;
always @(posedge clk_74a) cpu_set_sync <= {cpu_set_sync[1:0], cpu_set_tgl};
wire cpu_set_wr_74 = cpu_set_sync[2] ^ cpu_set_sync[1];

/* One more address bit. With [4:2] the ninth variable at 0x20000020 wrapped
 * onto slot 0 and silently overwrote Volume -- no error, just a corrupted
 * setting, which is why this had to move in step with the depth above. */
wire [3:0] set_widx = bridge_addr[5:2];

always @(posedge clk_74a) begin
    if (cpu_set_wr_74)
        set_reg[cpu_set_idx] <= cpu_set_dat;
    else if (bridge_wr && bridge_addr[31:28] == 4'h2)
        set_reg[set_widx] <= bridge_wr_data;
end

assign set_bridge_rd_data = set_reg[set_widx];
assign soc_set_rdata      = set_reg[soc_set_idx];
