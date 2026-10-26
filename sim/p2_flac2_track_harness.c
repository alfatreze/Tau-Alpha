/* Harness for the FLAC track driver (fw2/cores/flac2_track_core.h) against a card model (one command at a time, latency + jitter) through the REAL stream reader.
 * Usage: harness <file> <out.pcm> <info.txt> <ring cap> <seed> <lat> <jit> <flags> [plan]   plan = "seek@<pairs committed since the previous seek (or the start)>:<target>,seek@...,fail@<file offset>,res@<residuals per call>"
 * The driver plays the transport: pairs are STAGED per frame, COMMITTED on FRAME_OK, dropped on FRAME_BAD, silence committed at once; a seek flushes what is staged.
 * out.pcm: every committed pair (int32 L,R) in order; info.txt: status why phase | frames_ok frame_errors resyncs silent | wraps repositioned probes | one line per stretch: "S base count". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flac2_track_core.h"
static uint32_t lcg(uint32_t *x) { *x = *x * 1664525u + 1013904223u; return *x >> 8; }
int main(int argc, char **argv)
{
    if (argc < 9) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2; fseek(f, 0, SEEK_END); uint32_t fsz = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc(fsz + 8); if (fread(file, 1, fsz, f) != fsz) return 2; fclose(f);
    FILE *po = fopen(argv[2], "wb"); uint32_t cap = (uint32_t)atoi(argv[4]), seed = (uint32_t)atoi(argv[5]), lat = (uint32_t)atoi(argv[6]), jit = (uint32_t)atoi(argv[7]), flags = (uint32_t)atoi(argv[8]);
    struct { uint64_t at, target; int done; } seeks[16]; int ns = 0, ni = 0; uint32_t fail_at = 0xFFFFFFFFu, maxres = 512;
    if (argc > 9) { const char *p = argv[9]; while (*p) {
        if (!strncmp(p, "seek@", 5)) { p += 5; seeks[ns].at = strtoull(p, (char **)&p, 10); p++; seeks[ns].target = strtoull(p, (char **)&p, 10); seeks[ns].done = 0; ns++; }
        else if (!strncmp(p, "res@", 4)) { p += 4; maxres = (uint32_t)strtoul(p, (char **)&p, 10); }
        else if (!strncmp(p, "fail@", 5)) { p += 5; fail_at = (uint32_t)strtoul(p, (char **)&p, 10); }
        else p++;
        if (*p == ',') p++; } }
    static uint8_t ring[65536], sbuf[65536], vbuf[4096]; static int32_t fbuf[65536], out[2 * 4096], stage[2 * 80000]; static stream_t s; static f2t_t t;
    FILE *info = fopen(argv[3], "w"); uint32_t rng = seed * 2654435761u + 1u;
    stream_init(&s, cap, 0x7FFFFFFFu); f2t_init(&t, sbuf, 65536, vbuf, 4096, fbuf, 65536, flags); f2t_begin(&t, &s, fsz);
    struct { int busy; io_req_t r; uint64_t done_at; } card = {0};
    uint64_t T = 0, committed_total = 0, nstage = 0, since_seek = 0; int status = -1, opened = 0, spins = 0; uint32_t frames_ok = 0;
    while (T < 4000000000ull && ++spins < 100000000) {
        io_result_t res = IO_PENDING;
        if (card.busy && T >= card.done_at) {
            if (card.r.off >= fail_at) res = IO_FAIL;
            else { for (uint32_t i = 0; i < card.r.len; i++) ring[(card.r.dst + i) % cap] = file[card.r.off + i]; res = IO_OK; }
            card.busy = 0;
        }
        io_req_t rq;
        if (stream_pump(&s, (uint32_t)T, res, &rq)) { card.busy = 1; card.r = rq; card.done_at = T + lat + (jit ? lcg(&rng) % jit : 0); }
        for (int i = ni; i < ns && i == ni; i++) if (!seeks[i].done && opened && t.phase == F2T_PH_PLAY && committed_total - since_seek >= seeks[i].at) {
            seeks[i].done = 1; fprintf(info, "S %llu %llu\n", (unsigned long long)t.base, (unsigned long long)t.put);       /* the stretch that ends here */
            nstage = 0; t.staged = 0; since_seek = committed_total; f2t_seek(&t, &s, seeks[i].target); ni++; break; }
        uint32_t np = 0; int ev = 0; int st = f2t_step(&t, &s, ring, out, 4096, maxres, &np, &ev);
        if (st == F2T_OPENED) opened = 1;
        if (st == F2T_PAIRS) {
            if (ev & F2_EV_SILENCE) { fwrite(out, 8, np, po); committed_total += np; }
            else if (ev & F2_EV_FRAME_BAD) nstage = 0;
            else { if (nstage + np > 80000u) { status = 9; break; } memcpy(stage + 2 * nstage, out, 8u * np); nstage += np; if (ev & F2_EV_FRAME_OK) { fwrite(stage, 8, nstage, po); committed_total += nstage; nstage = 0; frames_ok++; } }
        }
        if (st == F2T_END) { status = 0; break; }
        if (st == F2T_FAIL) { status = 1; break; }
        T += (st == F2T_NEED) ? 50 : 5;
    }
    if (status < 0) status = 8;
    fprintf(info, "S %llu %llu\n", (unsigned long long)t.base, (unsigned long long)t.put);
    fprintf(info, "R %d %u %u | %u %u %u %llu | %u %u %u | %u\n", status, t.why, t.phase, frames_ok, t.rec.frame_errors, t.rec.resyncs, (unsigned long long)t.rec.silent_pairs, t.wraps, t.repositioned, t.seek_probes, opened);
    fclose(po); fclose(info); return 0;
}
