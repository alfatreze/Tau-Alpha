/* Runs fw/qrcode.h (real firmware code) under tools/rv32sim.py. Data file: [minver, maxver, mask+1 (0 = automatic)] then the
 * payload bytes. Prints the chosen version and mask and the symbol as row-major bits (packed, hex); sim/test_qr.py compares
 * it with segno (byte mode, level M) and decodes the rendered image with OpenCV. */
#include "hostio.h"
#include "../../fw/qrcode.h"

static uint8_t in[2800];
static uint8_t buf[QR_BUF_LEN] __attribute__((aligned(4)));

int main(void)
{
    uint32_t n = hfilesize();
    if (n < 3u || n > sizeof(in)) { hputs("BADIN"); hnl(); return 0; }
    hread(0, in, n);
    uint32_t v = 0, m = 0;
    uint32_t sz = qr_encode(in + 3, n - 3u, in[0], in[1], (int)in[2] - 1, buf, &v, &m);
    if (!sz) { hputs("TOOBIG"); hnl(); return 0; }
    hputs("QR "); hputu(v); hputc(' '); hputu(m); hputc(' '); hputu(sz); hputc(' ');
    uint32_t acc = 0, nb = 0;
    for (uint32_t y = 0; y < sz; y++)
        for (uint32_t x = 0; x < sz; x++) {
            acc = (acc << 1) | (uint32_t)qr_get(buf, sz, x, y);
            if (++nb == 4u) { hputc("0123456789abcdef"[acc]); acc = 0; nb = 0; }
        }
    if (nb) hputc("0123456789abcdef"[acc << (4u - nb)]);
    hnl();
    return 0;
}
