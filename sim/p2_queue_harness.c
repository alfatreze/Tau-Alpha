/* Host harness for fw2/cores/queue_core.h: a command script on stdin, one result per line.
 *   set N | shuffle SEED KEEP | restore | repeat R | next AUTO | prev | goto ID | peek K | cur
 * set N makes the order 0..N-1 (the natural order; restore rebuilds it). Prints `id pos n` after each command. */
#include <stdio.h>
#include <string.h>
#include "queue_core.h"
int main(void)
{
    static uint16_t ids[20000], orig[20000]; static queue_t q; char cmd[16]; unsigned a, b; uint32_t on = 0;
    while (scanf("%15s", cmd) == 1) {
        if (!strcmp(cmd, "set")) { scanf("%u", &a); on = a; for (unsigned i = 0; i < a; i++) orig[i] = ids[i] = (uint16_t)i; queue_set(&q, ids, a); }
        else if (!strcmp(cmd, "shuffle")) { scanf("%u %u", &a, &b); queue_shuffle(&q, a, (int)b); }
        else if (!strcmp(cmd, "restore")) queue_restore(&q, orig, on);
        else if (!strcmp(cmd, "repeat")) { scanf("%u", &a); q.repeat = (uint8_t)a; }
        else if (!strcmp(cmd, "next")) { scanf("%u", &a); unsigned r = queue_next(&q, (int)a); printf("%u %u %u\n", r, q.pos, q.n); continue; }
        else if (!strcmp(cmd, "prev")) { unsigned r = queue_prev(&q); printf("%u %u %u\n", r, q.pos, q.n); continue; }
        else if (!strcmp(cmd, "goto")) { scanf("%u", &a); unsigned r = (unsigned)queue_goto(&q, (uint16_t)a); printf("%u %u %u\n", r, q.pos, q.n); continue; }
        else if (!strcmp(cmd, "peek")) { scanf("%u", &a); printf("%u %u %u\n", queue_peek(&q, a), q.pos, q.n); continue; }
        else if (!strcmp(cmd, "dump")) { for (unsigned i = 0; i < q.n; i++) printf("%u ", ids[i]); printf("\n"); continue; }
        printf("%u %u %u\n", queue_cur(&q), q.pos, q.n);
    }
    return 0;
}
