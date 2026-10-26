/* Host harness for fw2/cores/font_core.h: a mock engine. stdin: "R file" loads a record and parses it, "F pos" flips one bit of the record in memory, "L unit gen_step" runs the load (unit 0/1 = the memory exists;
 * gen_step = how much the load counter moves, 1 = right). Prints the parse result and, for a load, its result, the words written, the first advance and the log of what it did ("fence", "ctrl1", "firstw"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font_core.h"
static uint32_t gen, words_written, first_write_seen, unit_present = 1, gen_step = 1, log_n; static char logbuf[64][16];
static void lg(const char *s) { if (log_n < 64) { strncpy(logbuf[log_n], s, 15); logbuf[log_n][15] = 0; log_n++; } }
static void fence(void *c) { (void)c; lg("fence"); }
static uint32_t rd_ctrl(void *c) { (void)c; return (unit_present ? 0x80000000u : 0u) | (gen << 16) | FT_WORDS; }
static void wr_ctrl(void *c, uint32_t v) { (void)c; if (v & 1u) lg("ctrl1"); }
static void wr_data(void *c, uint32_t v) { (void)c; (void)v; if (!first_write_seen) { first_write_seen = 1; lg("firstw"); } words_written++; if (words_written % FT_WORDS == 0) gen = (gen + gen_step) & 0xFFFu; }
int main(void)
{
    static uint32_t buf[16384]; uint32_t len = 0; ft_t f; char cmd, name[256]; int u, gs;
    ft_ops_t ops = { fence, rd_ctrl, wr_ctrl, wr_data, 0 };
    while (scanf(" %c", &cmd) == 1) {
        if (cmd == 'R') { scanf("%255s", name); FILE *fp = fopen(name, "rb"); len = (uint32_t)fread(buf, 1, sizeof buf, fp); fclose(fp); printf("parse %d\n", ft_parse((const uint8_t *)buf, len, &f)); }
        else if (cmd == 'F') { unsigned pos; scanf("%u", &pos); ((uint8_t *)buf)[pos] ^= 1; printf("parse %d\n", ft_parse((const uint8_t *)buf, len, &f)); }
        else if (cmd == 'L') {
            scanf("%d %d", &u, &gs); unit_present = (uint32_t)u; gen_step = (uint32_t)gs; words_written = 0; first_write_seen = 0; log_n = 0;
            uint8_t adv[FT_GLYPHS]; memset(adv, 0xEE, sizeof adv);
            int r = ft_load(&f, &ops, adv);
            printf("load %d words %u adv0 %u log", r, words_written, adv[0]);
            for (uint32_t i = 0; i < log_n; i++) printf(" %s", logbuf[i]);
            printf("\n");
        }
    }
    return 0;
}
