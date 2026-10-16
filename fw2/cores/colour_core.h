/* fw2/cores/colour_core.h -- the colour picker of the Configure page (no drawing, no registers; host-tested by sim/test_p2_colour.py). A colour parameter is a raw RGB565 value. The picker edits three channels
 * (R 0..31, G 0..63, B 0..31): Up/Down choose the channel (wrapping), Left/Right change it by one and clamp (a held key repeats), A accepts the colour, B leaves it as it was. */
#ifndef COLOUR_CORE_H
#define COLOUR_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint16_t orig, v; uint8_t ch; } cp_t;
enum { CPE_NONE = 0, CPE_CHANGED, CPE_ACCEPT, CPE_CANCEL };
#define CP_UP 1u
#define CP_DOWN 2u
#define CP_LEFT 4u
#define CP_RIGHT 8u
#define CP_A 16u
#define CP_B 32u
static inline uint32_t cp_get(uint16_t v, uint32_t ch) { return ch == 0u ? (v >> 11) & 31u : ch == 1u ? (v >> 5) & 63u : v & 31u; }
static inline uint32_t cp_max(uint32_t ch) { return ch == 1u ? 63u : 31u; }
static inline uint16_t cp_set(uint16_t v, uint32_t ch, uint32_t x)
{
    if (x > cp_max(ch)) x = cp_max(ch);
    if (ch == 0u) return (uint16_t)((v & 0x07FFu) | (x << 11));
    if (ch == 1u) return (uint16_t)((v & 0xF81Fu) | (x << 5));
    return (uint16_t)((v & 0xFFE0u) | x);
}
static inline void cp_open(cp_t *p, uint16_t v) { p->orig = v; p->v = v; p->ch = 0; }
static inline int cp_input(cp_t *p, uint32_t keys)
{
    if (keys & CP_B) { p->v = p->orig; return CPE_CANCEL; }
    if (keys & CP_A) return CPE_ACCEPT;
    if (keys & (CP_UP | CP_DOWN)) { p->ch = (uint8_t)((keys & CP_DOWN) ? (p->ch + 1u) % 3u : (p->ch + 2u) % 3u); return CPE_NONE; }
    if (keys & (CP_LEFT | CP_RIGHT)) {
        uint32_t x = cp_get(p->v, p->ch);
#if BUG == 1
        if (keys & CP_RIGHT) x = x >= cp_max(p->ch) ? 0u : x + 1u; else x = x ? x - 1u : cp_max(p->ch);        /* mutant: wraps instead of clamping */
#else
        if (keys & CP_RIGHT) { if (x < cp_max(p->ch)) x++; } else if (x) x--;
#endif
        const uint16_t nv = cp_set(p->v, p->ch, x);
        if (nv == p->v) return CPE_NONE;
        p->v = nv; return CPE_CHANGED;
    }
    return CPE_NONE;
}
#endif
