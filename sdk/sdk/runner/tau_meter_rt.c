/* tau_meter_rt.c: the little a freestanding meter needs from a C library, so no libgcc or compiler-rt is required. MIT licence.
 * Every function is weak: a meter that defines its own (the shipped meters do) wins without a link error. The compiler may also emit calls to memcpy and memset on its own. */
#include <stdint.h>
#include <stddef.h>
#define W __attribute__((weak))
W void *memset(void *d, int c, size_t n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
W void *memcpy(void *d, const void *s, size_t n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }
W void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s;
    if (p < q) while (n--) *p++ = *q++; else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}
W int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *p = (const uint8_t *)a, *q = (const uint8_t *)b;
    for (; n; n--, p++, q++) if (*p != *q) return *p < *q ? -1 : 1;
    return 0;
}
/* 64-bit shifts by a variable amount (the compiler calls these at -Oz); built from 32-bit halves so they do not call themselves. */
W uint64_t __ashldi3(uint64_t v, int n) { uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32); n &= 63; if (!n) return v; if (n >= 32) { hi = lo << (n - 32); lo = 0; } else { hi = (hi << n) | (lo >> (32 - n)); lo <<= n; } return ((uint64_t)hi << 32) | lo; }
W uint64_t __lshrdi3(uint64_t v, int n) { uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32); n &= 63; if (!n) return v; if (n >= 32) { lo = hi >> (n - 32); hi = 0; } else { lo = (lo >> n) | (hi << (32 - n)); hi >>= n; } return ((uint64_t)hi << 32) | lo; }
W int64_t __ashrdi3(int64_t v, int n) { uint32_t lo = (uint32_t)v; int32_t hi = (int32_t)(v >> 32); n &= 63; if (!n) return v; if (n >= 32) { lo = (uint32_t)(hi >> (n - 32)); hi >>= 31; } else { lo = (lo >> n) | ((uint32_t)hi << (32 - n)); hi >>= n; } return (int64_t)(((uint64_t)(uint32_t)hi << 32) | lo); }
/* 64-bit division: a shift and subtract loop (a meter that needs it is slow: prefer 32-bit math). */
static uint64_t udivmod64(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) { r = (r << 1) | ((n >> i) & 1u); if (r >= d) { r -= d; q |= (uint64_t)1 << i; } }
    if (rem) *rem = r;
    return q;
}
W uint64_t __udivdi3(uint64_t n, uint64_t d) { return udivmod64(n, d, 0); }
W uint64_t __umoddi3(uint64_t n, uint64_t d) { uint64_t r; udivmod64(n, d, &r); return r; }
W int64_t __divdi3(int64_t n, int64_t d) { const int neg = (n < 0) != (d < 0); uint64_t q = udivmod64(n < 0 ? -(uint64_t)n : (uint64_t)n, d < 0 ? -(uint64_t)d : (uint64_t)d, 0); return neg ? -(int64_t)q : (int64_t)q; }
W int64_t __moddi3(int64_t n, int64_t d) { uint64_t r; udivmod64(n < 0 ? -(uint64_t)n : (uint64_t)n, d < 0 ? -(uint64_t)d : (uint64_t)d, &r); return n < 0 ? -(int64_t)r : (int64_t)r; }
