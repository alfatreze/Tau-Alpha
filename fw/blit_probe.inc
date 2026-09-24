/* ============================================================= BLIT ENGINE feature probe (Phase F, PHASE_F_SPEC.md section 12)
 *
 * B1-B6 (OP_BLIT/OP_BAR/OP_SBLIT, the R_BLT_IDX/R_BLT_DATA register file) are NOT gated by any
 * bitstream macro at the RTL level -- mp3_fb.sv and mp3_soc.v compile them in unconditionally,
 * `TAU_BLIT` only controls a local `define in core_game.vh that nothing else reads (confirmed by
 * reading the source, not assumed). So "does this bitstream have the blit engine" cannot be
 * answered by a feature-bit register the way PSRAM's PS_ID or the cold-code IF_CFG bit can --
 * there isn't one. The one real, load-bearing difference an OLD bitstream (built before this work
 * existed) has is that its R_FB_GO decode only reads a 2-bit opcode field; B-103 widened it to 3
 * bits by claiming what was GO's unused padding bit. Writing OP_BLIT (3'b100 = 4) to that old,
 * narrower decode silently truncates to 2'b00 = OP_RUN -- the exact "fail-safe came free" B-103
 * already documented, but never turned into something firmware can actually detect and act on.
 *
 * This probe does that: issue an OP_BLIT-shaped command with h=2 and a small, non-default
 * dst_stride (8, unlike RUN/RECT/COPY's fixed 512) into a scratch location, then check whether
 * the SECOND row's word actually changed. Old RTL's OP_RUN fallback only ever touches row 0 (it
 * has no h/stride concept); new RTL's real OP_BLIT touches both rows. The scratch location is
 * columns 480/488 of framebuffer row 0 -- inside the 400-511 column range mp3_fb.sv's own header
 * comment says is never displayed (stride is 512 words/line, only 400 are shown), so this can
 * never corrupt or visibly flash a real pixel regardless of the result.
 *
 * Readback uses the same CPU-uncached-window idiom already proven elsewhere in this firmware
 * (the SDRAM stress test's `*(volatile uint16_t*)(0xA0000000u + (word_addr << 1))`, fw/player.c
 * around R_SDR_*): the draw engine and the CPU window address the same physical SDRAM, byte
 * offset = word address * 2.
 *
 * Originally gated behind its own diagnostic-only macro (default off), from when nothing in this
 * firmware issued an operational OP_BLIT/OP_BAR/OP_SBLIT/OP_CBLIT command yet (confirmed by the
 * B-112 full audit) and this only existed to answer the question section 12 asks -- can firmware
 * detect the blit engine's absence and degrade gracefully -- ahead of the first real feature that
 * would need `BLIT_READY()` to make that call. **That feature now exists (B8/section 5):
 * `set_draw_thumb()`'s OP_CBLIT path needs a real answer, not a diagnostic one, so this also
 * compiles and runs whenever TAU_METER_THUMBS is on** -- every settings-UI build, not just the
 * opt-in diagnostic probe build. TAU_BLIT_PROBE stays for standalone diagnostic builds that want
 * this probe without the meter-thumbs feature pulling it in. */
#if TAU_BLIT_PROBE || TAU_METER_THUMBS
/* R_BLT_IDX/R_BLT_DATA/FB_OP_BLIT now live in player.c's shared register block
 * (B-127 promoted them there for the blit-storm Check test to use too). */
static uint8_t blit_ready;
#define BLIT_READY() (blit_ready != 0u)

static void blit_probe(void)
{
    const uint32_t probe_addr = 480u;   /* row 0, column 480 -- past the displayed 0..399 */
    const uint32_t probe_stride = 8u;   /* tiny and non-default, so row 1 lands at 488 */
    const uint32_t probe_row2 = probe_addr + probe_stride;
    volatile uint16_t *sentinel =
        (volatile uint16_t *)(uintptr_t)(0xA0000000u + (probe_row2 << 1));

    *sentinel = 0xDEADu;

    fb_wait();
    REG(R_BLT_IDX) = 2u; REG(R_BLT_DATA) = 0u;              /* DST_BASE = 0 */
    REG(R_BLT_IDX) = 3u; REG(R_BLT_DATA) = probe_stride;    /* DST_STRIDE = 8, not the usual 512 */
    REG(R_FB_ADDR)  = probe_addr;
    REG(R_FB_SIZE)  = (2u << 9) | 1u;                       /* h=2, w=1 */
    REG(R_FB_COLOR) = 0u;                                   /* fg=bg=0 -> source offset 0 (a benign read) */
    REG(R_FB_GO)    = FB_OP_BLIT;
    fb_wait();

    blit_ready = (*sentinel != 0xDEADu) ? 1u : 0u;

    /* Restore the sticky state every real BLIT caller will expect as its
     * power-up default, so this probe leaves nothing behind. */
    REG(R_BLT_IDX) = 3u; REG(R_BLT_DATA) = 512u;
}
#else
#define BLIT_READY() 0
#endif
