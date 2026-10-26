/* Host check of stream_wait_idle(): a read in flight is waited out WITHOUT a new one being left marked in flight (the Rite of Spring lock). A card model with a latency completes the read; the
 * caller then wants the channel, and afterwards the normal pump must read on. Prints "IDLE steps=N" (steps until idle, 0 = never within the bound), "NEXT ok" when the following pump issued a read
 * and its completion was collected, and "TIMEOUTS n". */
#include <stdio.h>
#include "stream_core.h"
int main(void)
{
    static stream_t s; io_req_t rq;
    stream_init(&s, 24576, 5000000u);
    stream_open(&s, 0, 1000000u, 0);
    uint32_t now = 0; int started = 0;
    if (stream_pump(&s, now, IO_PENDING, &rq)) started = 1;                /* the first read goes out */
    int steps = 0, idle = 0;
    for (int i = 1; i <= 2000; i++) {
        now += 6666u;                                                      /* 100 us */
        io_result_t res = (started && i >= 200) ? IO_OK : IO_PENDING;       /* the card answers after 20 ms */
        if (res == IO_OK) started = 0;
        if (stream_wait_idle(&s, now, res)) { idle = 1; steps = i; break; }
    }
    printf("IDLE steps=%d\n", idle ? steps : 0);
    /* the channel is free; the transport's normal pump resumes */
    int issued = stream_pump(&s, now + 6666u, IO_PENDING, &rq);
    printf("NEXT %s\n", issued ? "ok" : "none");
    if (issued) { stream_pump(&s, now + 20000u, IO_OK, &rq); }
    printf("TIMEOUTS %u READS %u\n", s.timeouts, s.reads);
    return 0;
}
