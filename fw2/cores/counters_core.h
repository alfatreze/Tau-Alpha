/* fw2/cores/counters_core.h -- Player 2 counter registry (docs/features/PLAYER_V2_SPEC.md principle P9 and section 12.2). Pure logic, host-tested (sim/test_p2_counters.py).
 *
 * A counter is registered ONCE (name, kind, unit, address of the value). The Info page, the report export and the stress logs are all generated from the table, so adding
 * a counter adds it everywhere and no row index, label array or row count is maintained by hand (the cause of the 2026-10-15 heap-floor miss and of the truncated RING row).
 * The table is cold data with fixed-width names (the labels cost no hot RAM). One counter is one row; a quantity with several fields is several counters. */
#ifndef COUNTERS_CORE_H
#define COUNTERS_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif

enum { CK_U8 = 1, CK_U16, CK_U32, CK_I32, CK_STR };      /* CK_STR: src is a NUL-terminated text of at most 39 characters (version stamp, track description) */
enum { CU_NONE = 0, CU_PAIRS, CU_MS, CU_PCT, CU_CYC, CU_BYTES, CU_PERMILLE };

#define CNT_NAME_MAX 16u                 /* including the terminator */
#define CNT_VALUE_MAX 40u                /* digits and unit, no terminator */

typedef struct {
    char     name[CNT_NAME_MAX];
    uint8_t  kind, unit;
    const volatile void *src;
} cnt_t;

static inline int32_t cnt_read(const cnt_t *c)
{
    switch (c->kind) {
    case CK_U8:  return (int32_t)*(const volatile uint8_t *)c->src;
#if BUG == 4
    case CK_U16: return (int32_t)*(const volatile uint32_t *)c->src;      /* reads the neighbour's bytes too */
#else
    case CK_U16: return (int32_t)*(const volatile uint16_t *)c->src;
#endif
    default:     return *(const volatile int32_t *)c->src;
    }
}

static inline char *cnt_dec(char *o, int32_t v, int is_signed)
{
    char tmp[12]; int n = 0; uint32_t u;
    if (is_signed && v < 0) { *o++ = '-'; u = (uint32_t)(-(int64_t)v); } else u = (uint32_t)v;
#if BUG == 5
    if (!is_signed && v < 0) { *o++ = '-'; u = (uint32_t)(-(int64_t)v); }  /* an unsigned counter above 2^31 prints negative */
#endif
    do { tmp[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) *o++ = tmp[--n];
    return o;
}

static inline const char *cnt_unit_str(uint8_t u)
{
    switch (u) { case CU_PAIRS: return " PAIRS"; case CU_MS: return " MS"; case CU_PCT: return "%"; case CU_CYC: return " CYC"; case CU_BYTES: return " B"; case CU_PERMILLE: return " PERMILLE"; default: return ""; }
}

/* The value text of one counter: digits and unit, no terminator. Returns the end. */
static inline char *cnt_fmt(const cnt_t *c, char *o)
{
    if (c->kind == CK_STR) {
        const volatile char *s = (const volatile char *)c->src;
        for (uint32_t i = 0; s[i] && i < CNT_VALUE_MAX - 1u; i++) *o++ = s[i];
        return o;
    }
    o = cnt_dec(o, cnt_read(c), c->kind == CK_I32);
    for (const char *u = cnt_unit_str(c->unit); *u; u++) *o++ = *u;
    return o;
}

typedef void (*cnt_emit_fn)(void *ctx, const uint8_t *entry, uint32_t len);

/* One report entry per counter: row(u8), name, 0, value text. Returns the number of entries emitted. Nothing is cut (the longest entry is 1 + 15 + 1 + 40). */
static inline uint32_t cnt_export(const cnt_t *t, uint32_t n, cnt_emit_fn emit, void *ctx)
{
    uint32_t emitted = 0;
#if BUG == 1
    if (n) n--;                                                            /* the last counter is never exported */
#endif
    for (uint32_t i = 0; i < n; i++) {
        uint8_t e[1 + CNT_NAME_MAX + CNT_VALUE_MAX]; uint32_t k = 0;
        char v[CNT_VALUE_MAX + 1]; char *end = cnt_fmt(&t[i], v); uint32_t vl = (uint32_t)(end - v);
#if BUG == 2
        if (vl > 28u) vl = 28u;                                            /* the old export cut every value at 28 characters */
#endif
        e[k++] = (uint8_t)i;
        for (const char *p = t[i].name; *p && k < 1 + CNT_NAME_MAX - 1; p++) e[k++] = (uint8_t)*p;
        e[k++] = 0;
        for (uint32_t j = 0; j < vl; j++) e[k++] = (uint8_t)v[j];
        emit(ctx, e, k); emitted++;
    }
    return emitted;
}

/* Find a counter by name (the stress logs pick entries by name). -1 when absent. */
static inline int cnt_find(const cnt_t *t, uint32_t n, const char *name)
{
    for (uint32_t i = 0; i < n; i++) {
        const char *a = t[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
#if BUG == 3
        if (!*b) return (int)i;                                            /* a prefix matches */
#else
        if (!*a && !*b) return (int)i;
#endif
    }
    return -1;
}
#endif
