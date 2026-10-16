/* Player 2 cover art (spec section 11): the album's pre-converted TIM1 cover (tau-art/cover_128.pal256.timg, written by Tau Omega / tools/sync_media.py) is read through data slot 7,
 * its 256-colour palette goes into the draw engine's palette, its index bytes are streamed into an off-screen plane through the SDRAM mailbox, and one palette blit (OP_CBLIT) draws it.
 * Blocking (about 100 to 200 ms with a 16 KB cover): the caller quiesces the media stream first and calls it only while the governor is NORMAL. A cover is optional; any failure is a code. */
#ifndef P2_ART_H
#define P2_ART_H
#include <stdint.h>
#include "../cores/timg_core.h"
#define ART_SLOT 7u
int  art_load(const char *path);                  /* TIMG_OK or a TIMG_E_* code; nothing is drawn on an error */
void art_draw(uint32_t dx, uint32_t dy);          /* the loaded cover at (dx,dy), centred in a 128 x 128 box */
extern uint32_t art_loaded, art_failed, art_last_err, art_ms, art_probe_state, art_clut_start;
#endif
