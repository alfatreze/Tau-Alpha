/* Player 2 minimal C support: the three string functions Helix (and the compiler) call, and the decoder's arena allocator. No libc is linked (spec section 13). */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *dp = d; const uint8_t *sp = s;
    if (((uintptr_t)dp | (uintptr_t)sp | n) % 4u == 0u) { uint32_t *dw = (uint32_t *)dp; const uint32_t *sw = (const uint32_t *)sp; for (size_t i = 0; i < n / 4u; i++) dw[i] = sw[i]; return d; }
    while (n--) *dp++ = *sp++;
    return d;
}
void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *dp = d; const uint8_t *sp = s;
    if (dp < sp || dp >= sp + n) { while (n--) *dp++ = *sp++; }
    else { dp += n; sp += n; while (n--) *--dp = *--sp; }
    return d;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
void *memset(void *d, int c, size_t n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }

/* A bump allocator with a live count: Helix allocates its eight blocks together and frees them together, so when the count returns to zero the whole arena is free again.
 * Measured peak 23,824 B (fw/alloc.c, hardware); 24,576 leaves 752. Nothing else in Player 2 allocates. */
#define ARENA_BYTES 24576u
static uint8_t arena[ARENA_BYTES] __attribute__((aligned(8)));
static uint32_t arena_next, arena_live;
uint32_t arena_peak;
void *malloc(size_t n)
{
    uint32_t sz = ((uint32_t)n + 7u) & ~7u; if (!sz) sz = 8u;
    if (arena_next + sz > ARENA_BYTES) return (void *)0;
    void *p = &arena[arena_next]; arena_next += sz; arena_live++;
    if (arena_next > arena_peak) arena_peak = arena_next;
    return p;
}
void free(void *p) { if (!p) return; if (arena_live && --arena_live == 0) arena_next = 0; }
