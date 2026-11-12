#pragma GCC optimize ("Os")
#include "packfile.h"
#include "slot.h"
#include "../platform/mmio.h"
#define PACKS_SLOT 9u                         /* the core's own tau-packs.bin (a v2 bin, shipped with the core) */
#define METERS_SLOT 10u                       /* the user's tau-meters.bin (Assets/tau/common, not core-specific, deferload, optional): the caller passes ctx 1 for it */
#define PACKFILE_CHUNK 1024u                 /* the slot driver reads into the tag zone (4 KB); a pack image is copied out of it a kilobyte at a time */
extern char _tag_start;

int packfile_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len)
{
    const uint32_t slot = ctx == (void *)1 ? METERS_SLOT : PACKS_SLOT;
    const volatile uint8_t *win = (const volatile uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_tag_start);     /* the tag zone through the uncached alias */
    while (len) {
        const uint32_t n = len < PACKFILE_CHUNK ? len : PACKFILE_CHUNK;
        if (!slot_read_blocking(slot, off, (uint32_t)(uintptr_t)&_tag_start, n, CLK_HZ * 3u)) return 0;
        for (uint32_t i = 0; i < n; i++) dst[i] = win[i];
        off += n; dst += n; len -= n;
    }
    return 1;
}
