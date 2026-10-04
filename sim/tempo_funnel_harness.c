/* Host harness for fw/tempo_core.h (B-557): decoded PCM read from a raw int16 file (interleaved when stereo), delivered in frames of a given size through tempo_frame(),
 * every output pair collected. usage: tempo_funnel_harness <in.raw> <out.raw> <rate> <channels> <speed_q8> <frame_samples> [abort_after_pairs]
 * stderr: "pairs frames_done aborted". The staging ring is a host array; a second start (after an abort) replays the input from the beginning into a fresh stretcher. */
#include <stdio.h>
#include <stdlib.h>
static uint32_t ps_l[16384], ps_r[16384];                 /* TEMPO_RING / 2 words each */
#define TEMPO_PS_L ((volatile uint32_t *)ps_l)
#define TEMPO_PS_R ((volatile uint32_t *)ps_r)
static FILE *g_out; static long g_pairs, g_abort_at; static int g_stereo;
static int push_pair(int32_t l, int32_t r)
{
    if (g_abort_at >= 0 && g_pairs >= g_abort_at) return 0;
    int16_t b[2] = { (int16_t)l, (int16_t)r };
    fwrite(b, 2, g_stereo ? 2 : 1, g_out);
    g_pairs++;
    return 1;
}
#define TEMPO_PUSH(l, r) push_pair((l), (r))
#include "../fw/tempo_core.h"
static tempo_t T;
int main(int argc, char **argv)
{
    if (argc < 7) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 3;
    fseek(f, 0, SEEK_END); long bytes = ftell(f); fseek(f, 0, SEEK_SET);
    const unsigned fs = (unsigned)atoi(argv[3]), ch = (unsigned)atoi(argv[4]), spd = (unsigned)atoi(argv[5]), frame = (unsigned)atoi(argv[6]);
    g_abort_at = argc > 7 ? atol(argv[7]) : -1; g_stereo = ch == 2;
    const long total = bytes / 2;
    int16_t *pcm = malloc((size_t)bytes);
    if (fread(pcm, 2, (size_t)total, f) != (size_t)total) return 4;
    fclose(f);
    g_out = fopen(argv[2], "wb"); if (!g_out) return 5;
    tempo_start(&T, fs, (uint8_t)ch, spd);
    long pos = 0; int aborted = 0; long frames = 0;
    while (pos < total) {
        long m = total - pos < (long)frame ? total - pos : (long)frame;
        if (!tempo_frame(&T, pcm + pos, (uint32_t)m, ch == 2)) { aborted = 1; break; }
        pos += m; frames++;
    }
    fclose(g_out);
    fprintf(stderr, "%ld %ld %d\n", g_pairs, frames, aborted);
    return 0;
}
