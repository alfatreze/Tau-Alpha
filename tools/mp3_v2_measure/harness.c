/* MP3 v2 measurement harness: decodes the first FRAMES frames of a file with the CURRENT decoder (Helix, as built into Player 2),
 * for the per-stage profile under tools/mp3_v2_measure/rvprof.c. Contains no decoder code. The file is read through the sim's DMA window. */
#include <stdint.h>
#include "mp3dec.h"
#define IO(a) (*(volatile uint32_t *)(a))
#define FILEBUF ((unsigned char *)0x00400000)
static short pcm[2304];
static void pr(const char *s) { while (*s) IO(0xF0000000) = (uint8_t)*s++; }
static void pn(unsigned v) { char b[12]; int i = 11; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = '0' + v % 10; v /= 10; } pr(b + i); }
int main(void)
{
    unsigned size = IO(0xF000001C), done = 0, nframes = 0;
    while (done < size) {
        IO(0xF0000010) = (uint32_t)(FILEBUF + done); IO(0xF0000014) = done;
        IO(0xF0000018) = size - done > 65536 ? 65536 : size - done;
        unsigned k = IO(0xF0000018); if (!k) break; done += k;
    }
    HMP3Decoder d = MP3InitDecoder();
    unsigned char *p = FILEBUF; int left = (int)size;
    if (p[0] == 'I' && p[1] == 'D' && p[2] == '3') { int skip = 10 + ((p[6] << 21) | (p[7] << 14) | (p[8] << 7) | p[9]); p += skip; left -= skip; }
    for (;;) {
        int off = MP3FindSyncWord(p, left); if (off < 0) break; p += off; left -= off;
        int err = MP3Decode(d, &p, &left, pcm, 0);
        if (err) { if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) { if (left < 4) break; continue; } break; }
        nframes++;
        if (nframes >= FRAMES) break;
    }
    MP3FrameInfo fi; MP3GetLastFrameInfo(d, &fi);
    pr("frames="); pn(nframes); pr(" sr="); pn(fi.samprate); pr(" ch="); pn(fi.nChans); pr(" br="); pn(fi.bitrate); pr("\n");
    return 0;
}
