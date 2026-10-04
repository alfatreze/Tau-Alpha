/* B-561: differential harness for the FLAC Rice fast path (docs/features/FLAC_RICE_DECODER_SPEC.md
 * section 7 item 1). Built twice by sim/test_flac_rice_fast.py: FLAC_RICE_FAST=1 (new) and 0 (old).
 * Reads a case file, one case per line:  mode order blocksize chunk M hexblob
 *   mode 0 = residual() (batch), mode 1 = rice_init + rice_next per value (the streamed channel)
 *   chunk  = bytes the read callback hands over per call (varies buffer-end alignment)
 *   M      = number of leading values to hash separately (the ones decoded from real data when the
 *            stream is truncated)
 * Prints per case:  err eof absbits hash_all hash_prefix
 * absbits = bits consumed from the start of the blob (-1 once eof, where the position is meaningless). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "flac.h"

extern flac_err flac_test_residual(flac_t *f, uint32_t order, int32_t *out);
extern flac_err flac_test_rice_stream(flac_t *f, uint32_t order, uint32_t n, int32_t *out);

typedef struct { const uint8_t *data; long len, pos; int chunk; long total; } src_t;

static int cb_read(void *ctx, uint8_t *dst, int n)
{
    src_t *s = ctx;
    long avail = s->len - s->pos;
    if (avail <= 0) return 0;
    int m = n < s->chunk ? n : s->chunk;
    if (m > avail) m = (int)avail;
    memcpy(dst, s->data + s->pos, (size_t)m);
    s->pos += m; s->total += m;
    return m;
}

static uint32_t fnv(const int32_t *v, long n)
{
    uint32_t h = 2166136261u;
    for (long i = 0; i < n; i++) { h ^= (uint32_t)v[i]; h *= 16777619u; }
    return h;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *fp = fopen(argv[1], "r");
    if (!fp) return 2;
    static char line[1 << 20];
    static int32_t out[8192];
    while (fgets(line, sizeof line, fp)) {
        int mode, order, bsz, chunk, M;
        char *hex = NULL;
        if (sscanf(line, "%d %d %d %d %d", &mode, &order, &bsz, &chunk, &M) != 5) continue;
        hex = line;
        for (int i = 0; i < 5; i++) { hex = strchr(hex, ' '); if (!hex) break; hex++; }
        if (!hex) hex = line + strlen(line);
        size_t hl = strcspn(hex, "\r\n");
        long len = (long)(hl / 2);
        uint8_t *blob = malloc((size_t)len + 1);
        for (long i = 0; i < len; i++) { unsigned b; sscanf(hex + 2 * i, "%2x", &b); blob[i] = (uint8_t)b; }

        static flac_t f;
        memset(&f, 0, sizeof f);
        src_t s = { blob, len, 0, chunk, 0 };
        f.read = cb_read; f.ctx = &s;
        f.blocksize = (uint32_t)bsz;
        memset(out, 0, sizeof out);
        flac_err e = mode == 0 ? flac_test_residual(&f, (uint32_t)order, out)
                               : flac_test_rice_stream(&f, (uint32_t)order, (uint32_t)(bsz - order), out);
        long absbits = f.eof ? -1 : (s.total - (long)(f.have - f.pos)) * 8 - (long)f.bitcnt;
        long nv = bsz - order;
        printf("%d %d %ld %08x %08x\n", (int)e, f.eof, absbits, fnv(out, nv), fnv(out, M < nv ? M : nv));
        free(blob);
    }
    return 0;
}
