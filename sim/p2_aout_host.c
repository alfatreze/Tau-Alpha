/* Host model for fw2/aout/aout.c and fw2/transport/tone.c (sim/test_p2_aout.py). The REAL module code is compiled against a model of the hardware: the ring reader (PSRAM
 * window array, write/read index, push count), the 2,048-pair FIFO drained at the output rate, the hold bit and the flush, with simulated time driven by the loop.
 * A ground-truth ledger knows, for every pair ever put, its value, segment and source position, and for every pair the reader pushed, its push index: the module's view
 * (heard segment, position, window contents, accounting) is checked against it at every loop. Prints "KEY value" lines. */
#define P2_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#define CLK_HZ 66666667u
typedef unsigned long long u64;

/* ---- hardware model ---- */
static u64 T;                                        /* cycles */
static uint32_t hw_ring[52000 + 8];
static uint32_t m_wptr, m_ridx, m_cnt, m_base, m_size = 52000, m_enable, m_hold = 1, m_inc = 0, m_cymo, m_gain_t, m_gain_c;
static uint32_t fifo[4096]; static uint32_t f_head, f_n;        /* the FIFO: values, oldest first */
static u64 consume_frac;                              /* fixed 32.32: pairs consumed per cycle accumulates */
static u64 dropped_unread, dropped_fifo, consumed_total, silent_ticks, reader_pushed;
static uint32_t consumed_idx_next; static int consumed_idx_valid; static u64 consumed_holes;
static u64 mismatch_data, mismatch_heard, checked_heard;
/* ledger */
typedef struct { uint32_t val, gen, src; } lab_t;
static lab_t *ringq; static uint32_t rq_h, rq_t;     /* put but not yet pushed, oldest first */
#define RQN (1u << 17)
static lab_t *label_at; static uint8_t *label_ok;   /* indexed by push count */
#define LBN (1u << 24)
static uint64_t ref_ph; static u64 ref_bad;          /* independent sine reference */
static u64 put_total_pairs;

static void reader_step(uint32_t cyc_budget)
{
    while (cyc_budget >= 32 && m_enable && !m_hold && m_ridx != m_wptr && f_n < 1536) {
        uint32_t v = hw_ring[m_ridx];
        lab_t tr = ringq[rq_h & (RQN - 1)];
        if (v != tr.val) mismatch_data++;                       /* the window held something other than what was put for this slot */
        rq_h++;
        fifo[(f_head + f_n) & 4095] = v; f_n++;
        label_at[m_cnt & (LBN - 1)] = tr; label_ok[m_cnt & (LBN - 1)] = 1;
        m_cnt++; reader_pushed++;
        if (++m_ridx == m_size) m_ridx = 0;
        cyc_budget -= 32;
    }
}
static void advance(u64 dc)
{
    while (dc) {
        uint32_t step = dc > 66000 ? 66000 : (uint32_t)dc;      /* about 1 ms: consumption and reader stay coupled */
        dc -= step; T += step;
        consume_frac += (u64)step * m_inc;                      /* pairs << 32 */
        u64 want = consume_frac >> 32; consume_frac &= 0xFFFFFFFFull;
        for (u64 i = 0; i < want; i++) {
            if (f_n) { f_head = (f_head + 1) & 4095; f_n--; consumed_total++; }
            else silent_ticks++;
        }
        reader_step(step);
    }
}
static uint32_t REG_RD_impl(uint32_t a)
{
    switch (a) {
    case 0x8000000Cu: return (uint32_t)T;
    case 0x80000194u: return 0x80000000u | (m_hold << 1) | m_enable;
    case 0x80000190u: return m_ridx;
    case 0x80000198u: return m_cnt;
    case 0x80000034u: return f_n | (f_n == 0 ? 1u << 16 : 0);
    default: return 0;
    }
}
static void REG_WR_impl(uint32_t a, uint32_t v)
{
    switch (a) {
    case 0x80000194u: m_enable = v & 1; m_hold = (v >> 1) & 1; if (!m_enable) { m_ridx = m_wptr; } break;
    case 0x80000184u: m_base = v; break;
    case 0x80000188u: m_size = v; break;
    case 0x8000018Cu: m_wptr = v; if (!m_enable) m_ridx = v; break;
    case 0x80000038u: m_inc = v; break;
    case 0x80000150u: m_cymo = v; break;
    case 0x8000016Cu: m_gain_t = v; break;
    case 0x80000168u: m_gain_c = v; break;
    case 0x80000034u:
        if (v & 1) {                                           /* flush: the FIFO and the unread ring are dropped in one write */
            dropped_fifo += f_n; f_n = 0;
            uint32_t unread = m_wptr >= m_ridx ? m_wptr - m_ridx : m_wptr + m_size - m_ridx;
            dropped_unread += unread; rq_h += unread; m_ridx = m_wptr;
        }
        break;
    }
}
#define REG_RD(a) REG_RD_impl(a)
#define REG_WR(a, v) REG_WR_impl((a), (v))
#define P2_RING_WINDOW hw_ring

/* ---- hooks: the ledger sees every accepted put ---- */
static uint32_t pend_val[1024]; static uint32_t pend_n;
static void host_on_put(const int16_t *pcm, uint32_t n, int stereo)
{
    pend_n = n;
    for (uint32_t i = 0; i < n; i++) {
        int32_t l = pcm[stereo ? 2 * i : i], r = stereo ? pcm[2 * i + 1] : l;
        pend_val[i] = ((uint32_t)(uint16_t)(int16_t)r << 16) | (uint32_t)(uint16_t)(int16_t)l;
    }
}
#define P2_HOST_ON_PUT(p, n, s) host_on_put((p), (n), (s))
static uint32_t tone_rate_now(void);
static void host_label(uint32_t gen, uint32_t src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        lab_t l = { pend_val[i], gen, src + i };
        ringq[rq_t & (RQN - 1)] = l; rq_t++;
        /* independent reference: a double-precision sine at the accumulated phase of the rate in force */
        double ph = (double)(ref_ph & 0xFFFFFFFFull) / 4294967296.0;
        double ex = 32767.0 * sin(6.283185307179586 * ph) / 16.0;
        int16_t got = (int16_t)(pend_val[i] & 0xFFFF);
        if (fabs((double)got - ex) > 2.0) ref_bad++;
        ref_ph += (u64)(((u64)1000 << 32) / tone_rate_now());
        put_total_pairs++;
    }
}
#define P2_HOST_LABEL(g, s, n) host_label((g), (s), (n))

#include "../fw2/aout/aout.c"
#include "../fw2/transport/tone.c"
static uint32_t tone_rate_now(void) { return tone.rate; }

/* ---- driver ---- */
static u64 seed_s = 12345;
static uint32_t rnd(void) { seed_s = seed_s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(seed_s >> 33); }

static void check_heard(void)
{
    aout_snap_t s = aout_snap(); uint32_t h = aout_heard(&s);
    if (!s.fifo) return;
    if (!label_ok[h & (LBN - 1)]) return;
    lab_t t = label_at[h & (LBN - 1)];
    checked_heard++;
    if (!tone.cur_valid || tone.cur_gen != t.gen || tone.cur_pos != t.src) mismatch_heard++;
}

int main(int argc, char **argv)
{
    ringq = calloc(RQN, sizeof(lab_t)); label_at = calloc(LBN, sizeof(lab_t)); label_ok = calloc(LBN, 1);
    int scenario = argc > 1 ? atoi(argv[1]) : 0;
    if (aout_init() != 0) { printf("INIT_FAIL 1\n"); return 1; }
    tone_init(1);
    if (scenario == 1) {                                          /* overfill: no governor, the module alone must keep the margin */
        static int16_t blk[700]; for (int i = 0; i < 700; i++) blk[i] = (int16_t)(i * 7 + 3);
        tone.rate = 44100; aout_set_rate(44100, 1);
        u64 mism = 0; uint32_t put_n = 0;
        for (int it = 0; it < 200; it++) {
            uint32_t n = aout_put(blk, 700, 0);
            for (uint32_t i = 0; i < n; i++) { lab_t l = { pend_val[i], 0, 0 }; ringq[rq_t & (RQN - 1)] = l; rq_t++; put_n++; }
            advance(66000 * 3);
        }
        /* the pairs still readable behind the read index (the margin) must be intact: compare slots [ridx-margin, ridx) with what the ledger says was pushed there */
        for (uint32_t k = 1; k <= AOUT_RING_MARGIN && k <= m_cnt; k++) {
            uint32_t idx = (m_ridx + AOUT_RING_PAIRS - k) % AOUT_RING_PAIRS;
            if (label_ok[(m_cnt - k) & (LBN - 1)] && hw_ring[idx] != label_at[(m_cnt - k) & (LBN - 1)].val) mism++;
        }
        printf("MARGIN_MISMATCH %llu\nSTALLS %u\nPUT %u\nDATA_MISMATCH %llu\n", mism, aout_stats.put_stalls, put_n, mismatch_data);
        return 0;
    }
    /* scenario 0: the tone transport for 45 s of simulated time with keys, stalls and flushes */
    u64 end = (u64)CLK_HZ * 45;
    int hold_until_set = 0; u64 hold_until = 0;
    int fired[8] = {0};
    uint32_t stalls = 0;
    if (scenario == 2) end = (u64)CLK_HZ * 80;
    tone_key(KEY_A);
    while (T < end) {
        u64 now_ms = T / (CLK_HZ / 1000);
        uint32_t key = 0;
        if (scenario == 2) { fired[0] = fired[1] = fired[2] = fired[3] = fired[4] = 1; }
        if (!fired[0] && now_ms >= 13500) { fired[0] = 1; key = KEY_B; hold_until = now_ms + 2000; hold_until_set = 1; }
        if (hold_until_set && now_ms >= hold_until) { hold_until_set = 0; key = KEY_B; }
        if (!fired[1] && now_ms >= 20000) { fired[1] = 1; key = KEY_X; }
        if (!fired[2] && now_ms >= 25000) { fired[2] = 1; key = KEY_Y; }
        if (!fired[3] && now_ms >= 30000) { fired[3] = 1; key = KEY_A; }
        if (!fired[4] && now_ms >= 36000) { fired[4] = 1; key = KEY_Y; }
        if (key) tone_key(key);
        tone_pump(); tone_heard(); check_heard(); aout_poll(tone.playing);
        /* the loop's own cost, and sometimes a stall (a slot settle, a repaint): up to 500 ms, never longer than the lead */
        u64 cost = 40000 + (rnd() % 40000);
        if (rnd() % 400 == 0) { uint32_t ms = 30 + rnd() % 470; cost += (u64)ms * (CLK_HZ / 1000); stalls++; }
        advance(cost);
    }
    printf("CONSUMED %llu\nPUT_PAIRS %llu\nSWAPS %u\nACCT_ERR %u\nACCT_BAD %u\nOVERFLOW %u\nUNDER_RING %u\nDATA_MISMATCH %llu\nHEARD_MISMATCH %llu\nHEARD_CHECKED %llu\nREF_BAD %llu\nFLUSHES %u\n"
           "ENDFILL %u\nDRY %u\nDROPPED_UNREAD %llu\nDROPPED_FIFO %llu\nSTALLS_INJECTED %u\nRING_MIN %u\nRING_MAX %u\nGEN %u\nPUT_STALLS %u\n",
           consumed_total, put_total_pairs, tone_tl.swaps, tone_tl.acct_err, tone.acct_bad, tone_tl.overflow, aout_stats.under_ring, mismatch_data, mismatch_heard, checked_heard,
           ref_bad, aout_stats.flushes, (uint32_t)((m_wptr >= m_ridx ? m_wptr - m_ridx : m_wptr + m_size - m_ridx) + f_n), aout_stats.dry, dropped_unread, dropped_fifo, stalls, aout_stats.ring_min, aout_stats.ring_max, tone.gen, aout_stats.put_stalls);
    return 0;
}
