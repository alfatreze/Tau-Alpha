/* Host check of fw2/cores/save_probe_core.h: one boot of the save-slot self-test against a RAM model. usage: harness <in.bin|-> <out.bin> <epoch> [tear_after_writes]
 * Prints one line "boots= crc= pat_bad= first= last_loaded= last_written= prev_epoch= seed= crc_written=" (hex where it is a hash). The RAM model is the file as 2,048 little-endian words;
 * tear_after_writes models a Quit that lands in the middle of the self-test: later writes are dropped. sim/test_p2_save_probe.py drives it. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../fw2/cores/save_probe_core.h"
static uint32_t mem[SVP_WORDS]; static uint32_t idx; static long writes, tear = -1;
static void io_seek(uint32_t w) { idx = w; }
static uint32_t io_rd(void) { const uint32_t v = mem[idx % SVP_WORDS]; idx++; return v; }
static void io_wr(uint32_t v) { if (tear < 0 || writes < tear) mem[idx % SVP_WORDS] = v; writes++; idx++; }
int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    memset(mem, 0, sizeof mem);
    if (strcmp(argv[1], "-")) { FILE *f = fopen(argv[1], "rb"); if (!f) return 3; uint8_t b[SVP_WORDS * 4]; if (fread(b, 1, sizeof b, f) != sizeof b) return 3; fclose(f);
        for (uint32_t i = 0; i < SVP_WORDS; i++) mem[i] = b[4*i] | b[4*i+1] << 8 | b[4*i+2] << 16 | (uint32_t)b[4*i+3] << 24; }
    if (argc > 4) tear = atol(argv[4]);
    const svp_io_t io = { io_seek, io_rd, io_wr }; svp_res_t r;
    svp_run(&io, (uint32_t)strtoul(argv[3], 0, 0), &r);
    printf("boots=%u crc=%u pat_bad=%u first=%u last_loaded=%08x last_written=%08x prev_epoch=%u seed=%u crc_written=%08x\n", r.boots, r.crc_state, r.pat_bad, r.first, r.last_value_loaded, r.last_value_written, r.prev_epoch, r.seed, r.crc_written);
    FILE *o = fopen(argv[2], "wb"); uint8_t b[SVP_WORDS * 4];
    for (uint32_t i = 0; i < SVP_WORDS; i++) { b[4*i] = mem[i]; b[4*i+1] = mem[i] >> 8; b[4*i+2] = mem[i] >> 16; b[4*i+3] = mem[i] >> 24; }
    fwrite(b, 1, sizeof b, o); fclose(o);
    return 0;
}
