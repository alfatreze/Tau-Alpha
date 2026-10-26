/* stdin: a name line, then the file as hex on one line; prints "found lead tail" */
#include <stdio.h>
#include <string.h>
#include "skiptab_core.h"
int main(void)
{
    static char name[512], hex[20000]; static uint8_t buf[10000];
    while (fgets(name, sizeof name, stdin) && fgets(hex, sizeof hex, stdin)) {
        name[strcspn(name, "\n")] = 0; uint32_t n = 0;
        for (const char *p = hex; p[0] && p[1] && p[0] != '\n' && n < sizeof buf; p += 2) { unsigned v; sscanf(p, "%2x", &v); buf[n++] = (uint8_t)v; }
        uint32_t lead = 0, tail = 0; int f = skt_lookup(buf, n, name, &lead, &tail);
        printf("%d %u %u\n", f, lead, tail);
    }
    return 0;
}
