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
 * **B-192 correction, 2026-09-25: the raw CPU pointer this used to use here was never actually
 * safe.** `0xA0000000` is the draw engine's OWN framebuffer/guard region (the first ~1 MiB of
 * SDRAM -- `fb_cmd_addr` is hard-capped at 19 bits, so the draw engine can never even reach
 * further in), which the CPU's uncached SDRAM window does NOT cover at all: that window starts a
 * full 1 MiB later, at `PL_SDRAM_BASE` (0xA0100000, `fw/playlist.inc`). A raw CPU load/store below
 * that hits no decoded range and the bus never gets an ACK -- it hangs forever, live-diagnosed via
 * JTAG ISSP (docs/AUDIT_TRAIL.md B-191/B-192) tracing the exact stuck instruction to this
 * function's own sentinel read. Fixed by using the SDRAM mailbox (R_SDR_ADDR/DATA/CTRL/RDATA/
 * STATUS) instead, the same mechanism `fw/playlist.inc`'s `pl_sdram_prove()` already uses -- unlike
 * a CPU pointer, the mailbox addresses SDRAM directly and reaches this region fine. Byte-enable
 * `0b0011` (the low 16 bits of the 32-bit mailbox transaction) keeps the write to exactly the one
 * halfword the original pointer touched, not its neighbour.
 *
 * Originally gated behind its own diagnostic-only macro (default off), from when nothing in this
 * firmware issued an operational OP_BLIT/OP_BAR/OP_SBLIT/OP_CBLIT command yet (confirmed by the
 * B-112 full audit) and this only existed to answer the question section 12 asks -- can firmware
 * detect the blit engine's absence and degrade gracefully -- ahead of the first real feature that
 * would need `BLIT_READY()` to make that call. **That feature now exists (B8/section 5):
 * `set_draw_thumb()`'s OP_CBLIT path needs a real answer, not a diagnostic one, so this also
 * compiles whenever TAU_METER_THUMBS is on -- every settings-UI build, not just the opt-in
 * diagnostic probe build. TAU_BLIT_PROBE stays for standalone diagnostic builds that want this
 * probe without the meter-thumbs feature pulling it in.
 *
 * **B-162 correction, 2026-09-24: NOT called at boot any more.** It briefly was (this same
 * session), and hung TAU_0_5_0_A_6 on every single boot -- the splash drew correctly (so
 * A_COMPOSE's retiming and text rendering are fine) but the loading-progress animation, which
 * starts right after this call in the boot sequence, never appeared. Deferred to
 * `blit_probe_ensure()` below: first actual use, well after boot, so a hang there is recoverable
 * rather than bricking every single boot.
 *
 * **B-170 correction, 2026-09-24: the custom DST_STRIDE was the actual cause, not the timing.**
 * The deferred probe hung AGAIN on `TAU_0_5_0_A_9` (Blit Test's first real call to it, well after
 * boot -- ruling out "too early" as the explanation). Pattern once spotted: this function was the
 * ONLY code anywhere in this codebase that ever wrote a non-default `DST_STRIDE` (8, via the
 * sticky `R_BLT_IDX`/`R_BLT_DATA` mechanism) -- both hangs trace back to the one thing this probe
 * did that no proven-working `OP_BLIT` caller (`fb_blit()`, `CT_BLT`, hardware-verified B-146/
 * B-164) ever does. Rewritten to use the SAME default-stride (512) addressing every other caller
 * already relies on: the second probe row is the NEXT row at the framebuffer's own stride, not a
 * custom 8-word step, so `R_BLT_IDX`/`R_BLT_DATA` are never touched at all. The distinguishing
 * logic is unchanged (old RTL's `OP_RUN` fallback only ever touches row 0; a real `OP_BLIT` with
 * h=2 touches both) -- only the addressing convention changed, to one already proven twice on real
 * hardware. Still not confirmed on hardware as of this entry -- the hypothesis is strong (the one
 * differentiating factor across two separate hangs) but not directly traced live. */
/* R_BLT_IDX/R_BLT_DATA/FB_OP_BLIT now live in player.c's shared register block
 * (B-127 promoted them there for the blit-storm Check test to use too). */
static uint8_t blit_ready;
static uint8_t blit_probed;
#define BLIT_READY() (blit_ready != 0u)

static void blit_probe(void)
{
    const uint32_t probe_addr = 480u;                    /* row 0, column 480 -- past the displayed 0..399 */
    const uint32_t probe_row2 = probe_addr + FB_STRIDE;  /* next row at the DEFAULT stride (512) -- B-170:
                                                           * never touches R_BLT_IDX/R_BLT_DATA, same
                                                           * addressing convention fb_blit()/CT_BLT already
                                                           * use successfully on real hardware. */
    uint32_t t;

    /* Mailbox write, all four byte lanes -- byte_en=0b1111, exactly pl_sdram_prove()'s own
     * proven 0x3F pattern (this project has no precedent anywhere for a PARTIAL byte_en
     * write, so this doesn't guess at one). probe_row2 is already in 16-bit-SDRAM-word units,
     * the same units R_SDR_ADDR takes; the write also touches the next word (probe_row2+1,
     * unused scratch padding, harmless) since one mailbox transaction covers 32 bits. */
    t = cycles();
    REG(R_SDR_ADDR) = probe_row2; REG(R_SDR_DATA) = 0x0000DEADu;
    REG(R_SDR_CTRL) = 0x3Fu;                                /* start, write, all lanes */
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u)
        if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) { blit_ready = 0u; return; }

    fb_wait();
    REG(R_FB_ADDR)  = probe_addr;
    REG(R_FB_SIZE)  = (2u << 9) | 1u;                       /* h=2, w=1 */
    REG(R_FB_COLOR) = 0u;                                   /* fg=bg=0 -> source offset 0 (a benign read) */
    REG(R_FB_GO)    = FB_OP_BLIT;
    fb_wait();

    /* fb_wait() only confirms the command was accepted into the draw engine's own dispatch
     * FIFO, not that the underlying multi-cycle SDRAM write has actually landed -- this
     * project's own measurements (A-094/A-100) cite up to ~380 cycles worst-case for a single
     * SDRAM access. Give it time to settle before the mailbox readback, or an intermittent
     * false "not detected" is possible (a genuine read-before-write-completes race between the
     * draw engine's own SDRAM client and the CPU mailbox's separate one, both on the same
     * single-port arbiter). */
    t = cycles();
    while ((uint32_t)(cycles() - t) < 512u) { }

    t = cycles();
    REG(R_SDR_ADDR) = probe_row2; REG(R_SDR_CTRL) = 0x3Du;  /* start, read, all lanes */
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u)
        if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) { blit_ready = 0u; return; }

    /* Compare the full 32-bit readback against the full written pattern, not just one
     * 16-bit half -- this project has no existing precedent pinning down which half of a
     * 32-bit mailbox transaction maps to the lower-numbered SDRAM word, so don't guess.
     * The draw engine's OP_BLIT only ever touches the probe_row2 pixel itself; if it
     * changed, the full 32-bit value differs from what was written regardless of which
     * half that pixel occupies. */
    blit_ready = (REG(R_SDR_RDATA) != 0x0000DEADu) ? 1u : 0u;
}

/* B-162: run the probe at most once, on first actual need -- never at boot. */
static void blit_probe_ensure(void)
{
    if (!blit_probed) { blit_probe(); blit_probed = 1u; }
}

/* Helios/Talos H1 (docs/HELIOS_SPEC.md section 10): the same class of probe as blit_probe()
 * above, but for B11's OP_RRECT specifically -- a DIFFERENT bitstream boundary than BLIT_READY()
 * answers. `OP_BLIT`/`OP_BAR` fit within the 3-bit cmd_op decode B-103 already widened, so
 * BLIT_READY() says nothing about B11's own later widening to 4 bits (R_FB_GO bit 14, mp3_soc.v:
 * `fb_cmd_op <= {dDAT_MOSI[14], dDAT_MOSI[2:0]}`). On the bitstream shipped as of this session
 * (predates commit 1278cc7, B11), writing FB_OP_RRECT's bit 14 is silently dropped by the old
 * 3-bit-wide decode, truncating cmd_op to 0 (OP_RUN) -- a single 1px line, not a hang, but a real,
 * silent visual regression for `fb_round_rect`/`fb_round_rect_on` (section 10's own fail-safe
 * requirement) if this were ever skipped. Same technique as blit_probe(): a small `h=2`, `radius=0`
 * command into the same proven-safe off-screen columns (400-511) -- radius=0 means the corner
 * engine never arms (`rrect_pending` requires a nonzero radius), so this degenerates to exactly
 * the same "does h=2 actually write both rows" test BLIT_READY() already uses, just through the
 * new opcode's own decode path instead of the old one. */
static uint8_t rrect_ready;
static uint8_t rrect_probed;
#define RRECT_READY() (rrect_ready != 0u)

static void rrect_probe(void)
{
    const uint32_t probe_addr = 480u;
    const uint32_t probe_row2 = probe_addr + FB_STRIDE;
    uint32_t t;

    t = cycles();
    REG(R_SDR_ADDR) = probe_row2; REG(R_SDR_DATA) = 0x0000DEADu;
    REG(R_SDR_CTRL) = 0x3Fu;
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u)
        if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) { rrect_ready = 0u; return; }

    fb_wait();
    REG(R_FB_ADDR)  = probe_addr;
    REG(R_FB_SIZE)  = (2u << 9) | 1u;                       /* h=2, w=1 */
    REG(R_FB_COLOR) = 0u;                                   /* fg=bg=0 -> source offset 0 (a benign read) */
    REG(R_FB_GO)    = FB_OP_RRECT;                          /* radius=0 -- no corner engine, plain h=2 fill */
    fb_wait();

    t = cycles();
    while ((uint32_t)(cycles() - t) < 512u) { }

    t = cycles();
    REG(R_SDR_ADDR) = probe_row2; REG(R_SDR_CTRL) = 0x3Du;
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u)
        if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) { rrect_ready = 0u; return; }

    rrect_ready = (REG(R_SDR_RDATA) != 0x0000DEADu) ? 1u : 0u;
}

static void rrect_probe_ensure(void)
{
    if (!rrect_probed) { rrect_probe(); rrect_probed = 1u; }
}
