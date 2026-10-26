#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "report_record.h"
#define SR_FN static
#include "../../fw/suite_core.h"       /* the TAUD record builder: Tau-written pure core shared with the old player and the decoder tools (moves to fw2/cores at S7) */

static void emit(void *ctx, const uint8_t *entry, uint32_t len) { sr_tlv((sr_t *)ctx, (uint8_t)SR_T_INFOTEXT, entry, len); }

uint32_t report_record_build_meter(uint8_t *buf, uint32_t cap, const uint8_t *payload, uint32_t n)
{
    sr_t s;
    sr_init(&s, buf, cap, 0u);
    sr_tlv(&s, (uint8_t)SR_T_METERCFG, payload, n);
    return sr_finish(&s);
}
uint32_t report_record_build(uint8_t *buf, uint32_t cap, const cnt_t *table, uint32_t n)
{
    sr_t s;
    sr_init(&s, buf, cap, 0u);
    cnt_export(table, n, emit, &s);
    return sr_finish(&s);
}
