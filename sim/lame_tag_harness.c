/* B-624: reads an MP3 file (after its ID3v2 tag) and prints the LAME tag's delay and padding using fw/lame_tag.h. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "lame_tag.h"
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    static uint8_t buf[70000];
    uint8_t hdr[10];
    uint32_t off = 0;
    if (fread(hdr, 1, 10, f) == 10 && hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3')
        off = 10u + (((uint32_t)(hdr[6] & 0x7F) << 21) | ((uint32_t)(hdr[7] & 0x7F) << 14) | ((uint32_t)(hdr[8] & 0x7F) << 7) | (uint32_t)(hdr[9] & 0x7F));
    fseek(f, (long)off, SEEK_SET);                 /* ID3v2 tags with cover art can be hundreds of KB: seek past it */
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    off = 0;
    if (n < 16) { printf("none\n"); return 0; }
    /* the frame header is 4 bytes; the Xing/Info tag sits 17-32 bytes after it: scan from the first sync */
    uint32_t s = off;
    while (s + 4 < n && !(buf[s] == 0xFF && (buf[s + 1] & 0xE0) == 0xE0)) s++;
    uint16_t d = 0, p = 0;
    if (lame_scan(buf + s + 4, (uint32_t)(n - s - 4), &d, &p)) printf("%u %u\n", d, p); else printf("none\n");
    return 0;
}
