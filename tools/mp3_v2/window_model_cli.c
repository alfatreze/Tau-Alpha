/* Command-line wrapper around the golden model of the hardware window unit (sim/mp3_poly_model_core.h, unchanged), for tools/mp3_v2/window_abi_check.py.
 * Own code. Usage: window_model <words.bin> <pcm.bin> [mode]
 *   words.bin: little-endian int32, per slot 64 words: channel 0's 32 words in push order P0..P31, then channel 1's.
 *   pcm.bin  : little-endian int16, per slot 64 samples: channel 0's 32, then channel 1's.
 *   mode 0 (default): both channels pushed as given; mode 1 (mono): the 32 words of channel 0 are pushed as BOTH channels (the Helix mono branch).
 * The model keeps the history across slots; a new run starts with zeroed history (what `clear` does). Compile with -DTABLES_H="tables.h" -I <dir with tables.h>
 * (tables.h from tools/gen_mp3_poly_rom.py --h) and -I sim/. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include TABLES_H
#include "mp3_poly_model_core.h"

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    int mono = argc > 3 && atoi(argv[3]) == 1;
    FILE *fi = fopen(argv[1], "rb"), *fo = fopen(argv[2], "wb");
    if (!fi || !fo) return 2;
    int32_t w[64]; int16_t out[64];
    while (fread(w, 4, 64, fi) == 64) {
        push(0, w);
        push(1, mono ? w : w + 32);
        short o0[32], o1[32];
        window(0, o0); window(1, o1);
        for (int k = 0; k < 32; k++) { out[k] = o0[k]; out[32 + k] = o1[k]; }
        fwrite(out, 2, 64, fo);
    }
    fclose(fi); fclose(fo);
    return 0;
}
