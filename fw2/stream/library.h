/* Player 2 library access (spec section 10): loads the index image (data slot 5) into PSRAM once, proves the PSRAM window first, then answers path/size queries. */
#ifndef P2_LIBRARY_H
#define P2_LIBRARY_H
#include <stdint.h>
#define LIBP_MAX_PATH 200u
extern uint8_t  lib_state;        /* 0 not loaded, 1 loaded, 2 failed */
extern uint8_t  lib_err;          /* LIB_E_* when failed, 0x80 = no PSRAM window */
extern uint32_t lib_ms;           /* load time */
extern uint32_t lib_n_tracks;
void     library_load(void);
uint32_t library_path(uint32_t id, char *out, uint32_t cap);      /* returns the length, 0 on failure */
uint32_t library_size(uint32_t id);                                 /* bytes from the details record, 0 when the index has none */
uint32_t library_rate(uint32_t id);                                 /* source Hz from the details record, 0 when unknown */
uint32_t library_codec(uint32_t id);                                /* 1 MP3, 2 FLAC, 0 unknown */
uint32_t library_album_jump(uint32_t id, int dir);                  /* first track of the previous (-1) / next (+1) album, 0xFFFFFFFF at the ends */
uint32_t library_title(uint32_t id, char *out, uint32_t cap);
uint16_t *library_queue_buf(void);
uint16_t *library_orig_buf(void);                                   /* the natural order, kept to undo a shuffle */                                  /* PSRAM array of LIB_MAX_TRACKS u16 */
#endif
