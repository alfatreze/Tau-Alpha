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
static uint8_t blit_probe_tries;     /* B-328: a failed probe is retried (up to 3 times) instead of latching 'not ready' for the session */
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
    /* B-328: poll instead of one fixed 512-cycle wait. This probe is first run at the first track load, right after chrome drawing
     * was queued ahead of it, so the copy can land later than 512 cycles; the old single read then saw the untouched sentinel, set
     * blit_ready=0 and (blit_probed) never asked again, so BLIT_READY() stayed false for the whole session and every consumer
     * (TIM1 covers: Info E6 on every run, CBLIT thumbnails) silently fell back. Now wait up to ~50 ms for the word to change. */
    const uint32_t t0 = cycles();
    uint32_t rd = 0x0000DEADu;
    do {
        t = cycles();
        while ((uint32_t)(cycles() - t) < 512u) { }
        t = cycles();
        REG(R_SDR_ADDR) = probe_row2; REG(R_SDR_CTRL) = 0x3Du;  /* start, read, all lanes */
        while ((uint32_t)(cycles() - t) < 128u) { }
        while (REG(R_SDR_STATUS) & 1u)
            if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) { blit_ready = 0u; return; }
        rd = REG(R_SDR_RDATA);
    } while (rd == 0x0000DEADu && (uint32_t)(cycles() - t0) < CLK_HZ / 20u);

    /* Compare the full 32-bit readback against the full written pattern, not just one
     * 16-bit half. The draw engine's OP_BLIT only ever touches the probe_row2 pixel itself; if it
     * changed, the full 32-bit value differs from what was written. */
    blit_ready = (rd != 0x0000DEADu) ? 1u : 0u;
}

/* B-162: run the probe at most once, on first actual need -- never at boot. */
static void blit_probe_ensure(void)
{
    if (!blit_probed) {
        blit_probe();
        if (blit_ready || ++blit_probe_tries >= 3u) blit_probed = 1u;
    }
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
static uint8_t rrect_probe_tries;    /* B-335: a failed probe is retried (up to 3 times) instead of latching 'not ready' for the session */
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

    /* B-335: poll instead of one fixed 512-cycle wait (the same false negative blit_probe() had, B-328): the probe first runs when the first
     * selected row is drawn, right behind queued chrome drawing, so the fill can land after 512 cycles; the single read then saw the untouched
     * sentinel, rrect_ready stayed 0 and (rrect_probed) it was never asked again, so the hardware rounded rectangles may never have been used
     * on a Pocket. Wait up to ~50 ms for the word to change. */
    const uint32_t t0 = cycles();
    uint32_t rd = 0x0000DEADu;
    do {
        t = cycles();
        while ((uint32_t)(cycles() - t) < 512u) { }
        t = cycles();
        REG(R_SDR_ADDR) = probe_row2; REG(R_SDR_CTRL) = 0x3Du;
        while ((uint32_t)(cycles() - t) < 128u) { }
        while (REG(R_SDR_STATUS) & 1u)
            if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) { rrect_ready = 0u; return; }
        rd = REG(R_SDR_RDATA);
    } while (rd == 0x0000DEADu && (uint32_t)(cycles() - t0) < CLK_HZ / 20u);

    rrect_ready = (rd != 0x0000DEADu) ? 1u : 0u;
}
static void rrect_probe_ensure(void)
{
    if (!rrect_probed) {
        rrect_probe();
        if (rrect_ready || ++rrect_probe_tries >= 3u) rrect_probed = 1u;
    }
}

/* B12 (B-454): OP_BAR's lit-row count widened 7->9 bits (cmd_glyph_hi, R_FB_GO bits 16:15) --
 * deliberately no hardware-capability probe here (owner: alpha-stage firmware/bitstream pairs are
 * always built and shipped together, so an old-bitstream fallback is unneeded complexity right now;
 * revisit once betas/a real release need firmware to run against a bitstream it wasn't built
 * alongside). fb_bar() (fw/player.c) sends the full 9-bit value unconditionally. */

/* ==== B-334: alpha blend from firmware (Talos B5, the pipelined version, B-327) ==========================================================
 * A blended OP_BLIT computes dst = (src * a + dst * (256 - a)) >> 8 per colour channel for the whole rectangle, reading the destination
 * from the framebuffer itself. It needs a bitstream built with TAU_BLIT_BLEND (BLEND_READY() finds out, once, with a known-answer blit)
 * and it uses the STICKY blend field (R_BLT_IDX 5), so two rules apply:
 *   1. the field is consulted when a command EXECUTES, not when it is queued, so it may only be switched off after every blend blit has
 *      executed -- fb_fence() below waits for that;
 *   2. only OP_BLIT is affected (never RECT/COPY/CHAR/CBLIT), so leaving it on briefly is harmless to everything else, but no OTHER
 *      OP_BLIT (Chladni's replication, the probes) may run while it is on.
 * All cold code: only the meters' trail uses it. Off-screen cells (columns 508.., rows 1019..1021) are the scratch pixels. */
#define BLEND_CELL_X 508u
#define BLEND_CELL_Y 1021u
static uint8_t blend_state;                  /* 0 not probed yet, 1 ready, 2 not available on this bitstream */
#define BLEND_READY() (blend_state == 1u)

COLD_FN3 static int blend_mb_write(uint32_t a, uint32_t d)
{
    REG(R_SDR_ADDR) = a; REG(R_SDR_DATA) = d; REG(R_SDR_CTRL) = 0x3Fu;
    const uint32_t t = cycles();
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u) if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) return -1;
    return 0;
}
COLD_FN3 static int blend_mb_read(uint32_t a, uint32_t *out)
{
    REG(R_SDR_ADDR) = a; REG(R_SDR_CTRL) = 0x3Du;
    const uint32_t t = cycles();
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u) if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) return -1;
    *out = REG(R_SDR_RDATA);
    return 0;
}

/* Returns once every command queued before it has executed (the engine runs its queue in order): a one-pixel rectangle is queued behind
 * them and the mailbox polls that pixel for the change. Gives up after about 50 ms. Does nothing while an overlay holds the engine
 * (its commands are dropped, so there is nothing to wait for). */
COLD_FN3 static void fb_fence(void)
{
    if (FB_HELD()) return;
    const uint32_t a = BLEND_CELL_Y * FB_STRIDE + BLEND_CELL_X;
    uint32_t r = 0;
    if (blend_mb_write(a, 0xDEADBEEFu)) return;
    fb_rect(BLEND_CELL_X, BLEND_CELL_Y, 1u, 1u, 0x0001u);
    const uint32_t t0 = cycles();
    do {
        uint32_t t = cycles();
        while ((uint32_t)(cycles() - t) < 256u) { }
        if (blend_mb_read(a, &r)) return;
    } while (r == 0xDEADBEEFu && (uint32_t)(cycles() - t0) < CLK_HZ / 20u);
}

/* B-440: owner reports trail=0 fixes the Scope accumulation completely, but trail=5% and trail=80%
 * look identical -- severity isn't scaling with the configured fade amount at all, which is what you'd
 * see if the alpha value never actually reaches the blend hardware correctly (stuck near 0 regardless
 * of what's requested) rather than a proportional/rounding issue. R_BLT_IDX/R_BLT_DATA (bits[15:8] =
 * alpha) are write-only in hardware (docs/MMIO_ALLOCATION.md 0xC0/0xC4: "W"), same as SRC_BASE/
 * DST_BASE -- shadow the last value WRITTEN here, same technique as fb_set_bases()'s dbg_base_src/dst,
 * so a mismatch between what firmware intended and what the mailbox-confirmed blend actually reflects
 * (B-439's JTAG reads, in hindsight, were consistent with alpha stuck at 0 the whole time -- dismissed
 * then as an inconclusive 1-LSB-contrast rounding artifact, worth re-checking now this points here) is
 * visible without another RTL/JTAG cycle. */
static uint32_t dbg_blend_alpha;
/* Blend on with source weight `a` (0..255, mode 0 = the DSP alpha); off. */
COLD_FN3 static void fb_blend_on(uint32_t a)  { fb_wait(); REG(R_BLT_IDX) = 5u; REG(R_BLT_DATA) = ((a & 0xFFu) << 8) | 1u; dbg_blend_alpha = a & 0xFFu; }
COLD_FN3 static void fb_blend_off(void)       { REG(R_BLT_IDX) = 5u; REG(R_BLT_DATA) = 0u; }

/* Known-answer probe: blend white over black at a = 128; a bitstream with the blend gives 0x7BEF, one without it copies 0xFFFF. Either
 * half of the mailbox word may hold the pixel (the halves' order is not assumed). Not run while an overlay holds the engine. */
COLD_FN3 static void blend_probe(void)
{
    if (FB_HELD()) return;
    blend_state = 2u;
    blit_probe_ensure();
    if (!BLIT_READY()) return;
    const uint32_t src = 1019u * FB_STRIDE + BLEND_CELL_X, dst = 1020u * FB_STRIDE + BLEND_CELL_X;
    uint32_t r = 0;
    if (blend_mb_write(src, 0xFFFFFFFFu) || blend_mb_write(dst, 0u)) return;
    fb_blend_on(128u);
    fb_blit(BLEND_CELL_X, 1019u, BLEND_CELL_X, 1020u, 1u, 1u);
    fb_fence();
    fb_blend_off();
    if (blend_mb_read(dst, &r)) return;
    if ((r & 0xFFFFu) == 0x7BEFu || (r >> 16) == 0x7BEFu) blend_state = 1u;
}
COLD_FN3 static void blend_ensure(void)
{
    if (!blend_state) blend_probe();
}
