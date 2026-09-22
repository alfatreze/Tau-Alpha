/* Builds a fixed sample report with fw/suite_core.h under tools/rv32sim.py and prints the record (hex), the QR text, the
 * persisted words and the short code; sim/test_suite.py compares them with tools/decode_tau_suite.py's own encoder. */
#include "hostio.h"
#include "../../fw/suite_core.h"

static uint8_t rec[300];
static char txt[500];

int main(void)
{
    sr_t s;
    sr_init(&s, rec, sizeof(rec), 1);
    uint32_t build[4] = { 0x0300u, 0x4D503317u, 0x1Fu, 5008u };      /* fw 0.3.0, bitstream, flags, heap gap */
    sr_vals(&s, SR_T_BUILD, 4, build, 4);
    sr_test(&s, 0, SR_PASS, 89); sr_test(&s, 1, SR_PASS, 380); sr_test(&s, 2, SR_PASS, 1049216);
    sr_test(&s, 3, SR_FAIL, 12); sr_test(&s, 4, SR_SKIP, 0);
    uint32_t cost[4] = { 48, 57, 31, 37 }; sr_vals(&s, SR_T_SDRAM, 2, cost, 4);
    uint32_t audio[4] = { 2, 0, 3, 380 }; sr_vals(&s, SR_T_AUDIO, 2, audio, 4);
    uint32_t decprof[4] = { 5, 13, 57, 68 };      /* h, i, s pct (MP3), r pct (FLAC) -- B-088/B-089 */
    sr_vals(&s, SR_T_DECPROF, 2, decprof, 4);
    /* Decode Profile Sweep: repeatable, one entry per track (B-090/B-091/B-092). */
    uint8_t sw0[10 + 5] = { 0, 100, 3, 0, 11, 0, 55, 0, 0, 0, 'T','r','k',' ','A' };   /* track 0, 1.00x: h3 i11 s55 r0, title "Trk A" */
    uint8_t sw1[10 + 5] = { 1, 125, 0, 0, 0, 0, 0, 0, 68, 0, 'T','r','k',' ','B' };    /* track 1, 1.25x: h0 i0 s0 r68, title "Trk B" */
    sr_tlv(&s, SR_T_DECSWEEP, sw0, sizeof(sw0));
    sr_tlv(&s, SR_T_DECSWEEP, sw1, sizeof(sw1));
    uint32_t len = sr_finish(&s);
    hputs("REC "); for (uint32_t i = 0; i < len; i++) { hputc("0123456789abcdef"[rec[i] >> 4]); hputc("0123456789abcdef"[rec[i] & 15]); } hnl();
    sr_text(rec, len, txt, sizeof(txt)); hputs("TXT "); hputs(txt); hnl();
    sp_t p = { 1, 7, 2, 0x0007u, 0x0008u, 380, 316, 0, 3, 154, 0, 18, 3 };
    uint32_t w[4]; sp_pack(&p, w);
    hputs("WORDS"); for (int i = 0; i < 4; i++) { hputc(' '); hputx(w[i]); } hnl();
    char sc[40]; sp_short(w, 0x4D503317u, sc); hputs("SHORT "); hputs(sc); hnl();
    return 0;
}
