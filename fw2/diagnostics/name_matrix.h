#ifndef P2_NAME_MATRIX_H
#define P2_NAME_MATRIX_H
#include <stdint.h>
/* The accented-name matrix on a Pocket (BUG-001 step 3, docs/issues/001-unicode-playlist-paths.md; the card side is tools/name_matrix.py). Diagnostic Build only (fw2/diag_features.json). name_matrix_run() reads
 * <media root>/name-test/matrix.tnm (2,048 bytes: "TNM1 <n>", then "<id>|<o or f>|<absolute path, raw bytes>" per variant), tries slot_open_path() for each path through the cover's slot, reads the first bytes of
 * what it opened and checks the ID3 title "NM<id>" proves it is THAT file, and keeps one result nibble per variant (below). Returns the number of unmet expectations (0 = every 'o' opened the right file and every 'f'
 * failed cleanly); nm_err says why nothing ran. */
uint32_t name_matrix_run(void);
/* nm_res_lo: variants 0..7, nm_res_hi: 8..15, one nibble each (variant 0 = the lowest nibble):
 *   0 not run   1 opened, and it is the right file   2 opened, but another file (the ID3 title is not NM<id>)   3 opened, the read failed
 *   4 open failed, the card says no such file (target code 4)   5 open failed, another target code   6 refused by the firmware (the path is over its 222-byte limit)   7 open timed out */
extern uint32_t nm_res_lo, nm_res_hi, nm_bad, nm_err, nm_dry;
/* nm_err: 0 ran; 1 no library root; 2 matrix.tnm did not open; 3 matrix.tnm did not read; 4 not run (the audio is under pressure: the governor is above NORMAL); 5 the header or the count is wrong; 6 a line is malformed */
#endif
