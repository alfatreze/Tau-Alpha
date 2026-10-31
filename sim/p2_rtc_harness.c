#include <stdio.h>
#include "rtc_core.h"
/* reads "valid date_hex time_hex uptime" lines, prints the clock text of each */
int main(void)
{
    unsigned valid, date, time, up; char out[24];
    while (scanf("%u %x %x %u", &valid, &date, &time, &up) == 4) { rtc_clock_text(valid, date, time, up, out); printf("%s\n", out); }
    return 0;
}
