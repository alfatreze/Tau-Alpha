/* Host check of fw/flac.c's Vorbis-comment ReplayGain capture (B-599): feeds a hand-built FLAC header (STREAMINFO + VORBIS_COMMENT) from stdin to the REAL flac_open()
 * and prints what it kept: "have track_text album_text". Built by sim/test_replaygain.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flac.h"
static unsigned char blob[4096]; static int blen, bpos;
static int rd(void *ctx, uint8_t *dst, int n) { (void)ctx; int k = 0; while (k < n && bpos < blen) dst[k++] = blob[bpos++]; return k; }
static int sk(void *ctx, uint32_t n) { (void)ctx; bpos += (int)n; return 1; }
int main(void)
{
    blen = (int)fread(blob, 1, sizeof blob, stdin);
    static char ti[64], ar[64], al[64], yr[8], tk[8];
    static flac_t f; memset(&f, 0, sizeof f);
    f.tag_title = ti; f.tag_artist = ar; f.tag_album = al; f.tag_year = yr; f.tag_trk = tk; f.tag_cap = sizeof ti; f.skip = sk;
    static int32_t ch0[4608];
    flac_err e = flac_open(&f, rd, 0, ch0, 4608);
    printf("%d %u [%s] [%s] title=[%s]\n", (int)e, (unsigned)f.rg_have, (f.rg_have & 1) ? f.rg_txt[0] : "", (f.rg_have & 2) ? f.rg_txt[1] : "", ti);
    return 0;
}
