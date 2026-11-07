#ifndef P2_NAME_MATRIX_H
#define P2_NAME_MATRIX_H
#include <stdint.h>
/* The accented-name matrix on a Pocket (BUG-001, docs/issues/001-unicode-playlist-paths.md; the card side is tools/name_matrix.py). Diagnostic Build only (fw2/diag_features.json). name_matrix_run() reads
 * <media root>/name-test/matrix.tnm (3,072 bytes: "TNM2 <n>", then "<id>|<o, f or p>|<file id>|<absolute path, raw bytes>" per variant), tries slot_open_path() for each path through the cover's slot, reads the first bytes of what
 * it opened and checks the ID3 title "NM<file id>" proves it is THAT file. Round 2 (B-992): it also keeps the target's own error code of each open. Returns the number of unmet expectations; nm_err says why nothing ran. */
uint32_t name_matrix_run(void);
void name_matrix_auto(void);          /* Select+Start (the Report) calls it: the matrix runs ONCE per boot, at the first Report that finds the audio calm (a run refused for pressure does not use the turn), so the owner needs no Check run first; Check, A or Y, runs it again */
/* Four words each, one nibble per variant (variant 0 the lowest nibble of word 0, variant 8 of word 1, 16 of word 2, 24 of word 3):
 *   nm_res: 0 not run   1 opened, the right file   2 opened, another file (the title is not NM<file id>)   3 opened, the read failed   4 open failed, target code 4 = MALFORMED PATH   5 open failed, any other target code (nm_cod has it: 3 = file not found)
 *           6 refused by the firmware (the path is over its limit)   7 open timed out
 *   nm_cod: the error code of the open: 0 it opened; 1..7 the target's own 0192 code (docs/host-target-commands: 2 slot undefined, 3 file not found, 4 malformed path, 5 general error); 9 no template; 10 refused by the firmware (too long); 12 timeout
*/
extern uint32_t nm_res[4], nm_cod[4], nm_bad, nm_err, nm_dry, nm_runs;      /* nm_runs: the runs that went ahead since boot */
/* nm_err: 0 ran; 1 no library root; 2 matrix.tnm did not open; 3 matrix.tnm did not read; 4 not run (the audio is under pressure); 5 the header or the count is wrong; 6 a line is malformed */
#endif
