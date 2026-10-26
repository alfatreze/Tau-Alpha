#pragma GCC optimize ("Os")
#include "skiplist.h"
#include "slot.h"
#include "../platform/mmio.h"
#include "../cores/skiptab_core.h"
extern char _tag_start;
uint32_t skiplist_hits, skiplist_misses, skiplist_errors;
static uint32_t no_table_dir = 0;                         /* hash of a folder known to have no tau-skip.bin */

int skiplist_lookup(const char *path, uint32_t *lead, uint32_t *tail)
{
    char dir[200]; uint32_t last = 0, n = 0;
    for (; path[n] && n + 16u < sizeof dir; n++) if (path[n] == '/') last = n + 1u;
    if (!last) return 0;
    for (uint32_t i = 0; i < last; i++) dir[i] = path[i];
    dir[last] = 0;
    uint32_t dh = skt_fnv(dir) | 1u;
    if (dh == no_table_dir) { skiplist_misses++; return 0; }
    static const char name[] = "tau-skip.bin";
    for (uint32_t i = 0; i < sizeof name; i++) dir[last + i] = name[i];
    if (!slot_open_path(7u, dir)) { no_table_dir = dh; skiplist_misses++; return 0; }          /* no file: not an error */
    if (!slot_read_blocking(7u, 0u, (uint32_t)(uintptr_t)&_tag_start, 4096u, CLK_HZ * 2u)) { skiplist_errors++; no_table_dir = dh; return 0; }
    const uint8_t *b = (const uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_tag_start);
    int f = skt_lookup(b, 4096u, path + last, lead, tail);
    if (f) skiplist_hits++; else skiplist_misses++;
    return f;
}
