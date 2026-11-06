/* Meter presets from files, the firmware side (docs/features/METER_PRESETS_EXTERNAL.md, B-967; formats and the pure reader: fw2/cores/mpreset_core.h). Cold code: it runs from the PSRAM image and keeps almost nothing in
 * hot RAM. For ONE meter at a time (the one on screen) it reads the meter's index file into a PSRAM cache and, when a preset is chosen, reads that preset's file (about 190 bytes) the way a cover is read: through the
 * cover's data slot, only while the governor is NORMAL, after the media stream's command in flight has finished. The list a user sees is [DEFAULT] + the index; position 0 is the meter's default values. */
#ifndef P2_MPRESET_HOST_H
#define P2_MPRESET_HOST_H
#include <stdint.h>
#include "../cores/mpreset_core.h"
uint32_t mph_enter(uint32_t mid);                       /* reads the index of meter manifest id `mid` into the cache; returns the number of presets (0: none, or the read failed: see mph_err) */
uint32_t mph_count(void);                               /* presets in the cached index */
void     mph_name(uint32_t pos, char *out);             /* the name at list position pos (0 = "DEFAULT", 1.. = the index entries), at most 15 characters */
int      mph_values(uint32_t mid, uint32_t pos, const mp_param_t *ps, uint32_t np, uint16_t *vals);   /* the values of list position pos; 0 when the file cannot be read now (the caller keeps what it has) */
extern uint32_t mph_idx_ok, mph_reads, mph_fails, mph_err, mph_read_max, mph_open_max;      /* diagnostics (Report rows MPX ...): indexes loaded, preset files read, failures, the last error, the worst times in cycles */
#endif
