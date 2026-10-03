/* ===== candidate subframe()/subframe_stream() ===== */
#include "flac_lpc_hw.h"

static flac_err subframe(flac_t *f, int32_t *out, uint32_t bps)
{
    (void)bits(f, 1);
    uint32_t type  = bits(f, 6);
    uint32_t wasted = 0;
    if (bits(f, 1)) wasted = unary(f) + 1u;
    bps -= wasted;
    uint32_t n = f->blocksize;

    if (type == 0u) {
        int32_t v = sbits(f, bps);
        for (uint32_t i = 0; i < n; i++) out[i] = v;
    } else if (type == 1u) {
        for (uint32_t i = 0; i < n; i++) out[i] = sbits(f, bps);
    } else if (type >= 8u && type <= 12u) {
        uint32_t order = type - 8u;
        for (uint32_t i = 0; i < order; i++) out[i] = sbits(f, bps);
        { flac_err e = residual(f, order, out + order); if (e) return e; }
        const int8_t *c = fixed_coef[order];
        for (uint32_t i = order; i < n; i++) { // @cat=LPC
            int32_t p = 0; // @cat=LPC
            for (uint32_t j = 0; j < order; j++) p += c[j] * out[i - 1u - j]; // @cat=LPC
            out[i] += p; // @cat=LPC
        }
    } else if (type >= 32u) {
        uint32_t order = type - 31u;
        for (uint32_t i = 0; i < order; i++) out[i] = sbits(f, bps);
        uint32_t prec  = bits(f, 4) + 1u;
        int32_t  shift = sbits(f, 5);
        if (prec == 16u || shift < 0) return FLAC_ERR_DATA;
        int32_t coef[FLAC_MAX_ORDER];
        for (uint32_t i = 0; i < order; i++) coef[i] = sbits(f, prec);
        uint32_t i = order;
#define SWPRED(I) ({ int64_t p_ = 0; for (uint32_t j_ = 0; j_ < order; j_++) p_ += (int64_t)coef[j_] * out[(I) - 1u - j_]; (int32_t)(p_ >> shift); })
#define NEXT0() (OPT1 ? rice_next_fast(f, &r) : rice_next(f, &r))
        if (TAU_LPC_FW && OPT3 && tau_lpc_hw_begin(order, shift, coef, out)) {
            rice_t r; flac_err e = rice_init(f, &r, order); if (e) return e;
            if (i < n) {
                int32_t infl = NEXT0(); // @cat=RICE
                lpc_put(infl); // @cat=LPC
                int32_t nres = (i + 1u < n) ? NEXT0() : 0; // @cat=RICE
                int failed = 0;
                while (i < n) { // @cat=LPC
                    int ok; int32_t s = lpc_get(&ok); // @cat=LPC
                    if (!ok) { failed = 1; break; }
                    int more = (i + 1u < n); // @cat=LPC
                    if (more) { lpc_put(nres); infl = nres; } // @cat=LPC
                    out[i++] = s; // @cat=LPC
                    if (more && i + 1u < n) nres = NEXT0(); // @cat=RICE
                }
                if (failed) {
                    out[i] = infl + SWPRED(i); i++;
                    if (i < n) { out[i] = nres + SWPRED(i); i++; }
                    for (; i < n; i++) out[i] = NEXT0() + SWPRED(i);
                }
            }
            if (f->eof) return FLAC_ERR_SHORT;
        } else {
            { flac_err e = residual(f, order, out + order); if (e) return e; }
            if (TAU_LPC_FW && !OPT3 && tau_lpc_hw_begin(order, shift, coef, out)) {
                for (; i < n; i++) { // @cat=LPC
                    int ok; // @cat=LPC
                    int32_t s = tau_lpc_hw_sample(out[i], &ok); // @cat=LPC
                    if (!ok) break; // @cat=LPC
                    out[i] = s; // @cat=LPC
                }
            }
            for (; i < n; i++) { // @cat=LPC
                int64_t p = 0; // @cat=LPC
                for (uint32_t j = 0; j < order; j++) // @cat=LPC
                    p += (int64_t)coef[j] * out[i - 1u - j]; // @cat=LPC
                out[i] += (int32_t)(p >> shift); // @cat=LPC
            }
        }
#undef SWPRED
#undef NEXT0
    } else {
        return FLAC_ERR_DATA;
    }
    if (wasted) for (uint32_t i = 0; i < n; i++) out[i] <<= wasted;
    return FLAC_OK;
}

static int16_t to16(int32_t v, uint32_t bps)
{
    if (bps > 16u) v >>= (int)(bps - 16u);
    else if (bps < 16u) v <<= (int)(16u - bps);
    if (v >  32767) v =  32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}

static flac_err subframe_stream(flac_t *f, uint32_t bps, uint32_t out_bps,
                                uint8_t m, flac_sink_fn sink, void *sctx)
{
    int32_t *buf = f->ch0;
    uint32_t n = f->blocksize;
    (void)bits(f, 1);
    uint32_t type = bits(f, 6);
    uint32_t wasted = 0;
    if (bits(f, 1)) wasted = unary(f) + 1u;
    bps -= wasted;
    int16_t pcm[128];
    uint32_t k = 0, i = 0;

    /* generic EMIT: identical to the shipped macro */
#define EMIT(S) do { int32_t a_ = buf[i], s_ = (S), l_, r_; \
    if      (m == 8u)  { l_ = a_; r_ = a_ - s_; } \
    else if (m == 9u)  { r_ = s_; l_ = s_ + a_; } \
    else if (m == 10u) { int32_t mid_ = (a_ << 1) | (s_ & 1); l_ = (mid_ + s_) >> 1; r_ = (mid_ - s_) >> 1; } \
    else               { l_ = a_; r_ = s_; } \
    pcm[k * 2] = to16(l_, out_bps); pcm[k * 2 + 1] = to16(r_, out_bps); \
    buf[i] = s_; if (++k == 64u) { sink(sctx, pcm, k); k = 0; } i++; } while (0)
    /* candidate 2: mode fixed at compile time, conversion inlined (O16: output known to be 16-bit) */
#define EMIT_MX(S, MODE, O16) do { int32_t a_ = buf[i], s_ = (S), l_, r_; \
    if      ((MODE) == 8)  { l_ = a_; r_ = a_ - s_; } \
    else if ((MODE) == 9)  { r_ = s_; l_ = s_ + a_; } \
    else if ((MODE) == 10) { int32_t mid_ = (a_ << 1) | (s_ & 1); l_ = (mid_ + s_) >> 1; r_ = (mid_ - s_) >> 1; } \
    else                   { l_ = a_; r_ = s_; } \
    if (O16) { pcm[k * 2] = clamp16(l_); pcm[k * 2 + 1] = clamp16(r_); } \
    else     { pcm[k * 2] = to16i(l_, out_bps); pcm[k * 2 + 1] = to16i(r_, out_bps); } \
    buf[i] = s_; if (++k == 64u) { sink(sctx, pcm, k); k = 0; } i++; } while (0)
#define EMIT_8(S)   EMIT_MX(S, 8, 0)
#define EMIT_9(S)   EMIT_MX(S, 9, 0)
#define EMIT_10(S)  EMIT_MX(S, 10, 0)
#define EMIT_0(S)   EMIT_MX(S, 0, 0)
#define EMIT_8h(S)  EMIT_MX(S, 8, 1)
#define EMIT_9h(S)  EMIT_MX(S, 9, 1)
#define EMIT_10h(S) EMIT_MX(S, 10, 1)
#define EMIT_0h(S)  EMIT_MX(S, 0, 1)

    if (type == 0u) {
        int32_t v = sbits(f, bps) << wasted;
        while (i < n) EMIT(v);
    } else if (type == 1u) {
        while (i < n) EMIT(sbits(f, bps) << wasted);
    } else if ((type >= 8u && type <= 12u) || type >= 32u) {
        int lpc = type >= 32u;
        uint32_t order = lpc ? type - 31u : type - 8u;
        int32_t warm[FLAC_MAX_ORDER], coef[FLAC_MAX_ORDER];
        int32_t shift = 0;
        for (uint32_t j = 0; j < order; j++) warm[j] = sbits(f, bps);
        if (lpc) {
            uint32_t prec = bits(f, 4) + 1u;
            shift = sbits(f, 5);
            if (prec == 16u || shift < 0) return FLAC_ERR_DATA;
            for (uint32_t j = 0; j < order; j++) coef[j] = sbits(f, prec);
        }
        rice_t r; flac_err e = rice_init(f, &r, order);
        if (e) return e;
        for (uint32_t j = 0; j < order; j++) EMIT(warm[j] << wasted);
        int32_t rb[64]; uint32_t ri = 0, rn = 0, rleft = n - order;
        (void)rb; (void)ri; (void)rn; (void)rleft;
#define NEXT_RES() (OPT1 ? (ri == rn ? (rn = rleft < 64u ? rleft : 64u, rleft -= rn, rice_block(f, &r, rb, rn), ri = 1u, rb[0]) : rb[ri++]) : rice_next(f, &r))
#define SWP(I) ({ int64_t p_ = 0; for (uint32_t j_ = 0; j_ < order; j_++) p_ += (int64_t)coef[j_] * (buf[(I) - 1u - j_] >> wasted); (int32_t)(p_ >> shift); })
#define STREAM_LOOPS(EM) do { \
        if (!lpc) { \
            const int8_t *c = fixed_coef[order]; \
            while (i < n) { \
                int32_t p = 0; \
                for (uint32_t j = 0; j < order; j++) p += c[j] * (buf[i - 1u - j] >> wasted); \
                int32_t res_ = NEXT_RES(); \
                EM((res_ + p) << wasted); \
            } \
        } else { \
            if (TAU_LPC_FW && OPT3 && i < n && tau_lpc_hw_begin(order, shift, coef, warm)) { \
                int32_t infl = NEXT_RES(); lpc_put(infl); \
                int32_t nres = (i + 1u < n) ? NEXT_RES() : 0; \
                int failed = 0; \
                while (i < n) { \
                    int ok; int32_t s = lpc_get(&ok); \
                    if (!ok) { failed = 1; break; } \
                    int more = (i + 1u < n); \
                    if (more) { lpc_put(nres); infl = nres; } \
                    EM(s << wasted); \
                    if (more && i + 1u < n) nres = NEXT_RES(); \
                } \
                if (failed) { \
                    EM((infl + SWP(i)) << wasted); \
                    if (i < n) EM((nres + SWP(i)) << wasted); \
                } \
            } else if (TAU_LPC_FW && !OPT3 && tau_lpc_hw_begin(order, shift, coef, warm)) { \
                while (i < n) { \
                    int32_t res = NEXT_RES(); \
                    int ok; int32_t s = tau_lpc_hw_sample(res, &ok); \
                    if (!ok) { EM((res + SWP(i)) << wasted); break; } \
                    EM(s << wasted); \
                } \
            } \
            while (i < n) { \
                int64_t p = 0; \
                for (uint32_t j = 0; j < order; j++) p += (int64_t)coef[j] * (buf[i - 1u - j] >> wasted); \
                int32_t res_ = NEXT_RES(); \
                EM((res_ + (int32_t)(p >> shift)) << wasted); \
            } \
        } } while (0)
        if (OPT2 >= 2 && out_bps == 16u) {
            switch (m) { case 8: STREAM_LOOPS(EMIT_8h); break; case 9: STREAM_LOOPS(EMIT_9h); break;
                         case 10: STREAM_LOOPS(EMIT_10h); break; default: STREAM_LOOPS(EMIT_0h); break; }
        } else if (OPT2 >= 1) {
            switch (m) { case 8: STREAM_LOOPS(EMIT_8); break; case 9: STREAM_LOOPS(EMIT_9); break;
                         case 10: STREAM_LOOPS(EMIT_10); break; default: STREAM_LOOPS(EMIT_0); break; }
        } else {
            STREAM_LOOPS(EMIT);
        }
#undef STREAM_LOOPS
#undef NEXT_RES
#undef SWP
    } else {
        return FLAC_ERR_DATA;
    }
#undef EMIT
    if (k) sink(sctx, pcm, k);
    return f->eof ? FLAC_ERR_SHORT : FLAC_OK;
}
/* ===== end candidate subframe ===== */
