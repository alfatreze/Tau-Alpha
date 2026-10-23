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
    printf("streaming t+0 stores %u reg1 %u\n", stores_at(), (regs[1] & 31u));
    now += CLK_HZ / 2u; settings_service(vals, 1);
    printf("streaming t+0.5s stores %u reg1 %u\n", stores_at(), (regs[1] & 31u));
    now += CLK_HZ / 2u + 20u; settings_service(vals, 1);             /* a second after the first unpublished change */
    printf("streaming t+1s stores %u reg1 %u\n", stores_at(), (regs[1] & 31u));
    vals[SF_COLOUR] = 9; now += 100; settings_service(vals, 0);      /* not streaming: at once */
    printf("idle stores %u reg1 %u\n", stores_at(), (regs[1] & 31u));
    vals[SF_COLOUR] = 11; now += 100; settings_service(vals, 1); now += 100; settings_service(vals, 1);
    vals[SF_COLOUR] = 9; now += 100; settings_service(vals, 1);      /* changed back to what was published: the timer restarts for a later change */
    now += CLK_HZ + 10u; settings_service(vals, 1);
    printf("revert stores %u reg1 %u\n", stores_at(), (regs[1] & 31u));
    /* the last played track (the lazy fields): not with the urgency of a setting while streaming, at once when nothing is */
    uint32_t s0 = stores_at();
    vals[SF_LAST0] = 0x41000007u; vals[SF_LAST1] = 42u;
    now += 100; settings_service(vals, 1);
    now += CLK_HZ * 2u; settings_service(vals, 1);
    printf("track streaming t+2s stores %u reg19 %u\n", stores_at() - s0, regs[19]);
    vals[SF_LAST1] = 43u; now += CLK_HZ * 5u; settings_service(vals, 1);                           /* a new track at about 7 s: the wait starts over */
    now += CLK_HZ * 5u; settings_service(vals, 1);
    printf("track streaming, new track, 5s later stores %u reg20 %u\n", stores_at() - s0, regs[20]);
    now += CLK_HZ * 4u; settings_service(vals, 1);
    printf("track streaming, 9s on the same track stores %u reg20 %u\n", stores_at() - s0, regs[20]);
    vals[SF_LAST1] = 44u; now += 100; settings_service(vals, 0);
    printf("track not streaming stores %u reg20 %u\n", stores_at() - s0, regs[20]);
    vals[SF_LAST1] = 45u; vals[SF_COLOUR] = 13u; now += 100; settings_service(vals, 1); now += CLK_HZ + 10u; settings_service(vals, 1);
    printf("a setting published with the track stores %u reg1 %u reg20 %u\n", stores_at() - s0, (regs[1] & 31u), regs[20]);
    return 0;
}
