/* Half of the B-307 cross-check: drives FDCT32 from a WRLOG-patched SCRATCH copy of dct32.c -- the exact mechanism
 * tools/gen_mp3_poly_rom.py / sim/mp3_poly_map.c / sim/mp3_poly_model.c already rely on and have proven correct. Same output format and same
 * call sequence as mp3_poly_fw_capture.c, so sim/test_mp3_poly_fw.py can diff the two byte for byte. */
#include <stdio.h>
#include <stdint.h>
#include "coder.h"
extern void FDCT32(int *buf, int *dest, int offset, int oddBlock, int gb);
int wlog[64], wn;
#define WLOG   wlog
#define WCOUNT wn
#include "mp3_poly_fw_check_common.h"
int main(void) { run(); return 0; }
