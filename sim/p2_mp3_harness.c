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
    uint32_t tail_skip = argc > 9 ? (uint32_t)atoi(argv[9]) : 0u; uint32_t seek_to = argc > 7 ? (uint32_t)atoi(argv[7]) : 0u, nout = argc > 8 ? (uint32_t)atoi(argv[8]) : 0u; int seeked = 0, seeked_tail = 0, plan_ok = 0; mp3_seek_t pl = {0, 0, 0}; uint32_t got = 0;
    uint32_t cap = (uint32_t)atoi(argv[3]), seed = (uint32_t)atoi(argv[4]), lat = (uint32_t)atoi(argv[5]), jit = (uint32_t)atoi(argv[6]);
    static uint8_t ring[65536]; static stream_t s; static mp3d_t d; uint32_t rng = seed * 2654435761u + 1u;
    stream_init(&s, cap, 0x7FFFFFFFu); stream_open(&s, 0, fsz, 0);
    if (!mp3d_open(&d, MP3InitDecoder())) return 3;
    d.file_size = fsz;
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
        if (st == MS_BLOCK && tail_skip && !seeked_tail) { seeked_tail = 1; mp3d_set_tail(&d, tail_skip); }
        if (st == MS_BLOCK && seek_to && !seeked) {                      /* the header is known now: plan, restart, and output from the seek point on */
            seeked = 1; plan_ok = mp3d_plan_seek(&d, seek_to, &pl);
            if (!plan_ok || !mp3d_seek(&d, &s, &pl)) { status = 3; break; }
            continue;
        }
        if (st == MS_BLOCK) {
            uint32_t n = b.pairs; if (nout && got + n > nout) n = nout - got;
            fwrite(b.pcm, sizeof(int16_t), n * b.ch, o); blocks++; got += n;
            if (nout && got >= nout) { status = 1; break; }
        }
        else if (st == MS_EOF) { status = 1; break; }
        else if (st == MS_ERROR) { status = 2; break; }
        else need++;
        t += 50;
    }
    fclose(o);
    printf("plan_ok=%d\nplan_off=%u\nplan_frame=%u\nstatus=%d\nblocks=%u\nneed=%u\npairs_out=%u\npairs_total=%u\nch=%u\nrate=%u\nspf=%u\ntrimmed=%d\nerrors=%u\nresyncs=%u\nskipped=%u\ntag=%u\nframes=%u\nxing=%u\ndelay=%u\npad=%u\nstarved=%u\n",
           plan_ok, pl.byte_off, pl.frame_idx, status, blocks, need, d.pairs_out, d.pairs_total, d.ch, d.rate, d.spf, d.trimmed, d.errors, d.resyncs, d.skipped_bytes, d.tag_bytes, d.frame_idx, d.xing_frames, d.delay, d.pad, s.starved);
    return 0;
}
