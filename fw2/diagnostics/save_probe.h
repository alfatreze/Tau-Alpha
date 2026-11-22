/* Save-slot self-test (diagnostic only; docs/features/meters/METER_SAVE_FILE_TEST.md, spike S1 of METER_SAVE_FILE_SPEC.md). On a bitstream built with TAU_SAVE it runs ONCE per boot, after the cold image is up:
 * it reads the 8 KB the Pocket loaded into the save RAM from `tau-save-probe.bin`, verifies it (CRC-32 and a pattern), and writes a new counter, value, seed and pattern back through the write-through port, so that a
 * Quit and a restart prove the whole round trip. The rows are "SV ..." on the Report and Info pages. The state lives in the cold image (no hot RAM). On a bitstream without the RAM it records SV PRESENT 0 and does nothing. */
#ifndef P2_SAVE_PROBE_H
#define P2_SAVE_PROBE_H
#include <stdint.h>
typedef struct {
    uint32_t present;       /* the RAM is built in (R_SAVE_ST bit 31) */
    uint32_t bridge_words;  /* 32-bit words the APF bridge wrote into the RAM at the load (x4 = bytes loaded; 2,048 for a full 8 KB file, 0 if the Pocket loaded nothing) */
    uint32_t table_bytes;   /* the size APF's slot table holds for the save slot (id 11): what the Quit will write back; 0 = slot not found or empty */
    uint32_t boots;         /* this boot's number from the file: 1 on a first boot, +1 for every boot whose predecessor's write came back */
    uint32_t crc;           /* 0 not checked (first boot), 1 the loaded file's CRC matched, 2 it did not */
    uint32_t pat_bad;       /* words of the loaded body that differ from the pattern (0 = pattern ok) */
    uint32_t last;          /* the value the previous boot wrote (hex): it must equal the value shown as written by that boot */
    uint32_t written;       /* the value THIS boot wrote (hex) */
    uint32_t prev_epoch;    /* the wall-clock epoch the previous boot stored (0: unknown) */
    uint32_t ms;            /* milliseconds the self-test took (read, verify, write) */
    uint32_t w0;            /* RAM word 0 exactly as loaded (a sound file: 56415354, the magic) */
    uint32_t w1;            /* RAM word 1 as loaded (a sound file: 1, the version) */
    uint32_t align;         /* where the loaded pattern sits: 0 aligned, 1 one word early, 2 one word late, 3 nowhere */
    uint32_t align_hits;    /* pattern words found at that place (2,040 for a whole file) */
    uint32_t runs;          /* 1 once it has run */
} sv_state_t;
extern sv_state_t sv_state;
void save_probe_run(void);
#endif
