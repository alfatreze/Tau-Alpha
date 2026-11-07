/* Host build of fw2/stream/library.c (the real file) around a model of the card and PSRAM: what the player's text accessors answer with and without a
 * name sidecar. The model: slot 5 is the library index, slot 7 is whatever slot_open_path opened last; reads past the end of a file fail (as on the card).
 *   p2_library_names_harness LIBRARY.tdb [NAMES.bin|-] 
 * Output: "state LIB_STATE LIB_ERR NAMES_STATE NAMES_ERR"; "opens N"; "M id TITLEHEX ARTISTHEX ALBUMHEX" per track (library_meta);
 * "B kind pos TEXTHEX" for every row of the artist, album and all-track lists (library_br_text). */
#define P2_HOST 1
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t cyc;
static uint32_t reg_rd(uint32_t a) { if (a == 0x8000000Cu) return cyc += 1000u; if (a == 0x80000088u) return 0x50535231u; if (a == 0x800000A0u) return 1u << 16; return 0; }
static void reg_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
#define REG_RD(a) reg_rd(a)
#define REG_WR(a, v) reg_wr((a), (v))
static uint8_t libmem[0x400000] __attribute__((aligned(16)));
static uint8_t namesmem[0x100000] __attribute__((aligned(16)));
static uint16_t queuemem[16384];
static uint8_t tagwin[4096] __attribute__((aligned(16)));
#define LIB_IMG ((volatile uint8_t *)libmem)
#define NAMES_IMG ((volatile uint8_t *)namesmem)
#define LIB_Q queuemem
#define LIB_TAGWIN tagwin
#include "../fw2/platform/mmio.h"
#include "../fw2/stream/slot.h"

static uint8_t *file[16]; static uint32_t fsz[16]; static int opened7 = 0, opens = 0; static char root_expect[256];
static uint8_t *slurp(const char *p, uint32_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); *n = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n + 1); if (fread(b, 1, *n, f) != *n) return 0; fclose(f); return b;
}
int slot_read_blocking(uint32_t slot, uint32_t off, uint32_t dst_off, uint32_t len, uint32_t to)
{
    (void)dst_off; (void)to;
    if (slot == 7u && !opened7) return 0;
    if (!file[slot] || (uint64_t)off + len > fsz[slot]) return 0;
    memcpy(tagwin, file[slot] + off, len);
    return 1;
}
int slot_open_path(uint32_t slot, const char *path)
{
    opens++;
    if (slot != 7u || !file[7]) return 0;
    if (strcmp(path, root_expect) != 0) { printf("badpath %s\n", path); return 0; }
    opened7 = 1; return 1;
}
uint8_t slot_err;
#include "../fw2/stream/library.c"

static void hex(const char *s) { if (!*s) { printf("-"); return; } for (; *s; s++) printf("%02x", (unsigned char)*s); }
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    file[5] = slurp(argv[1], &fsz[5]);
    if (argc > 2 && strcmp(argv[2], "-")) file[7] = slurp(argv[2], &fsz[7]);
    snprintf(root_expect, sizeof root_expect, "/Assets/tau/common/tau-names.bin");
    library_load();
    printf("state %u %u %u %u\nopens %d\n", lib_state, lib_err, names_state, names_err, opens);
    if (lib_state != 1) return 0;
    for (uint32_t i = 0; i < lib_n_tracks; i++) {
        char t[128], a[128], b[128];
        library_meta(i, t, sizeof t, a, sizeof a, b, sizeof b);
        printf("M %u ", i); hex(t); printf(" "); hex(a); printf(" "); hex(b); printf("\n");
    }
    uint8_t ks[3] = { 1, 2, 5 };                                        /* BK_ARTISTS, BK_ALBUMS, BK_TRACKS (cores/browse_core.h) */
    for (int k = 0; k < 3; k++) {
        uint32_t n = library_br_count(ks[k], 0);
        for (uint32_t p = 0; p < n; p++) { char o[128]; library_br_text(ks[k], 0, p, o, sizeof o); printf("B %u %u ", ks[k], p); hex(o); printf("\n"); }
    }
    return 0;
}
