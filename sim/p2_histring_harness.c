/* Host harness for fw2/cores/histring_core.h: a ring of `len` bytes in a word array. Commands: "i len" (clear, head 0), "p v" (push), "u n" (unwrap the newest n and print), "a age" (get one), "h" (print the head). */
#include <stdio.h>
#include "histring_core.h"
int main(void)
{
    static volatile uint32_t w[256]; static uint8_t out[1024]; uint32_t head = 0, len = 8; char c; unsigned a;
    while (scanf(" %c", &c) == 1) {
        if (c == 'i' && scanf("%u", &a) == 1) { len = a; hr_clear(w, 1024); head = 0; }
        else if (c == 'p' && scanf("%u", &a) == 1) { head = hr_back(head, len); hr_store(w, head, a); }
        else if (c == 'u' && scanf("%u", &a) == 1) { hr_unwrap(w, head, len, out, a); for (unsigned i = 0; i < a; i++) printf("%u ", out[i]); printf("\n"); }
        else if (c == 'a' && scanf("%u", &a) == 1) printf("%u\n", (unsigned)hr_get(w, head, len, a));
        else if (c == 'h') printf("%u\n", head);
    }
    return 0;
}
