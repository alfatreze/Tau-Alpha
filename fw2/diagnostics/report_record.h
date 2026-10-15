/* Player 2 report record (pure, host-tested): every counter of a table as one SR_T_INFOTEXT entry in the shared TAUD record format (fw/suite_core.h), so the existing decoder
 * (tools/decode_tau_suite.py --grid shot.png --table) reads a screenshot of the pixel grid back into the same name/value rows the Info page shows. */
#ifndef P2_REPORT_RECORD_H
#define P2_REPORT_RECORD_H
#include <stdint.h>
#include "../cores/counters_core.h"
/* Returns the record length (CRC included), or 0 if it did not fit in cap. */
uint32_t report_record_build(uint8_t *buf, uint32_t cap, const cnt_t *table, uint32_t n);
#endif
