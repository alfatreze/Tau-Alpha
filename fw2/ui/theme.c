#include "theme.h"
uint16_t th_role[TR_COUNT];
void theme_init(void)
{
    static const uint16_t def[TR_COUNT] = {
        [TR_BG_TOP] = 0x0000u, [TR_BG_BOTTOM] = 0x0000u, [TR_SURFACE] = 0x2945u, [TR_SURFACE_TRACK] = 0x18E3u, [TR_TEXT_PRIMARY] = 0xFFFFu, [TR_TEXT_SECONDARY] = 0x94B2u,
        [TR_ACCENT] = 0xFD05u, [TR_ON_ACCENT] = 0x0000u, [TR_ACCENT2] = 0xFD05u, [TR_OK] = 0x0600u, [TR_WARN] = 0xFE60u, [TR_DANGER] = 0xF9C0u,
        [TR_BASE] = 0x0862u, [TR_CHROME] = 0x10E5u, [TR_PILL] = 0x0320u, [TR_ERROR] = 0xF800u, [TR_FAINT] = 0x6B4Du, [TR_SPLASH_BG] = 0x0841u, [TR_SPLASH_BAR] = 0x27ECu,
        [TR_FS_RED] = 0xF249u, [TR_FS_TRACK] = 0x1905u };
    for (uint32_t i = 0; i < TR_COUNT; i++) th_role[i] = def[i];
}
