/* B-737 (gapless, trim primitive): decode a whole MP3 file with the REAL Helix decoder (third_party/libhelix-mp3) on the host, the way the firmware loop does
 * (MP3FindSyncWord + MP3Decode per frame, starting after the ID3v2 tag), and write the raw interleaved int16 PCM to stdout, one line "FRAMES n CH c RATE r"
 * on stderr. Used by sim/test_mp3_trim.py to prove the delay/padding trim aligns the decoded stream with the FLAC of the same recording. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "mp3dec.h"
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)n + 4); if (fread(buf, 1, (size_t)n, f) != (size_t)n) return 2; fclose(f);
    long pos = 0;
    if (n > 10 && !memcmp(buf, "ID3", 3)) pos = 10 + (((long)buf[6] << 21) | ((long)buf[7] << 14) | ((long)buf[8] << 7) | buf[9]);
    HMP3Decoder dec = MP3InitDecoder(); if (!dec) return 3;
    static short pcm[1152 * 2];
    long frames = 0; int ch = 0, rate = 0;
    while (pos < n) {
        int off = MP3FindSyncWord(buf + pos, (int)(n - pos));
        if (off < 0) break;
        pos += off;
        unsigned char *in = buf + pos; int left = (int)(n - pos);
        int err = MP3Decode(dec, &in, &left, pcm, 0);
        if (err) { if (err == ERR_MP3_INDATA_UNDERFLOW) break; pos += 1; continue; }
        pos = in - buf;
        MP3FrameInfo fi; MP3GetLastFrameInfo(dec, &fi);
        ch = fi.nChans; rate = fi.samprate;
        fwrite(pcm, sizeof(short), (size_t)fi.outputSamps, stdout);
        frames++;
    }
    fprintf(stderr, "FRAMES %ld CH %d RATE %d\n", frames, ch, rate);
    return 0;
}
