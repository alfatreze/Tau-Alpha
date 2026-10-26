/* Synthetic library: 5 artists (albums 3,0,2,4,1 -> 10 albums), album a has 3 + a%4 tracks, 2 playlists of 4 and 0 tracks. Commands on stdin:
 * init ROWS | up | down | pgup | pgdn | enter | back ; prints "depth kind parent sel top count act" after each. */
#include <stdio.h>
#include <string.h>
#include "browse_core.h"
static const uint32_t art_albums[5] = { 3, 0, 2, 4, 1 };
static uint32_t art_first(uint32_t a) { uint32_t s = 0; for (uint32_t i = 0; i < a; i++) s += art_albums[i]; return s; }
static uint32_t alb_tracks(uint32_t a) { return 3u + a % 4u; }
static uint32_t alb_first(uint32_t a) { uint32_t s = 0; for (uint32_t i = 0; i < a; i++) s += alb_tracks(i); return s; }
static uint32_t n_tracks(void) { return alb_first(10); }
static uint32_t count(uint8_t k, uint32_t p)
{
    switch (k) { case BK_HOME: return 5; case BK_ARTISTS: return 5; case BK_ALBUMS: return 10; case BK_ALBUMS_OF: return art_albums[p];
                 case BK_TRACKS_OF: return alb_tracks(p); case BK_TRACKS: return n_tracks(); case BK_LISTS: return 2; case BK_TRACKS_LIST: return p == 0 ? 4 : 0; }
    return 0;
}
static uint32_t child(uint8_t k, uint32_t p, uint32_t pos)
{
    switch (k) { case BK_ARTISTS: return pos; case BK_ALBUMS: return 9 - pos;                   /* a title order that is not the id order */
                 case BK_ALBUMS_OF: return art_first(p) + pos; case BK_TRACKS_OF: return alb_first(p) + pos; case BK_TRACKS: return n_tracks() - 1 - pos;
                 case BK_LISTS: return pos; case BK_TRACKS_LIST: return 100 + pos; }
    return 0;
}
int main(void)
{
    static const br_ops_t ops = { count, child }; static br_t b; char c[16]; unsigned a; br_result_t r = {0};
    while (scanf("%15s", c) == 1) {
        r.act = BR_NONE;
        if (!strcmp(c, "init")) { scanf("%u", &a); br_init(&b, &ops, a); }
        else if (!strcmp(c, "up")) br_move(&b, -1);
        else if (!strcmp(c, "down")) br_move(&b, 1);
        else if (!strcmp(c, "pgup")) br_page(&b, -1);
        else if (!strcmp(c, "pgdn")) br_page(&b, 1);
        else if (!strcmp(c, "enter")) r = br_enter(&b, &ops);
        else if (!strcmp(c, "back")) { if (!br_back(&b)) r.act = BR_NONE; }
        br_lv_t *l = br_cur(&b);
        printf("%u %u %u %u %u %u %u %u %u\n", b.depth, l->kind, l->parent, l->sel, l->top, l->count, r.act, r.id, r.pos);
    }
    return 0;
}
