// =============================================================================
// mp3_soc.v -- VexRiscv + BRAM + memory-mapped peripherals.
//
// Memory map (BYTE addresses as seen by firmware):
//   0x0000_0000 .. RAM_BYTES-1   RAM  (CACHED -- code, data, buffers)
//   0x8000_0000 +                MMIO (UNCACHED)
//
// The 0x8000_0000 split is MANDATORY, not stylistic: VexRiscv decides
// cacheability purely from physical address bit 31
//   assign ..._isIoAccess     = ..._physicalAddress[31];
//   assign stageB_bypassCache = (stageB_mmuRsp_isIoAccess || ...);
// so a peripheral placed below 0x8000_0000 would be read through the D-cache
// and firmware polling a status register would spin forever on a stale value.
// RAM deliberately stays cached -- that is what produced the measured 1.65 CPI.
//
// MMIO registers:
//   0x8000_0000  W  console character out (bits 7:0)
//   0x8000_0004  W  sim/exit + halt marker (unused on HW)
//   0x8000_0008  W  audio sample {right[31:16], left[15:0]}
//   0x8000_000C  R  free-running clk cycle counter
//   0x8000_0010  W  status word 0   (rendered on screen as 32 bit-blocks)
//   0x8000_0014  W  status word 1
//   0x8000_0018  W  status word 2
//   0x8000_001C  W  status word 3
//   0x8000_0020  W  target-cmd: id
//   0x8000_0024  W  target-cmd: slot offset
//   0x8000_0028  W  target-cmd: bridge address
//   0x8000_002C  W  target-cmd: length
//   0x8000_0030  W  target-cmd: go   (bit0 1=openfile 0=read -> starts command)
//                R  target-cmd: status {err[2:0], done, busy}
//
// The loader port (ld_*) writes bytes into RAM on port B. It carries BOTH the
// initial firmware image at boot AND, later, bytes streamed back by APF target
// reads -- both arrive over the same APF bridge write bus.
// =============================================================================

// RAM sizing: measured Helix needs are firmware image ~63 KB + decoder instance
// ~34 KB + MP3 read-ahead 32 KB + stack 16 KB = ~143 KB, so 128 KB is too small
// and the next power of two is 256 KB.
//
// RAM_WORDS **MUST BE A POWER OF TWO**. A 49152-entry array addressed by a
// 16-bit index cannot be mapped cleanly onto M10K, so Quartus built the memory
// out of logic instead and the fit exploded:
//   Error (170012): Fitter requires 3621 LABs ... device contains only 1848
// At 65536 it infers correctly: 256 KB costs ~256 M10K blocks (byte-wide arrays
// force x8 mode, ~80% bit efficiency), measured 271/308 total with the rest of
// the core. pcm_fifo adds ~8 more.
module mp3_soc #(
    parameter RAM_WORDS = 65536,           // 256 KB -- power of two, required
    parameter RAM_AW    = 16,
    // Phase 2 remains opt-in until its address map, uncached adapter, and
    // owner-mux path have passed the full Quartus and Pocket gates.
    parameter PHASE2_WINDOW_ENABLE = 0,
    // Uncached PSRAM CPU window at 0xA400_0000..A5FF_FFFF (B-016). Needs the Phase 2
    // decode (PHASE2_WINDOW_ENABLE): the legacy decode aliases that range onto MMIO.
    parameter PSRAM_WINDOW_ENABLE = 0,
    // Phase G2: instruction fetch from PSRAM. Cold code runs from the instruction alias 0x2400_0000..0x25FF_FFFF
    // (32 MiB, PSRAM byte offset = address - 0x2400_0000), read-only, through a second tau_psram_bus and a two-client
    // arbiter on the existing PSRAM port. Needs PSRAM_WINDOW_ENABLE. Inert (identical netlist) when 0.
    parameter PSRAM_IFETCH_ENABLE = 0,
    parameter IFETCH_GAP = 2,               // test hook: idle cycles after each PSRAM transaction (0 reproduces back-to-back requests)
    // Phase F B7: SDRAM busy-cycle counter (PHASE_F_SPEC.md section 5). The counter itself lives
    // outside this module, in clk_sdram, and is CDC'd in by the caller (tau_cdc_gray_ctr) -- this
    // just gates whether 0xBC exposes it or reads zero. Inert (identical netlist) when 0.
    parameter SDRAM_BUSY_ENABLE = 0,
    // Phase F B1 (PHASE_F_SPEC.md section 5): the sticky blit-state registers always exist (a few
    // flops, harmless either way) but blt_src_base/stride and blt_dst_base/stride are only wired
    // out to mp3_fb when this is set -- inert (identical netlist) when 0.
    parameter BLIT_ENABLE = 0
) (
    input  wire        clk,
    input  wire        rst,                // active high, hold until firmware loaded
    input  wire        clk_74a,            // for the dataslot_update CDC only

    // Byte-write port for firmware load + streamed data (same clock domain)
    input  wire        ld_wr,
    input  wire [31:0] ld_addr,
    input  wire [7:0]  ld_data,

    // Controller + OS menu state (clk_74a domain; resynced internally)
    input  wire [31:0] cont_key,
    input  wire        in_menu,

    // Fires when the user reloads the MP3 slot from the Core menu ("Load MP3"
    // is User-Reloadable, parameters bit 0). Verified against Analogue's docs
    // rather than assumed (see the 0192 mistake earlier in this project):
    // "If the slot is marked User Reloadable, APF sends 008A Data slot update
    // whenever the user picks a new file." clk_74a domain, one-cycle pulse.
    input  wire        dataslot_update,
    input  wire [15:0] dataslot_update_id,
    input  wire [31:0] dataslot_update_size,

    // "Data slot access all complete" (host command 008F). The BOOT path
    // already depends on this -- core_game.vh holds the CPU in reset until it
    // fires -- but the reload path only ever saw 008A, which APF sends when
    // the user PICKS a file, not when the slot is ready to read. That is the
    // whole asymmetry behind "the tag shows on boot but not after Load MP3".
    input  wire        dataslot_allcomplete,

    // Audio out -- driven by pcm_fifo, drained in hardware at the programmed
    // sample rate. Firmware pushes bursts; it never has to meet DAC timing.
    output wire [15:0] audio_l,
    output wire [15:0] audio_r,

    // Debug/status words rendered by the video block
    output reg  [31:0] status0,
    output reg  [31:0] status1,
    output reg  [31:0] status2,
    output reg  [31:0] status3,

    // Console character stream (for a future text overlay / trace)
    output reg         con_wr,
    output reg  [7:0]  con_char,

    // APF target-command request (clk domain; CDC handled in tgt_cmd.v)
    output reg         tgt_go,             // 1-cycle pulse
    output reg  [2:0]  tgt_cmd_sel,        // 0=0180 read 1=0192 openfile 2=0190 getfile 3=0184 write 4=0188 flush
    output reg  [15:0] tgt_id,
    output reg  [31:0] tgt_slotoffset,
    output reg  [31:0] tgt_bridgeaddr,
    output reg  [31:0] tgt_length,
    input  wire        tgt_busy,
    input  wire        tgt_done,
    input  wire [7:0]  tgt_seq,
    input  wire [2:0]  tgt_err,

    // Framebuffer draw command -> mp3_fb.sv (rev 7). One command is now a whole
    // primitive -- a run, a rect, or a complete anti-aliased glyph -- rather
    // than a single row, so the CPU issues ~30x fewer of them. The parameter
    // registers persist between pushes, so consecutive glyphs of the same
    // colour and size cost just an address write plus the GO write.
    // fb_cmd_full is checked before pushing; pushing while full is silently
    // dropped by mp3_fb's FIFO.
    output reg          fb_cmd_push,
    output reg  [2:0]   fb_cmd_op,   // 3 bits: bit 2 was GO's unused padding bit, claimed for OP_BLIT (Phase F B1)
    output reg  [18:0]  fb_cmd_addr,
    output reg  [8:0]   fb_cmd_w,
    output reg  [8:0]   fb_cmd_h,
    output reg  [15:0]  fb_cmd_fg,
    output reg  [15:0]  fb_cmd_bg,
    output reg  [6:0]   fb_cmd_glyph,
    output reg  [1:0]   fb_cmd_sx,
    output reg  [1:0]   fb_cmd_sy,
    input  wire         fb_cmd_full,

    // core_bridge_cmd's datatable: a dual-port BRAM whose OTHER port APF can
    // read and write over the bridge at 0xF8xx2xxx. That is what makes the
    // 0190 getfile / 0192 openfile structs reachable at all -- the frozen
    // shell's bridge_rd_data mux serves core_bridge_cmd alone, so a core
    // cannot otherwise expose memory to a bridge READ. This BRAM already sits
    // on both sides, and core_top leaves the user-side port undriven.
    output reg  [9:0]   dt_addr,
    output reg          dt_wren,
    output reg  [31:0]  dt_wdata,
    input  wire [31:0]  dt_q,

    // Persistent settings words, published to / read from interact.json.
    output reg  [3:0]   set_idx,
    output reg          set_wr,
    output reg  [31:0]  set_wdata,
    input  wire [31:0]  set_rdata,

    // Phase 1 SDRAM diagnostic mailbox.  This is intentionally MMIO-only:
    // ordinary CPU instruction/data accesses still go to BRAM.
    output reg          sdram_start,
    output reg          sdram_write,
    output reg  [24:0]  sdram_addr,
    output reg  [31:0]  sdram_wdata,
    output reg  [3:0]   sdram_byte_en,
    input  wire         sdram_busy,
    input  wire         sdram_done,
    input  wire [31:0]  sdram_rdata,

    // Phase 2 uncached CPU data-window client. Disabled by default; in the
    // disabled build these outputs are tied inactive and inputs are ignored.
    output wire         sdram_wb_req,
    output wire         sdram_wb_write,
    output wire [24:0]  sdram_wb_addr,
    output wire [31:0]  sdram_wb_wdata,
    output wire [3:0]   sdram_wb_byte_en,
    input  wire         sdram_wb_accept,
    input  wire         sdram_wb_done,
    input  wire [31:0]  sdram_wb_rdata,

    // Diagnostic-only visibility for the opt-in Phase 2 CPU-window probe.
    // These ports are inert in release builds and deliberately expose only
    // protocol metadata, never CPU data or program memory.
    output wire         sdram_wb_debug_cpu_req,
    output wire         sdram_wb_debug_we,
    output wire [31:0]  sdram_wb_debug_wdata,
    output wire [2:0]   sdram_wb_debug_cti,
    output wire [3:0]   sdram_wb_debug_sel,
    output wire         sdram_wb_debug_ack,
    output wire         sdram_wb_debug_unsupported,
    // Optional return-path probe visibility. These expose the adapter response
    // and CPU-facing registered ACK/data pair; they are inert in legacy builds
    // and exist only to isolate the diagnostic return boundary.
    output wire [31:0]  sdram_wb_debug_adapter_rdata,
    output wire         sdram_wb_debug_cpu_ack,
    output wire [31:0]  sdram_wb_debug_cpu_rdata,

    // Expansion MMIO (docs/MMIO_ALLOCATION.md): offsets 0x88..0xAC are handed to
    // an optional peripheral (the PSRAM diagnostic mailbox). With nothing
    // connected xm_rdata is not read, so legacy builds and testbenches are
    // unaffected.
    output wire [7:0]   xm_reg,
    output wire         xm_wr,
    output wire [31:0]  xm_wdata,
    input  wire [31:0]  xm_rdata,

    // PSRAM CPU window: controller-side request interface (single outstanding request,
    // level held until psram_done). Inert unless PSRAM_WINDOW_ENABLE.
    output wire         psram_req,
    output wire         psram_we,
    output wire [22:0]  psram_word,
    output wire [31:0]  psram_wdata,
    output wire [3:0]   psram_be,
    input  wire         psram_done,
    input  wire [31:0]  psram_rdata,
    input  wire         psram_guard,

    // Phase F B7: already-CDC'd SDRAM busy-cycle count (clk_sys domain, sourced from clk_sdram
    // by the caller). Unread when SDRAM_BUSY_ENABLE is 0, so legacy builds and testbenches that
    // do not wire this port are unaffected -- same convention as xm_rdata above.
    input  wire [31:0]  sdram_busy_rd,

    // Phase F B1: sticky blit-engine addressing state (section 9). Driven from mp3_soc's own
    // R_BLT_IDX/R_BLT_DATA registers regardless of BLIT_ENABLE; only OP_BLIT in mp3_fb.sv reads
    // them, and that opcode does not exist unless TAU_BLIT is built there too.
    output wire [24:0]  blt_src_base,
    output wire [9:0]   blt_src_stride,
    output wire [24:0]  blt_dst_base,
    output wire [9:0]   blt_dst_stride,

    // Phase F B2: colour-key transparency, sticky field 4 (section 9's KEY).
    output wire         blt_key_en,
    output wire [15:0]  blt_key,

    // Phase F B5: alpha blend, sticky field 5 (section 9's BLEND).
    output wire         blt_blend_en,
    output wire [2:0]   blt_blend_mode,
    output wire [7:0]   blt_blend_alpha,

    // Phase F B9 (Tier 2, section 5): palette re-index offset, sticky field 6
    // (section 9's REINDEX). Added to OP_CBLIT's palette index before the CLUT
    // lookup in mp3_fb.sv; default 0 is a true no-op, so there is no separate
    // enable bit the way B2/B5 have one.
    output wire [7:0]   blt_reindex,

    // Phase F B8 ("B8 detailed design", PHASE_F_SPEC.md section 5): CLUT load,
    // from mp3_soc's own R_CLUT_IDX/R_CLUT_DATA. A plain indexed write, not one
    // of the section-9 sticky fields (a 256-entry bulk table load is a
    // different kind of write traffic than five mostly-static config fields,
    // so it gets its own register pair rather than sharing BLT_IDX/DATA's).
    // Driven regardless of BLIT_ENABLE, same convention as the sticky fields
    // above; only OP_CBLIT in mp3_fb.sv reads the CLUT, and that opcode does
    // not exist unless TAU_BLIT is built there too.
    output wire         clut_wr,
    output wire [7:0]   clut_waddr,
    output wire [15:0]  clut_wdata
);

    // ---------------------------------------------------------------- CPU ---
    wire        iCYC, iSTB, iWE;
    wire [29:0] iADR;
    wire [31:0] iDAT_MOSI;
    wire [3:0]  iSEL;
    wire [2:0]  iCTI;
    wire [1:0]  iBTE;
    reg  [31:0] iDAT_MISO;
    reg         iACK;

    wire        dCYC, dSTB, dWE;
    wire [29:0] dADR;
    wire [31:0] dDAT_MOSI;
    wire [3:0]  dSEL;
    wire [2:0]  dCTI;
    wire [1:0]  dBTE;
    reg  [31:0] dDAT_MISO;
    reg         dACK;
    wire        sdram_wb_ack;
    wire [31:0] sdram_wb_cpu_rdata;
    wire        sdram_wb_unsupported;
    wire        d_bus_err;
    wire        d_req     = dCYC & dSTB & ~dACK;
    wire        i_req     = iCYC & iSTB & ~iACK;

    VexRiscv cpu (
        .externalResetVector   (32'h00000000),
        .timerInterrupt        (1'b0),
        .softwareInterrupt     (1'b0),
        .externalInterruptArray(32'b0),

        .iBusWishbone_CYC      (iCYC),
        .iBusWishbone_STB      (iSTB),
        .iBusWishbone_ACK      (iACK),
        .iBusWishbone_WE       (iWE),
        .iBusWishbone_ADR      (iADR),
        .iBusWishbone_DAT_MISO (iDAT_MISO),
        .iBusWishbone_DAT_MOSI (iDAT_MOSI),
        .iBusWishbone_SEL      (iSEL),
        .iBusWishbone_ERR      (1'b0),
        .iBusWishbone_CTI      (iCTI),
        .iBusWishbone_BTE      (iBTE),

        .dBusWishbone_CYC      (dCYC),
        .dBusWishbone_STB      (dSTB),
        .dBusWishbone_ACK      (dACK),
        .dBusWishbone_WE       (dWE),
        .dBusWishbone_ADR      (dADR),
        .dBusWishbone_DAT_MISO (dDAT_MISO),
        .dBusWishbone_DAT_MOSI (dDAT_MOSI),
        .dBusWishbone_SEL      (dSEL),
        .dBusWishbone_ERR      (d_bus_err),
        .dBusWishbone_CTI      (dCTI),
        .dBusWishbone_BTE      (dBTE),

        .clk                   (clk),
        .reset                 (rst)
    );

    // ------------------------------------------------------- RAM (4 banks) ---
    // Four byte-wide arrays so both ports get byte enables and Quartus infers
    // true dual-port M10K without a read-modify-write.
    reg [7:0] ram0 [0:RAM_WORDS-1];
    reg [7:0] ram1 [0:RAM_WORDS-1];
    reg [7:0] ram2 [0:RAM_WORDS-1];
    reg [7:0] ram3 [0:RAM_WORDS-1];

    // Port A: CPU (dBus has priority; iBus retries while stalled)
    //
    // Address decode (BYTE addresses; dADR is a WORD address so byte bit N maps
    // to dADR[N-2]):
    //   0x0000_0000  RAM, CACHED    -- code/data/stack, gives the good CPI
    //   0x8000_0000  MMIO           -- uncached (bit31), peripherals
    //   0xC000_0000  RAM, UNCACHED  -- same physical RAM, aliased
    //
    // The uncached alias exists because APF target reads DMA straight into RAM
    // over the bridge, behind the CPU's back. Reading that buffer through the
    // cached window could return stale lines with no way to tell. Firmware reads
    // streamed bytes via 0xC000_0000 and gets the real memory every time.
    wire        d_is_mmio;
    wire        d_is_ram;
    wire        d_is_sdram;
    wire        d_is_sdram_cached;
    wire [24:0] d_sdram_addr;

    wire dec_bram_cached, dec_bram_uncached, dec_mmio;
    wire dec_sdram_cached, dec_sdram_uncached;
    wire [24:0] dec_sdram_addr;
    wire        dec_psram;
    wire [22:0] dec_psram_word;
    wire        d_is_psram;
    wire        psram_wb_ack, psram_wb_unsupported;
    wire [31:0] psram_wb_dat;
    // The data-window client's controller-side signals (the port outputs are driven by the arbiter below).
    wire        dctl_req, dctl_we, dctl_done;
    wire [22:0] dctl_word;
    wire [31:0] dctl_wdata;
    wire [3:0]  dctl_be;
    // Phase G2 instruction-fetch client and its counters
    wire        i_is_psram = (PSRAM_IFETCH_ENABLE != 0) && (iADR[29:23] == 7'h12);   // 0x2400_0000..0x25FF_FFFF
    wire        ipsram_ack;
    wire [31:0] ipsram_dat;
    wire [31:0] if_n_rd, if_cyc_rd;
    tau_sdram_addr_decode u_sdram_addr_decode (
        .dadr(dADR), .bram_cached(dec_bram_cached),
        .bram_uncached(dec_bram_uncached), .mmio(dec_mmio),
        .sdram_cached(dec_sdram_cached), .sdram_uncached(dec_sdram_uncached),
        .sdram_addr(dec_sdram_addr),
        .psram_uncached(dec_psram), .psram_word(dec_psram_word)
    );

    generate
        if (PHASE2_WINDOW_ENABLE != 0) begin : g_phase2_window
            assign d_is_mmio        = dec_mmio;
            assign d_is_ram         = dec_bram_cached | dec_bram_uncached;
            assign d_is_sdram       = dec_sdram_uncached;
            assign d_is_sdram_cached= dec_sdram_cached;
            assign d_sdram_addr     = dec_sdram_addr;
            assign sdram_wb_debug_cpu_req = d_req & dec_sdram_uncached;
            assign sdram_wb_debug_we      = dWE;
            assign sdram_wb_debug_wdata   = dDAT_MOSI;
            assign sdram_wb_debug_cti     = dCTI;
            assign sdram_wb_debug_sel     = dSEL;
            assign sdram_wb_debug_ack     = sdram_wb_ack;
            assign sdram_wb_debug_unsupported = sdram_wb_unsupported;
            assign sdram_wb_debug_adapter_rdata = sdram_wb_cpu_rdata;
            assign sdram_wb_debug_cpu_ack = dACK;
            assign sdram_wb_debug_cpu_rdata = dDAT_MISO;

            tau_sdram_wb_adapter u_sdram_wb_adapter (
                .clk(clk), .rst(rst),
                .wb_cyc(dCYC & d_is_sdram), .wb_stb(dSTB), .wb_we(dWE),
                .wb_cti(dCTI), .wb_sdram_addr(d_sdram_addr),
                .wb_wdata(dDAT_MOSI), .wb_sel(dSEL),
                .wb_rdata(sdram_wb_cpu_rdata), .wb_ack(sdram_wb_ack),
                .wb_unsupported(sdram_wb_unsupported),
                .bridge_req(sdram_wb_req), .bridge_write(sdram_wb_write),
                .bridge_addr(sdram_wb_addr), .bridge_wdata(sdram_wb_wdata),
                .bridge_byte_en(sdram_wb_byte_en),
                .bridge_accept(sdram_wb_accept), .bridge_done(sdram_wb_done),
                .bridge_rdata(sdram_wb_rdata)
            );

`ifdef TAU_SIGNALTAP
            // SignalTap proof tap (docs/JTAG_DEBUG_ACCESS.md section 5); diagnostic builds only.
            // Bit map is mirrored in tools/gen_signaltap_stp.py: keep both in step.
            tau_signaltap_tap u_stp_tap (
                .clk(clk),
                .d({sdram_wb_rdata[0], rst, d_bus_err, dACK,
                    sdram_wb_addr[19:0],
                    sdram_wb_done, sdram_wb_accept, sdram_wb_write, sdram_wb_req,
                    sdram_wb_unsupported, sdram_wb_ack, dWE, (dCYC & d_is_sdram & dSTB)})
            );
`endif

            // Unsupported bursts and unmapped data addresses terminate as a
            // Wishbone error instead of silently hanging the CPU bus.
            assign d_is_psram = dec_psram && (PSRAM_WINDOW_ENABLE != 0);
            if (PSRAM_WINDOW_ENABLE != 0) begin : g_psram
                // One CPU beat = one controller request (KB-024 release timing); a guard-word
                // access ACKs with data 0 (the CPU has no bus-error handler) and sets the
                // controller's sticky guard flag.
                tau_psram_bus #(.REL_CYC(2), .GUARD_ERR(0)) u_psram_bus (
                    .clk(clk), .rst(rst),
                    .wb_cyc(dCYC & d_is_psram), .wb_stb(dSTB), .wb_we(dWE), .wb_cti(dCTI),
                    .wb_adr(dec_psram_word), .wb_dat_i(dDAT_MOSI), .wb_sel(dSEL),
                    .wb_dat_o(psram_wb_dat), .wb_ack(psram_wb_ack), .wb_err(),
                    .wb_unsupported(psram_wb_unsupported),
                    .ctl_req(dctl_req), .ctl_we(dctl_we), .ctl_word(dctl_word),
                    .ctl_wdata(dctl_wdata), .ctl_be(dctl_be),
                    .ctl_done(dctl_done), .ctl_rdata(psram_rdata), .ctl_guard(psram_guard)
                );
            end else begin : g_no_psram
                assign psram_wb_dat = 32'd0;
                assign psram_wb_ack = 1'b0;
                assign psram_wb_unsupported = 1'b0;
                assign dctl_req = 1'b0;   assign dctl_we = 1'b0;
                assign dctl_word = 23'd0; assign dctl_wdata = 32'd0;
                assign dctl_be = 4'd0;
            end

            assign d_bus_err = sdram_wb_unsupported | psram_wb_unsupported |
                (d_req & (dec_sdram_cached |
                          ~(d_is_mmio | d_is_ram | d_is_sdram | d_is_psram)));
        end else begin : g_legacy_window
            assign d_is_mmio         = dADR[29] & ~dADR[28];
            assign d_is_ram          = ~dADR[29] | dADR[28];
            assign d_is_sdram        = 1'b0;
            assign d_is_sdram_cached = 1'b0;
            assign d_is_psram        = 1'b0;
            assign psram_wb_dat      = 32'd0;
            assign psram_wb_ack      = 1'b0;
            assign psram_wb_unsupported = 1'b0;
            assign dctl_req = 1'b0;   assign dctl_we = 1'b0;
            assign dctl_word = 23'd0; assign dctl_wdata = 32'd0;
            assign dctl_be = 4'd0;
            assign d_sdram_addr      = 25'd0;
            assign sdram_wb_req      = 1'b0;
            assign sdram_wb_write    = 1'b0;
            assign sdram_wb_addr     = 25'd0;
            assign sdram_wb_wdata    = 32'd0;
            assign sdram_wb_byte_en  = 4'd0;
            assign sdram_wb_ack      = 1'b0;
            assign sdram_wb_cpu_rdata= 32'd0;
            assign sdram_wb_unsupported = 1'b0;
            assign sdram_wb_debug_cpu_req = 1'b0;
            assign sdram_wb_debug_we      = 1'b0;
            assign sdram_wb_debug_wdata   = 32'd0;
            assign sdram_wb_debug_cti     = 3'd0;
            assign sdram_wb_debug_sel     = 4'd0;
            assign sdram_wb_debug_ack     = 1'b0;
            assign sdram_wb_debug_unsupported = 1'b0;
            assign sdram_wb_debug_adapter_rdata = 32'd0;
            assign sdram_wb_debug_cpu_ack = 1'b0;
            assign sdram_wb_debug_cpu_rdata = 32'd0;
            assign d_bus_err         = 1'b0;
        end
    endgenerate

    // Priority: loader > dBus > iBus. The loader must win because the APF
    // bridge cannot be back-pressured -- a dropped byte would silently corrupt
    // the firmware image or a streamed buffer. MMIO needs no RAM port, so a
    // dBus MMIO access proceeds even while the loader holds the RAM.
    wire        ld_req     = ld_wr;
    wire        d_mmio_req = d_req & d_is_mmio;
    wire        d_ram_req  = d_req & d_is_ram;
    wire        serve_d    = d_ram_req & ~ld_req;
    wire        serve_i    = i_req & ~i_is_psram & ~d_ram_req & ~ld_req;      // BRAM fetches only

    // ONE always block, ONE address -> Quartus infers M10K cleanly.
    //
    // Originally the loader had its own write port in a second always block.
    // That is a true-dual-port pattern, and because it did not match Quartus's
    // inference template the whole 256 KB fell back to registers:
    //   Error (276003): Cannot convert all sets of registers into RAM megafunctions
    // Arbitrating the loader onto the single port avoids the issue entirely and
    // costs nothing real: during boot the CPU is held in reset, and while
    // streaming the bridge delivers only a few bytes per microsecond, so the
    // occasional stolen cycle is invisible next to a 50 MHz CPU.
    wire [RAM_AW-1:0] mem_addr = ld_req  ? ld_addr[RAM_AW+1:2]
                               : serve_d ? dADR[RAM_AW-1:0]
                                         : iADR[RAM_AW-1:0];
    wire              mem_we    = ld_req | (serve_d & dWE);
    wire [3:0]        mem_be    = ld_req ? (4'b0001 << ld_addr[1:0]) : dSEL;
    wire [31:0]       mem_wdata = ld_req ? {4{ld_data}} : dDAT_MOSI;
    reg  [31:0]       a_rdata;

    always @(posedge clk) begin
        if (mem_we & mem_be[0]) ram0[mem_addr] <= mem_wdata[7:0];
        if (mem_we & mem_be[1]) ram1[mem_addr] <= mem_wdata[15:8];
        if (mem_we & mem_be[2]) ram2[mem_addr] <= mem_wdata[23:16];
        if (mem_we & mem_be[3]) ram3[mem_addr] <= mem_wdata[31:24];
        a_rdata <= {ram3[mem_addr], ram2[mem_addr], ram1[mem_addr], ram0[mem_addr]};
    end

    // ------------------------------------------------------------- cycles ---
    reg [31:0] cycle_ctr;
    always @(posedge clk) cycle_ctr <= rst ? 32'd0 : cycle_ctr + 32'd1;

    // -------------------------------------------------------------- input ---
    // Two-stage resync from clk_74a. No bit-coherency requirement: these are
    // human button presses, so a one-cycle skew between bits is meaningless.
    reg [15:0] key_s0, key_s1;
    reg        menu_s0, menu_s1;
    always @(posedge clk) begin
        key_s0  <= cont_key[15:0];  key_s1  <= key_s0;
        menu_s0 <= in_menu;         menu_s1 <= menu_s0;
    end

    // ------------------------------------------------ dataslot_update CDC ---
    // Same toggle-crossing idiom as tgt_cmd.v: dataslot_update is a one-cycle
    // pulse on clk_74a, which a plain 2FF synchronizer would drop if it doesn't
    // line up with a clk_sys edge. Convert to a toggle (glitch-proof across any
    // clock ratio), latch the ID in the SAME clk_74a cycle so it is stable
    // by the time clk_sys detects the toggle edge, then compare against our
    // MP3 slot ID (2 -- must match data.json AND firmware's MP3_SLOT_ID).
    localparam [15:0] MP3_SLOT_ID = 16'd2;
    // Playlist slot ID (3 -- must match data.json AND firmware's PL_SLOT_ID).
    localparam [15:0] PL_SLOT_ID  = 16'd3;

    reg        upd_tgl_74 = 1'b0;
    reg [15:0] upd_id_74;
    reg [31:0] upd_size_74;
    always @(posedge clk_74a) begin
        if (dataslot_update) begin
            upd_tgl_74  <= ~upd_tgl_74;
            upd_id_74   <= dataslot_update_id;
            upd_size_74 <= dataslot_update_size;
        end
    end

    reg [2:0] upd_sync = 3'b0;
    always @(posedge clk) upd_sync <= {upd_sync[1:0], upd_tgl_74};
    wire mp3_reload_edge = (upd_sync[2] ^ upd_sync[1]) && (upd_id_74 == MP3_SLOT_ID);

    // The playlist slot raises 008A exactly as the MP3 slot does, but the edge
    // above is gated on the MP3 id -- so without this a "Load Playlist" pick is
    // invisible to firmware and silently does nothing.
    //
    // This comment used to add that polling slot 3 was NOT an option, because
    // touching another slot makes APF drop its fragment cache for the MP3 slot
    // and every refill then re-walks the FAT cluster chain. That is true of a
    // slot READ (0180) and was measured. It is NOT true of a 0190 getfile:
    // firmware polls slot 3's identity every 3 s while streaming and no tic is
    // audible (measured 2026-08-13). A metadata query does not walk the chain.
    // The distinction matters -- the poll is the only recovery route that
    // depends on neither this notification nor a menu edge.
    wire pl_reload_edge = (upd_sync[2] ^ upd_sync[1]) && (upd_id_74 == PL_SLOT_ID);

    reg mp3_reloaded, pl_reloaded;
    // Acked PER BIT rather than by any write, so clearing one flag cannot drop
    // the other -- a track change and a playlist change can land together.
    wire wr_reload  = d_req & d_is_mmio & dWE & (mmio_reg == R_RELOAD);
    wire ack_reload = wr_reload & dDAT_MOSI[0];
    wire ack_pl     = wr_reload & dDAT_MOSI[4];
    always @(posedge clk) begin
        if (rst)                  mp3_reloaded <= 1'b0;
        else if (mp3_reload_edge) mp3_reloaded <= 1'b1;
        else if (ack_reload)      mp3_reloaded <= 1'b0;

        if (rst)                 pl_reloaded <= 1'b0;
        else if (pl_reload_edge) pl_reloaded <= 1'b1;
        else if (ack_pl)         pl_reloaded <= 1'b0;
    end

    // dataslot_allcomplete is a LEVEL in clk_74a (set by 008F, cleared by the
    // 0080/0082 request handlers), so a plain synchroniser is enough -- no
    // toggle needed, unlike the one-cycle dataslot_update pulse above.
    //
    // slot_ready deliberately keys off a RISING edge after the reload rather
    // than requiring a preceding fall: the exact 008A/0080/008F ordering APF
    // uses for a deferload reload is not documented anywhere we have, so this
    // works whichever way round they arrive. If APF never re-sequences
    // allcomplete at all, slot_ready simply stays low and firmware falls
    // through on its timeout -- i.e. no worse than the current behaviour.
    // slot_fell is pure observability, so one hardware test can tell us which
    // of those actually happens instead of another round of inference.
    reg [2:0] ac_sync = 3'b0;
    always @(posedge clk) ac_sync <= {ac_sync[1:0], dataslot_allcomplete};
    wire ac_rise = ac_sync[1] & ~ac_sync[2];
    wire ac_fall = ~ac_sync[1] & ac_sync[2];

    reg slot_ready, slot_fell;
    reg [31:0] slot_size;
    always @(posedge clk) begin
        if (rst) begin
            slot_ready <= 1'b0; slot_fell <= 1'b0; slot_size <= 32'd0;
        end else if (mp3_reload_edge) begin
            slot_ready <= 1'b0; slot_fell <= 1'b0;
            slot_size  <= upd_size_74;
        end else begin
            if (ac_fall) slot_fell  <= 1'b1;
            if (ac_rise) slot_ready <= 1'b1;
        end
    end

    // --------------------------------------------------------------- MMIO ---
    localparam [7:0] R_CONSOLE = 8'h00, R_EXIT    = 8'h04, R_AUDIO   = 8'h08,
                     R_CYCLES  = 8'h0C, R_STAT0   = 8'h10, R_STAT1   = 8'h14,
                     R_STAT2   = 8'h18, R_STAT3   = 8'h1C, R_TGT_ID  = 8'h20,
                     R_TGT_OFF = 8'h24, R_TGT_ADR = 8'h28, R_TGT_LEN = 8'h2C,
                     R_TGT_GO  = 8'h30, R_PCM_ST  = 8'h34, R_PCM_RATE= 8'h38,
                     R_INPUT   = 8'h3C, R_VERSION = 8'h40, R_RELOAD  = 8'h44,
                     R_FB_ADDR = 8'h48, R_FB_SIZE = 8'h4C, R_FB_COLOR= 8'h50,
                     R_FB_GO   = 8'h54, R_FB_STALL= 8'h58, R_SLOT_SZ = 8'h5C,
                     R_DT_ADDR = 8'h60, R_DT_DATA = 8'h64,
                     R_EQ      = 8'h68, R_SET_IDX = 8'h6C,
                     R_SET_DAT = 8'h70, R_SDR_ADDR= 8'h74,
                     R_SDR_DATA= 8'h78, R_SDR_CTRL= 8'h7C,
                     R_SDR_RDATA=8'h80, R_SDR_STATUS=8'h84;
    // Phase F section 9: sticky blit-engine state (source/dest base+stride, B2's
    // colour key, B5's blend mode, B9's palette re-index), never entering the
    // per-command FIFO. R_BLT_IDX selects a field (0=SRC_BASE, 1=SRC_STRIDE,
    // 2=DST_BASE, 3=DST_STRIDE, 4=KEY: bit16=enable, bits[15:0]=RGB565 colour;
    // 5=BLEND: bit0=enable, bits[3:1]=mode -- 0=DSP 0-255 alpha, 1=PSX B/2+F/2,
    // 2=PSX B+F clamp, 3=PSX B-F clamp, 4=PSX B+F/4 clamp -- bits[15:8]=alpha
    // level, DSP mode only; 6=REINDEX: bits[7:0]=offset added to OP_CBLIT's
    // palette index, 0=no-op); each R_BLT_DATA write stores it and auto-
    // increments the index, so a burst of 7 writes loads the whole state with
    // one index write. Inert (no logic reads these) unless TAU_BLIT is built.
    localparam [7:0] R_BLT_IDX = 8'hC0, R_BLT_DATA = 8'hC4;
    // Phase F B8 ("B8 detailed design"): CLUT load. R_CLUT_IDX selects one of
    // 256 entries; each R_CLUT_DATA write stores the RGB565 value there and
    // auto-increments the index (wraps 255->0), so a burst of 256 DATA writes
    // loads the whole palette after one index write -- same convenience as
    // R_BLT_IDX/R_BLT_DATA's own auto-increment. Inert (no logic reads it)
    // unless TAU_BLIT is built.
    localparam [7:0] R_CLUT_IDX = 8'hC8, R_CLUT_DATA = 8'hCC;
    // B-186 (docs/AUDIT_TRAIL.md): ISSP proved the draw engine is NOT the Blit Test hang --
    // dispatch state cycled normally through idle/scanline-fill the whole time the CPU was
    // stuck. This register is the CPU-side equivalent of that same idea: firmware writes a
    // small checkpoint number here (fw/suite.inc's bt_crumb(), same call sites as its existing
    // SDRAM-word crumb, now writing both), and TAU_ISSP wires it out to a second, independent
    // probe instance -- readable live over JTAG regardless of whether the CPU itself is stuck,
    // the same property that made the BLIT probe useful. Plain write-only register, no side
    // effect if written on a bitstream without TAU_ISSP (same "inert unless built" convention
    // every Phase F register already uses). Never in the release or the normal Diagnostic
    // Build -- gated behind TAU_ISSP exactly like u_issp_blit in mp3_fb.sv.
    localparam [7:0] R_DBG_MARK = 8'hD0;

    // Bitstream/firmware interlock. Firmware compares this against its own
    // expected value and refuses to run on a mismatch.
    //
    // Rationale: the .rom loads from SD in seconds while the bitstream needs a
    // ~6 minute Quartus compile, so it is very easy to flash new firmware onto
    // stale RTL. That has already happened three times here, each time looking
    // like a logic bug (dead peripheral, no audio, unresponsive buttons) rather
    // than what it was. BUMP THIS whenever the MMIO map changes.
    localparam [31:0] CORE_VERSION = 32'h4D503317;   // "MP3" + rev 23 (target data-slot flush)

    wire [7:0] mmio_reg = {dADR[5:0], 2'b00};   // byte offset within MMIO page

    // Phase F section 9: sticky blit-engine state. Plain flops, no logic reads
    // them unless BLIT_ENABLE -- see the port declarations above and the reset
    // block below for the rest of this feature's mp3_soc-side footprint.
    reg  [2:0]  blt_idx = 3'd0;
    reg  [24:0] blt_src_base_r = 25'd0, blt_dst_base_r = 25'd0;
    reg  [9:0]  blt_src_stride_r = 10'd512, blt_dst_stride_r = 10'd512;
    reg         blt_key_en_r = 1'b0;
    reg  [15:0] blt_key_r = 16'd0;
    reg         blt_blend_en_r = 1'b0;
    reg  [2:0]  blt_blend_mode_r = 3'd0;
    reg  [7:0]  blt_blend_alpha_r = 8'd0;
    reg  [7:0]  blt_reindex_r = 8'd0;
    assign blt_src_base   = (BLIT_ENABLE != 0) ? blt_src_base_r   : 25'd0;
    assign blt_src_stride = (BLIT_ENABLE != 0) ? blt_src_stride_r : 10'd0;
    assign blt_dst_base   = (BLIT_ENABLE != 0) ? blt_dst_base_r   : 25'd0;
    assign blt_dst_stride = (BLIT_ENABLE != 0) ? blt_dst_stride_r : 10'd0;
    assign blt_key_en     = (BLIT_ENABLE != 0) ? blt_key_en_r     : 1'b0;
    assign blt_key        = (BLIT_ENABLE != 0) ? blt_key_r        : 16'd0;
    assign blt_blend_en   = (BLIT_ENABLE != 0) ? blt_blend_en_r   : 1'b0;
    assign blt_blend_mode = (BLIT_ENABLE != 0) ? blt_blend_mode_r : 3'd0;
    assign blt_blend_alpha= (BLIT_ENABLE != 0) ? blt_blend_alpha_r: 8'd0;
    assign blt_reindex    = (BLIT_ENABLE != 0) ? blt_reindex_r    : 8'd0;

    // Phase F B8: CLUT load state. clut_wr_r is a one-cycle pulse (not a level),
    // matching how mp3_fb.sv's own clut_wr port is meant to be driven -- a
    // single dDAT_MOSI write should write exactly one CLUT entry, not hold the
    // write-enable high across whatever the next unrelated MMIO write is.
    reg  [7:0]  clut_idx = 8'd0;
    reg         clut_wr_r = 1'b0;
    reg  [15:0] clut_wdata_r = 16'd0;
    assign clut_wr    = (BLIT_ENABLE != 0) ? clut_wr_r    : 1'b0;
    assign clut_waddr = clut_idx;
    assign clut_wdata = clut_wdata_r;

    // B-186: plain CPU-write scratch register, read live by TAU_ISSP's second probe instance
    // below. No enable gating (unlike clut_wr_r's pulse) -- this is meant to just SIT at its
    // last-written value forever, which is exactly what makes it readable after a hang.
    reg  [7:0]  dbg_mark = 8'd0;

    // Expansion window 0x88..0xAC (see docs/MMIO_ALLOCATION.md).
    assign xm_reg   = mmio_reg;
    assign xm_wr    = d_req & d_is_mmio & dWE;
    assign xm_wdata = dDAT_MOSI;
    wire   xm_range = (mmio_reg >= 8'h88) && (mmio_reg <= 8'hAC);

    // Diagnostic: clk_sys cycles spent with the draw FIFO full. Firmware
    // busy-waits on exactly that condition, so this is the direct measurement
    // of "is drawing actually stalling the CPU" -- the question the rev 6 UI
    // could only answer by A/B-ing the whole feature on real hardware.
    reg [31:0] fb_stall_ctr;
    always @(posedge clk) begin
        if (rst)             fb_stall_ctr <= 32'd0;
        else if (fb_cmd_full) fb_stall_ctr <= fb_stall_ctr + 32'd1;
    end

    // ------------------------------------------------------------ PCM FIFO ---
    wire        pcm_push  = d_req & d_is_mmio & dWE & (mmio_reg == R_AUDIO);
    // Any write to R_PCM_ST (previously read-only) flushes the FIFO. Used on a
    // track change or a seek, where samples already queued from BEFORE the
    // jump must not keep draining out the DAC over the new position's audio.
    wire        pcm_flush = d_req & d_is_mmio & dWE & (mmio_reg == R_PCM_ST);
    reg  [31:0] pcm_rate;
    wire        pcm_full, pcm_empty, pcm_underrun;
    wire [11:0] pcm_level;
    wire signed [15:0] fifo_l, fifo_r;
    reg   [2:0] eq_preset;

    pcm_fifo #(.AW(11)) u_pcm (
        .clk      (clk),
        .rst      (rst),
        .flush    (pcm_flush),
        .push     (pcm_push),
        .pdata    (dDAT_MOSI),
        .full     (pcm_full),
        .empty    (pcm_empty),
        .level    (pcm_level),
        .rate_inc (pcm_rate),
        .out_l    (fifo_l),
        .out_r    (fifo_r),
        .underrun (pcm_underrun)
    );

    // Preset EQ, spliced between the FIFO and this module's audio outputs.
    // Entirely inside clk_sys, so no new CDC -- sound_i2s already crosses into
    // clk_74a through its own sync_fifo and this sits on the near side of that.
    // Preset 0 is a true bypass inside eq_biquad, so with the EQ off the audio
    // path is bit-identical to what it was before this existed.
    eq_biquad #(.CLK_HZ(60_000_000), .RATE_HZ(48_000)) u_eq (
        .clk    (clk),
        .rst    (rst),
        .in_l   (fifo_l),
        .in_r   (fifo_r),
        .preset (eq_preset),
        .out_l  (audio_l),
        .out_r  (audio_r)
    );

    always @(posedge clk) begin
        con_wr      <= 1'b0;
        tgt_go      <= 1'b0;
        fb_cmd_push <= 1'b0;
        dt_wren     <= 1'b0;
        set_wr      <= 1'b0;
        sdram_start <= 1'b0;
        clut_wr_r   <= 1'b0;

        if (rst) begin
            status0 <= 32'd0; status1 <= 32'd0;
            status2 <= 32'd0; status3 <= 32'd0;
            tgt_id  <= 16'd0; tgt_slotoffset <= 32'd0;
            tgt_bridgeaddr <= 32'd0; tgt_length <= 32'd0;
            tgt_cmd_sel <= 3'd0;
            dt_addr <= 10'd0; dt_wdata <= 32'd0;
            fb_cmd_op <= 3'd0; fb_cmd_addr <= 19'd0;
            fb_cmd_w  <= 9'd0; fb_cmd_h    <= 9'd0;
            fb_cmd_fg <= 16'd0; fb_cmd_bg  <= 16'd0;
            fb_cmd_glyph <= 7'd0; fb_cmd_sx <= 2'd0; fb_cmd_sy <= 2'd0;
            // Default to 48 kHz at 50 MHz so a plain sample write still makes
            // sound before firmware programs the real rate.
            pcm_rate <= 32'd3435974;   // 48 kHz at clk_sys = 60 MHz
            eq_preset <= 3'd0;         // FLAT: bypass until asked otherwise
            set_idx <= 4'd0; set_wdata <= 32'd0;
            sdram_start <= 1'b0;
            sdram_write <= 1'b0;
            sdram_addr <= 25'd0;
            sdram_wdata <= 32'd0;
            sdram_byte_en <= 4'd0;
        end else if (d_req & d_is_mmio & dWE) begin
            case (mmio_reg)
                R_CONSOLE: begin con_char <= dDAT_MOSI[7:0]; con_wr <= 1'b1; end
                R_AUDIO:   ;   /* handled by pcm_push -> pcm_fifo */
                R_PCM_RATE: pcm_rate <= dDAT_MOSI;
                R_EQ:       eq_preset <= dDAT_MOSI[2:0];
                R_SET_IDX:  set_idx   <= dDAT_MOSI[3:0];
                R_SET_DAT:  begin set_wdata <= dDAT_MOSI; set_wr <= 1'b1; end
                R_SDR_ADDR: sdram_addr <= dDAT_MOSI[24:0];
                R_SDR_DATA: sdram_wdata <= dDAT_MOSI;
                // CTRL: bit0 start, bit1 write, bits[5:2] byte enables.
                // A start while busy is ignored; firmware polls STATUS bit0.
                R_SDR_CTRL: if (dDAT_MOSI[0] && !sdram_busy) begin
                    sdram_write <= dDAT_MOSI[1];
                    sdram_byte_en <= dDAT_MOSI[5:2];
                    sdram_start <= 1'b1;
                end
                R_PCM_ST:  ;   /* handled by pcm_flush -> pcm_fifo, above */
                R_STAT0:   status0 <= dDAT_MOSI;
                R_STAT1:   status1 <= dDAT_MOSI;
                R_STAT2:   status2 <= dDAT_MOSI;
                R_STAT3:   status3 <= dDAT_MOSI;
                R_TGT_ID:  tgt_id         <= dDAT_MOSI[15:0];
                R_TGT_OFF: tgt_slotoffset <= dDAT_MOSI;
                R_TGT_ADR: tgt_bridgeaddr <= dDAT_MOSI;
                R_TGT_LEN: tgt_length     <= dDAT_MOSI;
                R_TGT_GO:  begin tgt_cmd_sel <= dDAT_MOSI[2:0]; tgt_go <= 1'b1; end
                R_DT_ADDR: dt_addr  <= dDAT_MOSI[9:0];
                R_DT_DATA: begin dt_wdata <= dDAT_MOSI; dt_wren <= 1'b1; end
                R_FB_ADDR: fb_cmd_addr <= dDAT_MOSI[18:0];
                R_FB_SIZE: begin fb_cmd_w <= dDAT_MOSI[8:0];
                                 fb_cmd_h <= dDAT_MOSI[17:9]; end
                R_FB_COLOR: begin fb_cmd_fg <= dDAT_MOSI[15:0];
                                  fb_cmd_bg <= dDAT_MOSI[31:16]; end
                /* GO carries the per-glyph fields (op/char/scale) so drawing a
                 * string is two MMIO writes per character -- address, then this
                 * -- with colour and size left standing in their registers. */
                R_FB_GO:   begin fb_cmd_op    <= dDAT_MOSI[2:0];
                                 fb_cmd_glyph <= dDAT_MOSI[9:3];
                                 fb_cmd_sx    <= dDAT_MOSI[11:10];
                                 fb_cmd_sy    <= dDAT_MOSI[13:12];
                                 fb_cmd_push  <= 1'b1; end
                R_BLT_IDX:  blt_idx <= dDAT_MOSI[2:0];
                R_BLT_DATA: begin
                    case (blt_idx)
                        3'd0: blt_src_base_r   <= dDAT_MOSI[24:0];
                        3'd1: blt_src_stride_r <= dDAT_MOSI[9:0];
                        3'd2: blt_dst_base_r   <= dDAT_MOSI[24:0];
                        3'd3: blt_dst_stride_r <= dDAT_MOSI[9:0];
                        3'd4: begin
                            blt_key_en_r <= dDAT_MOSI[16];
                            blt_key_r    <= dDAT_MOSI[15:0];
                        end
                        3'd5: begin
                            blt_blend_en_r    <= dDAT_MOSI[0];
                            blt_blend_mode_r  <= dDAT_MOSI[3:1];
                            blt_blend_alpha_r <= dDAT_MOSI[15:8];
                        end
                        default: blt_reindex_r <= dDAT_MOSI[7:0];   // field 6: REINDEX
                    endcase
                    // 7 real fields (0..6): wraps back to 0 after REINDEX rather
                    // than counting up to 3'd7, so a burst of exactly 7 DATA
                    // writes loads the whole state and an 8th harmlessly restarts
                    // at SRC_BASE instead of landing on an unused index.
                    blt_idx <= (blt_idx == 3'd6) ? 3'd0 : blt_idx + 3'd1;
                end
                R_CLUT_IDX:  clut_idx <= dDAT_MOSI[7:0];
                R_CLUT_DATA: begin
                    clut_wdata_r <= dDAT_MOSI[15:0];
                    clut_wr_r    <= 1'b1;
                    clut_idx     <= clut_idx + 8'd1;   // wraps 255->0 naturally (8-bit)
                end
                R_DBG_MARK: dbg_mark <= dDAT_MOSI[7:0];
                default: ;
            endcase
        end
    end

    reg [31:0] mmio_rdata;
    always @(*) begin
        case (mmio_reg)
            R_CYCLES:  mmio_rdata = cycle_ctr;
            // {seq[15:8], err[4:2], done[1], busy[0]}. Poll seq, not done:
            // done is a level that stays set from the PREVIOUS command until
            // the next one is picked up, so reading it right after issuing can
            // report a completion that hasn't happened. See tgt_cmd.v.
            R_TGT_GO:  mmio_rdata = {16'd0, tgt_seq, 3'd0, tgt_err, tgt_done, tgt_busy};
            R_PCM_ST:  mmio_rdata = {13'd0, pcm_underrun, pcm_full, pcm_empty,
                                     4'd0, pcm_level};
            // {in_menu, cont1_key[15:0]}
            //  key bits: 0 up, 1 down, 2 left, 3 right, 4 A, 5 B, 6 X, 7 Y,
            //            14 select, 15 start
            R_INPUT:   mmio_rdata = {15'd0, menu_s1, key_s1};
            R_VERSION: mmio_rdata = CORE_VERSION;
            //  bit0 reload pending, bit1 slot ready (allcomplete rose since),
            //  bit2 allcomplete level now, bit3 allcomplete fell since reload,
            //  bit4 PLAYLIST slot reloaded
            R_RELOAD:  mmio_rdata = {27'd0, pl_reloaded, slot_fell, ac_sync[2],
                                     slot_ready, mp3_reloaded};
            R_SLOT_SZ: mmio_rdata = slot_size;
            /* BRAM read is registered: firmware writes R_DT_ADDR, then reads
             * R_DT_DATA as a separate MMIO transaction, so q_a has settled. */
            R_DT_DATA: mmio_rdata = dt_q;
            R_FB_GO:   mmio_rdata = {31'd0, fb_cmd_full};
            R_FB_STALL: mmio_rdata = fb_stall_ctr;
            R_EQ:      mmio_rdata = {29'd0, eq_preset};
            R_SET_DAT: mmio_rdata = set_rdata;
            R_SDR_RDATA: mmio_rdata = sdram_rdata;
            R_SDR_STATUS: mmio_rdata = {30'd0, sdram_done, sdram_busy};
            R_STAT0:   mmio_rdata = status0;
            R_STAT1:   mmio_rdata = status1;
            R_STAT2:   mmio_rdata = status2;
            R_STAT3:   mmio_rdata = status3;
            8'hB0:     mmio_rdata = if_n_rd;                              // instruction beats served from PSRAM
            8'hB4:     mmio_rdata = if_cyc_rd;                            // cycles the fetch stage waited on PSRAM
            8'hB8:     mmio_rdata = {31'd0, (PSRAM_IFETCH_ENABLE != 0)};  // feature present (write = clear counters)
            8'hBC:     mmio_rdata = (SDRAM_BUSY_ENABLE != 0) ? sdram_busy_rd : 32'd0;  // B7: SDRAM port-busy cycles, free-running since reset
            default:   mmio_rdata = xm_range ? xm_rdata : 32'h0;
        endcase
    end

    // --------------------------------------------------------- bus returns ---
    reg d_was_mmio, d_was_sdram, d_was_psram, i_was_psram;
    always @(posedge clk) begin
        if (rst) begin
            dACK <= 1'b0; iACK <= 1'b0;
            d_was_mmio <= 1'b0; d_was_sdram <= 1'b0; d_was_psram <= 1'b0; i_was_psram <= 1'b0;
        end else begin
            dACK       <= 1'b0;
            iACK       <= 1'b0;
            d_was_mmio <= d_mmio_req;
            d_was_sdram<= sdram_wb_ack;
            d_was_psram<= psram_wb_ack;
            i_was_psram<= ipsram_ack;
            // Ack only what actually got the port this cycle; anything blocked
            // by the loader simply retries (Wishbone masters hold their request).
            if (d_mmio_req | serve_d | sdram_wb_ack | psram_wb_ack) dACK <= 1'b1;
            if (serve_i | ipsram_ack) iACK <= 1'b1;      // registered once more, as dACK (KB-024)
        end
    end

    always @(*) begin
        dDAT_MISO = d_was_mmio  ? mmio_rdata :
                    d_was_sdram ? sdram_wb_cpu_rdata :
                    d_was_psram ? psram_wb_dat : a_rdata;
        iDAT_MISO = i_was_psram ? ipsram_dat : a_rdata;
    end

    // ------------------------------------------- Phase G2: PSRAM instruction fetch ---
    // One classic beat per instruction-bus beat (a cache line fill is eight of them: the wrapper does not care that the
    // beats are consecutive), read-only. The arbiter shares the single PSRAM controller port between this client and the
    // data window: the data client wins a tie, a granted transaction is never interrupted, and after every done pulse the
    // port is held idle for two cycles so back-to-back transactions from different clients never look like one request
    // (the KB-024 lesson: a client's req may fall one cycle after done).
    generate
        if (PSRAM_IFETCH_ENABLE != 0) begin : g_ifetch
            wire        ictl_req, ictl_we, ictl_done;
            wire [22:0] ictl_word;
            wire [31:0] ictl_wdata;
            wire [3:0]  ictl_be;
            tau_psram_bus #(.REL_CYC(2), .GUARD_ERR(0)) u_psram_ibus (
                .clk(clk), .rst(rst),
                .wb_cyc(iCYC & i_is_psram), .wb_stb(iSTB), .wb_we(1'b0), .wb_cti(3'b000),
                .wb_adr(iADR[22:0]), .wb_dat_i(32'd0), .wb_sel(4'b1111),
                .wb_dat_o(ipsram_dat), .wb_ack(ipsram_ack), .wb_err(), .wb_unsupported(),
                .ctl_req(ictl_req), .ctl_we(ictl_we), .ctl_word(ictl_word),
                .ctl_wdata(ictl_wdata), .ctl_be(ictl_be),
                .ctl_done(ictl_done), .ctl_rdata(psram_rdata), .ctl_guard(psram_guard)
            );
            reg       busy, gi;
            reg [1:0] gap;
            wire      sel_i = busy ? gi : (~dctl_req & ictl_req);
            wire      any_req = busy ? (gi ? ictl_req : dctl_req) : (dctl_req | ictl_req);
            assign psram_req   = any_req & (gap == 2'd0);
            assign psram_we    = sel_i ? ictl_we    : dctl_we;
            assign psram_word  = sel_i ? ictl_word  : dctl_word;
            assign psram_wdata = sel_i ? ictl_wdata : dctl_wdata;
            assign psram_be    = sel_i ? ictl_be    : dctl_be;
            assign dctl_done   = psram_done & busy & ~gi;
            assign ictl_done   = psram_done & busy &  gi;
            always @(posedge clk) begin
                if (rst) begin busy <= 1'b0; gi <= 1'b0; gap <= 2'd0; end
                else begin
                    if (gap != 2'd0) gap <= gap - 2'd1;
                    if (!busy && psram_req) begin busy <= 1'b1; gi <= sel_i; end
                    else if (busy && psram_done) begin busy <= 1'b0; gap <= IFETCH_GAP[1:0]; end
                end
            end
            // Counters: instruction beats served from PSRAM, and cycles the fetch stage held a PSRAM request.
            reg [31:0] n_r, cyc_r;
            wire       clr = d_req & d_is_mmio & dWE & (mmio_reg == 8'hB8);
            always @(posedge clk) begin
                if (rst | clr) begin n_r <= 32'd0; cyc_r <= 32'd0; end
                else begin
                    if (ipsram_ack) n_r <= n_r + 32'd1;
                    if (iCYC & iSTB & i_is_psram) cyc_r <= cyc_r + 32'd1;
                end
            end
            assign if_n_rd = n_r;
            assign if_cyc_rd = cyc_r;

            // B-188: DBGM showed bt_begin()'s first line (bt_crumb(1)) never executes at all --
            // bt_begin() is cold code, fetched through exactly this arbiter, so the leading
            // hypothesis is a stalled instruction fetch here, not anything inside bt_begin()'s
            // own logic. This probe answers it directly rather than guessing further: iCYC/iSTB/
            // i_is_psram show whether the CPU is even asking this window for an instruction right
            // now; ictl_req/dctl_req/busy/gi show whether the arbiter's own priority (data always
            // wins a tie -- sel_i's own expression, `~dctl_req & ictl_req`, when not busy) is
            // starving the instruction side by a data client holding dctl_req continuously; psram_req/
            // psram_done/ipsram_ack show whether a granted request ever actually completes at the
            // controller. Same convention as the other two ISSP instances -- TAU_ISSP-gated, never
            // in the release or the normal Diagnostic Build, read-only.
`ifdef TAU_ISSP
            altsource_probe #(
                .sld_auto_instance_index("YES"),
                .sld_instance_index(0),
                .instance_id("IFPS"),
                .probe_width(10),
                .source_width(1),
                .source_initial_value("0"),
                .enable_metastability("NO")
            ) u_issp_ifetch (
                .source_clk (clk),
                .probe       ({psram_done, psram_req, ipsram_ack, gi, busy,
                                dctl_req, ictl_req, i_is_psram, iSTB, iCYC}),
                .source      ()
            );
`endif
        end else begin : g_no_ifetch
            assign psram_req = dctl_req;   assign psram_we = dctl_we;
            assign psram_word = dctl_word; assign psram_wdata = dctl_wdata;
            assign psram_be = dctl_be;     assign dctl_done = psram_done;
            assign ipsram_ack = 1'b0;      assign ipsram_dat = 32'd0;
            assign if_n_rd = 32'd0;        assign if_cyc_rd = 32'd0;
        end
    endgenerate

    // B-186: second, independent ISSP instance -- the CPU-side checkpoint register above,
    // not the draw engine (u_issp_blit, mp3_fb.sv). Same convention: default OFF, gated
    // behind TAU_ISSP, never in the release or the normal Diagnostic Build, read-only
    // (source_width kept at its required minimum). sld_auto_instance_index avoids any manual
    // coordination with u_issp_blit's own index.
`ifdef TAU_ISSP
    altsource_probe #(
        .sld_auto_instance_index("YES"),
        .sld_instance_index(0),
        .instance_id("DBGM"),
        .probe_width(8),
        .source_width(1),
        .source_initial_value("0"),
        .enable_metastability("NO")
    ) u_issp_dbgmark (
        .source_clk (clk),
        .probe       (dbg_mark),
        .source      ()
    );
`endif

endmodule
