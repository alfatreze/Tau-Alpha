/* Host harness for fw2/cores/help_core.h. Input tokens: U D L B (keys on the help pages), "k s e n o" prints fb_kind(state, err, n_tracks, open_failed). Prints the page and whether Help closes after each key,
 * then every help line longer than HP_MAXCH and every fallback line longer than HP_MAXCH. */
#include <stdio.h>
#include <string.h>
#include "help_core.h"
int main(void)
{
    hp_t h; hp_open(&h); char tok[16];
    printf("open page=%u pages=%u\n", h.page, HP_PAGES);
    while (scanf("%15s", tok) == 1) {
        if (tok[0] == 'k') { unsigned s, e, n, o; if (scanf("%u %u %u %u", &s, &e, &n, &o) == 4) printf("kind %u\n", fb_kind(s, e, n, o)); continue; }
        uint32_t k = tok[0] == 'U' ? 1u << 0 : tok[0] == 'D' ? 1u << 1 : tok[0] == 'L' ? 1u << 2 : 1u << 5;
        int c = hp_input(&h, k);
        printf("%s page=%u close=%d\n", tok, h.page, c);
    }
    unsigned long_lines = 0;
    for (unsigned p = 0; p < HP_PAGES; p++) for (unsigned i = 0; i < hp_pages[p].n; i++) if (strlen(hp_pages[p].lines[i].s) > HP_MAXCH) { long_lines++; printf("LONG help %u/%u: %s\n", p, i, hp_pages[p].lines[i].s); }
    for (unsigned k = FB_NOLIB; k <= FB_NOFILES; k++) {
        const fb_text_t *t = &fb_texts[k];
        const char *all[] = { t->l1, t->l2, t->step[0], t->step[1], t->step[2], t->small[0], t->small[1] };
        if (strlen(t->l1) > FB_META_MAXCH || strlen(t->l2) > FB_META_MAXCH) { long_lines++; printf("LONG meta %u\n", k); }
        for (unsigned i = 0; i < 7; i++) if (strlen(all[i]) > HP_MAXCH) { long_lines++; printf("LONG fallback %u: %s\n", k, all[i]); }
    }
    printf("long=%u omega_url_on_help=%d\n", long_lines, strstr(hp_p1[4].s, HP_URL_OMEGA) != 0);
    return 0;
}
