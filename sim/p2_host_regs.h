/* -include'd by Player 2 host harnesses that compile a firmware .c with -DP2_HOST: REG_RD/REG_WR go to a model supplied by the harness. */
#include <stdint.h>
uint32_t host_rd(uint32_t a);
void host_wr(uint32_t a, uint32_t v);
#define REG_RD(a) host_rd((uint32_t)(a))
#define REG_WR(a, v) host_wr((uint32_t)(a), (v))
