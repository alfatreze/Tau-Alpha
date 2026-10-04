#ifndef CP_NFRAMES
#define CP_NFRAMES 40
#endif
/* Shared by the two Chladni pack harnesses (sim/chladni_pack_native.c: the meter compiled straight in; tools/host/pack_harness.c with -DPACK_CHL: the meter loaded as a pack and
 * run under rv32sim). The same deterministic services are provided on both sides (a cycle counter that advances on every read, the mailbox, the engine's copies, the audio-yield
 * test, the CPU percent, the toast) and every call is hashed in order with its arguments, so equal hashes mean the pack drives the hardware exactly as the built-in meter does.
 * Four scenarios: Lattice/LINE; Shimmer/EMBER with the yield test refusing some frames; OCEAN under high CPU (load shedding); a layout change mid-run with the mailbox's halves swapped. */
static uint32_t cp_rs, cp_cyc, cp_frame, cp_swapread, cp_hsb;
static uint32_t cp_rnd(void) { cp_rs = cp_rs * 1664525u + 1013904223u; return cp_rs >> 8; }
static uint64_t cp_H; static uint32_t cp_N;
static void cp_mix(const uint32_t *v, int n) { for (int i = 0; i < n; i++) { cp_H ^= v[i]; cp_H *= 1099511628211ull; } cp_N++; }
static uint32_t cp_cycles(void) { cp_cyc += 400000u; return cp_cyc; }
static int cp_mb_write(uint32_t a, uint32_t d) { const uint32_t v[3] = { 1, a, d }; cp_mix(v, 3); return 0; }
static int cp_mb_read(uint32_t a, uint32_t *o) { const uint32_t v[2] = { 2, a }; cp_mix(v, 2); *o = cp_swapread ? 0xF8000000u : 0x000007E0u; return 0; }
static void cp_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { const uint32_t v[7] = { 3, sx, sy, dx, dy, w, h }; cp_mix(v, 7); }
static void cp_sblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t a, uint32_t b) { const uint32_t v[9] = { 4, sx, sy, dx, dy, w, h, a, b }; cp_mix(v, 9); }
static void cp_wait(void) { const uint32_t v[1] = { 5 }; cp_mix(v, 1); }
static void cp_fence(void) { const uint32_t v[1] = { 6 }; cp_mix(v, 1); }
static void cp_set_bases(uint32_t s, uint32_t d) { const uint32_t v[3] = { 7, s, d }; cp_mix(v, 3); }
static void cp_toast(const char *m) { uint32_t h = 2166136261u; for (; *m; m++) { h ^= (uint8_t)*m; h *= 16777619u; } const uint32_t v[2] = { 8, h }; cp_mix(v, 2); }
static int cp_afford(void) { const int ok = (cp_frame % 7u) != 3u; const uint32_t v[2] = { 9, (uint32_t)ok }; cp_mix(v, 2); return ok; }
static uint32_t cp_cpu_hi;
static uint32_t cp_cpu_pct(void) { return cp_cpu_hi ? 97u : 40u; }
static int cp_held(void) { return 0; }

static void chl_trace(uint16_t *params, uint8_t *force, uint32_t (*run)(const mtr_in_t *), void (*report)(int scen, uint32_t n, uint64_t h))
{
    uint16_t base[MP_CHLADNI_N];
    for (uint32_t i = 0; i < MP_CHLADNI_N; i++) base[i] = params[i];
    for (int s = 0; s < 4; s++) {
        for (uint32_t i = 0; i < MP_CHLADNI_N; i++) params[i] = base[i];
        params[MP_CHLADNI_LAYOUT] = (s == 1) ? 1u : 0u;
        params[MP_CHLADNI_PAINT]  = (s == 0) ? 0u : (s == 2) ? 2u : 1u;
        cp_cpu_hi = (s == 2); cp_swapread = (s == 3);
        cp_rs = 31u + (uint32_t)s; cp_H = 1469598103934665603ull; cp_N = 0; cp_cyc = 0u; cp_hsb = (s == 1) ? 0x30000u : 0u;
        *force = 1;
        for (int n = 0; n < CP_NFRAMES; n++) {
            cp_frame = (uint32_t)n;
            if (s == 3 && n == 45) params[MP_CHLADNI_LAYOUT] = 1u;
            uint8_t spec[16];
            for (int i = 0; i < 16; i++) spec[i] = (n >= 30 && n < 36) ? 0 : (uint8_t)((cp_rnd() % 256u) * (16u - (uint32_t)i / 2u) / 16u);
            mtr_in_t in; for (unsigned k = 0; k < sizeof in; k++) ((uint8_t *)&in)[k] = 0;
            in.spec = spec; in.force = (uint8_t)(n == 0); in.dt_ms = 26u; in.bg = 0x18C3u;
            in.x = 20; in.y = 150; in.w = 360; in.h = 110;
            run(&in);
        }
        report(s, cp_N, cp_H);
    }
}
