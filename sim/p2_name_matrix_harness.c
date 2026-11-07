/* Host check of fw2/diagnostics/name_matrix.c (BUG-001 step 3): the real file against a model of the card. stdin lines: "root <text>", "degrade <0|1>", "file <hex path> <hex content>", "failread <hex path>", "errcode <hex path> <n>",
 * "timeout <hex path>". Prints "res_lo res_hi bad err dry ret". The model: slot_open_path refuses a path over 222 bytes (SLOT_E_TOOLONG) and a path not in the table (SLOT_E_OPEN, target code 4 unless "errcode"). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
static uint8_t tagbuf[4096];
#define TAGB ((const volatile uint8_t *)tagbuf)
#include "../fw2/diagnostics/name_matrix.c"
char _tag_start; aout_stats_t aout_stats; uint8_t slot_err, slot_errcode;
static char rootstr[96] = "/Assets/tau/common/"; static int degrade;
#define MAXF 64
static struct { uint8_t path[300]; int plen; uint8_t data[2048]; int dlen; int failread, errcode, timeout; } F[MAXF]; static int nf, cur = -1;
uint32_t library_root(char *out, uint32_t cap) { uint32_t n = (uint32_t)strlen(rootstr); if (n >= cap) return 0; memcpy(out, rootstr, n + 1); return n; }
void mp3play_io_quiesce(void) { }
uint32_t mp3play_degrade_now(void) { return (uint32_t)degrade; }
static int find(const char *p) { int n = (int)strlen(p); for (int i = 0; i < nf; i++) if (F[i].plen == n && !memcmp(F[i].path, p, (size_t)n)) return i; return -1; }
int slot_open_path(uint32_t slot, const char *p)
{
    (void)slot; slot_err = 0; slot_errcode = 0; cur = -1;
    if (strlen(p) > 222) { slot_err = SLOT_E_TOOLONG; return 0; }
    int i = find(p);
    if (i < 0) { slot_err = SLOT_E_OPEN; slot_errcode = 4; return 0; }
    if (F[i].timeout) { slot_err = SLOT_E_TIMEOUT; return 0; }
    if (F[i].errcode) { slot_err = SLOT_E_OPEN; slot_errcode = (uint8_t)F[i].errcode; return 0; }
    cur = i; return 1;
}
int slot_read_blocking(uint32_t slot, uint32_t off, uint32_t dst, uint32_t len, uint32_t to)
{
    (void)slot; (void)to; if (cur < 0 || F[cur].failread) return 0;
    uint32_t o = dst - (uint32_t)(uintptr_t)&_tag_start; if (o + len > sizeof tagbuf) return 0;
    for (uint32_t i = 0; i < len; i++) tagbuf[o + i] = off + i < (uint32_t)F[cur].dlen ? F[cur].data[off + i] : 0;
    return 1;
}
static int hex(const char *s, uint8_t *out) { int n = 0; while (s[0] && s[1] && s[0] != '\n') { unsigned v; sscanf(s, "%2x", &v); out[n++] = (uint8_t)v; s += 2; } return n; }
int main(void)
{
    char line[16384];
    while (fgets(line, sizeof line, stdin)) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        if (!strncmp(line, "root ", 5)) strncpy(rootstr, line + 5, sizeof rootstr - 1);
        else if (!strncmp(line, "degrade ", 8)) degrade = atoi(line + 8);
        else if (!strncmp(line, "file ", 5)) { char *sp = strchr(line + 5, ' '); *sp = 0; F[nf].plen = hex(line + 5, F[nf].path); F[nf].dlen = hex(sp + 1, F[nf].data); nf++; }
        else { int is_e = !strncmp(line, "errcode ", 8), is_f = !strncmp(line, "failread ", 9), is_t = !strncmp(line, "timeout ", 8); char *a = strchr(line, ' ') + 1; char *sp = is_e ? strchr(a, ' ') : 0; if (sp) *sp = 0;
            uint8_t p[300]; int n = hex(a, p); for (int i = 0; i < nf; i++) if (F[i].plen == n && !memcmp(F[i].path, p, (size_t)n)) { if (is_e) F[i].errcode = atoi(sp + 1); if (is_f) F[i].failread = 1; if (is_t) F[i].timeout = 1; } }
    }
    uint32_t r = name_matrix_run();
    printf("%08x %08x %u %u %u %u\n", nm_res_lo, nm_res_hi, nm_bad, nm_err, nm_dry, r);
    return 0;
}
