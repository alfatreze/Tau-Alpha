/* Player 2 card-slot driver (spec 9.3): the APF target commands behind one narrow interface. Reads are asynchronous (start, then poll); open-by-path and the identity query
 * are blocking and are called only from the transport's OPEN state (the old player blocks there too; with no audio running in S4 nothing is starved, and from S6 the
 * PCM ring covers the open). Exactly one command is ever in flight: callers must not start anything while slot_busy(). */
#ifndef P2_SLOT_H
#define P2_SLOT_H
#include <stdint.h>
#include "../cores/stream_core.h"
#define SLOT_MEDIA 2u            /* the track being played */
#define SLOT_LIB   5u            /* the library index (also the path template of record) */
void        slot_read_start(uint32_t slot, uint32_t off, uint32_t dst_off, uint32_t len);
io_result_t slot_read_poll(void);                /* IO_PENDING while the command runs; IO_OK / IO_FAIL once, then idle */
int         slot_busy(void);
int         slot_read_blocking(uint32_t slot, uint32_t off, uint32_t dst_off, uint32_t len, uint32_t timeout_cycles);
/* Opens `path` (absolute, as the library names it) into `slot`, using the library slot's path as the template. Returns 1 on success; slot_err then is 0. */
int         slot_open_path(uint32_t slot, const char *path);
extern uint8_t slot_err;         /* SLOT_E_* of the last open */
enum { SLOT_E_OK = 0, SLOT_E_NOTEMPLATE = 1, SLOT_E_TOOLONG = 2, SLOT_E_OPEN = 3, SLOT_E_TIMEOUT = 4 };
extern uint8_t slot_errcode;     /* the target's own result code of the last failed open (4 = file not on the card) */
extern uint32_t slot_opens, slot_open_fails, slot_open_ms_last, slot_open_ms_max;
#endif
