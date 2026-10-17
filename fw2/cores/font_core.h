/* fw2/cores/font_core.h -- loading a Tau font record (TFNT, docs/features/FONT_SPEC.md 3.2) into the drawing engine's writable font memory. Pure: every hardware access goes through the ops table, so the host test
 * (sim/test_p2_font.py) drives a mock engine and checks the ORDER: the record is validated first (a bad one touches nothing), the engine is fenced before the first word is written, every word goes in once
 * and in order, the load counter must have stepped by exactly one, and the advances replace the current table only after the memory holds the new atlas. */
#ifndef FONT_CORE_H
#define FONT_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define FT_GLYPHS 191u
#define FT_WORDS  (FT_GLYPHS * 32u)
#define FT_HDR    44u                                    /* magic .. ranges; the advance table follows */
typedef struct { const uint8_t *adv; const uint32_t *words; uint16_t weight; } ft_t;
enum { FTE_OK = 0, FTE_RECORD = 1, FTE_NO_UNIT = 2, FTE_LOAD = 3 };
typedef struct {
    void (*fence)(void *);                              /* every queued draw command has executed and the engine is idle */
    uint32_t (*rd_ctrl)(void *);                        /* FONT_CTRL: [31] present, [27:16] loads completed, [15:0] words */
    void (*wr_ctrl)(void *, uint32_t);                  /* FONT_CTRL: bit 0 restarts the write index */
    void (*wr_data)(void *, uint32_t);                  /* FONT_DATA: one word, the index steps on */
    void *ctx;
} ft_ops_t;
static inline uint32_t ft_ld16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static inline uint32_t ft_ld32(const uint8_t *p) { return ft_ld16(p) | (ft_ld16(p + 2) << 16); }
static inline uint32_t ft_crc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
/* 0 = a record this firmware can load. `rec` must be 4-byte aligned (the atlas is read as words) and `len` its exact size. */
static inline int ft_parse(const uint8_t *rec, uint32_t len, ft_t *o)
{
    const uint32_t adv_pad = (FT_GLYPHS + 3u) & ~3u;
    if (len != FT_HDR + adv_pad + FT_WORDS * 4u || ((uintptr_t)rec & 3u)) return 1;
    if (rec[0] != 'T' || rec[1] != 'F' || rec[2] != 'N' || rec[3] != 'T' || ft_ld16(rec + 4) != 1u) return 1;
#if BUG != 1
    if (ft_crc(rec + 12, len - 12u) != ft_ld32(rec + 8)) return 1;
#endif
    const uint8_t *b = rec + 12;
    if (b[18] != 16 || b[19] != 16 || b[20] != 2 || ft_ld16(b + 24) != 0x20 || ft_ld16(b + 26) != 0x7E || ft_ld16(b + 28) != 0xA0 || ft_ld16(b + 30) != 0xFF) return 1;
    o->weight = (uint16_t)ft_ld16(b + 16); o->adv = rec + FT_HDR; o->words = (const uint32_t *)(const void *)(rec + FT_HDR + adv_pad);
    return 0;
}
/* Loads `f` into the engine. On success `adv_out` (191 bytes) holds its advances. Returns FTE_*; `adv_out` is never touched on an error, and with no unit nothing is written. */
static inline int ft_load(const ft_t *f, const ft_ops_t *ops, uint8_t *adv_out)
{
    const uint32_t c0 = ops->rd_ctrl(ops->ctx);
    if (!(c0 >> 31) || (c0 & 0xFFFFu) != FT_WORDS) return FTE_NO_UNIT;
#if BUG != 2
    ops->fence(ops->ctx);
#endif
    ops->wr_ctrl(ops->ctx, 1u);
    for (uint32_t i = 0; i < FT_WORDS; i++) ops->wr_data(ops->ctx, f->words[i]);
    const uint32_t c1 = ops->rd_ctrl(ops->ctx);
#if BUG != 3
    if (((c1 >> 16) & 0xFFFu) != (((c0 >> 16) + 1u) & 0xFFFu)) return FTE_LOAD;
#else
    (void)c1;                                                       /* mutant: the counter is not checked */
#endif
    for (uint32_t i = 0; i < FT_GLYPHS; i++) adv_out[i] = f->adv[i];
    return FTE_OK;
}
#endif
