/* fw2/cores/histring_core.h -- a ring of byte samples kept in WORD-addressed memory (the PSRAM window takes word accesses only; the history of a time-series meter lives there, off the scarce on-chip RAM). Pure: the
 * memory is a `volatile uint32_t` array the caller places (a pack's .pstate, Layered Wave's ring). Host-tested (sim/test_p2_meter_helpers.py); Layered Wave's six layers use it.
 *
 * `head` is the ring index of the NEWEST sample; sample `age` (0 = newest) is at (head + age) mod len. A push moves the head back one slot and stores ONE byte with one word read-modify-write (the old design shifted up
 * to 400 bytes a push, free in SRAM and ruinous through a ~32 cycle window). A frame unwraps what it draws into a hot scratch with word reads (hr_unwrap), after which the draw code sees a plain array. `len` is a
 * multiple of 4, so a word never straddles the wrap; it must also be larger than the most samples a frame reads, or old samples alias new ones. Several rings may share one head (every layer pushes together). */
#ifndef HISTRING_CORE_H
#define HISTRING_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define HR_WORDS(len) ((len) / 4u)
/* The head after a push (the new newest slot): one slot back, wrapping. Call once per push, then hr_store() the sample into every ring that shares the head. */
static inline uint32_t hr_back(uint32_t head, uint32_t len)
{
#if BUG == 1
    return head == 0u ? len : head - 1u;                                                  /* wraps to len (one past the end) */
#else
    return head == 0u ? len - 1u : head - 1u;
#endif
}
static inline void hr_store(volatile uint32_t *w, uint32_t head, uint32_t v)
{
    const uint32_t wi = head >> 2, sh = (head & 3u) * 8u;
#if BUG == 2
    w[wi] = w[wi] | ((v & 0xFFu) << sh);                                                    /* only sets bits: an old sample bleeds into the new one */
#else
    w[wi] = (w[wi] & ~(0xFFu << sh)) | ((v & 0xFFu) << sh);
#endif
}
/* The newest `n` samples (newest first) into dst: dst[j] = the sample pushed j pushes ago. One word read per four samples. */
static inline void hr_unwrap(const volatile uint32_t *w, uint32_t head, uint32_t len, uint8_t *dst, uint32_t n)
{
    uint32_t i = head, j = 0;
    while (j < n) {
        const uint32_t word = w[i >> 2];
        for (uint32_t b = i & 3u; b < 4u && j < n; b++) dst[j++] = (uint8_t)(word >> (b * 8u));
        i = (i & ~3u) + 4u;
        if (i >= len) i -= len;
    }
}
static inline uint32_t hr_get(const volatile uint32_t *w, uint32_t head, uint32_t len, uint32_t age)       /* one sample, by age */
{
    uint32_t i = head + age; if (i >= len) i -= len;
    return (w[i >> 2] >> ((i & 3u) * 8u)) & 0xFFu;
}
static inline void hr_clear(volatile uint32_t *w, uint32_t len) { for (uint32_t i = 0; i < len / 4u; i++) w[i] = 0u; }
#endif
