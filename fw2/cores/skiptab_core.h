/* fw2/cores/skiptab_core.h -- the skip-silence table (spec 10.1). Tau Omega detects the long silences at the start and end of tracks offline and writes ONE small file per album,
 * `tau-skip.bin`, next to the tracks; the player only looks the numbers up. Pure and host-tested (sim/test_p2_skiptab.py); the format below is the contract with Omega.
 *
 *   0  "TSK1"            magic (the 1 is the version)
 *   4  u16 count         number of entries, at most 340 (the whole file fits one 4 KB read)
 *   6  u16 0             reserved
 *   8  u32 crc32         CRC-32 (zlib: polynomial 0xEDB88320, init and final xor 0xFFFFFFFF) of the entries
 *   12 entries[count]    { u32 name_hash, u32 lead_pairs, u32 tail_pairs }, little endian
 *
 * name_hash is FNV-1a (32 bit) of the track's FILE NAME as the library holds it (no folder), lead/tail are source sample pairs to skip at the start and at the end. A missing file, a
 * bad magic or CRC, or no entry for the track means: play the whole track. */
#ifndef SKIPTAB_CORE_H
#define SKIPTAB_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define SKT_MAX_ENTRIES 340u
#define SKT_HDR 12u
static inline uint32_t skt_fnv(const char *name)
{
    uint32_t h = 2166136261u;
    for (; *name; name++) { h ^= (uint8_t)*name;
#if BUG == 1
        h *= 16777619u; h &= 0x00FFFFFFu;                       /* truncated hash */
#else
        h *= 16777619u;
#endif
    }
    return h;
}
static inline uint32_t skt_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static inline uint32_t skt_le32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
/* buf: the first `len` bytes of the file. Returns 1 and fills lead/tail when the table is valid and holds the name; 0 otherwise (play the whole track). */
static inline int skt_lookup(const uint8_t *buf, uint32_t len, const char *name, uint32_t *lead, uint32_t *tail)
{
    if (len < SKT_HDR || buf[0] != 'T' || buf[1] != 'S' || buf[2] != 'K' || buf[3] != '1') return 0;
    uint32_t n = buf[4] | ((uint32_t)buf[5] << 8);
    if (n > SKT_MAX_ENTRIES || SKT_HDR + 12u * n > len) return 0;
#if BUG != 2
    if (skt_crc32(buf + SKT_HDR, 12u * n) != skt_le32(buf + 8)) return 0;
#endif
    uint32_t h = skt_fnv(name);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = buf + SKT_HDR + 12u * i;
#if BUG == 3
        if (i + 1u < n && skt_le32(e) == h) { *lead = skt_le32(e + 4); *tail = skt_le32(e + 8); return 1; }       /* the last entry is never found */
#else
        if (skt_le32(e) == h) { *lead = skt_le32(e + 4); *tail = skt_le32(e + 8); return 1; }
#endif
    }
    return 0;
}
#endif
