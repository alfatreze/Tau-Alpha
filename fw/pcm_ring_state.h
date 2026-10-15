/* fw/pcm_ring_state.h -- the constants, state and forward declarations of the hardware PCM ring firmware (fw/pcm_ring_fw.inc), included early in fw/player.c because pcm_flush(), the clock, the
 * ReplayGain code and the Info page need them before the .inc itself (which needs the cymo hooks). Also included by the host harness sim/pcm_ring_gp_harness.c. Everything is behind TAU_PCM_RING_FW.
 * The includer must provide: REG, uint types, prh_t (fw/pcm_ring_hw.h), COLD_SR. */
#ifndef PCM_RING_STATE_H
#define PCM_RING_STATE_H
#if TAU_PCM_RING_FW
/* B-746 (gapless G1, fw/pcm_ring_fw.inc): the hardware PCM ring is the decoders' output while rg_on. */
#define RG_SIZE    52000u          /* pairs (208 KB) */
#define RG_MARGIN  3328u           /* slots behind the read index kept readable: FIFO depth 2048 + the 1152-pair meter window + slack */
#define RG_BASE_W  0x1C0000u       /* PSRAM word address of ring[0] = byte +0x700000 of the window (map: docs/features/CYMO_TEMPO_INTEGRATION.md: image +0x10000 (4 MiB), queue +0x510000, tempo staging +0x600000..+0x640000, cold image 8 MiB) */
#define R_RING_BASE 0x80000184u
#define R_RING_SIZE 0x80000188u
#define R_RING_WPTR 0x8000018Cu
#define R_RING_RIDX 0x80000190u
#define R_RING_CTRL 0x80000194u
#define R_RING_CNT  0x80000198u
static prh_t    rg;
static uint8_t  rg_on, rg_held;
static uint32_t rg_fed, rg_n1, rg_min;
static uint16_t rg_und;                     /* Info > RING: FIFO empty while the ring held pairs (the reader fell behind), saturating */
static uint8_t  rg_put_frame(const int16_t *pcm_, uint32_t pairs, uint32_t stereo);
static uint8_t  rg_put1(int32_t l, int32_t r, uint8_t abortable);
static void     rg_publish(void);
static void     rg_hold(uint8_t h);
static void     rg_off(void);
static void     rg_probe(void);
static void     rg_playout(void);
static void     rg_meter_feed(void);
static uint32_t rg_lead(void);
/* G2b (fw/pcm_ring_fw.inc section "the gapless boundary"): gp_want = the end-of-file branch asked for a gapless hand-over, gp_mode = load_track() is running in that mode, gp_pend = the new track is
 * decoding but the old one is still being heard (the marker is not reached yet). */
static uint8_t  gp_want, gp_mode, gp_pend, gp_old_fmt;
static uint32_t rg_put_total, gp_marker, gp_pre_pp, gp_old_rate;
static uint16_t gp_applied, gp_aborted;
static uint32_t rg_ui_last;               /* G2c: when the MP3 loop last drew (cycles) */
/* G2c: 1 when at least 20 ms have passed since the last draw (and records it). */
static inline int rg_ui_due(void)
{
    const uint32_t t = cycles();
    if ((uint32_t)(t - rg_ui_last) < (CLK_HZ / 50u)) return 0;
    rg_ui_last = t;
    return 1;
}
static void     gp_eof_begin(void);
static void     gp_begin_load(void);
static void     gp_abort(void);
static int      gp_end_load(int opened);
static void     gp_handoff(void);
static void     gp_tick(void);
COLD_SR static void load_ui_tail(uint32_t *tphase);
/* What pcm_flush() does about the ring (a flush drops the unread pairs in hardware): the meter read-back and the put count restart at the push count, and a pending gapless marker is reached at once. */
static inline void rg_on_flush(void)
{
    if (!rg_on) return;
    rg_fed = REG(R_RING_CNT); rg_put_total = rg_fed;
    if (gp_pend) gp_marker = rg_fed;
}
#endif
#endif
