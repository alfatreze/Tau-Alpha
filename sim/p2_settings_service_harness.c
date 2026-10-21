/* Host check of fw2/settings/settings.c settings_service(): a model of the register file (32 words behind R_SET_IDX / R_SET_DAT) and of the cycle counter. Prints, per step, how many words differ between the
 * published register file and the values the player holds. */
#include <stdio.h>
#include <stdint.h>
#define P2_HOST 1
static uint32_t regs[32], idx, now;
#define R_CYCLES_ADDR 0x80000000u
static uint32_t host_rd(uint32_t a);
static void host_wr(uint32_t a, uint32_t v);
#define REG_RD(a) host_rd((uint32_t)(a))
#define REG_WR(a, v) host_wr((uint32_t)(a), (v))
#include "../fw2/settings/settings.c"
static uint32_t host_rd(uint32_t a) { if (a == R_SET_DAT) return regs[idx & 31u]; if (a == R_CYCLES) return now; return 0; }
static void host_wr(uint32_t a, uint32_t v) { if (a == R_SET_IDX) idx = v; else if (a == R_SET_DAT) regs[idx & 31u] = v; }
static uint32_t stores_at(void) { return settings_stats.stores; }
int main(void)
{
    uint32_t vals[SF_COUNT];
    regs[0] = 94; settings_init(vals);
    settings_service(vals, 0);                                       /* nothing changed: nothing stored */
    printf("start stores %u\n", stores_at());
    vals[SF_COLOUR] = 7;
    now += 10; settings_service(vals, 1);                            /* changed while streaming: not yet */
    printf("streaming t+0 stores %u reg1 %u\n", stores_at(), regs[1]);
    now += CLK_HZ / 2u; settings_service(vals, 1);
    printf("streaming t+0.5s stores %u reg1 %u\n", stores_at(), regs[1]);
    now += CLK_HZ / 2u + 20u; settings_service(vals, 1);             /* a second after the first unpublished change */
    printf("streaming t+1s stores %u reg1 %u\n", stores_at(), regs[1]);
    vals[SF_COLOUR] = 9; now += 100; settings_service(vals, 0);      /* not streaming: at once */
    printf("idle stores %u reg1 %u\n", stores_at(), regs[1]);
    vals[SF_COLOUR] = 11; now += 100; settings_service(vals, 1); now += 100; settings_service(vals, 1);
    vals[SF_COLOUR] = 9; now += 100; settings_service(vals, 1);      /* changed back to what was published: the timer restarts for a later change */
    now += CLK_HZ + 10u; settings_service(vals, 1);
    printf("revert stores %u reg1 %u\n", stores_at(), regs[1]);
    return 0;
}
