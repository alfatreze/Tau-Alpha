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
/* ReplayGain fields of the details record: flags (bits 4..7 = which exist), gains in centi-dB, peaks Q15. Returns 0 when the index has no details. */
int      library_rg(uint32_t id, uint32_t *flags, int32_t *tg, int32_t *ag, uint32_t *tp, uint32_t *ap);
/* Text of a track for the screen: title, artist and album (empty strings when unknown), and its stream facts. */
void     library_meta(uint32_t id, char *title, uint32_t tcap, char *artist, uint32_t acap, char *album, uint32_t bcap);
int      library_facts(uint32_t id, uint32_t *codec, uint32_t *ch, uint32_t *kbps, uint32_t *rate);
/* Browser access (cores/browse_core.h kinds). Counts and ids per level, the text of a row, and the queue a Play on a track row builds. */
uint32_t library_br_count(uint8_t kind, uint32_t parent);
uint32_t library_br_child(uint8_t kind, uint32_t parent, uint32_t pos);
void     library_br_text(uint8_t kind, uint32_t parent, uint32_t pos, char *out, uint32_t cap);
#define LQ_ALL_NATURAL 0xFEu                                                /* every track in id order (the natural order of Shuffle All) */
#define LQ_ALL_SHUFFLED 0xFFu
/* Fills q (u16 track ids) with the queue for Play on row `pos` of a level; *start is where playback begins in it. Returns the length. */
uint32_t library_build_queue(uint8_t kind, uint32_t parent, uint32_t pos, uint16_t *q, uint32_t cap, uint32_t *start, uint32_t seed);
uint32_t library_title(uint32_t id, char *out, uint32_t cap);
uint16_t *library_queue_buf(void);
uint16_t *library_orig_buf(void);                                   /* the natural order, kept to undo a shuffle */                                  /* PSRAM array of LIB_MAX_TRACKS u16 */
#endif
