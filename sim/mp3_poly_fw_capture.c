/* Half of the B-307 cross-check (docs/MP3_FILTERBANK_KERNEL_DESIGN.md section 6 step 4 item 2): drives FDCT32 from the REAL, committed
 * third_party/libhelix-mp3/real/dct32.c, compiled with -DTAU_POLY_FW=1 so its own permanent TAU_POLY_LOG() hook fires. For every call, prints
 * "wn v0 v1 ... v(wn-1)" -- the raw capture, BEFORE the skip-index-17 rule is applied (that's done identically on both sides by the Python
 * driver, sim/test_mp3_poly_fw.py, so this file and mp3_poly_scratch_capture.c can be diffed line for line). Same RNG and call sequence as
 * mp3_poly_scratch_capture.c by construction (both are driven from the one shared loop in sim/mp3_poly_fw_check_common.h). */
#include <stdio.h>
#include <stdint.h>
#include "coder.h"
extern void FDCT32(int *buf, int *dest, int offset, int oddBlock, int gb);
int tau_poly_wlog[33];
int tau_poly_wn;
#define WLOG   tau_poly_wlog
#define WCOUNT tau_poly_wn
#include "mp3_poly_fw_check_common.h"
int main(void) { run(); return 0; }
