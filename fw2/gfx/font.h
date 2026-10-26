/* Player 2 fonts (docs/features/FONT_SPEC.md): choosing a font loads its Tau font record (cold image) into the drawing engine's writable font memory and switches the advance table. Cold code. */
#ifndef P2_FONT_H
#define P2_FONT_H
#include <stdint.h>
extern uint32_t font_cur;           /* the index (Appearance order) of the font in the engine's memory: 0 = the one the bitstream powers up with */
extern uint32_t font_err;           /* 0, or the code of the last refused or failed load: 1 bad record, 2 no font memory, 3 the load counter did not step, 4 the engine was not idle */
extern uint32_t font_loads;         /* completed loads since boot */
extern uint32_t font_ms;            /* milliseconds the last load took */
int font_select(uint32_t idx);      /* 0 = the font is in the memory (or already was); else the code above. A failure leaves the font in the memory and its advances exactly as they were. */
extern uint8_t font_changed;        /* set when a load changed the font: the main loop repaints the view once */
#endif
