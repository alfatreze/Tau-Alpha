/* Host harness for fw2/cores/mp3_frames_core.h: the real Helix decoder driven through the REAL stream reader against an in-memory file served by a card model
 * (one command at a time, latency + jitter). Args: file out.pcm cap seed lat_us jit_us   Writes the trimmed interleaved int16 PCM; prints key=value lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mp3_frames_core.h"
static uint32_t lcg(uint32_t *x) { *x = *x * 1664525u + 1013904223u; return *x >> 8; }
int main(int argc, char **argv)
{
    if (argc < 7) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); uint32_t fsz = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc(fsz + 8); if (fread(file, 1, fsz, f) != fsz) return 2; fclose(f);
    FILE *o = fopen(argv[2], "wb");
    uint32_t cap = (uint32_t)atoi(argv[3]), seed = (uint32_t)atoi(argv[4]), lat = (uint32_t)atoi(argv[5]), jit = (uint32_t)atoi(argv[6]);
    static uint8_t ring[65536]; static stream_t s; static mp3d_t d; uint32_t rng = seed * 2654435761u + 1u;
    stream_init(&s, cap, 0x7FFFFFFFu); stream_open(&s, 0, fsz, 0);
    if (!mp3d_open(&d, MP3InitDecoder())) return 3;
    struct { int busy; io_req_t r; uint64_t done_at; } card = {0};
    uint64_t t = 0; int status = 0; uint32_t blocks = 0, need = 0;
    while (t < 600000000ull) {
        io_result_t res = IO_PENDING;
        if (card.busy && t >= card.done_at) {
            for (uint32_t i = 0; i < card.r.len; i++) ring[(card.r.dst + i) % cap] = file[card.r.off + i];
            res = IO_OK; card.busy = 0;
        }
        io_req_t rq;
        if (stream_pump(&s, (uint32_t)t, res, &rq)) { card.busy = 1; card.r = rq; card.done_at = t + lat + (jit ? lcg(&rng) % jit : 0); }
        pcm_blk_t b;
        mp3d_step_t st = mp3d_step(&d, &s, ring, &b);
        if (st == MS_BLOCK) { fwrite(b.pcm, sizeof(int16_t), b.pairs * b.ch, o); blocks++; }
        else if (st == MS_EOF) { status = 1; break; }
        else if (st == MS_ERROR) { status = 2; break; }
        else need++;
        t += 50;
    }
    fclose(o);
    printf("status=%d\nblocks=%u\nneed=%u\npairs_out=%u\npairs_total=%u\nch=%u\nrate=%u\nspf=%u\ntrimmed=%d\nerrors=%u\nresyncs=%u\nskipped=%u\ntag=%u\nframes=%u\nxing=%u\ndelay=%u\npad=%u\nstarved=%u\n",
           status, blocks, need, d.pairs_out, d.pairs_total, d.ch, d.rate, d.spf, d.trimmed, d.errors, d.resyncs, d.skipped_bytes, d.tag_bytes, d.frame_idx, d.xing_frames, d.delay, d.pad, s.starved);
    return 0;
}
