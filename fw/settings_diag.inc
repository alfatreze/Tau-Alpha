/* Diagnostics > Tests pages: window/read-write/playlist/cold tests and the counters (A-125).
 * Diagnostic Build only: included from settingsui.inc under #if TAU_DIAGNOSTIC (never compiled into `release`).
 * Moved out of that file unchanged (B-251); behaviour and generated code are identical. */
/* ---- Diagnostic Build tests (A-125). Short, blocking (well under a millisecond to a few ms),
 * run only from the menu. They use the CPU window at a SCRATCH area (physical 8 MiB), never the
 * playlist buffers (1 MiB..+13 KiB), the framebuffer (below 1 MiB) or anything on the card, and
 * refuse to run unless the window check passed. Results are short strings shown in the row. ---- */
#define DG_SCRATCH 0xA0800000u              /* physical 8 MiB */
static char dg_res[DG_N][22];
static uint32_t dg_base_under, dg_base_stall;

COLD_FN static void dg_set(uint32_t id, const char *a, uint32_t v, const char *b)
{
    char *q = set_str(dg_res[id], a);
    if (v != 0xFFFFFFFFu) q = ui_dec(q, v);
    q = set_str(q, b);
    *q = 0;
}
COLD_FN static uint32_t dg_pat(uint32_t a) { return (a * 2654435761u) ^ 0xA5C3E17Bu; }

COLD_FN static int dg_window_ok(uint32_t id)
{
    if (pl_sdram_ready()) return 1;
    dg_set(id, "NO WINDOW", 0xFFFFFFFFu, "");
    return 0;
}

/* Words written then read back-to-back (the A-093 bug read the previous beat), sub-word
 * writes with a merged read-back, and one distinct address per address line 2..25. */
COLD_FN static void dg_window_test(void)
{
    if (!dg_window_ok(DG_WIN)) return;
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)DG_SCRATCH;
    uint32_t checks = 0, fail = 0, first = 0;
    for (uint32_t i = 0; i < 64u; i++) p[i] = dg_pat(i);
    for (uint32_t i = 0; i < 64u; i++) {
        uint32_t v = p[i]; checks++;
        if (v != dg_pat(i)) { if (!fail) first = i; fail++; }
    }
    p[64] = 0xFFFFFFFFu;
    volatile uint8_t  *b = (volatile uint8_t *)(uintptr_t)DG_SCRATCH;
    volatile uint16_t *h = (volatile uint16_t *)(uintptr_t)DG_SCRATCH;
    b[256] = 0x11u; b[257] = 0x22u; h[129] = 0x4433u;
    checks++;
    if (p[64] != 0x44332211u) { if (!fail) first = 64u; fail++; }
    uint32_t addr[26], n = 0;
    addr[n++] = 0x00800000u;                              /* bit 23, the base */
    for (uint32_t k = 2; k <= 25u; k++)
        if (k != 23u) addr[n++] = 0x00800000u | (1u << k);
    for (uint32_t i = 0; i < n; i++)
        *(volatile uint32_t *)(uintptr_t)(0xA0000000u + addr[i]) = dg_pat(addr[i] + 7u);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = *(volatile uint32_t *)(uintptr_t)(0xA0000000u + addr[i]); checks++;
        if (v != dg_pat(addr[i] + 7u)) { if (!fail) first = 100u + i; fail++; }
    }
    if (fail) { dg_set(DG_WIN, "FAIL ", fail, " OF "); char *q = dg_res[DG_WIN]; while (*q) q++;
                q = ui_dec(q, checks); *q = 0; }
    else      dg_set(DG_WIN, "PASS ", checks, "");
    (void)first;
}

/* Net cycles per access (loop and cycles() overhead subtracted), min/avg/max over 256. */
COLD_FN static void dg_latency(uint32_t id, int write)
{
    if (!dg_window_ok(id)) return;
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)DG_SCRATCH;
    uint32_t ovh = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < 16u; i++) { uint32_t t = cycles(); uint32_t d = cycles() - t; if (d < ovh) ovh = d; }
    uint32_t mn = 0xFFFFFFFFu, mx = 0, sum = 0;
    for (uint32_t i = 0; i < 256u; i++) {
        uint32_t t = cycles();
        if (write) p[i & 63u] = i; else (void)p[i & 63u];
        uint32_t d = cycles() - t; d = d > ovh ? d - ovh : 0u;
        if (d < mn) mn = d;
        if (d > mx) mx = d;
        sum += d;
    }
    char *q = dg_res[id];
    q = ui_dec(q, mn); *q++ = '/'; q = ui_dec(q, sum / 256u); *q++ = '/'; q = ui_dec(q, mx);
    *q = 0;
}

/* pl_order is a permutation of 0..count-1, every pl_off points inside the parsed text at a
 * non-comment NUL-terminated name, and the parsed text still hashes to what it did at load. */
COLD_FN static void dg_playlist_check(void)
{
    if (!pl_count) { dg_set(DG_PLC, "NO PLAYLIST", 0xFFFFFFFFu, ""); return; }
    if (!dg_window_ok(DG_PLC)) return;
    uint8_t seen[(PL_MAX + 7u) / 8u];
    for (uint32_t i = 0; i < sizeof(seen); i++) seen[i] = 0;
    uint32_t bad = 0;
    for (uint32_t i = 0; i < pl_count; i++) {
        uint32_t f = pl_order[i];
        if (f >= pl_count || (seen[f >> 3] >> (f & 7u)) & 1u) { bad++; continue; }
        seen[f >> 3] |= (uint8_t)(1u << (f & 7u));
    }
    for (uint32_t i = 0; i < pl_count; i++) {
        uint32_t o = pl_off[i];
        if (o >= pl_dg_len || pl_text[o] == '#' || pl_text[o] == 0 || (i && o <= pl_off[i - 1u])) bad++;
    }
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < pl_dg_len; i++) { h ^= (uint32_t)(uint8_t)pl_text[i]; h *= 16777619u; }
    if (h != pl_dg_sig) bad++;
    if (bad) dg_set(DG_PLC, "FAIL ", bad, "");
    else     dg_set(DG_PLC, "PASS ", pl_count, "");
}

/* Cold code run from PSRAM: correctness of calls into and out of it, that a second call from the cache fetches nothing,
 * and the cost of executing 12 KB of code that cannot stay cached (cycles per instruction word, one decimal). */
COLD_FN static void dg_cold_test(void)
{
    if (!COLD_READY()) { dg_set(DG_COLD, "NOT LOADED E", cold_err, ""); return; }
    uint32_t fails = 0;
    volatile uint32_t *ifn = (volatile uint32_t *)(uintptr_t)0x800000B0u;
    REG(0x800000B8u) = 1u;                                   /* clear the fetch counters */
    if (cold_add(40u, 2u) != 42u) fails++;
    if (cold_loop(100u) != 15150u) fails++;
    if (cold_calls_hot(21u) != 43u) fails++;
    uint32_t n0 = *ifn;
    if (cold_add(1u, 1u) != 2u) fails++;
    if (*ifn != n0) fails++;                                 /* served from the I-cache: no new fills */
    uint32_t t0 = cycles(), b0 = *ifn;
    if (cold_big(7u) != 3007u) fails++;
    uint32_t t1 = cycles(), b1 = *ifn;
    if (cold_big(9u) != 3009u) fails++;
    uint32_t t2 = cycles(), b2 = *ifn;
    if (b1 - b0 < 2900u || b2 - b1 < 2900u) fails++;        /* the cache cannot hold it: both runs refill */
    if (fails) { dg_set(DG_COLD, "FAIL ", fails, ""); return; }
    uint32_t cpw10 = ((t2 - t1) * 10u) / 3000u;              /* cycles per instruction word x 10 */
    char *q = set_str(dg_res[DG_COLD], "PASS ");
    q = ui_dec(q, cpw10 / 10u); *q++ = '.'; q = ui_dec(q, cpw10 % 10u); q = set_str(q, " C/W"); *q = 0;
    (void)t0; (void)t1;
}

COLD_FN static void dg_run(uint32_t id)
{
    if (id == DG_WIN)      dg_window_test();
    else if (id == DG_RD)  dg_latency(DG_RD, 0);
    else if (id == DG_WR)  dg_latency(DG_WR, 1);
    else if (id == DG_PLC) dg_playlist_check();
    else if (id == DG_COLD) dg_cold_test();
    else {                                    /* CLEAR COUNTERS: Info shows counts since now */
        dg_base_under = pcm_under_n;
        dg_base_stall = REG(R_FB_STALL);
        dg_set(DG_CLR, "DONE", 0xFFFFFFFFu, "");
    }
}
