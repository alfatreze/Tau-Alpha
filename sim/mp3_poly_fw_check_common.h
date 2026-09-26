/* Shared driver loop for mp3_poly_fw_capture.c and mp3_poly_scratch_capture.c (B-307): identical RNG and FDCT32 call sequence on both sides,
 * so the two builds' stdout can be diffed byte for byte to prove the permanent TAU_POLY_FW capture matches the scratch-copy WRLOG mechanism. */
#define NSLOT 96
static uint32_t rng = 4242u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static void run(void)
{
    static int vbuf[MAX_NCHAN * VBUF_LENGTH];
    int vindex = 0;
    for (int s = 0; s < NSLOT; s++) {
        int b = s;
        for (int ch = 0; ch < 2; ch++) {
            int in[32];
            for (int i = 0; i < 32; i++) in[i] = ((int)(rnd() & 0x3FFFFF) - 0x200000) | 1;
            WCOUNT = 0;
            FDCT32(in, vbuf + ch * 32, vindex, b & 1, 8);
            printf("%d", WCOUNT);
            for (int k = 0; k < WCOUNT; k++) printf(" %d", WLOG[k]);
            printf("\n");
        }
        vindex = (vindex - (b & 1)) & 7;
    }
}
