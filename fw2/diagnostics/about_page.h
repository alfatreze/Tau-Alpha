#ifndef ABOUT_PAGE_H
#define ABOUT_PAGE_H
#include <stdint.h>
void about_page_init(void);                 /* the page opens at its top */
void about_page_key(uint32_t keys);         /* Up and Down scroll (a wrap, like Info) */
void about_page_draw(void);                 /* the rows in view, from the player's state at this moment */
#endif
