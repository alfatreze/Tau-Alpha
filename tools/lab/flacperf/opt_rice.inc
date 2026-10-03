/* ===== flacperf candidate code (scratch only) ===== */
#ifndef OPT1
#define OPT1 0
#endif
#ifndef OPT2
#define OPT2 0
#endif
#ifndef OPT3
#define OPT3 0
#endif
#ifndef CLZ_MODE
#define CLZ_MODE 0      /* 0 __builtin_clz (libgcc __clzsi2 on rv32im), 1 inline binary search, 2 small-q first */
#endif

static inline __attribute__((always_inline)) uint32_t clz32(uint32_t x)   /* x != 0 */
{ // @cat=RICE
#if CLZ_MODE == 0
    return (uint32_t)__builtin_clz(x); // @cat=RICE
#else
    uint32_t n = 0; // @cat=RICE
#if CLZ_MODE == 2
    if (x & 0x80000000u) return 0; // @cat=RICE
    if (x & 0x40000000u) return 1; // @cat=RICE
    if (x & 0x20000000u) return 2; // @cat=RICE
#endif
    if (!(x & 0xFFFF0000u)) { n += 16; x <<= 16; } // @cat=RICE
    if (!(x & 0xFF000000u)) { n += 8;  x <<= 8; } // @cat=RICE
    if (!(x & 0xF0000000u)) { n += 4;  x <<= 4; } // @cat=RICE
    if (!(x & 0xC0000000u)) { n += 2;  x <<= 2; } // @cat=RICE
    if (!(x & 0x80000000u)) { n += 1; } // @cat=RICE
    return n; // @cat=RICE
#endif
}

static inline __attribute__((always_inline)) int32_t zz(uint32_t v)
{
    return (v & 1u) ? -(int32_t)((v >> 1) + 1u) : (int32_t)(v >> 1); // @cat=RICE
}

/* One Rice value, parameter k (not the escape code). State in caller locals (acc/cnt/pos/have):
 * left-aligned 32-bit window cut from the 64-bit reservoir with 32-bit ops only, one clz, no 64-bit
 * shifts. Anything unusual (q >= 32, q+1+k > 32, or no whole word left in buf) goes to the original
 * unary()/bits() path, so bitstream consumption is identical by construction. */
static inline __attribute__((always_inline)) int32_t rice_one(flac_t *f, uint64_t *acc, uint32_t *cnt,
                                                               uint32_t *pos, uint32_t *have, uint32_t k)
{
    uint32_t c = *cnt; // @cat=RICE
    if (c < 32u) { // @cat=REFILL
        if (*pos + 4u <= *have) { // @cat=REFILL
            const uint8_t *p = f->buf + *pos; // @cat=REFILL
            uint32_t wd = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; // @cat=REFILL
            *acc = (*acc << 32) | wd; // @cat=REFILL
            *pos += 4u; c += 32u; *cnt = c; // @cat=REFILL
        } else goto slow; // @cat=REFILL
    }
    { // @cat=RICE
        uint32_t hi = (uint32_t)(*acc >> 32), lo = (uint32_t)*acc; // @cat=RICE
        uint32_t s = c - 32u; // @cat=RICE
        uint32_t w = s ? (hi << (32u - s)) | (lo >> s) : lo; // @cat=RICE
        if (w) { // @cat=RICE
            uint32_t q = clz32(w); // @cat=RICE
            uint32_t used = q + 1u + k; // @cat=RICE
            if (used <= 32u) { // @cat=RICE
                uint32_t t = (w << q) << 1; // @cat=RICE
                uint32_t v = (q << k) | (k ? t >> (32u - k) : 0u); // @cat=RICE
                *cnt = c - used; // @cat=RICE
                return zz(v); // @cat=RICE
            }
        }
    }
slow:
    f->bitacc = *acc; f->bitcnt = *cnt; f->pos = *pos; // @cat=RICE
    { uint32_t q = unary(f); uint32_t v = (q << k) | bits(f, k); // @cat=RICE
      *acc = f->bitacc; *cnt = f->bitcnt; *pos = f->pos; *have = f->have; // @cat=RICE
      return zz(v); } // @cat=RICE
}

/* n residuals continuing partition state r, into out[]. */
static void rice_block(flac_t *f, rice_t *r, int32_t *out, uint32_t n)
{
    while (n) { // @cat=RICE
        if (!r->left) { // @cat=RICE
            if (r->part >= r->parts) { *out++ = 0; n--; continue; } // @cat=RICE
            r->left  = r->per - (r->part == 0u ? r->order : 0u); // @cat=RICE
            r->param = bits(f, r->pbits); // @cat=RICE
            r->raw   = (r->param == r->escape) ? bits(f, 5) : 0u; // @cat=RICE
            r->part++; // @cat=RICE
            continue; // @cat=RICE
        }
        uint32_t m = n < r->left ? n : r->left; // @cat=RICE
        r->left -= m; n -= m; // @cat=RICE
        if (r->param == r->escape) { // @cat=RICE
            while (m--) *out++ = sbits(f, r->raw); // @cat=RICE
        } else {
            uint64_t acc = f->bitacc; uint32_t cnt = f->bitcnt, pos = f->pos, have = f->have, k = r->param; // @cat=RICE
            while (m--) *out++ = rice_one(f, &acc, &cnt, &pos, &have, k); // @cat=RICE
            f->bitacc = acc; f->bitcnt = cnt; f->pos = pos; // @cat=RICE
        }
    }
}

static inline __attribute__((always_inline)) int32_t rice_next_fast(flac_t *f, rice_t *r)
{
    if (!r->left || r->param == r->escape) return rice_next(f, r); // @cat=RICE
    r->left--; // @cat=RICE
    uint64_t acc = f->bitacc; uint32_t cnt = f->bitcnt, pos = f->pos, have = f->have; // @cat=RICE
    int32_t v = rice_one(f, &acc, &cnt, &pos, &have, r->param); // @cat=RICE
    f->bitacc = acc; f->bitcnt = cnt; f->pos = pos; // @cat=RICE
    return v; // @cat=RICE
}

static inline __attribute__((always_inline)) int16_t to16i(int32_t v, uint32_t bps)
{
    if (bps > 16u) v >>= (int)(bps - 16u); // @cat=EMIT
    else if (bps < 16u) v <<= (int)(16u - bps); // @cat=EMIT
    if (v >  32767) v =  32767; // @cat=EMIT
    if (v < -32768) v = -32768; // @cat=EMIT
    return (int16_t)v; // @cat=EMIT
}
static inline __attribute__((always_inline)) int16_t clamp16(int32_t v)
{
    if (v >  32767) v =  32767; // @cat=EMIT
    if (v < -32768) v = -32768; // @cat=EMIT
    return (int16_t)v; // @cat=EMIT
}

/* Hardware LPC put/get, inlined (candidate 3). Addresses come from the build, so flac.c keeps no
 * fixed map of its own; the real firmware would pass 0x80000134/138/13C. */
#ifndef LPC_MMIO_BASE
#define LPC_MMIO_BASE 0xF0000100u
#endif
#define LREG(o) (*(volatile uint32_t *)(LPC_MMIO_BASE + (o)))
extern int tau_lpc_hw_enable;
extern unsigned tau_lpc_stat_samples, tau_lpc_stat_timeout;
static inline __attribute__((always_inline)) void lpc_put(int32_t res) { LREG(0x14u) = (uint32_t)res; } // @cat=LPC
static inline __attribute__((always_inline)) int32_t lpc_get(int *ok)
{
    for (int t = 0; t < 4000; t++) { // @cat=LPC
        if (LREG(0x1Cu) & 4u) { // @cat=LPC
            int32_t s = (int32_t)LREG(0x18u); // @cat=LPC
            tau_lpc_stat_samples++; // @cat=LPC
            *ok = 1; return s; // @cat=LPC
        }
    }
    tau_lpc_stat_timeout++; tau_lpc_hw_enable = 0; *ok = 0; return 0; // @cat=LPC
}
/* ===== end candidate helpers ===== */
