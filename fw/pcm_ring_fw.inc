/* fw/pcm_ring_fw.inc -- gapless step G1: the decoders write into the hardware PCM ring instead of the audio FIFO (B-746; hardware src/fpga/core/tau_pcm_ring.sv, MMIO 0x184-0x198;
 * design docs/features/PCM_RING_HW_DESIGN.md; the pure put side is fw/pcm_ring_hw.h, host-tested). Everything here is behind TAU_PCM_RING_FW (default 0) and inert unless the boot
 * probe finds the unit AND the hardware gain stage owns the gain (the ring carries RAW samples: the gain is applied at the FIFO output, so a software gain would be skipped).
 * With the ring on: the CPU stores pairs into the PSRAM window and publishes an index; the hardware keeps the FIFO topped up by itself, so no firmware stall shorter than the ring's
 * fill underruns. The clock and the software meters follow the HEARD position (what the FIFO has released), not the decoded one. Pause HOLDS the reader; a natural end plays the ring out
 * before the next track loads (G2 will replace that wait with a boundary marker). Anything that flushes (track change, seek) drops the unread pairs in hardware. */
#if TAU_PCM_RING_FW
#define RG_SIZE    52000u          /* pairs (208 KB) */
#define RG_MARGIN  3328u           /* slots behind the read index kept readable: FIFO depth 2048 + the 1152-pair meter window + slack */
#define RG_BASE_W  0x1C0000u       /* PSRAM word address of ring[0] = byte +0x700000 of the window (map: docs/features/CYMO_TEMPO_INTEGRATION.md: image +0x10000 (4 MiB), queue +0x510000, tempo staging +0x600000..+0x640000, cold image 8 MiB) */
#define R_RING_BASE 0x80000184u
#define R_RING_SIZE 0x80000188u
#define R_RING_WPTR 0x8000018Cu
#define R_RING_RIDX 0x80000190u
#define R_RING_CTRL 0x80000194u
#define R_RING_CNT  0x80000198u
#define RG_WIN ((volatile uint32_t *)(uintptr_t)(0xA4000000u + 4u * RG_BASE_W))
static uint32_t rg_rd_ridx(void) { return REG(R_RING_RIDX); }
static void     rg_wr_word(uint32_t i, uint32_t w) { RG_WIN[i] = w; }
static void     rg_wr_wptr(uint32_t i) { REG(R_RING_WPTR) = i; }
static int rg_spin_common(int abortable)
{
    const uint32_t t0 = cycles();
    poll_input();
    refill_pump();
    { const uint32_t d = cycles() - t0; fl_idle_cyc += d; LD_WAIT(d); }
    return (abortable && reload_pending) ? 1 : 0;
}
static int rg_spin_ab(void) { return rg_spin_common(1); }
static int rg_spin_na(void) { return rg_spin_common(0); }
static const prh_hooks_t rg_hk_ab = { rg_rd_ridx, rg_wr_word, rg_wr_wptr, rg_spin_ab };
static const prh_hooks_t rg_hk_na = { rg_rd_ridx, rg_wr_word, rg_wr_wptr, rg_spin_na };

COLD_FN2 static uint8_t rg_put_frame(const int16_t *pcm_, uint32_t pairs, uint32_t stereo) { return prh_put(&rg, &rg_hk_ab, pcm_, pairs, stereo); }
COLD_FN2 static uint8_t rg_put1(int32_t l, int32_t r, uint8_t abortable)
{
    rg_n1++;
    return prh_put1(&rg, abortable ? &rg_hk_ab : &rg_hk_na, l, r, (rg_n1 & 63u) == 0u);
}
COLD_FN2 static void rg_publish(void) { REG(R_RING_WPTR) = rg.w; }
COLD_FN2 static void rg_hold(uint8_t h)
{
    if (!rg_on || rg_held == h) return;
    rg_held = h;
    REG(R_RING_CTRL) = 1u | ((uint32_t)h << 1);
}
COLD_FN2 static void rg_arm(void)
{
    REG(R_RING_CTRL) = 0u;                        /* disabled: the reader follows the write index */
    REG(R_RING_BASE) = RG_BASE_W; REG(R_RING_SIZE) = RG_SIZE; REG(R_RING_WPTR) = 0u;
    for (uint32_t n = 0; n < 1000u && REG(R_RING_RIDX) != 0u; n++) { }
    prh_init(&rg, RG_SIZE, RG_MARGIN, 0u);
    rg_held = 0u; rg_n1 = 0u;
    REG(R_RING_CTRL) = 1u;
    rg_fed = REG(R_RING_CNT);
    rg_on = 1u;
}
/* Back to the direct FIFO path (a hardware gain hand-over to software, the tempo stretcher): the unread ring is dropped and the FIFO flushed. */
COLD_FN2 static void rg_off(void)
{
    if (!rg_on) return;
    rg_on = 0u;
    REG(R_RING_CTRL) = 0u;
    pcm_flush();
}
/* At a track load while the ring is off (the first one after the cold image is up, and again after a tempo or hardware-gain excursion). */
COLD_FN2 static void rg_probe(void)
{
    if (!(REG(R_RING_CTRL) >> 31) || !vol_st.hw) return;
    volatile uint32_t *a = RG_WIN, *b = RG_WIN + (RG_SIZE - 1u);          /* the window must answer before the hardware is trusted with it */
    a[0] = 0x5A5AC3C3u; b[0] = 0xA5A53C3Cu; a[1] = 0x12345678u;
    if (a[0] != 0x5A5AC3C3u || b[0] != 0xA5A53C3Cu || a[1] != 0x12345678u) return;
    rg_arm();
}
/* A natural end: let everything decoded play out (the ring and the FIFO) before the next track's load flushes. Pause holds the reader; input and a reload request leave the wait. */
COLD_FN2 static void rg_playout(void)
{
    if (!rg_on) return;
    rg_publish();
    const uint32_t t0 = cycles();
    while (!reload_pending) {
        poll_input();
        rg_hold(paused ? 1u : 0u);
        if (!paused) {
            if (REG(R_RING_RIDX) == rg.w && PCM_EMPTY(REG(R_PCM_ST))) break;
            if ((uint32_t)(cycles() - t0) > CLK_HZ * 5u) break;           /* never wait for ever */
        }
    }
    rg_hold(0u);
}
/* The software meters at the heard position: the pairs the FIFO has released since the last call, read back from the ring (they are within the margin, so never overwritten). */
COLD_FN2 static void rg_meter_feed(void)
{
    const uint32_t cnt = REG(R_RING_CNT), heard = cnt - PCM_LEVEL(REG(R_PCM_ST));
    int32_t n = (int32_t)(heard - rg_fed);
    if (n <= 0) return;
    if (n > 1152) { rg_fed = heard - 1152u; n = 1152; }
    const uint32_t ridx = REG(R_RING_RIDX), back = cnt - rg_fed;
    uint32_t pos = ridx >= back ? ridx - back : ridx + RG_SIZE - back;
    for (int32_t i = 0; i < n; i++) {
        const uint32_t w = RG_WIN[pos];
        pcm[2 * i] = (short)(w & 0xFFFFu); pcm[2 * i + 1] = (short)(w >> 16);
        if (++pos == RG_SIZE) pos = 0u;
    }
    rg_fed += (uint32_t)n;
    meters_feed(pcm, 2 * n, 1);
}
/* Pairs decoded but not yet heard, for the clock. */
COLD_FN2 static uint32_t rg_lead(void) { return prh_lead(&rg, REG(R_RING_RIDX), PCM_LEVEL(REG(R_PCM_ST))); }
#endif
