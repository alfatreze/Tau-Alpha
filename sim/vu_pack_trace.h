#ifndef VP_NFRAMES
#define VP_NFRAMES 120
#endif
/* Shared by the two MASTER VU pack harnesses (sim/vu_pack_native.c: the meter compiled straight in; tools/host/pack_harness.c with -DPACK_VU: the meter loaded as a pack and
 * run under rv32sim). Same deterministic trace on both sides; every rectangle, colour set, glyph, string (hashed by content) is hashed in order. The info readout is fed through
 * in->info exactly as the host does (a new second every 38 frames). Four scenarios: defaults with the overlay on; overlay off; CUSTOM colours; a varying dt_ms. */
static uint32_t vp_rs;
static uint32_t vp_rnd(void) { vp_rs = vp_rs * 1664525u + 1013904223u; return vp_rs >> 8; }
static uint64_t vp_H; static uint32_t vp_N;
static void vp_mix(const uint32_t *v, int n) { for (int i = 0; i < n; i++) { vp_H ^= v[i]; vp_H *= 1099511628211ull; } vp_N++; }
static void vp_hash_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { const uint32_t v[6] = { 1, x, y, w, h, c }; vp_mix(v, 6); }
static void vp_hash_color(uint16_t fg, uint16_t bg) { const uint32_t v[3] = { 2, fg, bg }; vp_mix(v, 3); }
static void vp_hash_ch(uint32_t x, uint32_t y, char c, uint32_t sx, uint32_t sy) { const uint32_t v[6] = { 3, x, y, (uint32_t)(unsigned char)c, sx, sy }; vp_mix(v, 6); }
static uint32_t vp_hash_text(uint32_t x, uint32_t y, const char *s, uint32_t sx, uint32_t sy, uint32_t w)
{
    uint32_t v[7] = { 4, x, y, sx, sy, w, 0 }; uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
    v[6] = h; vp_mix(v, 7); return x;
}
static char *vp_dec(char *p, uint32_t v)
{
    char t[11]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    return p;
}
static uint32_t vp_cell(uint32_t size) { (void)size; return 16u; }

static void vu_trace(uint16_t *params, uint8_t *force, uint32_t (*run)(const mtr_in_t *), void (*report)(int scen, uint32_t n, uint64_t h))
{
    uint16_t base[MP_VU_MASTER_N];
    for (uint32_t i = 0; i < MP_VU_MASTER_N; i++) base[i] = params[i];
    for (int s = 0; s < 4; s++) {
        for (uint32_t i = 0; i < MP_VU_MASTER_N; i++) params[i] = base[i];
        params[MP_VU_MASTER_INFO] = (s == 1) ? 0u : 1u;
        if (s == 2) { params[MP_VU_MASTER_COLOR_MODE] = 1; params[MP_VU_MASTER_COLOR_GREEN] = 0x07E0; params[MP_VU_MASTER_COLOR_YELLOW] = 0xFFE0; params[MP_VU_MASTER_COLOR_RED] = 0xF800; }
        vp_rs = 99u + (uint32_t)s; vp_H = 1469598103934665603ull; vp_N = 0;
        *force = 1;
        for (int n = 0; n < VP_NFRAMES; n++) {
            mtr_info_t nf; for (unsigned k = 0; k < sizeof nf; k++) ((uint8_t *)&nf)[k] = 0;
            nf.sec = 1000u + (uint32_t)(n / 38); nf.hz = (n < 80) ? 44100u : 48000u; nf.kbps = (n < 80) ? 320u : 0u; nf.flac = (uint8_t)(n >= 80); nf.bps = 16;
            nf.encoder = (n % 50 < 25) ? "LAME3.99" : ""; nf.cpu_pct = (uint8_t)(20 + n % 60); nf.dec_pct = (uint8_t)((n / 38) == 1 ? 0xFFu : 30u + (uint32_t)n % 7u); nf.sdr_pct = (uint8_t)((n / 38) == 0 ? 0xFFu : 12u);
            const int quiet = (n >= 60 && n < 70);
            mtr_in_t in; for (unsigned k = 0; k < sizeof in; k++) ((uint8_t *)&in)[k] = 0;
            in.peak_l = quiet ? 0u : (vp_rnd() % 32768u); in.peak_r = quiet ? 0u : (vp_rnd() % 32768u);
            in.force = (uint8_t)(n == 0 || n == 30); in.dt_ms = (s == 3) ? (uint16_t)(26u + (uint32_t)(n % 5) * 20u) : 26u;
            in.bg = 0x18C3u; in.info = &nf;
            in.x = 16; in.y = 152; in.w = 368; in.h = 122;
            if (n == 30) *force = 1;
            run(&in);
        }
        report(s, vp_N, vp_H);
    }
}
