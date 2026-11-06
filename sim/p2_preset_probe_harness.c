/* Host check of fw2/diagnostics/preset_probe.c (B-963): the path it builds from the stamped version text, what it does with an open or read that fails, and that it accepts exactly the bytes the packager writes
 * (tools/package_dev_build.py --preset-probe) and counts anything else as bad. Prints one line per run: "run runs ok bad err path_or_-". */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define P2_HOST 1
static uint32_t clk;
#define REG_RD(a) (clk += 1000u)
#define REG_WR(a, v) ((void)(a), (void)(v))
static uint8_t tagbuf[4096];
#define TAGB ((const volatile uint8_t *)tagbuf)
#include "../fw2/diagnostics/preset_probe.c"
char _tag_start; const char p2_ver_field[48] = "TAUVER:0.6.1-dev.212+abc1234";
aout_stats_t aout_stats; aout_snap_t aout_snap(void) { aout_snap_t s = { 0, 0, 100, 5100 }; return s; }
void mp3play_io_quiesce(void) { }
static int open_ok = 1, read_ok = 1, corrupt = 0; static char last[128];
int slot_open_path(uint32_t slot, const char *p) { (void)slot; strncpy(last, p, sizeof last - 1); return open_ok; }
int slot_read_blocking(uint32_t slot, uint32_t off, uint32_t dst, uint32_t len, uint32_t to)
{
    (void)slot; (void)off; (void)dst; (void)to;
    if (!read_ok) return 0;
    tagbuf[0] = 'T'; tagbuf[1] = 'P'; tagbuf[2] = 'R'; tagbuf[3] = 'B';
    for (uint32_t i = 4; i < len; i++) tagbuf[i] = (uint8_t)(i * 37u + 11u);
    if (corrupt) tagbuf[100] ^= 1;
    return 1;
}
int main(void)
{
    preset_probe_run(); printf("good %u %u %u %u %s\n", pp_runs, pp_ok, pp_bad, pp_err, last);
    corrupt = 1; preset_probe_run(); printf("corrupt %u %u %u %u\n", pp_runs, pp_ok, pp_bad, pp_err);
    corrupt = 0; open_ok = 0; preset_probe_run(); printf("openfail %u %u %u %u\n", pp_runs, pp_ok, pp_bad, pp_err);
    open_ok = 1; read_ok = 0; preset_probe_run(); printf("readfail %u %u %u %u\n", pp_runs, pp_ok, pp_bad, pp_err);
    printf("fill %u open_max_nonzero %d\n", pp_fill_min, pp_open_max > 0);
    return 0;
}
