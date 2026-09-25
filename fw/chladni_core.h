/* Chladni meter core (B-271): portable, host-testable logic with no MMIO and no floating point.
 * Fixed point throughout (Q14 field, Q12 weights, Q8 energy); an rv32im core without an FPU runs it.
 * Design and the numbers behind every constant: docs/CHLADNI_METER_SPEC.md and tools/lab/chladni_lab.html.
 * The firmware module (fw/chladni.inc) supplies drawing; sim/test_chladni_core.py tests this file natively.
 *
 * The figure is the zero set of  F(x,y) = sum_k w_k ( cos(m pi x) cos(n pi y) + s_k cos(n pi x) cos(m pi y) ).
 * Every active mode has m and n of the same parity, and all modes share one parity class, so F changes only its
 * sign across a tile edge: the nodal set repeats exactly and one tile can be copied across the meter box. */
#ifndef CHLADNI_CORE_H
#define CHLADNI_CORE_H
#include <stdint.h>

#ifndef CHL_DATA
#define CHL_DATA
#endif

#define CHL_BANDS   16u
#define CHL_MAX_K   4u
#define CHL_POOL_N  15u
#define CHL_ORD_MAX 12u
#define CHL_MAX_RX  40u
#define CHL_MAX_RY  40u
#define CHL_HALF_N  (((CHL_MAX_RY + 1u) / 2u) * CHL_MAX_RX)   /* int16 entries the quarter fold needs */

#ifdef CHL_COUNT
static uint32_t chl_macs;                       /* host tests only: multiply-adds spent in the field */
#endif

typedef struct { uint8_t m, n; int16_t w, s; } chl_mode_t;      /* w: Q14 weight (sum <= 16384), s: Q14 family blend */

typedef struct {
    uint16_t eps0;          /* line half-width, Q14 of the field range */
    uint8_t  topk;          /* active modes, 1..CHL_MAX_K */
    uint8_t  mlim;          /* highest mode order allowed, <= CHL_ORD_MAX (tile_px / (3 * cell_px)) */
    uint16_t up_q12_s;      /* weight rise per second, Q12 */
    uint16_t dn_q12_s;      /* weight fall per second, Q12 */
    uint16_t morph_base;    /* family phase speed, phase units/s (1024 = one turn) */
    uint16_t morph_gain;    /* extra phase speed at full energy */
    uint8_t  sens_q4;       /* trigger gain, Q4 (26 = 1.6) */
    uint16_t refr_ms;       /* refractory time */
    uint8_t  tonal;         /* 1: trigger on a held change of the strongest band, 0: on spectral rise */
    uint8_t  fold;          /* 1: compute one quarter of the tile and mirror it (all modes share a parity class) */
} chl_cfg_t;

typedef struct {
    int32_t  w[CHL_BANDS];              /* Q12 */
    uint8_t  prev[CHL_BANDS];
    uint32_t psi;                       /* family phase, 1024 = one turn */
    uint32_t ema_q8;
    uint32_t last_ms;
    uint16_t burst_q8;
    uint16_t energy_q8;
    uint8_t  scene;
    uint8_t  arg, cand, hold;
    uint8_t  cls_n[2];
    uint8_t  pm[2][CHL_POOL_N], pn[2][CHL_POOL_N];
} chl_state_t;

/* cos(2 pi p / 1024) in Q14 from a 257-entry quarter wave. */
CHL_DATA static const int16_t chl_cosq[257] = {
    16384, 16384, 16383, 16381, 16379, 16376, 16373, 16369, 16364, 16359, 16353, 16347,
    16340, 16332, 16324, 16315, 16305, 16295, 16284, 16273, 16261, 16248, 16235, 16221,
    16207, 16192, 16176, 16160, 16143, 16125, 16107, 16088, 16069, 16049, 16029, 16008,
    15986, 15964, 15941, 15917, 15893, 15868, 15843, 15817, 15791, 15763, 15736, 15707,
    15679, 15649, 15619, 15588, 15557, 15525, 15493, 15460, 15426, 15392, 15357, 15322,
    15286, 15250, 15213, 15175, 15137, 15098, 15059, 15019, 14978, 14937, 14896, 14854,
    14811, 14768, 14724, 14680, 14635, 14589, 14543, 14497, 14449, 14402, 14354, 14305,
    14256, 14206, 14155, 14104, 14053, 14001, 13949, 13896, 13842, 13788, 13733, 13678,
    13623, 13567, 13510, 13453, 13395, 13337, 13279, 13219, 13160, 13100, 13039, 12978,
    12916, 12854, 12792, 12729, 12665, 12601, 12537, 12472, 12406, 12340, 12274, 12207,
    12140, 12072, 12004, 11935, 11866, 11797, 11727, 11656, 11585, 11514, 11442, 11370,
    11297, 11224, 11151, 11077, 11003, 10928, 10853, 10778, 10702, 10625, 10549, 10471,
    10394, 10316, 10238, 10159, 10080, 10001, 9921, 9841, 9760, 9679, 9598, 9516,
    9434, 9352, 9269, 9186, 9102, 9019, 8935, 8850, 8765, 8680, 8595, 8509,
    8423, 8337, 8250, 8163, 8076, 7988, 7900, 7812, 7723, 7635, 7545, 7456,
    7366, 7276, 7186, 7096, 7005, 6914, 6823, 6731, 6639, 6547, 6455, 6363,
    6270, 6177, 6084, 5990, 5897, 5803, 5708, 5614, 5520, 5425, 5330, 5235,
    5139, 5044, 4948, 4852, 4756, 4660, 4563, 4467, 4370, 4273, 4176, 4078,
    3981, 3883, 3786, 3688, 3590, 3492, 3393, 3295, 3196, 3098, 2999, 2900,
    2801, 2702, 2603, 2503, 2404, 2305, 2205, 2105, 2006, 1906, 1806, 1706,
    1606, 1506, 1406, 1306, 1205, 1105, 1005, 904, 804, 704, 603, 503,
    402, 302, 201, 101, 0,
};

static inline int32_t chl_cos(uint32_t p)
{
    p &= 1023u;
    uint32_t q = p >> 8, i = p & 255u;
    switch (q) {
    case 0:  return  chl_cosq[i];
    case 1:  return -chl_cosq[256u - i];
    case 2:  return -chl_cosq[i];
    default: return  chl_cosq[256u - i];
    }
}

/* Phase of cos(o pi (i + 1/2) / R) in table units; cell centres, so a tile edge is half a cell from the seam. */
static inline uint32_t chl_phase(uint32_t o, uint32_t R, uint32_t i)
{
    return ((o * 512u * (2u * i + 1u) + R) / (2u * R)) & 1023u;
}

/* One parity class of modes, simplest first: cls 0 = both even (2,0) (4,0) (4,2)..., cls 1 = both odd (3,1) (5,1).. */
static uint32_t chl_pool_build(uint32_t cls, uint32_t mlim, uint8_t *pm, uint8_t *pn)
{
    uint32_t cnt = 0;
    for (uint32_t m = cls ? 3u : 2u; m <= mlim && m <= CHL_ORD_MAX; m += 2u)
        for (uint32_t n = cls ? 1u : 0u; n < m; n += 2u) {
            uint32_t c = m * m + n * n, at = cnt;
            while (at > 0 && (uint32_t)pm[at - 1] * pm[at - 1] + (uint32_t)pn[at - 1] * pn[at - 1] > c) {
                pm[at] = pm[at - 1]; pn[at] = pn[at - 1]; at--;
            }
            pm[at] = (uint8_t)m; pn[at] = (uint8_t)n; cnt++;
            if (cnt >= CHL_POOL_N) return cnt;
        }
    return cnt;
}

static void chl_init(chl_state_t *s, const chl_cfg_t *c)
{
    uint8_t *z = (uint8_t *)s;
    for (uint32_t i = 0; i < sizeof *s; i++) z[i] = 0;
    s->ema_q8 = 5u;
    s->cand = s->arg = 0xFFu;
    for (uint32_t k = 0; k < 2u; k++) s->cls_n[k] = (uint8_t)chl_pool_build(k, c->mlim, s->pm[k], s->pn[k]);
}

/* Mean band level, Q8 (0..255). */
static uint32_t chl_energy(const uint8_t *lvl)
{
    uint32_t sum = 0;
    for (uint32_t b = 0; b < CHL_BANDS; b++) sum += lvl[b];
    return sum >> 4;
}

/* Called every meter tick (about 26 ms). Returns 1 when a trigger fired: the scene advances, the family phase turns a
 * quarter, and the line width surges. */
static int chl_detect(chl_state_t *s, const chl_cfg_t *c, const uint8_t *lvl, uint32_t now_ms)
{
    int fire = 0;
    uint32_t elapsed = now_ms - s->last_ms;
    if (c->tonal) {
        uint32_t a = 0;
        for (uint32_t b = 1; b < CHL_BANDS; b++) if (lvl[b] > lvl[a]) a = b;
        if (s->arg == 0xFFu) s->arg = (uint8_t)a;
        if (a != s->arg && lvl[a] > 60u) {
            if (s->cand == a) s->hold++; else { s->cand = (uint8_t)a; s->hold = 1; }
            if (s->hold >= 4u && elapsed > c->refr_ms) { fire = 1; s->arg = (uint8_t)a; s->hold = 0; }
        } else s->hold = 0;
    } else {
        uint32_t rise = 0;
        for (uint32_t b = 0; b < CHL_BANDS; b++) if (lvl[b] > s->prev[b]) rise += lvl[b] - s->prev[b];
        uint32_t thr = ((c->sens_q4 * s->ema_q8) >> 12) + 6u;
        if (rise > thr && elapsed > c->refr_ms) fire = 1;
        s->ema_q8 += (int32_t)((rise << 8) - s->ema_q8) >> 5;
    }
    for (uint32_t b = 0; b < CHL_BANDS; b++) s->prev[b] = lvl[b];
    if (fire) {
        s->last_ms = now_ms;
        s->scene = (uint8_t)(s->scene + 1u);
        s->burst_q8 = 256u;
        s->psi += 256u;
    }
    return fire;
}

/* Called once per figure update with the elapsed time. Weights integrate slowly on purpose: slow signals choose the
 * figure, fast ones only change line width. */
static void chl_update(chl_state_t *s, const chl_cfg_t *c, const uint8_t *lvl, uint32_t dt_ms)
{
    int32_t up = (int32_t)(c->up_q12_s * dt_ms / 1000u), dn = (int32_t)(c->dn_q12_s * dt_ms / 1000u);
    for (uint32_t b = 0; b < CHL_BANDS; b++) {
        int32_t t = ((int32_t)lvl[b] * lvl[b]) >> 4;
        int32_t d = t - s->w[b];
        if (d > up) d = up;
        if (d < -dn) d = -dn;
        s->w[b] += d;
    }
    s->energy_q8 = (uint16_t)chl_energy(lvl);
    s->psi += ((c->morph_base + ((s->energy_q8 * (uint32_t)c->morph_gain) >> 8)) * dt_ms) / 1000u;
    uint32_t dec = 1024u * dt_ms / 1000u;
    s->burst_q8 = s->burst_q8 > dec ? (uint16_t)(s->burst_q8 - dec) : 0u;
}

static const int8_t chl_offs[4][4] CHL_DATA = { {0, 0, 0, 0}, {1, -1, 2, -2}, {2, 1, -1, 3}, {-1, 2, 3, 1} };

/* Strongest topk bands become modes. With nothing above the floor the simplest figure of the class is shown, so the
 * box is never blank. Returns the number of modes. */
static uint32_t chl_select(const chl_state_t *s, const chl_cfg_t *c, chl_mode_t *out)
{
    uint32_t cls = s->scene & 1u, n = s->cls_n[cls], K = 0;
    uint32_t taken = 0, sum = 0;
    uint8_t band[CHL_MAX_K];
    for (; K < c->topk && K < CHL_MAX_K; K++) {
        int32_t best = 82;                            /* floor: about 2 percent */
        int32_t at = -1;
        for (uint32_t b = 0; b < CHL_BANDS; b++)
            if (!(taken & (1u << b)) && s->w[b] > best) { best = s->w[b]; at = (int32_t)b; }
        if (at < 0) break;
        taken |= 1u << at; band[K] = (uint8_t)at; sum += (uint32_t)best;
    }
    if (K == 0) {
        out[0].m = s->pm[cls][0]; out[0].n = s->pn[cls][0]; out[0].w = 16384; out[0].s = (int16_t)chl_cos(s->psi);
        return 1;
    }
    for (uint32_t k = 0; k < K; k++) {
        uint32_t b = band[k];
        int32_t idx = (int32_t)((b * (n - 1u) + 7u) / 15u) + chl_offs[(s->scene >> 1) & 3u][b & 3u];
        if (idx < 0) idx = 0;
        if (idx >= (int32_t)n) idx = (int32_t)n - 1;
        out[k].m = s->pm[cls][idx]; out[k].n = s->pn[cls][idx];
        out[k].w = (int16_t)(((uint32_t)s->w[b] * 16384u) / sum);
        out[k].s = (int16_t)chl_cos(s->psi + 147u * b);
    }
    return K;
}

/* Line half-width in cells, Q8, for the even-width test: eps grows with energy and with the trigger burst. */
static uint32_t chl_hw_q8(const chl_state_t *s, const chl_cfg_t *c, uint32_t Rx)
{
    uint32_t f = 141u + ((s->energy_q8 * 410u) >> 8) + ((s->burst_q8 * 230u) >> 8);
    uint32_t eps = ((uint32_t)c->eps0 * f) >> 8;
    uint32_t hw = (eps * Rx * 230u) >> 14;
    return hw < 38u ? 38u : hw;
}

/* One row of the field, Q14. cxm/cxn: per-mode column tables (K * Rx entries each). */
static void chl_field_row(const chl_mode_t *md, uint32_t K, uint32_t j, uint32_t Ry, uint32_t Rx, uint32_t ni,
                          const int16_t *cxm, const int16_t *cxn, int16_t *out)
{
    int32_t a[CHL_MAX_K], b[CHL_MAX_K];
    for (uint32_t k = 0; k < K; k++) {
        int32_t cyn = chl_cos(chl_phase(md[k].n, Ry, j)), cym = chl_cos(chl_phase(md[k].m, Ry, j));
        a[k] = ((int32_t)md[k].w * cyn) >> 14;
        b[k] = ((((int32_t)md[k].w * md[k].s) >> 14) * cym) >> 14;
    }
    for (uint32_t i = 0; i < ni; i++) {
        int32_t sum = 0;
        for (uint32_t k = 0; k < K; k++) sum += a[k] * cxm[k * Rx + i] + b[k] * cxn[k * Rx + i];
        out[i] = (int16_t)(sum >> 14);
    }
#ifdef CHL_COUNT
    chl_macs += ni * K * 2u;
#endif
}

typedef void (*chl_row_cb)(uint32_t j, const uint8_t *lvl, void *ud);

static inline int32_t chl_abs(int32_t v) { return v < 0 ? -v : v; }

/* Level of one cell, 0..3, from |F| against the gradient taken from the four neighbours (the tile wraps). */
static inline uint8_t chl_level(int32_t a, int32_t xl, int32_t xr, int32_t yu, int32_t yd, uint32_t hw_q8)
{
    int32_t g = (chl_abs(xr - xl) + chl_abs(yd - yu)) >> 1;
    if (g < 48) g = 48;
    int32_t T = (int32_t)hw_q8 * g, t1 = (T >> 2) + (T >> 4) + (T >> 6) + (T >> 8);
    int32_t a8 = a << 8;
    if (a8 >= T) return 0;
    if (a8 >= t1 * 2) return 1;
    return a8 >= t1 ? 2 : 3;
}

/* Render one tile of Rx x Ry cells row by row through cb. ring: 3 * Rx int16, cxm/cxn: CHL_MAX_K * Rx int16 each,
 * lvl: Rx bytes. Rows are computed in the order Ry-1, 0, 1 ... Ry-1, 0 so the tile wraps without storing it.
 *
 * half != NULL selects the quarter fold (CHL_HALF_N int16). Precondition: every mode has m and n of one parity class
 * (chl_select guarantees it), because then F(1-x,y) = F(x,1-y) = sg F(x,y) with sg = (-1)^m and the maths for three
 * quarters is a mirror. Only cells x < Rx/2 of rows y < Ry/2 are computed (about a quarter of the multiply-adds); the
 * rest are copied, with sign sg, from those. Proven equal to the full path in sim/test_chladni_core.py. */
static void chl_render(const chl_mode_t *md, uint32_t K, uint32_t Rx, uint32_t Ry, uint32_t hw_q8,
                       int16_t *ring, int16_t *half, int16_t *cxm, int16_t *cxn, uint8_t *lvl, chl_row_cb cb, void *ud)
{
    uint32_t hx = half ? (Rx + 1u) >> 1 : Rx, hy = (Ry + 1u) >> 1, done = 0;
    int32_t sg = (md[0].m & 1u) ? -1 : 1;
    for (uint32_t k = 0; k < K; k++)
        for (uint32_t i = 0; i < hx; i++) {
            cxm[k * Rx + i] = (int16_t)chl_cos(chl_phase(md[k].m, Rx, i));
            cxn[k * Rx + i] = (int16_t)chl_cos(chl_phase(md[k].n, Rx, i));
        }
    for (uint32_t t = 0; t < Ry + 2u; t++) {
        uint32_t row = (t + Ry - 1u) % Ry;
        int16_t *dst = &ring[(t % 3u) * Rx];
        if (!half) {
            chl_field_row(md, K, row, Ry, Rx, Rx, cxm, cxn, dst);
        } else {
            uint32_t rp = row < hy ? row : Ry - 1u - row;
            if (rp >= done) {                                   /* requests rise by one until the middle */
                int16_t *h = &half[rp * Rx];
                chl_field_row(md, K, rp, Ry, Rx, hx, cxm, cxn, h);
                for (uint32_t i = 0; i < Rx / 2u; i++) h[Rx - 1u - i] = (int16_t)(sg * h[i]);
                done = rp + 1u;
            }
            const int16_t *h = &half[rp * Rx];
            if (row < hy) for (uint32_t i = 0; i < Rx; i++) dst[i] = h[i];
            else          for (uint32_t i = 0; i < Rx; i++) dst[i] = (int16_t)(sg * h[i]);
        }
        if (t < 2u) continue;
        const int16_t *up = &ring[((t - 2u) % 3u) * Rx], *cur = &ring[((t - 1u) % 3u) * Rx], *dn = &ring[(t % 3u) * Rx];
        for (uint32_t i = 0; i < Rx; i++) {
            int32_t xl = cur[i ? i - 1u : Rx - 1u], xr = cur[i + 1u < Rx ? i + 1u : 0u];
            lvl[i] = chl_level(chl_abs(cur[i]), xl, xr, up[i], dn[i], hw_q8);
        }
        cb(t - 2u, lvl, ud);
    }
}

/* The two shipped presets (docs/CHLADNI_METER_SPEC.md 5c). Derived from the lab's Lattice and Shimmer templates:
 * tile = tw x th px sampled in cell px squares (Rx x Ry cells), div = meter ticks (about 26 ms) per figure update.
 * mlim = min(11, tw / (3 * cell)). */
typedef struct { uint16_t tw, th; uint8_t cell, Rx, Ry, div; chl_cfg_t c; } chl_preset_t;
#define CHL_PRESET_N 2u
static const chl_preset_t chl_presets[CHL_PRESET_N] CHL_DATA = {
    { 100, 110, 3, 33, 37, 3, { 1245, 3, 11,  668, 401,  63, 391, 26, 600, 0, 1 } },   /* Lattice: 4 tiles across, calm, ~13 updates/s */
    {  50,  55, 2, 25, 28, 2, { 1475, 4,  8, 1455, 873, 125, 782, 26, 350, 1, 1 } },   /* Shimmer: 8 tiles across, follows melody, ~19 updates/s */
};
_Static_assert(sizeof(int) >= 4, "the field maths assumes 32-bit int");
#endif
