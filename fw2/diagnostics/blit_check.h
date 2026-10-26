#ifndef BLIT_CHECK_H
#define BLIT_CHECK_H
#include <stdint.h>
#ifndef BLIT_CHECK_BG
#include "../ui/theme.h"
#define BLIT_CHECK_BG UI_BG_SCREEN                /* the colour the check puts back over its little box in the title strip */
#endif
uint32_t blit_check_run(void);                    /* the failed tests as bits (0 = all pass): 0 RECT 1 BAR 2 COPY 3 PALETTE 4 SCALED 5 BLEND 6 CLUT ORDER 7 WIDE COPY */
uint32_t blit_check_bad_px(void);                 /* the pixels that differed in the last run */
#endif
