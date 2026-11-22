/* Host harness for fw2/ui/meters_settings.c with the SAVE-FILE backend: the real interface (get / put / preset / drop) over a model of the save RAM and of the pool of two tunings. usage: harness <script>
 * commands (one per line): dt <slot index> <id> <size> (the size table model)   dtget <index>   st <hex SAVE_ST>   ram <file.bin|zero>   dump <file.bin>   put <id> <rev> <preset> <i:v,...|->   get <id> <rev>   preset <id> <rev>   drop <id> <rev>   poolput <id> <rev> <preset> <i:v,...>   poolfind <id> <rev>
 * The schema is np = 40 parameters, every maximum 65535, except parameter 3 (max 7). sim/test_p2_mset_backend.py drives it. */
#ifndef P2_HOST
#define P2_HOST 1
#endif
#define REG_RD(a) 0u                 /* the host build of meters_settings.c has its own save-RAM model; mmio.h only needs these names */
#define REG_WR(a, v) ((void)0)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../fw2/ui/meters_settings.c"
extern uint32_t ms_host_save_st; extern uint32_t ms_host_save_ram[2048]; extern uint32_t ms_host_dt[64];
static uint32_t pool_key[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu }; static uint32_t pool_w[2][4]; static uint32_t pool_age[2], clock_;
int mset_pool_find(uint32_t key) { for (int s = 0; s < 2; s++) if (pool_key[s] == key) return s; return -1; }
const uint32_t *mset_pool_words(uint32_t s) { return pool_w[s & 1u]; }
void mset_pool_drop(uint32_t key) { for (int s = 0; s < 2; s++) if (pool_key[s] == key) pool_key[s] = 0xFFFFFFFFu; }
void mset_pool_store(uint32_t key, const uint32_t *w)
{
    int s = mset_pool_find(key); if (s < 0) s = pool_key[0] == 0xFFFFFFFFu ? 0 : (pool_key[1] == 0xFFFFFFFFu ? 1 : (pool_age[0] <= pool_age[1] ? 0 : 1));
    pool_key[s] = key; pool_age[s] = ++clock_; memcpy(pool_w[s], w, 16);
}
static uint16_t mx[40];
static uint32_t parse_list(char *s, meter_settings_t *st) { st->n = 0; if (!s || !strcmp(s, "-")) return 0; while (*s && st->n < 40) { st->idx[st->n] = (uint8_t)strtoul(s, &s, 0); if (*s == ':') s++; st->val[st->n] = (uint16_t)strtoul(s, &s, 0); st->n++; if (*s == ',') s++; } return st->n; }
static void pr(const meter_settings_t *st) { printf(" preset=%u n=%u", st->preset, st->n); for (uint32_t k = 0; k < st->n; k++) printf("%c%u:%u", k ? ',' : ' ', st->idx[k], st->val[k]); printf("\n"); }
int main(int argc, char **argv)
{
    for (int i = 0; i < 40; i++) mx[i] = 65535; mx[3] = 7;
    if (argc < 2) return 2; FILE *f = fopen(argv[1], "r"); if (!f) return 3; char line[512];
    while (fgets(line, sizeof line, f)) {
        char *cmd = strtok(line, " \n"); if (!cmd) continue;
        char *a1 = strtok(0, " \n"), *a2 = strtok(0, " \n"), *a3 = strtok(0, " \n"), *a4 = strtok(0, " \n");
        meter_settings_t st; memset(&st, 0, sizeof st);
        if (!strcmp(cmd, "st")) ms_host_save_st = (uint32_t)strtoul(a1, 0, 16);
        else if (!strcmp(cmd, "ram")) { memset(ms_host_save_ram, 0, sizeof ms_host_save_ram); if (strcmp(a1, "zero")) { FILE *r = fopen(a1, "rb"); uint8_t b[8192]; if (!r || fread(b, 1, 8192, r) != 8192) return 4; fclose(r); for (int w = 0; w < 2048; w++) ms_host_save_ram[w] = b[4*w] | b[4*w+1] << 8 | b[4*w+2] << 16 | (uint32_t)b[4*w+3] << 24; } }
        else if (!strcmp(cmd, "dt")) { const uint32_t i = (uint32_t)strtoul(a1, 0, 0); ms_host_dt[2 * i] = (uint32_t)strtoul(a2, 0, 0); ms_host_dt[2 * i + 1] = (uint32_t)strtoul(a3, 0, 0); }
        else if (!strcmp(cmd, "dtget")) printf("dt %u %u\n", ms_host_dt[2 * strtoul(a1, 0, 0)], ms_host_dt[2 * strtoul(a1, 0, 0) + 1]);
        else if (!strcmp(cmd, "dump")) { FILE *o = fopen(a1, "wb"); uint8_t b[8192]; for (int w = 0; w < 2048; w++) { b[4*w] = ms_host_save_ram[w]; b[4*w+1] = ms_host_save_ram[w] >> 8; b[4*w+2] = ms_host_save_ram[w] >> 16; b[4*w+3] = ms_host_save_ram[w] >> 24; } fwrite(b, 1, 8192, o); fclose(o); }
        else if (!strcmp(cmd, "put")) { st.preset = (uint8_t)strtoul(a3, 0, 0); parse_list(a4, &st); printf("put kept=%u\n", meter_settings_put((uint32_t)strtoul(a1, 0, 16), (uint32_t)strtoul(a2, 0, 0), &st, mx, 40)); }
        else if (!strcmp(cmd, "get")) { const int r = meter_settings_get((uint32_t)strtoul(a1, 0, 16), (uint32_t)strtoul(a2, 0, 0), mx, 40, &st); printf("get %d", r); if (r) pr(&st); else printf("\n"); }
        else if (!strcmp(cmd, "preset")) { uint32_t p = 99; const int r = meter_settings_preset((uint32_t)strtoul(a1, 0, 16), (uint32_t)strtoul(a2, 0, 0), &p); printf("preset %d %u\n", r, r ? p : 0); }
        else if (!strcmp(cmd, "drop")) { meter_settings_drop((uint32_t)strtoul(a1, 0, 16), (uint32_t)strtoul(a2, 0, 0)); printf("drop\n"); }
        else if (!strcmp(cmd, "poolput")) { st.preset = (uint8_t)strtoul(a3, 0, 0); parse_list(a4, &st); printf("poolput kept=%u\n", pool_put((uint32_t)strtoul(a1, 0, 16), (uint32_t)strtoul(a2, 0, 0), &st, mx, 40)); }
        else if (!strcmp(cmd, "poolfind")) printf("poolfind %d\n", mset_pool_find(tune_key((uint32_t)strtoul(a1, 0, 16), (uint32_t)strtoul(a2, 0, 0))) >= 0);
    }
    return 0;
}
