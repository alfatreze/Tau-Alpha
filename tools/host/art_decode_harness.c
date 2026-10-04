/* B-567: runs the REAL fw/art.inc (cover finder, picojpeg decode, box-filter scaler) under tools/rv32sim.py on one file
 * delivered as the data blob: an MP3 (starts with "ID3": the whole tag is the APIC search window) or a FLAC (starts with
 * "fLaC": PICTURE block). Everything the firmware provides around it is a stub: the tag buffer, the file reads, and
 * fb_rect() which writes the 128x128 stash into a plain array.
 *
 * Prints:  rc=<art_decode result> fail=<art_fail_code> hash=<FNV of the stash>   and  sig=<art_sig_of result>
 * Built by sim/test_art_decode.py; the same ELF is used for the access counts in docs/features/RAM_BSS_AUDIT.md. */
#include "hostio.h"

#define TAU_ART_PSRAM 0
#define ART_PSRAM
#define COLD_FN2
#define ART_IMG   128u
#define ART_PAD   0u
#define ART_STASH_Y 0u
#define TAG_SIZE  4096u
#define TAG_OFF   0u
#define MP3_SLOT_ID 0u
#define FMT_FLAC  2
#define FMT_MP3   1

static int track_fmt;
static uint8_t tagbuf[TAG_SIZE];
static uint16_t stash[ART_IMG][ART_IMG];

static int target_read_slot(uint32_t slot, uint32_t off, uint32_t dst_off, uint32_t len)
{
    (void)slot; (void)dst_off;
    if (len > TAG_SIZE || off + len > MMIO_SIZE) return 0;
    return hread(off, tagbuf, len) == len;
}

static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++)
            if (y + j < ART_IMG && x + i < ART_IMG) stash[y + j][x + i] = c;
}

/* picojpeg is included into this unit (not linked) so that its static work buffers can be poisoned below. */
#include "../../third_party/picojpeg/picojpeg.c"
#include "../../fw/art.inc"

/* POISON=1: fill every buffer that is a candidate for living in uninitialised PSRAM (docs/features/RAM_BSS_AUDIT.md)
 * with junk before each decode. The result must not change: it proves nothing relies on those buffers being zero. */
#ifdef POISON
static void poison(void)
{
    memset(art_rowcnt, 0xA5, sizeof art_rowcnt);
    memset(art_colcnt, 0xA5, sizeof art_colcnt);
    memset(art_line,   0xA5, sizeof art_line);
    memset(art_xmap,   0xA5, sizeof art_xmap);
    memset(art_yslot,  0xA5, sizeof art_yslot);
    memset(gInBuf,     0xA5, sizeof gInBuf);
    memset(gHuffVal0,  0xA5, sizeof gHuffVal0); memset(gHuffVal1, 0xA5, sizeof gHuffVal1);
    memset(gHuffVal2,  0xA5, sizeof gHuffVal2); memset(gHuffVal3, 0xA5, sizeof gHuffVal3);
    memset(&gHuffTab0, 0xA5, sizeof gHuffTab0); memset(&gHuffTab1, 0xA5, sizeof gHuffTab1);
    memset(&gHuffTab2, 0xA5, sizeof gHuffTab2); memset(&gHuffTab3, 0xA5, sizeof gHuffTab3);
    memset(gQuant0,    0xA5, sizeof gQuant0);   memset(gQuant1,   0xA5, sizeof gQuant1);
}
#else
static void poison(void) {}
#endif

/* The addresses the access-count script needs (printed so it does not have to parse the ELF). */
int main(void)
{
    uint8_t first[4];
    hread(0, first, 4);
    track_fmt = (first[0] == 'f') ? FMT_FLAC : FMT_MP3;
    uint32_t tag_len = 0;
    if (track_fmt == FMT_MP3) {
        uint8_t h[10];
        hread(0, h, 10);
        tag_len = 10u + (((uint32_t)h[6] << 21) | ((uint32_t)h[7] << 14) | ((uint32_t)h[8] << 7) | h[9]);
    }
    uint32_t sig = art_sig_of(tag_len);
    poison();
    int rc = art_decode(tag_len);
    uint32_t hsh = 2166136261u;
    for (uint32_t y = 0; y < ART_IMG; y++)
        for (uint32_t x = 0; x < ART_IMG; x++) { hsh ^= stash[y][x]; hsh *= 16777619u; }
    hputs("rc="); hputu((uint32_t)rc);
    hputs(" fail="); hputu(art_fail_code);
    hputs(" hash="); hputx(hsh);
    hputs(" sig="); hputx(sig);
    hputc('\n');
    return 0;
}
